// PS-WRAP: DirectShow camera via libavdevice (ดู pswrapcamera.h)
#include <pswrapcamera.h>

#include <QVideoSink>
#include <QVideoFrameFormat>
#include <QMetaObject>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QMutex>
#include <QWaitCondition>
#include <QDeadlineTimer>
#include <QScopeGuard>
#include <QHash>
#include <QSet>
#include <QTimer>

#include <mutex>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#ifdef Q_OS_WIN
#include <windows.h>
#include <dshow.h>
#include <oleauto.h>
#endif

Q_LOGGING_CATEGORY(pswrapCam, "pswrap.camera")

namespace {
constexpr int kNoVideoMs = 3000;        // ไม่มีเฟรมแรก / เฟรมหยุดนานเท่านี้ → state "error" (worker ยังพยายามต่อ — เฟรมมาเมื่อไหร่กลับ live)
constexpr int kProbeDeadlineMs = 10000; // avformat_find_stream_info ต้องเสร็จภายในนี้ (กล้องเปิดได้แต่ไม่ส่งเฟรม)
constexpr int kExitWaitMs = 4000;       // ตอนออกจากแอป รอ worker ปิด device ได้นานสุดเท่านี้

// ทะเบียน worker ทั้ง process — leak ตั้งใจ (worker thread อาจยังวิ่งตอน static destructor ทำงาน)
struct CamRegistry {
    QMutex mutex;
    QWaitCondition cond;
    QSet<PswrapCameraWorker *> alive;   // สร้างแล้ว run() ยังไม่จบ
    QHash<QString, int> closing;        // ชื่อ device → จำนวน worker ที่สั่งหยุดแล้วแต่ยังปิดไม่เสร็จ
};
CamRegistry &camRegistry()
{
    static CamRegistry *r = new CamRegistry;
    return *r;
}

void pswrapCamPostRoutine()
{
    // ~QCoreApplication: QML ถูกทำลายแล้ว (PswrapCamera สั่งหยุดแบบไม่รอ) → รอให้ worker ปล่อย device จริงก่อน process จบ
    if (!PswrapCameraWorker::stopAllAndWait(kExitWaitMs))
        qCWarning(pswrapCam) << "camera worker still closing at exit after" << kExitWaitMs << "ms — the device may need replugging";
}

QString ffErr(int ret)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(ret, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

int pswrap_interrupt_cb(void *opaque)
{
    auto *w = static_cast<PswrapCameraWorker *>(opaque);
    return w && w->interruptNow() ? 1 : 0;
}

const QString kIdle = QStringLiteral("idle");
const QString kOpening = QStringLiteral("opening");
const QString kLive = QStringLiteral("live");
const QString kError = QStringLiteral("error");
}

// ---------------------------------------------------------------- enumeration
QStringList PswrapCamera::enumerateDirectShowDevices()
{
    QStringList out;
#ifdef Q_OS_WIN
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool need_uninit = (init == S_OK || init == S_FALSE);
    ICreateDevEnum *dev_enum = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, reinterpret_cast<void **>(&dev_enum))) && dev_enum) {
        IEnumMoniker *enum_moniker = nullptr;
        if (dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_moniker, 0) == S_OK && enum_moniker) {
            IMoniker *moniker = nullptr;
            while (enum_moniker->Next(1, &moniker, nullptr) == S_OK) {
                IPropertyBag *bag = nullptr;
                if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, reinterpret_cast<void **>(&bag))) && bag) {
                    VARIANT var;
                    VariantInit(&var);
                    if (SUCCEEDED(bag->Read(L"FriendlyName", &var, nullptr)) && var.vt == VT_BSTR && var.bstrVal) {
                        const QString name = QString::fromWCharArray(var.bstrVal);
                        if (!name.isEmpty() && !out.contains(name))
                            out.append(name);
                    }
                    VariantClear(&var);
                    bag->Release();
                }
                moniker->Release();
            }
            enum_moniker->Release();
        }
        dev_enum->Release();
    }
    if (need_uninit)
        CoUninitialize();
#endif
    return out;
}

