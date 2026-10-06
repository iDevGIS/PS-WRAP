// PS-WRAP: AI person segmentation (ดู pswrapsegmenter.h)
#include <pswrapsegmenter.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QVideoFrameFormat>
#include <cmath>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <onnxruntime_c_api.h>

Q_LOGGING_CATEGORY(pswrapSeg, "pswrap.segmenter")

static constexpr int kModelSize = 256;

// ---------------------------------------------------------------- ORT handle
struct PswrapSegmenterWorker::Ort
{
#ifdef Q_OS_WIN
    HMODULE lib = nullptr;
#endif
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    OrtSessionOptions *opts = nullptr;
    OrtSession *session = nullptr;
    OrtMemoryInfo *mem = nullptr;
    const char *in_names[1] = { "pixel_values" };
    const char *out_names[1] = { "alphas" };

    ~Ort()
    {
        if (api) {
            if (session) api->ReleaseSession(session);
            if (opts) api->ReleaseSessionOptions(opts);
            if (mem) api->ReleaseMemoryInfo(mem);
            if (env) api->ReleaseEnv(env);
        }
#ifdef Q_OS_WIN
        if (lib) FreeLibrary(lib);
#endif
    }
};

QString PswrapSegmenter::runtimePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("onnxruntime.dll"));
}

QString PswrapSegmenter::modelPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models/selfie_segmentation.onnx"));
}

bool PswrapSegmenter::available() const
{
    return QFile::exists(runtimePath()) && QFile::exists(modelPath());
}

// ---------------------------------------------------------------- PswrapSegmenter
PswrapSegmenter::PswrapSegmenter(QObject *parent)
    : QObject(parent)
{
    input_sink = new QVideoSink(this);
    connect(input_sink, &QVideoSink::videoFrameChanged, this, &PswrapSegmenter::onInputFrame);
}

PswrapSegmenter::~PswrapSegmenter()
{
    stopWorker();
}

void PswrapSegmenter::setEnabled(bool v)
{
    if (v == is_enabled)
        return;
    is_enabled = v;
    emit enabledChanged();
    if (v) startWorker(); else stopWorker();
}

void PswrapSegmenter::setForwardSink(QVideoSink *s)
{
    if (s == forward_sink) return;
    forward_sink = s;
    emit forwardSinkChanged();
}

void PswrapSegmenter::setMaskSink(QVideoSink *s)
{
    if (s == mask_sink) return;
    mask_sink = s;
    emit maskSinkChanged();
}

void PswrapSegmenter::setSmoothing(qreal v)
{
    v = qBound(0.0, v, 0.95);
    if (qFuzzyCompare(v, smooth)) return;
    smooth = v;
    if (worker) worker->setSmoothing(v);
    emit smoothingChanged();
}

void PswrapSegmenter::onInputFrame(const QVideoFrame &frame)
{
    feed(frame);
}

void PswrapSegmenter::feed(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    if (forward_sink)
        forward_sink->setVideoFrame(frame);
    if (frame.width() != frame_w || frame.height() != frame_h) {
        frame_w = frame.width();
        frame_h = frame.height();
        emit frameSizeChanged();
    }
    if (is_enabled && worker && is_ready)
        worker->submit(frame);
}

void PswrapSegmenter::startWorker()
{
    if (worker)
        return;
    if (!available()) {
        last_error = QStringLiteral("AI background removal unavailable (onnxruntime.dll or model missing)");
        emit errorChanged();
        qCWarning(pswrapSeg) << last_error;
        return;
    }
    worker = new PswrapSegmenterWorker(this);
    worker->setSmoothing(smooth);
    worker->start();
}

void PswrapSegmenter::stopWorker()
{
    if (!worker)
        return;
    worker->requestStop();
    if (!worker->wait(4000)) {
        qCWarning(pswrapSeg) << "segmenter worker did not stop in time, terminating";
        worker->terminate();
        worker->wait(1000);
    }
    delete worker;
    worker = nullptr;
    if (is_ready) { is_ready = false; emit readyChanged(); }
}

void PswrapSegmenter::deliverMask(const QVideoFrame &mask, int ms)
{
    QMetaObject::invokeMethod(this, [this, mask, ms]() {
        if (mask_sink)
            mask_sink->setVideoFrame(mask);
        if (ms != infer_ms) { infer_ms = ms; emit inferenceMsChanged(); }
    }, Qt::QueuedConnection);
}

