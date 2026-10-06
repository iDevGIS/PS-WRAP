// PS-WRAP: face tracker v2 (ดู pswrapfacetracker.h)
#include <pswrapfacetracker.h>
#include <pswrapsegmenter.h>   // runtimePath()

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QImage>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <onnxruntime_c_api.h>

Q_LOGGING_CATEGORY(pswrapFace, "pswrap.face")

static constexpr int kBlaze = 128;
static constexpr int kMesh = 256;
static constexpr int kLandmarks = 478;
static constexpr float kCropScale = 1.5f;     // ขยาย bbox เป็นสี่เหลี่ยมจัตุรัส ×1.5 (ตาม MediaPipe)
static constexpr int kMaxMiss = 10;
static constexpr double kPi = 3.14159265358979;

// MediaPipe face mesh indices
namespace LM {
    constexpr int eyeL_outer = 33, eyeL_inner = 133, eyeL_top = 159, eyeL_bot = 145;
    constexpr int eyeR_inner = 362, eyeR_outer = 263, eyeR_top = 386, eyeR_bot = 374;
    constexpr int noseTip = 1, noseBridge = 6;
    constexpr int lipTop = 13, lipBot = 14, mouthL = 61, mouthR = 291;
    constexpr int forehead = 10, chin = 152, templeL = 234, templeR = 454;
}

struct PswrapFaceTrackerWorker::Ort
{
#ifdef Q_OS_WIN
    HMODULE lib = nullptr;
#endif
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    OrtSessionOptions *opts = nullptr;
    OrtSession *blaze = nullptr;
    OrtSession *mesh = nullptr;
    OrtMemoryInfo *mem = nullptr;
    const char *blaze_in[4] = { "image", "conf_threshold", "iou_threshold", "max_detections" };
    const char *blaze_out[1] = { "selectedBoxes" };
    const char *mesh_in[1] = { "input_12" };
    const char *mesh_out[2] = { "Identity", "Identity_1" };
    ~Ort()
    {
        if (api) {
            if (mesh) api->ReleaseSession(mesh);
            if (blaze) api->ReleaseSession(blaze);
            if (opts) api->ReleaseSessionOptions(opts);
            if (mem) api->ReleaseMemoryInfo(mem);
            if (env) api->ReleaseEnv(env);
        }
#ifdef Q_OS_WIN
        if (lib) FreeLibrary(lib);
#endif
    }
};