QString PswrapCamera::appDir() const { return QCoreApplication::applicationDirPath(); }

// ---------------------------------------------------------------- PswrapCamera
PswrapCamera::PswrapCamera(QObject *parent)
    : QObject(parent)
{
    static bool post_routine_added = false;   // สร้างบน GUI thread เท่านั้น (QML)
    if (!post_routine_added && QCoreApplication::instance()) {
        qAddPostRoutine(pswrapCamPostRoutine);
        post_routine_added = true;
    }
    watchdog = new QTimer(this);
    watchdog->setInterval(500);
    connect(watchdog, &QTimer::timeout, this, &PswrapCamera::onWatchdog);
    refreshDevices();
}

PswrapCamera::~PswrapCamera()
{
    // ไม่รอ: worker ปิด device บน thread ตัวเองแล้วลบตัวเอง (ออกจากแอป → post routine รอให้)
    ++generation;
    if (worker) {
        disconnect(worker, nullptr, this, nullptr);
        worker->requestStop();
        worker = nullptr;
    }
}

void PswrapCamera::refreshDevices()
{
    const QStringList list = enumerateDirectShowDevices();
    if (list == device_list)
        return;
    device_list = list;
    emit devicesChanged();
}

void PswrapCamera::setDeviceName(const QString &name)
{
    if (name == device_name)
        return;
    device_name = name;
    emit deviceNameChanged();
    restart();
}

void PswrapCamera::setActive(bool v)
{
    if (v == is_active)
        return;
    is_active = v;
    emit activeChanged();
    restart();
}

void PswrapCamera::setVideoSink(QVideoSink *s)
{
    if (s == sink)
        return;
    sink = s;
    emit videoSinkChanged();
}

void PswrapCamera::setState(const QString &s, const QString &user_error)
{
    if (s == cam_state && user_error == error_string)
        return;
    cam_state = s;
    error_string = user_error;
    emit stateChanged();
}

void PswrapCamera::setRunning(bool r)
{
    if (is_running == r)
        return;
    is_running = r;
    emit runningChanged();
}

void PswrapCamera::restart()
{
    stopWorker();
    if (frames_received || frames_notified) {
        frames_received = frames_notified = 0;
        emit framesReceivedChanged();
    }
    if (!is_active || device_name.isEmpty()) {
        setState(kIdle);
        return;
    }
    if (!last_error.isEmpty()) {
        last_error.clear();
        emit errorChanged();
    }
    setState(kOpening);

    const quint64 gen = ++generation;
    auto *w = new PswrapCameraWorker(device_name);
    // ทุกสัญญาณ queued + เช็ค generation: worker ที่สั่งหยุดแล้วอาจยังมีเฟรม/สถานะค้างในคิว
    connect(w, &PswrapCameraWorker::opened, this, [this, gen](int, int, const QString &) {
        if (gen == generation)
            setRunning(true);
    }, Qt::QueuedConnection);
    connect(w, &PswrapCameraWorker::frameCaptured, this, [this, gen](const QVideoFrame &frame) {
        if (gen != generation)
            return;
        ++frames_received;
        frame_clock.restart();
        if (cam_state != kLive)
            setState(kLive);
        if (frames_received == 1) {
            frames_notified = 1;
            emit framesReceivedChanged();
        }
        if (sink)
            sink->setVideoFrame(frame);
        emit frameReady(frame);
    }, Qt::QueuedConnection);
    connect(w, &PswrapCameraWorker::failed, this, [this, gen](const QString &user_msg, const QString &detail) {
        if (gen != generation)
            return;
        last_error = detail;
        emit errorChanged();
        setState(kError, user_msg);
    }, Qt::QueuedConnection);
    connect(w, &PswrapCameraWorker::closed, this, [this, gen]() {
        if (gen != generation)
            return;
        // worker จบเอง (เปิดไม่ได้/เฟรมหยุด/device หลุด) — ไม่ใช่เพราะเราสั่งหยุด
        worker = nullptr;
        watchdog->stop();
        setRunning(false);
        if (cam_state != kError)
            setState(kError, tr("The camera closed unexpectedly — try replugging it"));
    }, Qt::QueuedConnection);
    connect(w, &QThread::finished, w, &QObject::deleteLater);
    worker = w;
    open_clock.start();
    frame_clock.invalidate();
    watchdog->start();
    w->start();
}