void PswrapSegmenter::reportReady(bool ok, const QString &err)
{
    QMetaObject::invokeMethod(this, [this, ok, err]() {
        if (is_ready != ok) { is_ready = ok; emit readyChanged(); }
        if (err != last_error) { last_error = err; emit errorChanged(); }
        if (!ok && !err.isEmpty()) qCWarning(pswrapSeg) << err;
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------- worker
PswrapSegmenterWorker::PswrapSegmenterWorker(PswrapSegmenter *owner)
    : owner(owner)
{
}

PswrapSegmenterWorker::~PswrapSegmenterWorker()
{
    delete ort;
}

void PswrapSegmenterWorker::submit(const QVideoFrame &frame)
{
    QMutexLocker lock(&mutex);
    pending = frame;
    has_pending = true;
    cond.wakeOne();
}

void PswrapSegmenterWorker::requestStop()
{
    stop_flag.store(1);
    QMutexLocker lock(&mutex);
    cond.wakeAll();
}

bool PswrapSegmenterWorker::loadRuntime(QString &err)
{
    ort = new Ort();
#ifdef Q_OS_WIN
    const QString path = PswrapSegmenter::runtimePath();
    ort->lib = LoadLibraryW(reinterpret_cast<const wchar_t *>(path.utf16()));
    if (!ort->lib) { err = QStringLiteral("cannot load %1").arg(path); return false; }
    using GetBaseFn = const OrtApiBase *(ORT_API_CALL *)(void);
    auto get_base = reinterpret_cast<GetBaseFn>(GetProcAddress(ort->lib, "OrtGetApiBase"));
    if (!get_base) { err = QStringLiteral("OrtGetApiBase not found"); return false; }
    const OrtApiBase *base = get_base();
    ort->api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!ort->api) { err = QStringLiteral("onnxruntime.dll too old (need API %1)").arg(ORT_API_VERSION); return false; }
#else
    err = QStringLiteral("segmenter supported on Windows only");
    return false;
#endif
    const OrtApi *api = ort->api;
    auto check = [api, &err](OrtStatus *st) {
        if (!st) return true;
        err = QString::fromUtf8(api->GetErrorMessage(st));
        api->ReleaseStatus(st);
        return false;
    };
    if (!check(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "pswrap", &ort->env))) return false;
    if (!check(api->CreateSessionOptions(&ort->opts))) return false;
    api->SetIntraOpNumThreads(ort->opts, 2);
    api->SetSessionGraphOptimizationLevel(ort->opts, ORT_ENABLE_ALL);
    const QString model = PswrapSegmenter::modelPath();
#ifdef Q_OS_WIN
    if (!check(api->CreateSession(ort->env, reinterpret_cast<const wchar_t *>(model.utf16()), ort->opts, &ort->session))) return false;
#endif
    if (!check(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &ort->mem))) return false;
    input_buf.assign(static_cast<size_t>(3 * kModelSize * kModelSize), 0.0f);
    prev_mask.assign(static_cast<size_t>(kModelSize * kModelSize), 0.0f);
    return true;
}

bool PswrapSegmenterWorker::runInference(const QVideoFrame &frame, QVideoFrame &mask_out)
{
    QVideoFrame f = frame;
    QImage img = f.toImage();
    if (img.isNull())
        return false;
    img = img.scaled(kModelSize, kModelSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    // HWC uint8 → CHW float 0..1 (preprocessor: rescale 1/255, no mean/std)
    const int n = kModelSize * kModelSize;
    float *r = input_buf.data(), *g = r + n, *b = g + n;
    for (int y = 0; y < kModelSize; ++y) {
        const uchar *row = img.constScanLine(y);
        for (int x = 0; x < kModelSize; ++x) {
            const int i = y * kModelSize + x;
            r[i] = row[x * 3 + 0] / 255.0f;
            g[i] = row[x * 3 + 1] / 255.0f;
            b[i] = row[x * 3 + 2] / 255.0f;
        }
    }
    const OrtApi *api = ort->api;
    const int64_t shape[4] = { 1, 3, kModelSize, kModelSize };
    OrtValue *in = nullptr, *out = nullptr;
    OrtStatus *st = api->CreateTensorWithDataAsOrtValue(ort->mem, input_buf.data(), input_buf.size() * sizeof(float), shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in);
    if (st) { api->ReleaseStatus(st); return false; }
    st = api->Run(ort->session, nullptr, ort->in_names, &in, 1, ort->out_names, 1, &out);
    api->ReleaseValue(in);
    if (st) { api->ReleaseStatus(st); return false; }
    float *alphas = nullptr;
    st = api->GetTensorMutableData(out, reinterpret_cast<void **>(&alphas));
    if (st || !alphas) { if (st) api->ReleaseStatus(st); api->ReleaseValue(out); return false; }

    // temporal smoothing (EMA) → RGBA8 (gray) 256×256
    const float a = smoothing.load() / 1000.0f;
    QVideoFrame mask(QVideoFrameFormat(QSize(kModelSize, kModelSize), QVideoFrameFormat::Format_RGBA8888));
    if (!mask.map(QVideoFrame::WriteOnly)) { api->ReleaseValue(out); return false; }
    uchar *dst = mask.bits(0);
    const int stride = mask.bytesPerLine(0);
    for (int y = 0; y < kModelSize; ++y) {
        uchar *row = dst + y * stride;
        for (int x = 0; x < kModelSize; ++x) {
            const int i = y * kModelSize + x;
            float v = alphas[i];
            if (v < 0.f) v = 0.f; else if (v > 1.f) v = 1.f;
            v = a * prev_mask[i] + (1.f - a) * v;
            prev_mask[i] = v;
            const uchar u = static_cast<uchar>(std::lround(v * 255.f));
            row[x * 4 + 0] = u; row[x * 4 + 1] = u; row[x * 4 + 2] = u; row[x * 4 + 3] = 255;
        }
    }
    mask.unmap();
    api->ReleaseValue(out);
    mask_out = mask;
    return true;
}

void PswrapSegmenterWorker::run()
{
    QString err;
    if (!loadRuntime(err)) {
        owner->reportReady(false, err);
        return;
    }
    owner->reportReady(true, QString());
    qCInfo(pswrapSeg) << "ONNX Runtime loaded, model ready";

    while (stop_flag.load() == 0) {
        QVideoFrame frame;
        {
            QMutexLocker lock(&mutex);
            while (!has_pending && stop_flag.load() == 0)
                cond.wait(&mutex, 250);
            if (stop_flag.load() != 0) break;
            frame = pending;
            pending = QVideoFrame();
            has_pending = false;
        }
        QElapsedTimer t; t.start();
        QVideoFrame mask;
        if (runInference(frame, mask))
            owner->deliverMask(mask, static_cast<int>(t.elapsed()));
    }
}