QString PswrapFaceTracker::modelPath() { return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models/blazeface.onnx")); }
QString PswrapFaceTracker::meshModelPath() { return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models/face_mesh.onnx")); }
bool PswrapFaceTracker::available() const { return QFile::exists(PswrapSegmenter::runtimePath()) && QFile::exists(modelPath()); }
bool PswrapFaceTracker::meshAvailable() const { return available() && QFile::exists(meshModelPath()); }

PswrapFaceTracker::PswrapFaceTracker(QObject *parent) : QObject(parent)
{
    input_sink = new QVideoSink(this);
    connect(input_sink, &QVideoSink::videoFrameChanged, this, &PswrapFaceTracker::onInputFrame);
}
PswrapFaceTracker::~PswrapFaceTracker() { stopWorker(); }

void PswrapFaceTracker::setEnabled(bool v)
{
    if (v == is_enabled) return;
    is_enabled = v; emit enabledChanged();
    if (v) startWorker(); else stopWorker();
}
void PswrapFaceTracker::setForwardSink(QVideoSink *s) { if (s == forward_sink) return; forward_sink = s; emit forwardSinkChanged(); }
void PswrapFaceTracker::setSmoothing(qreal v)
{
    v = qBound(0.0, v, 0.95);
    if (qFuzzyCompare(v, smooth)) return;
    smooth = v; if (worker) worker->setSmoothing(v); emit smoothingChanged();
}
void PswrapFaceTracker::onInputFrame(const QVideoFrame &frame) { feed(frame); }
void PswrapFaceTracker::feed(const QVideoFrame &frame)
{
    if (!frame.isValid()) return;
    if (forward_sink) forward_sink->setVideoFrame(frame);
    if (frame.width() != frame_w || frame.height() != frame_h) { frame_w = frame.width(); frame_h = frame.height(); emit frameSizeChanged(); }
    if (is_enabled && worker && is_ready) worker->submit(frame);
}
void PswrapFaceTracker::startWorker()
{
    if (worker) return;
    if (!available()) {
        last_error = QStringLiteral("Face effects unavailable (onnxruntime.dll or models/blazeface.onnx missing)");
        emit errorChanged(); qCWarning(pswrapFace) << last_error; return;
    }
    worker = new PswrapFaceTrackerWorker(this);
    worker->setSmoothing(smooth);
    worker->start();
}
void PswrapFaceTracker::stopWorker()
{
    if (!worker) return;
    worker->requestStop();
    if (!worker->wait(4000)) { qCWarning(pswrapFace) << "face worker did not stop, terminating"; worker->terminate(); worker->wait(1000); }
    delete worker; worker = nullptr;
    if (is_ready) { is_ready = false; emit readyChanged(); }
    if (res.found) { res = PswrapFaceResult(); emit faceChanged(); }
}
void PswrapFaceTracker::deliverFace(const PswrapFaceResult &r)
{
    QMetaObject::invokeMethod(this, [this, r]() {
        res = r; emit faceChanged();
        if (r.ms != infer_ms) { infer_ms = r.ms; emit inferenceMsChanged(); }
    }, Qt::QueuedConnection);
}
void PswrapFaceTracker::reportReady(bool ok, const QString &err)
{
    QMetaObject::invokeMethod(this, [this, ok, err]() {
        if (is_ready != ok) { is_ready = ok; emit readyChanged(); }
        if (err != last_error) { last_error = err; emit errorChanged(); }
        if (!ok && !err.isEmpty()) qCWarning(pswrapFace) << err;
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------- worker
PswrapFaceTrackerWorker::PswrapFaceTrackerWorker(PswrapFaceTracker *owner) : owner(owner) {}
PswrapFaceTrackerWorker::~PswrapFaceTrackerWorker() { delete ort; }
void PswrapFaceTrackerWorker::submit(const QVideoFrame &frame) { QMutexLocker lock(&mutex); pending = frame; has_pending = true; cond.wakeOne(); }
void PswrapFaceTrackerWorker::requestStop() { stop_flag.store(1); QMutexLocker lock(&mutex); cond.wakeAll(); }

bool PswrapFaceTrackerWorker::loadRuntime(QString &err)
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
    if (!ort->api) { err = QStringLiteral("onnxruntime.dll too old"); return false; }
#else
    err = QStringLiteral("face tracker supported on Windows only"); return false;
#endif
    const OrtApi *api = ort->api;
    auto check = [api, &err](OrtStatus *st) { if (!st) return true; err = QString::fromUtf8(api->GetErrorMessage(st)); api->ReleaseStatus(st); return false; };
    if (!check(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "pswrap-face", &ort->env))) return false;
    if (!check(api->CreateSessionOptions(&ort->opts))) return false;
    api->SetIntraOpNumThreads(ort->opts, 2);
    api->SetSessionGraphOptimizationLevel(ort->opts, ORT_ENABLE_ALL);
#ifdef Q_OS_WIN
    const QString blaze = PswrapFaceTracker::modelPath();
    if (!check(api->CreateSession(ort->env, reinterpret_cast<const wchar_t *>(blaze.utf16()), ort->opts, &ort->blaze))) return false;
    const QString mesh = PswrapFaceTracker::meshModelPath();
    if (QFile::exists(mesh)) {
        OrtStatus *st = api->CreateSession(ort->env, reinterpret_cast<const wchar_t *>(mesh.utf16()), ort->opts, &ort->mesh);
        if (st) { qCWarning(pswrapFace) << "face mesh unavailable:" << api->GetErrorMessage(st); api->ReleaseStatus(st); ort->mesh = nullptr; }
    }
    mesh_ok = ort->mesh != nullptr;
#endif
    if (!check(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &ort->mem))) return false;
    blaze_buf.assign(static_cast<size_t>(3 * kBlaze * kBlaze), 0.0f);
    mesh_buf.assign(static_cast<size_t>(kMesh * kMesh * 3), 0.0f);
    return true;
}

bool PswrapFaceTrackerWorker::detectBlaze(const QImage &src, bool &found, float out[16])
{
    found = false;
    QImage img = src;
    if (img.width() > 512) img = img.scaled(512, 512, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    img = img.scaled(kBlaze, kBlaze, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    const int n = kBlaze * kBlaze;
    float *r = blaze_buf.data(), *g = r + n, *b = g + n;
    for (int y = 0; y < kBlaze; ++y) {
        const uchar *row = img.constScanLine(y);
        for (int x = 0; x < kBlaze; ++x) { const int i = y * kBlaze + x; r[i] = row[x * 3] / 255.0f; g[i] = row[x * 3 + 1] / 255.0f; b[i] = row[x * 3 + 2] / 255.0f; }
    }
    const OrtApi *api = ort->api;
    const int64_t img_shape[4] = { 1, 3, kBlaze, kBlaze };
    const int64_t one[1] = { 1 };
    float conf = 0.5f, iou = 0.3f; int64_t maxdet = 4;
    OrtValue *in[4] = { nullptr, nullptr, nullptr, nullptr }; OrtValue *outv = nullptr; bool ok = true;
    auto chk = [&](OrtStatus *st) { if (st) { api->ReleaseStatus(st); ok = false; } };
    chk(api->CreateTensorWithDataAsOrtValue(ort->mem, blaze_buf.data(), blaze_buf.size() * sizeof(float), img_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[0]));
    chk(api->CreateTensorWithDataAsOrtValue(ort->mem, &conf, sizeof(conf), one, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[1]));
    chk(api->CreateTensorWithDataAsOrtValue(ort->mem, &iou, sizeof(iou), one, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[2]));
    chk(api->CreateTensorWithDataAsOrtValue(ort->mem, &maxdet, sizeof(maxdet), one, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in[3]));
    if (ok) chk(api->Run(ort->blaze, nullptr, ort->blaze_in, in, 4, ort->blaze_out, 1, &outv));
    for (auto *v : in) if (v) api->ReleaseValue(v);
    if (!ok || !outv) { if (outv) api->ReleaseValue(outv); return false; }
    OrtTensorTypeAndShapeInfo *info = nullptr; size_t count = 0;
    if (!api->GetTensorTypeAndShape(outv, &info)) { api->GetTensorShapeElementCount(info, &count); api->ReleaseTensorTypeAndShapeInfo(info); }
    float *data = nullptr;
    if (count >= 16 && !api->GetTensorMutableData(outv, reinterpret_cast<void **>(&data)) && data) {
        bool valid = true;
        for (int i = 0; i < 16; ++i) { out[i] = data[i]; if (!std::isfinite(out[i])) valid = false; }
        found = valid && out[2] > out[0] && out[3] > out[1] && (out[2] - out[0]) > 0.02f;
    }
    api->ReleaseValue(outv);
    return true;
}

// crop_norm: สี่เหลี่ยม (normalized ของเฟรม) อาจเกินขอบ → เติมดำ
bool PswrapFaceTrackerWorker::runMesh(const QImage &img, const QRectF &crop_norm, std::vector<float> &pts, float &presence)
{
    const int W = img.width(), H = img.height();
    const QRect crop(QPoint(int(std::lround(crop_norm.x() * W)), int(std::lround(crop_norm.y() * H))),
                     QSize(int(std::lround(crop_norm.width() * W)), int(std::lround(crop_norm.height() * H))));
    if (crop.width() < 8 || crop.height() < 8) return false;
    QImage canvas(crop.size(), QImage::Format_RGB888);
    canvas.fill(Qt::black);
    const QRect inter = crop.intersected(QRect(0, 0, W, H));
    if (inter.isEmpty()) return false;
    {
        QImage part = img.copy(inter).convertToFormat(QImage::Format_RGB888);
        const int ox = inter.x() - crop.x(), oy = inter.y() - crop.y();
        for (int y = 0; y < part.height(); ++y)
            std::memcpy(canvas.scanLine(oy + y) + ox * 3, part.constScanLine(y), static_cast<size_t>(part.width() * 3));
    }
    QImage in = canvas.scaled(kMesh, kMesh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    // NHWC float 0..1
    float *dst = mesh_buf.data();
    for (int y = 0; y < kMesh; ++y) {
        const uchar *row = in.constScanLine(y);
        float *d = dst + static_cast<size_t>(y) * kMesh * 3;
        for (int i = 0; i < kMesh * 3; ++i) d[i] = row[i] / 255.0f;
    }
    const OrtApi *api = ort->api;
    const int64_t shape[4] = { 1, kMesh, kMesh, 3 };
    OrtValue *in_v = nullptr; OrtValue *out_v[2] = { nullptr, nullptr };
    OrtStatus *st = api->CreateTensorWithDataAsOrtValue(ort->mem, mesh_buf.data(), mesh_buf.size() * sizeof(float), shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in_v);
    if (st) { api->ReleaseStatus(st); return false; }
    st = api->Run(ort->mesh, nullptr, ort->mesh_in, &in_v, 1, ort->mesh_out, 2, out_v);
    api->ReleaseValue(in_v);
    if (st) { api->ReleaseStatus(st); for (auto *v : out_v) if (v) api->ReleaseValue(v); return false; }
    float *lm = nullptr; float *pr = nullptr;
    const bool ok = !api->GetTensorMutableData(out_v[0], reinterpret_cast<void **>(&lm)) && lm && !api->GetTensorMutableData(out_v[1], reinterpret_cast<void **>(&pr)) && pr;
    if (ok) {
        pts.assign(lm, lm + kLandmarks * 3);
        presence = 1.0f / (1.0f + std::exp(-pr[0]));
        // crop px (0..256, z สเกลเดียวกัน) → normalized ของเฟรม (z → หน่วย "สัดส่วนความกว้างเฟรม")
        const float sx = static_cast<float>(crop.width()) / kMesh / W, sy = static_cast<float>(crop.height()) / kMesh / H;
        const float ox = static_cast<float>(crop.x()) / W, oy = static_cast<float>(crop.y()) / H;
        for (int i = 0; i < kLandmarks; ++i) { pts[i * 3] = ox + pts[i * 3] * sx; pts[i * 3 + 1] = oy + pts[i * 3 + 1] * sy; pts[i * 3 + 2] *= sx; }
    }
    for (auto *v : out_v) if (v) api->ReleaseValue(v);
    return ok;
}

void PswrapFaceTrackerWorker::applyFilters(PswrapFaceResult &r, double dt)
{
    const double s = smoothing.load() / 1000.0;   // 0..0.95 → min_cutoff 4 (ตามไว) .. 0.6 (นุ่ม)
    const double mc = 4.0 - 3.4 * s;
    QPointF *pts[] = { &r.eyeL, &r.eyeR, &r.eyeOuterL, &r.eyeOuterR, &r.noseTip, &r.noseBridge, &r.mouthTop, &r.mouthBottom, &r.mouthL, &r.mouthR, &r.forehead, &r.chin, &r.templeL, &r.templeR };
    size_t k = 0;
    for (QPointF *p : pts) {
        filt[k].min_cutoff = mc; filt[k + 1].min_cutoff = mc;
        p->setX(filt[k++].filter(p->x(), dt)); p->setY(filt[k++].filter(p->y(), dt));
    }
    qreal *scal[] = { &r.yaw, &r.pitch, &r.roll, &r.faceWidth, &r.faceHeight };
    for (qreal *v : scal) {
        filt[k].min_cutoff = mc;
        filt[k].beta = (v == &r.faceWidth || v == &r.faceHeight) ? 6.0 : 0.3;   // องศา: หน่วยใหญ่ → beta เล็ก
        *v = filt[k++].filter(*v, dt);
    }
    for (size_t j = 0; j < 4; ++j) filt[k + j].min_cutoff = mc;
    const double rx = filt[k].filter(r.rect.x(), dt), ry = filt[k + 1].filter(r.rect.y(), dt), rw = filt[k + 2].filter(r.rect.width(), dt), rh = filt[k + 3].filter(r.rect.height(), dt);
    r.rect = QRectF(rx, ry, rw, rh);
}

void PswrapFaceTrackerWorker::run()
{
    QString err;
    if (!loadRuntime(err)) { owner->reportReady(false, err); return; }
    owner->reportReady(true, QString());
    qCInfo(pswrapFace) << "face tracker ready, mesh =" << mesh_ok;
    clock.start();
    while (stop_flag.load() == 0) {
        QVideoFrame frame;
        {
            QMutexLocker lock(&mutex);
            while (!has_pending && stop_flag.load() == 0) cond.wait(&mutex, 250);
            if (stop_flag.load() != 0) break;
            frame = pending; pending = QVideoFrame(); has_pending = false;
        }
        const qint64 t0 = clock.nsecsElapsed();
        const double dt = last_ns > 0 ? std::max(1e-3, (t0 - last_ns) / 1e9) : 1.0 / 30.0;
        last_ns = t0;

        QVideoFrame f = frame;
        QImage img = f.toImage();
        if (img.isNull()) continue;
        const int W = img.width(), H = img.height();
        const double ar = static_cast<double>(W) / H;   // y (สัดส่วนสูง) → หน่วยเดียวกับ x: หาร ar

        PswrapFaceResult r;
        bool got_mesh = false;
        std::vector<float> pts; float presence = 0.f;

        // 1) mesh จากครอปที่ติดตามอยู่
        if (mesh_ok && have_face && !track_crop.isEmpty()) {
            if (runMesh(img, track_crop, pts, presence) && presence > 0.5f) got_mesh = true;
        }
        // 2) ถ้าไม่มี → BlazeFace หาใหม่ แล้ว (ถ้ามี mesh) ทำ mesh จาก bbox
        if (!got_mesh) {
            float det[16]; bool found = false;
            if (detectBlaze(img, found, det) && found) {
                const double bx0 = det[1], by0 = det[0], bx1 = det[3], by1 = det[2];
                const double cx = (bx0 + bx1) / 2, cy = (by0 + by1) / 2;
                const double side_px = std::max((bx1 - bx0) * W, (by1 - by0) * H) * kCropScale;
                const QRectF crop(cx - side_px / W / 2, cy - side_px / H / 2, side_px / W, side_px / H);
                if (mesh_ok && runMesh(img, crop, pts, presence) && presence > 0.5f) {
                    got_mesh = true;
                } else {
                    // fallback: 6 จุดหยาบจาก BlazeFace
                    r.found = true; r.mesh = false;
                    QPointF e1(det[4], det[5]), e2(det[6], det[7]); if (e1.x() > e2.x()) std::swap(e1, e2);
                    r.eyeL = r.eyeOuterL = e1; r.eyeR = r.eyeOuterR = e2;
                    r.noseTip = r.noseBridge = QPointF(det[8], det[9]);
                    r.mouthTop = r.mouthBottom = QPointF(det[10], det[11]);
                    QPointF c1(det[12], det[13]), c2(det[14], det[15]); if (c1.x() > c2.x()) std::swap(c1, c2);
                    r.mouthL = r.templeL = c1; r.mouthR = r.templeR = c2;
                    r.forehead = QPointF(cx, by0); r.chin = QPointF(cx, by1);
                    r.rect = QRectF(bx0, by0, bx1 - bx0, by1 - by0);
                    r.faceWidth = (bx1 - bx0); r.faceHeight = (by1 - by0) / ar;
                    r.roll = std::atan2((e2.y() - e1.y()) / ar, e2.x() - e1.x()) * 180.0 / kPi;
                    track_crop = crop;
                }
            }
        }
        if (got_mesh) {
            auto P = [&](int i) { return QPointF(pts[i * 3], pts[i * 3 + 1]); };
            auto Z = [&](int i) { return static_cast<double>(pts[i * 3 + 2]); };
            r.found = true; r.mesh = true;
            r.eyeL = (P(LM::eyeL_outer) + P(LM::eyeL_inner) + P(LM::eyeL_top) + P(LM::eyeL_bot)) / 4.0;
            r.eyeR = (P(LM::eyeR_outer) + P(LM::eyeR_inner) + P(LM::eyeR_top) + P(LM::eyeR_bot)) / 4.0;
            r.eyeOuterL = P(LM::eyeL_outer); r.eyeOuterR = P(LM::eyeR_outer);
            r.noseTip = P(LM::noseTip); r.noseBridge = P(LM::noseBridge);
            r.mouthTop = P(LM::lipTop); r.mouthBottom = P(LM::lipBot); r.mouthL = P(LM::mouthL); r.mouthR = P(LM::mouthR);
            r.forehead = P(LM::forehead); r.chin = P(LM::chin); r.templeL = P(LM::templeL); r.templeR = P(LM::templeR);
            // head pose (องศา) จาก z ก่อนสลับ L/R: x,z หน่วย = สัดส่วนความกว้างเฟรม
            const double tdx = r.templeR.x() - r.templeL.x(), tdz = Z(LM::templeR) - Z(LM::templeL);
            r.yaw = std::atan2(tdz, std::max(1e-6, std::abs(tdx))) * 180.0 / kPi;           // + = ขมับขวา (ของคน) ลึกกว่า = หันไปทางซ้ายของภาพ
            const double fdy = (r.chin.y() - r.forehead.y()) / ar, fdz = Z(LM::chin) - Z(LM::forehead);
            r.pitch = std::atan2(fdz, std::max(1e-6, fdy)) * 180.0 / kPi;                 // + = คางลึกกว่าหน้าผาก = เงย
            r.faceWidth = std::sqrt(tdx * tdx + tdz * tdz);
            r.faceHeight = std::sqrt(fdy * fdy + fdz * fdz);
            // ให้ "L" = ซ้ายในภาพเสมอ
            if (r.eyeL.x() > r.eyeR.x()) { std::swap(r.eyeL, r.eyeR); std::swap(r.eyeOuterL, r.eyeOuterR); }
            if (r.templeL.x() > r.templeR.x()) std::swap(r.templeL, r.templeR);
            if (r.mouthL.x() > r.mouthR.x()) std::swap(r.mouthL, r.mouthR);
            r.roll = std::atan2((r.eyeR.y() - r.eyeL.y()) / ar, r.eyeR.x() - r.eyeL.x()) * 180.0 / kPi;
            // bbox จาก landmark
            float x0 = 1, y0 = 1, x1 = 0, y1 = 0;
            for (int i = 0; i < kLandmarks; ++i) { x0 = std::min(x0, pts[i * 3]); x1 = std::max(x1, pts[i * 3]); y0 = std::min(y0, pts[i * 3 + 1]); y1 = std::max(y1, pts[i * 3 + 1]); }
            r.rect = QRectF(x0, y0, x1 - x0, y1 - y0);
            // ครอปเฟรมถัดไปจาก landmark bbox
            const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
            const double side_px = std::max((x1 - x0) * W, (y1 - y0) * H) * kCropScale;
            track_crop = QRectF(cx - side_px / W / 2, cy - side_px / H / 2, side_px / W, side_px / H);
        }

        if (r.found) {
            have_face = true; miss_count = 0;
            applyFilters(r, dt);
            r.ms = static_cast<int>((clock.nsecsElapsed() - t0) / 1000000);
            owner->deliverFace(r);
        } else if (have_face && miss_count < kMaxMiss) {
            miss_count++;   // คงค่าล่าสุดสั้นๆ (QML ยังเห็น found จากค่าก่อน)
        } else {
            if (have_face) { for (auto &f2 : filt) f2.reset(); }
            have_face = false; track_crop = QRectF();
            PswrapFaceResult none; none.ms = static_cast<int>((clock.nsecsElapsed() - t0) / 1000000);
            owner->deliverFace(none);
        }
    }
}
