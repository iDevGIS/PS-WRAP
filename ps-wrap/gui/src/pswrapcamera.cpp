// PS-WRAP: DirectShow camera via libavdevice (ดู pswrapcamera.h)
#include <pswrapcamera.h>

#include <QVideoSink>
#include <QVideoFrameFormat>
#include <QMetaObject>
#include <QLoggingCategory>
#include <QCoreApplication>

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
    refreshDevices();
}

PswrapCamera::~PswrapCamera()
{
    stopWorker();
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

void PswrapCamera::restart()
{
    stopWorker();
    if (!is_active || device_name.isEmpty())
        return;
    if (!last_error.isEmpty()) {
        last_error.clear();
        emit errorChanged();
    }
    worker = new PswrapCameraWorker(this, device_name);
    worker->start();
}

void PswrapCamera::stopWorker()
{
    if (!worker)
        return;
    worker->requestStop();
    if (!worker->wait(3000)) {
        qCWarning(pswrapCam) << "camera worker did not stop in time, terminating";
        worker->terminate();
        worker->wait(1000);
    }
    delete worker;
    worker = nullptr;
    if (is_running) {
        is_running = false;
        emit runningChanged();
    }
}

void PswrapCamera::deliverFrame(const QVideoFrame &frame)
{
    // worker thread → main thread (QVideoFrame เป็น value type แชร์ buffer ได้)
    QMetaObject::invokeMethod(this, [this, frame]() {
        if (sink)
            sink->setVideoFrame(frame);
        emit frameReady(frame);
    }, Qt::QueuedConnection);
}

void PswrapCamera::reportError(const QString &msg)
{
    QMetaObject::invokeMethod(this, [this, msg]() {
        qCWarning(pswrapCam) << "camera error:" << msg;
        last_error = msg;
        emit errorChanged();
    }, Qt::QueuedConnection);
}

void PswrapCamera::reportRunning(bool r)
{
    QMetaObject::invokeMethod(this, [this, r]() {
        if (is_running == r)
            return;
        is_running = r;
        emit runningChanged();
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------- worker
PswrapCameraWorker::PswrapCameraWorker(PswrapCamera *owner, const QString &device)
    : owner(owner), device(device)
{
}

static int pswrap_interrupt_cb(void *opaque)
{
    auto *w = static_cast<PswrapCameraWorker *>(opaque);
    return w && w->stopRequested() ? 1 : 0;
}

void PswrapCameraWorker::run()
{
    static std::atomic<bool> registered { false };
    if (!registered.exchange(true))
        avdevice_register_all();

    const AVInputFormat *in_fmt = av_find_input_format("dshow");
    if (!in_fmt) {
        owner->reportError(QStringLiteral("dshow input not available in this ffmpeg build"));
        return;
    }

    AVFormatContext *fmt_ctx = avformat_alloc_context();
    fmt_ctx->interrupt_callback.callback = pswrap_interrupt_cb;
    fmt_ctx->interrupt_callback.opaque = this;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rtbufsize", "64M", 0);   // กัน "real-time buffer too full" กับกล้องเสมือน 1080p

    const QByteArray url = (QStringLiteral("video=") + device).toUtf8();
    int ret = avformat_open_input(&fmt_ctx, url.constData(), in_fmt, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char buf[256];
        av_strerror(ret, buf, sizeof(buf));
        owner->reportError(QStringLiteral("cannot open \"%1\": %2").arg(device, QString::fromUtf8(buf)));
        avformat_free_context(fmt_ctx);
        return;
    }
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        owner->reportError(QStringLiteral("no stream info for \"%1\"").arg(device));
        avformat_close_input(&fmt_ctx);
        return;
    }
    int stream_index = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { stream_index = static_cast<int>(i); break; }
    }
    if (stream_index < 0) {
        owner->reportError(QStringLiteral("no video stream on \"%1\"").arg(device));
        avformat_close_input(&fmt_ctx);
        return;
    }
    AVCodecParameters *par = fmt_ctx->streams[stream_index]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        owner->reportError(QStringLiteral("no decoder for camera format"));
        avformat_close_input(&fmt_ctx);
        return;
    }
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, par);
    codec_ctx->thread_count = 2;
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        owner->reportError(QStringLiteral("cannot open decoder"));
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    qCInfo(pswrapCam) << "camera opened:" << device << par->width << "x" << par->height << "codec" << codec->name;
    owner->reportRunning(true);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    SwsContext *sws = nullptr;

    while (!stopRequested()) {
        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) { msleep(2); continue; }
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
            owner->deliverFrame(vf);
        }
    }

    if (sws) sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    owner->reportRunning(false);
    qCInfo(pswrapCam) << "camera closed:" << device;
}