void PswrapCamera::stopWorker()
{
    // ไม่บล็อก GUI: ตั้ง flag → worker ตัด av_read_frame/probe, avformat_close_input บน thread ตัวเอง แล้ว deleteLater
    ++generation;
    watchdog->stop();
    if (worker) {
        disconnect(worker, nullptr, this, nullptr);
        worker->requestStop();
        worker = nullptr;
    }
    setRunning(false);
}

void PswrapCamera::onWatchdog()
{
    if (!worker) {
        watchdog->stop();
        return;
    }
    if (frames_received != frames_notified) {
        frames_notified = frames_received;
        emit framesReceivedChanged();
    }
    if (cam_state == kError)
        return;
    if (frames_received == 0) {
        if (open_clock.elapsed() >= kNoVideoMs) {
            qCWarning(pswrapCam) << "no video from camera" << device_name << "after" << open_clock.elapsed() << "ms"
                                 << (is_running ? "(device opened, no frames)" : "(still opening / waiting for the device)");
            setState(kError, tr("Camera is busy or not responding — try replugging it"));
        }
    } else if (frame_clock.isValid() && frame_clock.elapsed() >= kNoVideoMs) {
        qCWarning(pswrapCam) << "camera" << device_name << "stopped sending frames for" << frame_clock.elapsed() << "ms";
        setState(kError, tr("The camera stopped sending video — try replugging it"));
    }
}

// ---------------------------------------------------------------- worker
PswrapCameraWorker::PswrapCameraWorker(const QString &device)
    : device(device)
{
    CamRegistry &r = camRegistry();
    QMutexLocker lock(&r.mutex);
    r.alive.insert(this);   // ลงทะเบียนตั้งแต่สร้าง (owner start ทันที) — post routine ตอนออกจะรอได้ครบ
}

void PswrapCameraWorker::requestStopLocked()
{
    stop_flag.store(1);
    if (!done && !closing_registered) {
        closing_registered = true;
        ++camRegistry().closing[device];
    }
}

void PswrapCameraWorker::requestStop()
{
    CamRegistry &r = camRegistry();
    QMutexLocker lock(&r.mutex);
    requestStopLocked();
    r.cond.wakeAll();   // ปลุก worker ตัวนี้ถ้ารอ device ตัวเก่าอยู่
}

bool PswrapCameraWorker::interruptNow()
{
    if (stopRequested())
        return true;
    const qint64 dl = deadline_ms.load();
    if (dl >= 0 && run_clock.elapsed() > dl)
        return true;
    if (throttle_spin.load())
        QThread::usleep(1000);   // probe แบบ NONBLOCK ได้ EAGAIN แล้ววนทันที → หน่วงให้ไม่กิน CPU เต็ม core
    return false;
}

bool PswrapCameraWorker::stopAllAndWait(int timeout_ms)
{
    CamRegistry &r = camRegistry();
    QMutexLocker lock(&r.mutex);
    for (PswrapCameraWorker *w : std::as_const(r.alive))
        w->requestStopLocked();
    r.cond.wakeAll();
    QDeadlineTimer deadline(timeout_ms);
    while (!r.alive.isEmpty()) {
        if (!r.cond.wait(&r.mutex, deadline))
            break;
    }
    return r.alive.isEmpty();
}

