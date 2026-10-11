// PS-WRAP Link (spike) — ดูคำอธิบายใน pswraplink.h
#include <pswraplink.h>
#include <pswraprecorder.h>
#include "streamsession.h"

#include <chiaki/controller.h>
#include <chiaki/time.h>
#include <rtc/rtc.hpp>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>   // timeBeginPeriod — pacer ต้องการ sleep 1 ms จริง
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

Q_LOGGING_CATEGORY(pswrapLink, "pswrap.link", QtInfoMsg)

namespace {

constexpr uint16_t kPort = 18800;
constexpr int kBits = 32;          // บาร์โค้ด = เวลา ms 32 บิตล่าง (หน้าเว็บคิด mod 2^32)
constexpr int kBarHeight = 48;
constexpr uint8_t kPayloadType = 96;
constexpr uint32_t kSsrc = 42;
constexpr int kPlayoutDelayExtId = 6;
constexpr int64_t kPadTimeoutUs = 500000;   // ไม่มีข้อความจอยนานกว่านี้ = ปล่อยทุกปุ่ม
constexpr float kStickDeadzone = 0.12f;     // OR ของจอยใช้ค่าสัมบูรณ์มากสุด — stick เพื่อนที่ลอยนิดๆ จะแย่ง stick เรา

int64_t wallMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

int64_t monoUs()
{
	return static_cast<int64_t>(chiaki_time_now_monotonic_us());
}

QString avErr(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(err, buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

// ---------------------------------------------------------------- tap → NVENC low-latency
class LinkTap : public PsWrapRawTap
{
public:
	explicit LinkTap(int kbps) : kbps(kbps), default_kbps(kbps) {}
	~LinkTap() override
	{
		stopThread();
		av_frame_free(&pending.frame);
		av_frame_free(&own);
		av_packet_free(&pkt);
		if (enc)
			avcodec_free_context(&enc);
		if (sws)
			sws_freeContext(sws);
	}

	// ทุก callback ด้านล่างเรียกบน thread ของ tap — ห้ามบล็อก
	std::function<void(const uint8_t *data, size_t size, int64_t pts_ms)> send;
	std::function<void()> on_detached;   // GUI thread
	std::atomic<bool> want_idr{true};
	std::atomic<int> max_height{1080};   // เพื่อนขอได้ (มือถือ 720) — เปลี่ยนแล้ว encoder เปิดใหม่ที่เฟรมถัดไป
	std::atomic<int> target_kbps{0};     // 0 = ค่าตั้งต้น (env) · เพื่อนขอได้ — เปลี่ยนแล้วเปิด encoder ใหม่

	void startThread() { th = std::thread([this]() { run(); }); }
	void stopThread()
	{
		{
			std::lock_guard<std::mutex> lock(m);
			quit = true;
		}
		cv.notify_all();
		if (th.joinable())
			th.join();
	}

	void videoFrame(const AVFrame *frame) override { videoFrameAt(frame, -1); }
	void videoFrameAt(const AVFrame *frame, int64_t capture_us) override
	{
		if (frame->format != AV_PIX_FMT_NV12) {
			wrong_format++;
			return;
		}
		AVFrame *ref = av_frame_clone(frame);
		if (!ref)
			return;
		{
			std::lock_guard<std::mutex> lock(m);
			// latency มาก่อน: เก็บแค่เฟรมล่าสุด เฟรมที่ยังไม่ได้ encode ทิ้งเลย
			if (pending.frame) {
				av_frame_free(&pending.frame);
				dropped++;
			}
			pending = Pending{ref, capture_us, monoUs()};
		}
		cv.notify_one();
	}
	void audioMix(const float *, const float *, int64_t, int64_t) override {}
	void detached() override
	{
		if (on_detached)
			on_detached();
	}

	std::atomic<quint64> dropped{0}, wrong_format{0};

private:
	struct Pending
	{
		AVFrame *frame = nullptr;
		int64_t capture_us = -1;
		int64_t tapped_us = 0;
	};

	int kbps;             // ที่ encoder ใช้อยู่ (thread ของ tap)
	const int default_kbps;
	std::thread th;
	std::mutex m;
	std::condition_variable cv;
	Pending pending;
	bool quit = false;

	// thread ของ tap เท่านั้น
	AVCodecContext *enc = nullptr;
	AVFrame *own = nullptr;   // สำเนาของเรา (แปะบาร์โค้ดได้ ไม่แตะเฟรมที่ recorder ใช้ต่อ)
	AVPacket *pkt = nullptr;
	SwsContext *sws = nullptr;   // ย่อ (pipeline อาจเป็น 4K — มือถือ decode 4K60 ไม่ไหว: decode 100–160 ms)
	int sws_src_w = 0, sws_src_h = 0, sws_dst_w = 0, sws_dst_h = 0;
	double scale_sum = 0;
	QString encoder_name;
	bool enc_failed = false;
	int64_t stat_start_us = 0;
	int stat_n = 0;
	double cap_sum = 0, cap_max = 0, tap_sum = 0, enc_sum = 0, enc_max = 0;
	uint64_t stat_bytes = 0, stat_frame_max = 0;
	quint64 stat_dropped0 = 0;

	bool openEncoder(int w, int h)
	{
		if (enc)
			avcodec_free_context(&enc);
		const char *const names[] = {"h264_nvenc", "libx264"};
		for (const char *n : names) {
			const AVCodec *codec = avcodec_find_encoder_by_name(n);
			if (!codec)
				continue;
			AVCodecContext *c = avcodec_alloc_context3(codec);
			c->width = w;
			c->height = h;
			c->pix_fmt = AV_PIX_FMT_NV12;
			c->time_base = AVRational{1, 1000};
			c->framerate = AVRational{60, 1};
			c->bit_rate = int64_t(kbps) * 1000;
			c->rc_max_rate = c->bit_rate;
			c->rc_buffer_size = static_cast<int>(c->bit_rate / 30);   // VBV ~2 เฟรม
			c->gop_size = 60 * 30;                                    // keyframe เมื่อเพื่อนขอ (PLI) เป็นหลัก
			c->max_b_frames = 0;
			c->color_range = AVCOL_RANGE_MPEG;
			c->colorspace = AVCOL_SPC_BT709;
			c->color_primaries = AVCOL_PRI_BT709;
			c->color_trc = AVCOL_TRC_BT709;
			if (!strcmp(n, "h264_nvenc")) {
				av_opt_set(c->priv_data, "preset", "p4", 0);
				av_opt_set(c->priv_data, "tune", "ull", 0);
				av_opt_set(c->priv_data, "rc", "cbr", 0);
				av_opt_set(c->priv_data, "zerolatency", "1", 0);
				av_opt_set(c->priv_data, "delay", "0", 0);
				av_opt_set(c->priv_data, "forced-idr", "1", 0);
				av_opt_set(c->priv_data, "profile", "baseline", 0);
			} else {
				av_opt_set(c->priv_data, "preset", "ultrafast", 0);
				av_opt_set(c->priv_data, "tune", "zerolatency", 0);
				av_opt_set(c->priv_data, "profile", "baseline", 0);
			}
			const int err = avcodec_open2(c, codec, nullptr);
			if (err < 0) {
				qCWarning(pswrapLink) << "encoder" << n << "failed:" << avErr(err);
				avcodec_free_context(&c);
				continue;
			}
			enc = c;
			encoder_name = QString::fromLatin1(n);
			qCInfo(pswrapLink).nospace() << "encoder " << n << " " << w << "x" << h << " " << kbps << " kbps";
			return true;
		}
		return false;
	}

	void stamp(AVFrame *f, uint32_t value)
	{
		const int w = f->width;
		const int bar = std::min(kBarHeight, f->height);
		const int bw = w / kBits;
		for (int i = 0; i < kBits; i++) {
			const uint8_t v = (value >> (kBits - 1 - i)) & 1 ? 235 : 16;
			for (int y = 0; y < bar; y++)
				memset(f->data[0] + y * f->linesize[0] + i * bw, v, bw);
		}
		for (int y = 0; y < bar / 2; y++)
			memset(f->data[1] + y * f->linesize[1], 128, w);   // chroma กลาง — สีเกมใต้แถบไม่ให้อ่านผิด
	}

	void run()
	{
		pkt = av_packet_alloc();
		stat_start_us = monoUs();
		for (;;) {
			Pending p;
			{
				std::unique_lock<std::mutex> lock(m);
				cv.wait(lock, [this]() { return quit || pending.frame; });
				if (quit)
					return;
				p = pending;
				pending = Pending();
			}
			encodeOne(p);
			av_frame_free(&p.frame);
			report();
		}
	}

	void encodeOne(const Pending &p)
	{
		const AVFrame *src = p.frame;
		int w = src->width & ~1, h = src->height & ~1;
		const int mh = max_height.load();
		if (mh > 0 && h > mh) {
			w = static_cast<int>(std::lround(double(src->width) * mh / src->height)) & ~1;
			h = mh & ~1;
		}
		const bool scaling = w != (src->width & ~1) || h != (src->height & ~1);
		if (enc_failed)
			return;
		const int want_kbps = target_kbps.load() > 0 ? target_kbps.load() : default_kbps;
		if (!enc || enc->width != w || enc->height != h || want_kbps != kbps) {
			kbps = want_kbps;
			av_frame_free(&own);
			if (!openEncoder(w, h)) {
				qCWarning(pswrapLink) << "no usable encoder — Link video off";
				enc_failed = true;
				return;
			}
			want_idr = true;
		}
		if (!own) {
			own = av_frame_alloc();
			own->format = AV_PIX_FMT_NV12;
			own->width = w;
			own->height = h;
			if (av_frame_get_buffer(own, 0) < 0) {
				av_frame_free(&own);
				return;
			}
		}
		const int64_t t0 = monoUs();
		av_frame_make_writable(own);
		if (scaling) {
			// ต้องเช็คขนาดปลายทางด้วย — เพื่อนเปลี่ยนความสูงกลางทาง (1080→720) แล้ว context เก่าเขียนเกินบัฟเฟอร์ (L04 แอปพัง)
			if (!sws || sws_src_w != src->width || sws_src_h != src->height || sws_dst_w != w || sws_dst_h != h) {
				if (sws)
					sws_freeContext(sws);
				sws = sws_alloc_context();
				av_opt_set_int(sws, "srcw", src->width, 0);
				av_opt_set_int(sws, "srch", src->height, 0);
				av_opt_set_int(sws, "src_format", AV_PIX_FMT_NV12, 0);
				av_opt_set_int(sws, "dstw", w, 0);
				av_opt_set_int(sws, "dsth", h, 0);
				av_opt_set_int(sws, "dst_format", AV_PIX_FMT_NV12, 0);
				av_opt_set_int(sws, "sws_flags", SWS_BILINEAR, 0);
				av_opt_set_int(sws, "threads", 4, 0);
				if (sws_init_context(sws, nullptr, nullptr) < 0) {
					sws_freeContext(sws);
					sws = nullptr;
					return;
				}
				sws_src_w = src->width;
				sws_src_h = src->height;
				sws_dst_w = w;
				sws_dst_h = h;
				qCInfo(pswrapLink).nospace() << "scale " << src->width << "x" << src->height << " -> " << w << "x" << h;
			}
			sws_scale(sws, src->data, src->linesize, 0, src->height, own->data, own->linesize);
			scale_sum += (monoUs() - t0) / 1000.0;
		} else {
			// คัดลอกเฉพาะขนาดที่ encoder ใช้ (เฟรมต้นทางอาจกว้าง/สูงเป็นเลขคี่)
			for (int y = 0; y < h; y++)
				memcpy(own->data[0] + y * own->linesize[0], src->data[0] + y * src->linesize[0], w);
			for (int y = 0; y < h / 2; y++)
				memcpy(own->data[1] + y * own->linesize[1], src->data[1] + y * src->linesize[1], w);
		}
		// เวลา "ตอนจับภาพ" ตามนาฬิกาจริง = ตอนนี้ - (ผ่านไปกี่ µs ตั้งแต่จับ)
		const int64_t cap_wall = p.capture_us > 0 ? wallMs() - (t0 - p.capture_us) / 1000 : wallMs();
		stamp(own, static_cast<uint32_t>(cap_wall));
		own->pts = src->pts;
		own->pict_type = want_idr.exchange(false) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
		if (avcodec_send_frame(enc, own) < 0)
			return;
		while (avcodec_receive_packet(enc, pkt) == 0) {
			const double enc_ms = (monoUs() - t0) / 1000.0;
			enc_sum += enc_ms;
			enc_max = std::max(enc_max, enc_ms);
			stat_bytes += uint64_t(pkt->size);
			stat_frame_max = std::max(stat_frame_max, uint64_t(pkt->size));
			if (send)
				send(pkt->data, size_t(pkt->size), pkt->pts);
			av_packet_unref(pkt);
		}
		if (p.capture_us > 0) {
			const double cap_ms = (p.tapped_us - p.capture_us) / 1000.0;
			cap_sum += cap_ms;
			cap_max = std::max(cap_max, cap_ms);
		}
		tap_sum += (t0 - p.tapped_us) / 1000.0;
		stat_n++;
	}

	void report()
	{
		const int64_t now = monoUs();
		if (now - stat_start_us < 5000000 || stat_n == 0)
			return;
		const double secs = (now - stat_start_us) / 1e6;
		const quint64 drop = dropped.load();
		qCInfo(pswrapLink).noquote() << QStringLiteral(
			"[enc] %1 %2x%3  frames %4 (%5 fps)  capture→tap avg %6 max %7 ms  tap→encode %8 ms  scale %14 ms  encode avg %9 max %10 ms  "
			"sent %11 kbps  frame max %12 KB  dropped %13")
			.arg(encoder_name).arg(enc ? enc->width : 0).arg(enc ? enc->height : 0).arg(stat_n).arg(stat_n / secs, 0, 'f', 1)
			.arg(cap_sum / stat_n, 0, 'f', 1).arg(cap_max, 0, 'f', 1).arg(tap_sum / stat_n, 0, 'f', 2)
			.arg(enc_sum / stat_n, 0, 'f', 2).arg(enc_max, 0, 'f', 2).arg(stat_bytes * 8 / 1000.0 / secs, 0, 'f', 0)
			.arg(stat_frame_max / 1024.0, 0, 'f', 1).arg(drop - stat_dropped0).arg(scale_sum / stat_n, 0, 'f', 2);
		stat_start_us = now;
		stat_n = 0;
		cap_sum = cap_max = tap_sum = enc_sum = enc_max = scale_sum = 0;
		stat_bytes = stat_frame_max = 0;
		stat_dropped0 = drop;
	}
};

// ---------------------------------------------------------------- pacer
// ทยอยปล่อย RTP แทนการปล่อยทั้งเฟรมทีเดียว (เครื่องปลายทาง/เน็ตบางตัวรับ burst ไม่ไหว — L07 หาย ~20%)
// ไม่ใช้ rtc::PacingHandler: มันจำกัด budget สะสมไว้แค่ 1 sendInterval (2 ms) แต่ timer Windows ตื่นทุก ~15.6 ms
// → ส่งได้ ~1/8 ของอัตราที่ตั้ง (L10: ตั้ง 12M ได้ 2.2M คิวบวม 10 วิ) · ตัวนี้คิด budget ตามเวลาจริง + timer 1 ms ของตัวเอง
class SmoothPacer : public rtc::MediaHandler
{
public:
	explicit SmoothPacer(double bytes_per_sec) : rate(bytes_per_sec)
	{
		th = std::thread([this]() { run(); });
	}
	~SmoothPacer() override
	{
		{
			std::lock_guard<std::mutex> lock(m);
			quit = true;
		}
		cv.notify_all();
		if (th.joinable())
			th.join();
	}

	void outgoing(rtc::message_vector &messages, const rtc::message_callback &send) override
	{
		{
			std::lock_guard<std::mutex> lock(m);
			send_cb = send;
			for (auto &msg : messages) {
				queued_bytes += msg->size();
				q.push_back(std::move(msg));
			}
			// คิวเกิน 150 ms ของอัตราส่ง = ทิ้งของเก่า (latency มาก่อน — NACK/PLI ซ่อมเอง)
			while (queued_bytes > rate * 0.15 && !q.empty()) {
				queued_bytes -= q.front()->size();
				q.pop_front();
				dropped++;
			}
			max_queued = std::max(max_queued, queued_bytes);
		}
		messages.clear();
		cv.notify_one();
	}

private:
	const double rate;   // bytes/s
	std::thread th;
	std::mutex m;
	std::condition_variable cv;
	std::deque<rtc::message_ptr> q;
	rtc::message_callback send_cb;
	size_t queued_bytes = 0, max_queued = 0;
	uint64_t dropped = 0, sent_bytes = 0;
	bool quit = false;

	void run()
	{
#ifdef _WIN32
		timeBeginPeriod(1);
#endif
		using clock = std::chrono::steady_clock;
		auto last = clock::now();
		auto last_report = last;
		double budget = 0;
		std::unique_lock<std::mutex> lock(m);
		while (!quit) {
			if (q.empty()) {
				cv.wait(lock, [this]() { return quit || !q.empty(); });
				last = clock::now();
				budget = 1500;   // packet แรกของเฟรมออกทันที
				continue;
			}
			const auto now = clock::now();
			// burst ไม่เกิน 4 ms ของอัตราส่ง
			budget = std::min(budget + std::chrono::duration<double>(now - last).count() * rate, rate * 0.004 + 1500);
			last = now;
			while (!q.empty() && budget > 0) {
				rtc::message_ptr msg = std::move(q.front());
				q.pop_front();
				const size_t size = msg->size();
				queued_bytes -= size;
				budget -= double(size);
				sent_bytes += size;
				auto cb = send_cb;
				lock.unlock();
				cb(std::move(msg));
				lock.lock();
			}
			if (now - last_report > std::chrono::seconds(5)) {
				qCInfo(pswrapLink).noquote() << QStringLiteral("[pacer] %1 kbps cap, sent %2 kbps, max queue %3 ms, dropped %4")
					.arg(rate * 8 / 1000, 0, 'f', 0).arg(sent_bytes * 8 / 1000.0 / 5, 0, 'f', 0)
					.arg(max_queued / rate * 1000, 0, 'f', 1).arg(dropped);
				last_report = now;
				sent_bytes = max_queued = 0;
			}
			if (!q.empty()) {
				lock.unlock();
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				lock.lock();
			}
		}
#ifdef _WIN32
		timeEndPeriod(1);
#endif
	}
};

ChiakiControllerState padToState(const QJsonObject &o)
{
	ChiakiControllerState st;
	chiaki_controller_state_set_idle(&st);
	// Gamepad API standard mapping → ปุ่ม PlayStation (6, 7 = L2/R2 analog แยกด้านล่าง)
	static const uint32_t kMap[18] = {
		CHIAKI_CONTROLLER_BUTTON_CROSS, CHIAKI_CONTROLLER_BUTTON_MOON, CHIAKI_CONTROLLER_BUTTON_BOX, CHIAKI_CONTROLLER_BUTTON_PYRAMID,
		CHIAKI_CONTROLLER_BUTTON_L1, CHIAKI_CONTROLLER_BUTTON_R1, 0, 0,
		CHIAKI_CONTROLLER_BUTTON_SHARE, CHIAKI_CONTROLLER_BUTTON_OPTIONS, CHIAKI_CONTROLLER_BUTTON_L3, CHIAKI_CONTROLLER_BUTTON_R3,
		CHIAKI_CONTROLLER_BUTTON_DPAD_UP, CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN, CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT,
		CHIAKI_CONTROLLER_BUTTON_PS, CHIAKI_CONTROLLER_BUTTON_TOUCHPAD};
	const uint32_t b = static_cast<uint32_t>(o.value(QStringLiteral("b")).toInteger());
	for (int i = 0; i < 18; i++)
		if (b & (1u << i))
			st.buttons |= kMap[i];
	auto trig = [](double v) { return static_cast<uint8_t>(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); };
	st.l2_state = trig(o.value(QStringLiteral("l2")).toDouble());
	st.r2_state = trig(o.value(QStringLiteral("r2")).toDouble());
	const QJsonArray a = o.value(QStringLiteral("a")).toArray();
	auto axis = [&](int i) -> int16_t {
		const double v = a.size() > i ? a.at(i).toDouble() : 0.0;
		if (std::fabs(v) < kStickDeadzone)
			return 0;
		return static_cast<int16_t>(std::lround(std::clamp(v, -1.0, 1.0) * 32767.0));
	};
	st.left_x = axis(0);
	st.left_y = axis(1);
	st.right_x = axis(2);
	st.right_y = axis(3);
	return st;
}

} // namespace

// ---------------------------------------------------------------- Impl
struct PsWrapLink::Impl
{
	PsWrapRecorder *rec = nullptr;
	std::function<bool(PsWrapRecConfig *, QString *)> config_getter;
	Mode mode = Mode::Off;
	int kbps = 8000;
	std::string stun;
	QPointer<StreamSession> session;
	QTimer start_timer;
	QTimer pad_timer;
	int start_tries = 0;

	std::shared_ptr<LinkTap> tap;
	bool tap_added = false;
	std::unique_ptr<rtc::WebSocketServer> server;

	// ใช้ข้าม thread (rtc / tap) ใต้ m
	std::mutex m;
	std::shared_ptr<rtc::WebSocket> ws;   // ต้องถือเอง — server ไม่ถือให้
	std::shared_ptr<rtc::PeerConnection> pc;
	std::shared_ptr<rtc::Track> track;
	std::shared_ptr<rtc::DataChannel> dc;
	std::atomic<bool> track_open{false};
	std::atomic<uint64_t> gen{0};          // เปลี่ยนทุกครั้งที่หยุด/ต่อใหม่ — callback รุ่นเก่าทิ้งตัวเอง
	std::atomic<int64_t> last_pad_us{0};
	std::atomic<bool> pad_pending_release{false};
	bool pad_held = false;                  // GUI thread
};

PsWrapLink::Mode PsWrapLink::envMode()
{
	const QByteArray v = qgetenv("PSWRAP_LINK_DEV").trimmed();
	if (v == "2")
		return Mode::VideoInput;
	if (v == "1")
		return Mode::Video;
	return Mode::Off;
}

PsWrapLink::PsWrapLink(QObject *parent, PsWrapRecorder *recorder, std::function<bool(PsWrapRecConfig *, QString *)> config_getter, Mode mode)
	: QObject(parent), d(std::make_unique<Impl>())
{
	d->rec = recorder;
	d->config_getter = std::move(config_getter);
	d->mode = mode;
	bool ok = false;
	const int kbps = qEnvironmentVariableIntValue("PSWRAP_LINK_KBPS", &ok);
	if (ok && kbps >= 1000 && kbps <= 50000)
		d->kbps = kbps;
	const QByteArray stun = qgetenv("PSWRAP_LINK_STUN");
	d->stun = stun.isEmpty() ? std::string("stun:turn.ps-wrap.com:3478") : stun.toStdString();
	d->start_timer.setInterval(1000);
	connect(&d->start_timer, &QTimer::timeout, this, [this]() {
		if (tryStart() || ++d->start_tries >= 60)
			d->start_timer.stop();
	});
	d->pad_timer.setInterval(100);
	connect(&d->pad_timer, &QTimer::timeout, this, &PsWrapLink::pollPad);
	rtc::InitLogger(rtc::LogLevel::Warning);
	qCWarning(pswrapLink).noquote() << "PS-WRAP Link DEV mode" << (mode == Mode::VideoInput ? "video + friend input" : "video only")
		<< "— signaling ws://0.0.0.0:" << kPort << "(no auth, LAN/test only)" << d->kbps << "kbps";
}

PsWrapLink::~PsWrapLink()
{
	stop();
}

void PsWrapLink::setSession(StreamSession *session)
{
	if (!session) {
		stop();
		return;
	}
	d->session = session;
	d->start_tries = 0;
	if (!tryStart())
		d->start_timer.start();   // หน้าต่าง/สตรีมยังไม่พร้อม (ยังไม่มีเฟรม) → ลองใหม่ทุกวินาที
}

bool PsWrapLink::tryStart()
{
	if (d->tap_added)
		return true;
	if (!d->rec || !d->config_getter)
		return false;
	PsWrapRecConfig cfg;
	QString err;
	if (!d->config_getter(&cfg, &err))
		return false;
	if (cfg.hdr) {
		qCWarning(pswrapLink) << "HDR stream — Link spike needs SDR";
		d->start_tries = 1000;
		return false;
	}

	auto tap = std::make_shared<LinkTap>(d->kbps);
	Impl *dd = d.get();
	tap->send = [dd](const uint8_t *data, size_t size, int64_t pts_ms) {
		std::shared_ptr<rtc::Track> t;
		{
			std::lock_guard<std::mutex> lock(dd->m);
			t = dd->track;
		}
		if (!t || !dd->track_open)
			return;
		try {
			t->sendFrame(reinterpret_cast<const std::byte *>(data), size,
			             rtc::FrameInfo(std::chrono::duration<double>(double(pts_ms) / 1000.0)));
		} catch (const std::exception &e) {
			qCWarning(pswrapLink) << "send:" << e.what();
		}
	};
	tap->startThread();
	if (!d->rec->addTap(cfg, tap, &err)) {
		qCWarning(pswrapLink) << "addTap failed:" << err;
		tap->stopThread();
		return false;
	}
	d->tap = tap;
	d->tap_added = true;
	tap->on_detached = [this]() { d->tap_added = false; };

	// ---- signaling (spike: ทีละคน คนใหม่มาแทนคนเก่า)
	rtc::WebSocketServerConfiguration wscfg;
	wscfg.port = kPort;
	wscfg.bindAddress = "0.0.0.0";
	try {
		d->server = std::make_unique<rtc::WebSocketServer>(wscfg);
	} catch (const std::exception &e) {
		qCWarning(pswrapLink) << "signaling server failed:" << e.what();
		return true;   // ภาพยัง encode อยู่ (ดู log [enc]) แม้ไม่มีคนต่อ
	}
	const uint64_t my_gen = ++d->gen;
	std::weak_ptr<LinkTap> wtap = tap;   // rtc thread อ่าน d->tap ตรงๆ ไม่ได้ (GUI reset ได้ทุกเมื่อ)
	d->server->onClient([this, dd, wtap, my_gen](std::shared_ptr<rtc::WebSocket> ws) {
		if (dd->gen != my_gen)
			return;
		qCInfo(pswrapLink).noquote() << "client" << QString::fromStdString(ws->remoteAddress().value_or("?"));
		{
			std::lock_guard<std::mutex> lock(dd->m);
			dd->ws = ws;
		}
		std::weak_ptr<rtc::WebSocket> wws = ws;
		ws->onOpen([this, dd, wws, wtap, my_gen]() {
			auto ws = wws.lock();
			if (!ws || dd->gen != my_gen)
				return;
			rtc::Configuration cfg;
			cfg.iceServers.emplace_back(dd->stun);
			cfg.disableAutoNegotiation = true;
			auto pc = std::make_shared<rtc::PeerConnection>(cfg);
			std::weak_ptr<rtc::PeerConnection> wpc = pc;
			pc->onStateChange([](rtc::PeerConnection::State st) {
				std::ostringstream s;
				s << st;
				qCInfo(pswrapLink).noquote() << "pc" << QString::fromStdString(s.str());
			});
			pc->onGatheringStateChange([wws, wpc](rtc::PeerConnection::GatheringState st) {
				if (st != rtc::PeerConnection::GatheringState::Complete)
					return;
				auto pc = wpc.lock();
				auto ws = wws.lock();
				if (!pc || !ws)
					return;
				auto desc = pc->localDescription();
				QJsonObject o{{QStringLiteral("type"), QString::fromStdString(desc->typeString())},
				              {QStringLiteral("sdp"), QString::fromStdString(std::string(*desc))}};
				ws->send(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString());
			});

			rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
			media.addH264Codec(kPayloadType, "profile-level-id=42e02a;packetization-mode=1;level-asymmetry-allowed=1");
			media.addSSRC(kSsrc, "pswrap", "pswrap-stream", "pswrap-video");
			media.addExtMap(rtc::Description::Entry::ExtMap(kPlayoutDelayExtId, "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay"));
			auto track = pc->addTrack(media);
			auto rtp = std::make_shared<rtc::RtpPacketizationConfig>(kSsrc, "pswrap", kPayloadType, rtc::H264RtpPacketizer::ClockRate);
			rtp->playoutDelayId = kPlayoutDelayExtId;   // 0/0 = เบราว์เซอร์แสดงทันที
			rtp->playoutDelayMin = 0;
			rtp->playoutDelayMax = 0;
			auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtp);
			packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtp));
			packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
			packetizer->addToChain(std::make_shared<rtc::PliHandler>([wtap]() {
				if (auto t = wtap.lock())
					t->want_idr = true;
			}));

			// เพื่อนบอกค่าที่อยากได้ใน path ของ WebSocket (ws://host/?h=720&kbps=4000&pace=1.5) — ต้องรู้ก่อนสร้าง pacer
			// pace = อัตราปล่อย packet เทียบ bitrate (0 = ปล่อยทั้งเฟรมทีเดียว) — เครื่องปลายทางบางตัวรับ burst ไม่ไหว (packet หาย ~20%)
			const QUrlQuery q(QUrl(QString::fromStdString(ws->path().value_or("/"))).query());
			int kbps = q.queryItemValue(QStringLiteral("kbps")).toInt();
			const int h = q.queryItemValue(QStringLiteral("h")).toInt();
			const double pace = q.hasQueryItem(QStringLiteral("pace")) ? q.queryItemValue(QStringLiteral("pace")).toDouble() : 0.0;
			if (auto t = wtap.lock()) {
				// ไม่ระบุ = กลับค่าตั้งต้น (เดิมค่าของเพื่อนคนก่อนค้างอยู่ใน tap — L11: เปิดใหม่ไม่ใส่ kbps ยังได้ 4M)
				t->target_kbps = (kbps >= 500 && kbps <= 50000) ? kbps : 0;
				t->max_height = (h >= 360 && h <= 2160) ? h : 1080;
				kbps = t->target_kbps.load() > 0 ? t->target_kbps.load() : dd->kbps;
			}
			if (pace > 0.0) {
				packetizer->addToChain(std::make_shared<SmoothPacer>(kbps * 1000.0 / 8.0 * pace));
			}
			qCInfo(pswrapLink).nospace() << "friend wants h=" << h << " kbps=" << kbps << " pace=" << pace;
			track->setMediaHandler(packetizer);
			track->onOpen([dd, wtap, my_gen]() {
				if (dd->gen != my_gen)
					return;
				qCInfo(pswrapLink) << "video open";
				if (auto t = wtap.lock())
					t->want_idr = true;
				dd->track_open = true;
			});
			track->onClosed([dd]() { dd->track_open = false; });

			rtc::DataChannelInit init;
			init.reliability.unordered = true;   // จอย: สถานะล่าสุดสำคัญกว่าของเก่า ไม่ส่งซ้ำ
			init.reliability.maxRetransmits = 0;
			auto dc = pc->createDataChannel("input", init);
			std::weak_ptr<rtc::DataChannel> wdc = dc;
			dc->onMessage([this, dd, wdc, wtap, my_gen](rtc::message_variant msg) {
				auto str = std::get_if<std::string>(&msg);
				auto dc = wdc.lock();
				if (!str || !dc || dd->gen != my_gen)
					return;
				const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(*str)).object();
				const QString type = o.value(QStringLiteral("type")).toString();
				if (type == QLatin1String("ping")) {
					QJsonObject r{{QStringLiteral("type"), QStringLiteral("pong")}, {QStringLiteral("t"), o.value(QStringLiteral("t"))},
					              {QStringLiteral("host"), double(wallMs())}};
					dc->send(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
				} else if (type == QLatin1String("pad")) {
					QJsonObject r{{QStringLiteral("type"), QStringLiteral("pad-ack")}, {QStringLiteral("t"), o.value(QStringLiteral("t"))}};
					dc->send(QJsonDocument(r).toJson(QJsonDocument::Compact).toStdString());
					if (dd->mode == Mode::VideoInput && o.contains(QStringLiteral("b"))) {
						const ChiakiControllerState st = padToState(o);
						dd->last_pad_us = monoUs();
						QMetaObject::invokeMethod(this, [this, st]() {
							if (d->session) {
								d->session->PsWrapSetLinkState(st);
								d->pad_held = true;
							}
						}, Qt::QueuedConnection);
					}
				} else if (type == QLatin1String("config")) {
					// เพื่อนบอกความสูงที่อยากได้ (มือถือ 720, PC 1080) — spike: คนเดียว ใช้ค่าล่าสุด
					const int h = std::clamp(o.value(QStringLiteral("h")).toInt(1080), 360, 2160);
					if (auto t = wtap.lock()) {
						t->max_height = h;
						t->want_idr = true;
					}
					qCInfo(pswrapLink) << "friend asked for height" << h;
				} else if (type == QLatin1String("report")) {
					QString line = QStringLiteral("[browser] latency ") + o.value(QStringLiteral("now")).toString();
					const QJsonObject rows = o.value(QStringLiteral("rows")).toObject();
					for (auto it = rows.begin(); it != rows.end(); ++it)
						line += QStringLiteral(" | ") + it.key() + QStringLiteral(": ") + it.value().toString();
					qCInfo(pswrapLink).noquote() << line;
				}
			});
			dc->onClosed([dd]() { dd->pad_pending_release = true; });

			{
				std::lock_guard<std::mutex> lock(dd->m);
				if (dd->pc)
					dd->pc->close();
				dd->track_open = false;
				dd->pc = pc;
				dd->track = track;
				dd->dc = dc;
			}
			pc->setLocalDescription();   // offer
		});
		ws->onMessage([dd, my_gen](rtc::message_variant msg) {
			auto str = std::get_if<std::string>(&msg);
			if (!str || dd->gen != my_gen)
				return;
			const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(*str)).object();
			if (o.value(QStringLiteral("type")).toString() != QLatin1String("answer"))
				return;
			std::shared_ptr<rtc::PeerConnection> pc;
			{
				std::lock_guard<std::mutex> lock(dd->m);
				pc = dd->pc;
			}
			if (pc)
				pc->setRemoteDescription(rtc::Description(o.value(QStringLiteral("sdp")).toString().toStdString(), "answer"));
		});
	});
	d->pad_timer.start();
	qCInfo(pswrapLink).nospace() << "started: tap " << cfg.width << "x" << cfg.height << "@" << cfg.fps << ", signaling :" << kPort;
	return true;
}