int PswrapCameraWorker::openInput(AVFormatContext **ctx, bool with_hints, QString *detail)
{
    const AVInputFormat *in_fmt = av_find_input_format("dshow");
    if (!in_fmt) {
        *detail = QStringLiteral("dshow input not available in this ffmpeg build");
        return AVERROR_DEMUXER_NOT_FOUND;
    }
    AVFormatContext *fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        *detail = QStringLiteral("out of memory");
        return AVERROR(ENOMEM);
    }
    fmt_ctx->interrupt_callback.callback = pswrap_interrupt_cb;
    fmt_ctx->interrupt_callback.opaque = this;
    // NONBLOCK: dshow_read_packet คืน EAGAIN แทน WaitForMultipleObjects(INFINITE) — กล้องไม่ส่งเฟรมก็ยังหยุด worker ได้ทันที
    fmt_ctx->flags |= AVFMT_FLAG_NONBLOCK;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rtbufsize", "64M", 0);   // กัน "real-time buffer too full" กับกล้องเสมือน 1080p
    if (with_hints) {
        // facecam ไม่ต้องใหญ่: ขอ 720p30 ก่อน (กล้องบางตัว default = 1080p YUY2 5 fps) · ไม่รองรับ → เปิดใหม่แบบ default
        av_dict_set(&opts, "video_size", "1280x720", 0);
        av_dict_set(&opts, "framerate", "30", 0);
    }

    const QByteArray url = (QStringLiteral("video=") + device).toUtf8();
    const int ret = avformat_open_input(&fmt_ctx, url.constData(), in_fmt, &opts);   // ล้มเหลว = free ctx + ตั้ง nullptr ให้เอง
    av_dict_free(&opts);
    if (ret < 0) {
        *detail = QStringLiteral("cannot open \"%1\"%2: %3").arg(device, with_hints ? QStringLiteral(" (1280x720@30)") : QString(), ffErr(ret));
        *ctx = nullptr;
        return ret;
    }
    *ctx = fmt_ctx;
    return 0;
}

void PswrapCameraWorker::run()
{
    run_clock.start();
    CamRegistry &reg = camRegistry();
    bool opened_ok = false;
    auto finish = qScopeGuard([this, &reg]() {
        QMutexLocker lock(&reg.mutex);
        done = true;
        reg.alive.remove(this);
        if (closing_registered) {
            closing_registered = false;
            int &n = reg.closing[device];
            if (--n <= 0)
                reg.closing.remove(device);
        }
        reg.cond.wakeAll();
    });
    auto fail = [this](const QString &user_msg, const QString &detail) {
        if (stopRequested())
            return;   // ถูกสั่งหยุดระหว่างเปิด — ไม่ใช่ error
        qCWarning(pswrapCam) << "camera open failed:" << detail;
        emit failed(user_msg, detail);
    };
    const QString busy_msg = QCoreApplication::translate("PswrapCamera", "Camera is busy or not responding — try replugging it");

    // ตัวก่อนหน้าของ device เดียวกันยังปิดไม่เสร็จ → รอ (dshow เปิดซ้อนกับตัวที่กำลัง Stop graph = busy/ค้าง)
    {
        QMutexLocker lock(&reg.mutex);
        bool logged = false;
        while (reg.closing.value(device) > 0 && !stopRequested()) {
            if (!logged) {
                qCInfo(pswrapCam) << "waiting for the previous close of" << device;
                logged = true;
            }
            reg.cond.wait(&reg.mutex, 50);
        }
        if (logged)
            qCInfo(pswrapCam) << "previous close done after" << run_clock.elapsed() << "ms";
    }
    if (stopRequested()) {
        emit closed();
        return;
    }

    static std::once_flag av_registered;   // worker หลายตัวพร้อมกัน — ต้องลงทะเบียนเสร็จก่อนใครเรียก av_find_input_format
    std::call_once(av_registered, []() { avdevice_register_all(); });

    AVFormatContext *fmt_ctx = nullptr;
    QString detail;
    int ret = openInput(&fmt_ctx, true, &detail);
    if (ret < 0 && !stopRequested()) {
        qCInfo(pswrapCam) << "camera open with 720p30 hint failed, retrying with the device default:" << detail;
        ret = openInput(&fmt_ctx, false, &detail);
    }
    if (ret < 0) {
        fail(busy_msg, detail);
        emit closed();
        return;
    }

    AVCodecContext *codec_ctx = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr;
    SwsContext *sws = nullptr;
    // ปิดทุกอย่างบน thread นี้เสมอ (รวม avformat_close_input → IMediaControl::Stop + ปล่อย graph) — GUI ไม่ต้องรอ
    auto cleanup = qScopeGuard([&]() {
        const qint64 t0 = run_clock.elapsed();
        if (sws) sws_freeContext(sws);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        if (opened_ok)
            qCInfo(pswrapCam) << "camera closed:" << device << "(close took" << (run_clock.elapsed() - t0) << "ms)";
        emit closed();
    });

    // probe มีกำหนดเวลา + หน่วงการวน EAGAIN · ถูกสั่งหยุดก็หลุดทันที (AVERROR_EXIT)
    deadline_ms.store(run_clock.elapsed() + kProbeDeadlineMs);
    throttle_spin.store(1);
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    throttle_spin.store(0);
    deadline_ms.store(-1);
    if (ret < 0) {
        fail(busy_msg, QStringLiteral("no stream info for \"%1\": %2").arg(device, ffErr(ret)));
        return;
    }
    int stream_index = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { stream_index = static_cast<int>(i); break; }
    }
    if (stream_index < 0) {
        fail(busy_msg, QStringLiteral("no video stream on \"%1\"").arg(device));
        return;
    }
    AVCodecParameters *par = fmt_ctx->streams[stream_index]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        fail(QCoreApplication::translate("PswrapCamera", "This camera's video format is not supported"),
             QStringLiteral("no decoder for camera format of \"%1\" (codec id %2)").arg(device).arg(static_cast<int>(par->codec_id)));
        return;
    }
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx || avcodec_parameters_to_context(codec_ctx, par) < 0) {
        fail(busy_msg, QStringLiteral("cannot set up decoder for \"%1\"").arg(device));
        return;
    }
    codec_ctx->thread_count = 2;
    if ((ret = avcodec_open2(codec_ctx, codec, nullptr)) < 0) {
        fail(QCoreApplication::translate("PswrapCamera", "This camera's video format is not supported"),
             QStringLiteral("cannot open decoder for \"%1\": %2").arg(device, ffErr(ret)));
        return;
    }

    opened_ok = true;
    qCInfo(pswrapCam) << "camera opened:" << device << par->width << "x" << par->height << "codec" << codec->name
                      << "in" << run_clock.elapsed() << "ms";
    emit opened(par->width, par->height, QString::fromUtf8(codec->name));

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        fail(busy_msg, QStringLiteral("out of memory"));
        return;
    }

    while (!stopRequested()) {
        ret = av_read_frame(fmt_ctx, pkt);
        if (ret == AVERROR(EAGAIN)) { msleep(4); continue; }   // NONBLOCK: ยังไม่มีเฟรม (owner watchdog ตัดสินว่า "ไม่มีภาพ")
        if (ret < 0) {
            if (!stopRequested()) {
                qCWarning(pswrapCam) << "camera read failed:" << device << ffErr(ret);
                emit failed(QCoreApplication::translate("PswrapCamera", "The camera stopped sending video — try replugging it"),
                            QStringLiteral("read error on \"%1\": %2").arg(device, ffErr(ret)));
            }
            break;
        }
        if (pkt->stream_index != stream_index) { av_packet_unref(pkt); continue; }
        if (avcodec_send_packet(codec_ctx, pkt) < 0) { av_packet_unref(pkt); continue; }
        av_packet_unref(pkt);
        while (!stopRequested() && avcodec_receive_frame(codec_ctx, frame) == 0) {
            const int w = frame->width, h = frame->height;
            if (w <= 0 || h <= 0) continue;
            sws = sws_getCachedContext(sws, w, h, static_cast<AVPixelFormat>(frame->format), w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) continue;
            QVideoFrame vf(QVideoFrameFormat(QSize(w, h), QVideoFrameFormat::Format_RGBA8888));
            if (!vf.map(QVideoFrame::WriteOnly)) continue;
            uint8_t *dst[4] = { vf.bits(0), nullptr, nullptr, nullptr };
            int dst_stride[4] = { vf.bytesPerLine(0), 0, 0, 0 };
            sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dst_stride);
            vf.unmap();
            emit frameCaptured(vf);
        }
    }
}