void PsWrapLink::pollPad()
{
	// GUI thread: เพื่อนเงียบเกิน 500 ms หรือ data channel ปิด → ปล่อยทุกปุ่ม (กันปุ่มค้างตอนเน็ตหลุด)
	if (!d->pad_held)
		return;
	const bool timeout = monoUs() - d->last_pad_us.load() > kPadTimeoutUs;
	if (timeout || d->pad_pending_release.exchange(false)) {
		if (d->session) {
			ChiakiControllerState idle;
			chiaki_controller_state_set_idle(&idle);
			d->session->PsWrapSetLinkState(idle);
		}
		d->pad_held = false;
		qCInfo(pswrapLink) << "friend input released" << (timeout ? "(timeout)" : "(closed)");
	}
}

void PsWrapLink::stop()
{
	d->start_timer.stop();
	d->pad_timer.stop();
	d->gen++;
	if (d->pad_held && d->session) {
		ChiakiControllerState idle;
		chiaki_controller_state_set_idle(&idle);
		d->session->PsWrapSetLinkState(idle);
	}
	d->pad_held = false;
	{
		std::lock_guard<std::mutex> lock(d->m);
		d->track_open = false;
		if (d->pc)
			d->pc->close();
		d->pc.reset();
		d->track.reset();
		d->dc.reset();
		d->ws.reset();
	}
	d->server.reset();
	if (d->tap) {
		if (d->tap_added && d->rec)
			d->rec->removeTap(d->tap.get());
		d->tap->stopThread();
		d->tap.reset();
	}
	d->tap_added = false;
	d->session.clear();
}
