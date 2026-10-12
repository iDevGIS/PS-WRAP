// PS-WRAP Link — ดูคำอธิบายใน pswraplink.h · ผลวัด/เหตุผลการออกแบบ: PS-WRAP-LINK/ผลการทดลอง.md
#include <pswraplink.h>
#include <pswraplinkvoice.h>
#include <pswraprecorder.h>
#include <pswrapsecrets.h>
#ifdef PSWRAP_LINK_BETA
#include "pswraplink_betakey.h"   // สร้างตอน configure จากไฟล์นอก repo (ดู gui/CMakeLists.txt)
#endif
#include "streamsession.h"

#include <qrcodegen.hpp>

#include <chiaki/controller.h>
#include <chiaki/time.h>
#include <rtc/rtc.hpp>

#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QWebSocket>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>   // timeBeginPeriod — pacer ต้องการ sleep 1 ms จริง
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

Q_LOGGING_CATEGORY(pswrapLink, "pswrap.link", QtInfoMsg)

namespace {

constexpr int kBits = 32;          // บาร์โค้ด (DEV) = เวลา ms 32 บิตล่าง (หน้าเว็บคิด mod 2^32)
constexpr int kBarHeight = 48;
constexpr uint8_t kVideoPt = 96;
constexpr uint8_t kAudioPt = 111;
constexpr int kPlayoutDelayExtId = 6;
constexpr int64_t kPadTimeoutUs = 500000;   // ผู้ถือจอยเงียบนานกว่านี้ = ปล่อยทุกปุ่ม
constexpr float kStickDeadzone = 0.12f;     // OR ของจอยใช้ค่าสัมบูรณ์มากสุด — stick เพื่อนที่ลอยนิดๆ จะแย่ง stick เรา
constexpr int kMaxFriends = 4;              // เน็ตขาขึ้น: P2P ส่งแยกทุกคน (8 Mbps × คน)
constexpr qint64 kReclaimMs = 15000;         // คนถือจอยหลุด/รีเฟรช → กลับมาภายในนี้ได้จอยคืนเอง
constexpr int kAudioKbps = 96;
constexpr int kVoiceKbps = 32;   // ไมค์เจ้าของเครื่อง (mono) แยก track
const char *const kHostKeyTarget = "PS-WRAP/link/host-key";
const char *const kDefaultSignal = "wss://link.ps-wrap.com/ws/host";

// ความสูงที่รองรับ (เพื่อนเลือกได้ในหน้าเว็บ) → bitrate · 1080 = env PSWRAP_LINK_KBPS ได้
int normalizeHeight(int h)
{
	return h >= 1000 || h <= 0 ? 1080 : h >= 600 ? 720 : 480;
}
int kbpsFor(int height, int kbps_1080)
{
	return height >= 1080 ? kbps_1080 : height >= 720 ? 5000 : 2500;
}

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

std::string compact(const QJsonObject &o)
{
	return QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
}

// ---------------------------------------------------------------- ภาพ: tap → ย่อ → NVENC low-latency (1 ตัวต่อความสูง)
// ---------------------------------------------------------------- HDR → SDR (สตรีม HDR10 ส่งให้เพื่อนเป็น SDR)
// เบราว์เซอร์รับ H.264 SDR เท่านั้น · ทำแบบตารางค่า (เร็ว ~2 ms ต่อเฟรม 1080p บน CPU) แทนแปลงสีเต็มรูปแบบ:
//   ความสว่าง: PQ → nits → บีบไฮไลต์ (Reinhard ขยาย, ขาวอ้างอิง 203 nits ≈ 85%, ยอด 1000 nits) → gamma SDR 2.4 → Y 8 บิต
//   สี: ปรับตามสัดส่วนความสว่างที่เปลี่ยน × 1.15 (สีใน BT.2020 จะดูซีดถ้าแสดงเป็น BT.709 ตรงๆ) · ไม่ได้แปลง gamut จริง
struct HdrToSdrLut
{
	uint8_t y[1024];
	uint16_t chroma_q8[1024];   // ตัวคูณสี × 256 ตามความสว่าง (Y 10 บิต)
	HdrToSdrLut()
	{
		const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32, c3 = 2392.0 / 4096 * 32;
		const double white = 203.0, wmax = 1000.0 / white;
		for (int v = 0; v < 1024; v++) {
			const double e = std::clamp((v - 64) / 876.0, 0.0, 1.0);
			const double p = std::pow(e, 1.0 / m2);
			const double nits = 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
			const double x = nits / white;
			// × 1.31: ขาวอ้างอิง (UI เกม ~203 nits) ออกมา ~85% แทน 76% (ไม่มืด) · ไฮไลต์ค่อยๆ อิ่มจนเต็มที่ ~500 nits
			const double out = std::min(1.0, 1.31 * x * (1.0 + x / (wmax * wmax)) / (1.0 + x));
			const double yp = std::pow(out, 1.0 / 2.4);
			y[v] = uint8_t(std::lround(16.0 + 219.0 * yp));
			const double ratio = e > 0.002 ? std::min(2.5, yp / e) : 1.0;
			chroma_q8[v] = uint16_t(std::lround(ratio * 1.15 * 256.0));
		}
	}
};

// P010 (Y/UV 16 บิต ค่าอยู่ 10 บิตบน, limited range) → NV12 SDR ขนาดเท่ากัน
void tonemapP010ToNv12(const AVFrame *src, AVFrame *dst, int w, int h)
{
	static const HdrToSdrLut lut;
	for (int yy = 0; yy < h; yy++) {
		const uint16_t *in = reinterpret_cast<const uint16_t *>(src->data[0] + yy * src->linesize[0]);
		uint8_t *out = dst->data[0] + yy * dst->linesize[0];
		for (int x = 0; x < w; x++)
			out[x] = lut.y[in[x] >> 6];
	}
	for (int cy = 0; cy < h / 2; cy++) {
		const uint16_t *uv = reinterpret_cast<const uint16_t *>(src->data[1] + cy * src->linesize[1]);
		const uint16_t *y0 = reinterpret_cast<const uint16_t *>(src->data[0] + (2 * cy) * src->linesize[0]);
		const uint16_t *y1 = reinterpret_cast<const uint16_t *>(src->data[0] + (2 * cy + 1) * src->linesize[0]);
		uint8_t *out = dst->data[1] + cy * dst->linesize[1];
		for (int cx = 0; cx < w / 2; cx++) {
			const int ly = ((y0[2 * cx] >> 6) + (y0[2 * cx + 1] >> 6) + (y1[2 * cx] >> 6) + (y1[2 * cx + 1] >> 6) + 2) >> 2;
			const int k = lut.chroma_q8[ly];
			for (int c = 0; c < 2; c++) {
				const int c10 = (uv[2 * cx + c] >> 6) - 512;
				const int c8 = 128 + ((c10 * k) >> 10);   // /4 (10→8 บิต) และ /256 (q8)
				out[2 * cx + c] = uint8_t(std::clamp(c8, 16, 240));
			}
		}
	}
}

class LinkVideoTap : public PsWrapRawTap
{
public:
	LinkVideoTap(int height, int kbps, bool dev_stamp) : height(height), kbps(kbps), dev_stamp(dev_stamp) {}
	~LinkVideoTap() override
	{
		stopThread();
		av_frame_free(&pending.frame);
		av_frame_free(&own);
		av_packet_free(&pkt);
		if (enc)
			avcodec_free_context(&enc);
		if (sws)
			sws_freeContext(sws);
		av_frame_free(&hdr_tmp);
	}

	const int height;
	const int kbps;
	std::function<void(const uint8_t *data, size_t size, int64_t pts_ms)> send;   // thread ของ tap
	std::function<void()> on_detached;                                              // GUI thread
	std::atomic<bool> want_idr{true};

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
		if (frame->format != AV_PIX_FMT_NV12 && frame->format != AV_PIX_FMT_P010LE)
			return;   // NV12 = SDR · P010 = HDR10 (tone map เป็น SDR ตอน encode)
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

private:
	struct Pending
	{
		AVFrame *frame = nullptr;
		int64_t capture_us = -1;
		int64_t tapped_us = 0;
	};

	const bool dev_stamp;
	std::thread th;
	std::mutex m;
	std::condition_variable cv;
	Pending pending;
	bool quit = false;
	std::atomic<quint64> dropped{0};

	// thread ของ tap เท่านั้น
	AVCodecContext *enc = nullptr;
	AVFrame *own = nullptr;   // สำเนาของเรา (ย่อ/แปะบาร์โค้ดได้ ไม่แตะเฟรมที่ recorder ใช้ต่อ)
	AVPacket *pkt = nullptr;
	SwsContext *sws = nullptr;   // pipeline อาจเป็น 4K — มือถือ decode 4K60 ไม่ไหว (L02: decode 100–160 ms)
	int sws_src_w = 0, sws_src_h = 0, sws_dst_w = 0, sws_dst_h = 0;
	AVPixelFormat sws_fmt = AV_PIX_FMT_NONE;
	AVFrame *hdr_tmp = nullptr;   // HDR: ภาพ P010 หลังย่อ (ก่อนแปลงเป็น SDR)
	bool hdr_logged = false;
	bool enc_failed = false;
	int64_t stat_start_us = 0;
	int stat_n = 0;
	double cap_sum = 0, cap_max = 0, enc_sum = 0, enc_max = 0;
	uint64_t stat_bytes = 0;
	quint64 stat_dropped0 = 0;

	bool openEncoder(int w, int h)
	{
		if (enc)
			avcodec_free_context(&enc);
		const char *const names[] = {"h264_nvenc", "h264_amf", "libx264"};
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
			c->rc_buffer_size = static_cast<int>(c->bit_rate / 30);   // VBV ~2 เฟรม: เฟรมใหญ่ไม่ล้นจนต้องรอ
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
			} else if (!strcmp(n, "h264_amf")) {
				av_opt_set(c->priv_data, "usage", "ultralowlatency", 0);
				av_opt_set(c->priv_data, "rc", "cbr", 0);
				av_opt_set(c->priv_data, "profile", "constrained_baseline", 0);
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
			qCInfo(pswrapLink).nospace() << "video encoder " << n << " " << w << "x" << h << " " << kbps << " kbps";
			return true;
		}
		return false;
	}

	// เช็คขนาด/ฟอร์แมตปลายทางด้วย — context เก่าเขียนเกินบัฟเฟอร์ได้ (L04: แอปพังตอน 1080→720)
	bool ensureSws(int sw, int sh, AVPixelFormat fmt, int dw, int dh)
	{
		if (sws && sws_src_w == sw && sws_src_h == sh && sws_fmt == fmt && sws_dst_w == dw && sws_dst_h == dh)
			return true;
		if (sws)
			sws_freeContext(sws);
		sws = sws_alloc_context();
		av_opt_set_int(sws, "srcw", sw, 0);
		av_opt_set_int(sws, "srch", sh, 0);
		av_opt_set_int(sws, "src_format", fmt, 0);
		av_opt_set_int(sws, "dstw", dw, 0);
		av_opt_set_int(sws, "dsth", dh, 0);
		av_opt_set_int(sws, "dst_format", fmt, 0);   // ย่ออย่างเดียว ไม่แปลงฟอร์แมต (HDR แปลงสีเองหลังย่อ)
		av_opt_set_int(sws, "sws_flags", SWS_BILINEAR, 0);
		av_opt_set_int(sws, "threads", 4, 0);
		if (sws_init_context(sws, nullptr, nullptr) < 0) {
			sws_freeContext(sws);
			sws = nullptr;
			return false;
		}
		sws_src_w = sw;
		sws_src_h = sh;
		sws_fmt = fmt;
		sws_dst_w = dw;
		sws_dst_h = dh;
		return true;
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
		if (h > height) {
			w = static_cast<int>(std::lround(double(src->width) * height / src->height)) & ~1;
			h = height & ~1;
		}
		const bool scaling = w != (src->width & ~1) || h != (src->height & ~1);
		if (enc_failed)
			return;
		if (!enc || enc->width != w || enc->height != h) {
			av_frame_free(&own);
			if (!openEncoder(w, h)) {
				qCWarning(pswrapLink) << "no usable H.264 encoder — Link video off";
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
		const bool hdr = src->format == AV_PIX_FMT_P010LE;
		if (hdr) {
			if (!hdr_logged) {
				hdr_logged = true;
				qCInfo(pswrapLink).noquote() << "video: HDR stream, tone mapping to SDR for friends";
			}
			// ย่อก่อน (P010 → P010) แล้วค่อยแปลงสี — ทำงานบนภาพเล็ก
			const AVFrame *hsrc = src;
			if (scaling) {
				if (!hdr_tmp || hdr_tmp->width != w || hdr_tmp->height != h) {
					av_frame_free(&hdr_tmp);
					hdr_tmp = av_frame_alloc();
					hdr_tmp->format = AV_PIX_FMT_P010LE;
					hdr_tmp->width = w;
					hdr_tmp->height = h;
					if (av_frame_get_buffer(hdr_tmp, 0) < 0) {
						av_frame_free(&hdr_tmp);
						return;
					}
				}
				if (!ensureSws(src->width, src->height, AV_PIX_FMT_P010LE, w, h))
					return;
				sws_scale(sws, src->data, src->linesize, 0, src->height, hdr_tmp->data, hdr_tmp->linesize);
				hsrc = hdr_tmp;
			}
			tonemapP010ToNv12(hsrc, own, w, h);
		} else if (scaling) {
			if (!ensureSws(src->width, src->height, AV_PIX_FMT_NV12, w, h))
				return;
			sws_scale(sws, src->data, src->linesize, 0, src->height, own->data, own->linesize);
		} else {
			for (int y = 0; y < h; y++)
				memcpy(own->data[0] + y * own->linesize[0], src->data[0] + y * src->linesize[0], w);
			for (int y = 0; y < h / 2; y++)
				memcpy(own->data[1] + y * own->linesize[1], src->data[1] + y * src->linesize[1], w);
		}
		if (dev_stamp) {
			// เวลา "ตอนจับภาพ" ตามนาฬิกาจริง = ตอนนี้ - (ผ่านไปกี่ µs ตั้งแต่จับ)
			const int64_t cap_wall = p.capture_us > 0 ? wallMs() - (t0 - p.capture_us) / 1000 : wallMs();
			stamp(own, static_cast<uint32_t>(cap_wall));
		}
		own->pts = src->pts;
		own->pict_type = want_idr.exchange(false) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
		if (avcodec_send_frame(enc, own) < 0)
			return;
		while (avcodec_receive_packet(enc, pkt) == 0) {
			const double enc_ms = (monoUs() - t0) / 1000.0;
			enc_sum += enc_ms;
			enc_max = std::max(enc_max, enc_ms);
			stat_bytes += uint64_t(pkt->size);
			if (send)
				send(pkt->data, size_t(pkt->size), pkt->pts);
			av_packet_unref(pkt);
		}
		if (p.capture_us > 0) {
			const double cap_ms = (p.tapped_us - p.capture_us) / 1000.0;
			cap_sum += cap_ms;
			cap_max = std::max(cap_max, cap_ms);
		}
		stat_n++;
	}

	void report()
	{
		const int64_t now = monoUs();
		if (now - stat_start_us < 30000000 || stat_n == 0)
			return;   // ทุก 30 วิ — พอดูย้อนหลังได้ ไม่ท่วม log
		const double secs = (now - stat_start_us) / 1e6;
		const quint64 drop = dropped.load();
		qCInfo(pswrapLink).noquote() << QStringLiteral("[enc %1p] %2 fps | capture-to-tap %3 ms (max %4) | encode %5 ms (max %6) | %7 kbps | dropped %8")
			.arg(height).arg(stat_n / secs, 0, 'f', 1).arg(cap_sum / stat_n, 0, 'f', 1).arg(cap_max, 0, 'f', 1)
			.arg(enc_sum / stat_n, 0, 'f', 2).arg(enc_max, 0, 'f', 2).arg(stat_bytes * 8 / 1000.0 / secs, 0, 'f', 0)
			.arg(drop - stat_dropped0);
		stat_start_us = now;
		stat_n = 0;
		cap_sum = cap_max = enc_sum = enc_max = 0;
		stat_bytes = 0;
		stat_dropped0 = drop;
	}
};

// ---------------------------------------------------------------- เสียง: Opus 10 ms (1 ชุด ส่งทุกคน)
// เกม (stereo) กับไมค์ของเจ้าของเครื่อง (mono) แยกกันคนละ track — หน้าเว็บเพื่อนปรับความดังแยกได้
class LinkOpusOut
{
public:
	~LinkOpusOut()
	{
		av_frame_free(&frame);
		av_packet_free(&pkt);
		if (enc)
			avcodec_free_context(&enc);
	}

	std::function<void(const uint8_t *data, size_t size, int64_t pos)> send;   // worker ของ recorder

	bool open(int channels, int kbps, QString *error)
	{
		const AVCodec *codec = avcodec_find_encoder_by_name("libopus");
		if (!codec) {
			*error = QStringLiteral("libopus not available");
			return false;
		}
		ch = channels;
		enc = avcodec_alloc_context3(codec);
		enc->sample_rate = 48000;
		av_channel_layout_default(&enc->ch_layout, channels);
		enc->sample_fmt = AV_SAMPLE_FMT_FLT;
		enc->bit_rate = kbps * 1000;
		enc->time_base = AVRational{1, 48000};
		av_opt_set(enc->priv_data, "application", "lowdelay", 0);
		av_opt_set(enc->priv_data, "frame_duration", "10", 0);
		const int err = avcodec_open2(enc, codec, nullptr);
		if (err < 0) {
			*error = avErr(err);
			avcodec_free_context(&enc);
			return false;
		}
		frame = av_frame_alloc();
		frame->format = AV_SAMPLE_FMT_FLT;
		frame->nb_samples = enc->frame_size;
		frame->sample_rate = 48000;
		av_channel_layout_copy(&frame->ch_layout, &enc->ch_layout);
		if (av_frame_get_buffer(frame, 0) < 0)
			return false;
		pkt = av_packet_alloc();
		qCInfo(pswrapLink) << "audio encoder libopus" << kbps << "kbps" << channels << "ch frame" << enc->frame_size;
		return true;
	}
	void reset() { buf.clear(); }
	// planar L/R → interleaved (หรือ mono = เฉลี่ย) → Opus ทีละ 10 ms
	void push(const float *left, const float *right, int64_t n)
	{
		if (!enc || n <= 0)
			return;
		for (int64_t i = 0; i < n; i++) {
			if (ch == 1) {
				buf.push_back(0.5f * (left[i] + right[i]));
			} else {
				buf.push_back(left[i]);
				buf.push_back(right[i]);
			}
		}
		const size_t fs = size_t(enc->frame_size), per = fs * size_t(ch);
		while (buf.size() >= per) {
			av_frame_make_writable(frame);
			memcpy(frame->data[0], buf.data(), per * sizeof(float));
			buf.erase(buf.begin(), buf.begin() + std::ptrdiff_t(per));
			frame->pts = out_pos;
			out_pos += int64_t(fs);
			if (avcodec_send_frame(enc, frame) < 0)
				return;
			while (avcodec_receive_packet(enc, pkt) == 0) {
				if (send)
					send(pkt->data, size_t(pkt->size), sent_pos);
				sent_pos += int64_t(fs);
				av_packet_unref(pkt);
			}
		}
	}

private:
	AVCodecContext *enc = nullptr;
	AVFrame *frame = nullptr;
	AVPacket *pkt = nullptr;
	std::vector<float> buf;
	int ch = 2;
	int64_t out_pos = 0, sent_pos = 0;
};

class LinkAudioTap : public PsWrapRawTap
{
public:
	LinkOpusOut game;    // track "audio" (เพื่อนส่งไมค์กลับมาทาง track นี้ด้วย — sendrecv)
	LinkOpusOut voice;   // track "voice": ไมค์ของเจ้าของเครื่อง
	std::function<void()> on_detached;   // GUI thread

	bool open(QString *error)
	{
		return game.open(2, kAudioKbps, error) && voice.open(1, kVoiceKbps, error);
	}

	void videoFrame(const AVFrame *) override {}
	void audioMix(const float *, const float *, int64_t, int64_t) override {}   // ใช้ audioSplit แทน
	void audioSplit(const float *gameL, const float *gameR, const float *micL, const float *micR, int64_t n, int64_t pos) override
	{
		if (n <= 0)
			return;
		if (expected >= 0 && pos != expected) {   // ช่องว่าง (ไม่ควรเกิด — pipeline เติมเงียบเอง)
			game.reset();
			voice.reset();
		}
		expected = pos + n;
		game.push(gameL, gameR, n);
		voice.push(micL, micR, n);
	}
	void detached() override
	{
		if (on_detached)
			on_detached();
	}

private:
	int64_t expected = -1;
};

// ---------------------------------------------------------------- pacer
// ทยอยปล่อย RTP แทนการปล่อยทั้งเฟรมทีเดียว (บางเครื่อง/เน็ตรับ burst ไม่ไหว)
// ไม่ใช้ rtc::PacingHandler: budget สะสมแค่ 1 sendInterval (2 ms) แต่ timer Windows ตื่นทุก ~15.6 ms
// → ส่งได้ ~1/8 ของอัตราที่ตั้ง (L10: ตั้ง 12M ได้ 2.2M คิวบวม 10 วิ) · ตัวนี้คิด budget ตามเวลาจริง + timer 1 ms
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
			}
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
	size_t queued_bytes = 0;
	bool quit = false;

	void run()
	{
#ifdef _WIN32
		timeBeginPeriod(1);
#endif
		using clock = std::chrono::steady_clock;
		auto last = clock::now();
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
			budget = std::min(budget + std::chrono::duration<double>(now - last).count() * rate, rate * 0.004 + 1500);
			last = now;
			while (!q.empty() && budget > 0) {
				rtc::message_ptr msg = std::move(q.front());
				q.pop_front();
				queued_bytes -= msg->size();
				budget -= double(msg->size());
				auto cb = send_cb;
				lock.unlock();
				cb(std::move(msg));
				lock.lock();
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

// รูปโปรไฟล์จากเพื่อน: รับแค่ data URI ของรูปที่ถอดได้จริง ≤ 64 KB → ตัดกลางสี่เหลี่ยม → PNG 64×64 ที่เรา encode เอง
// (ไม่ส่งต่อไบต์ของเพื่อนตรงๆ — กันไฟล์แปลกปลอม/ใหญ่เกิน)
QString sanitizeAvatar(const QString &uri)
{
	// ไม่รับ webp: Qt ของ build นี้ไม่มี plugin qwebp (ถอดไม่ได้อยู่ดี — หน้าเว็บส่ง PNG)
	static const QRegularExpression re(QStringLiteral("^data:image/(png|jpeg|gif);base64,([A-Za-z0-9+/=]+)$"));
	if (uri.size() > 90000)
		return QString();
	const auto m = re.match(uri);
	if (!m.hasMatch())
		return QString();
	const QByteArray raw = QByteArray::fromBase64(m.captured(2).toLatin1());
	if (raw.isEmpty() || raw.size() > 64 * 1024)
		return QString();
	QImage img;
	if (!img.loadFromData(raw) || img.width() < 8 || img.height() < 8 || img.width() > 2048 || img.height() > 2048)
		return QString();
	const int side = std::min(img.width(), img.height());
	img = img.copy((img.width() - side) / 2, (img.height() - side) / 2, side, side)
	         .scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
	         .convertToFormat(QImage::Format_ARGB32);
	QByteArray png;
	QBuffer buf(&png);
	buf.open(QIODevice::WriteOnly);
	img.save(&buf, "PNG");
	return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

// สีประจำชื่อ (คงที่ต่อชื่อ) — ใช้ในแชทและป้าย Link
QString nameColor(const QString &name)
{
	static const char *const palette[] = {"#4dabf7", "#a78bfa", "#34d399", "#f472b6", "#fb923c", "#38d9a9", "#ffd43b"};
	return QString::fromLatin1(palette[qHash(name) % (sizeof(palette) / sizeof(palette[0]))]);
}

// แชทจากเพื่อน: ตัดอักขระควบคุม/ช่องว่างซ้ำ · ยาวสุด 200
QString cleanChat(const QString &text)
{
	QString out;
	out.reserve(text.size());
	for (const QChar ch : text) {
		if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format)
			continue;
		out += ch == QChar(u'\n') || ch == QChar(u'\t') ? QChar(u' ') : ch;
	}
	return out.simplified().left(200);
}

QString qrDataUri(const QString &text)
{
	if (text.isEmpty())
		return QString();
	const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
	const int border = 2, size = qr.getSize() + border * 2;
	QString path;
	for (int y = 0; y < qr.getSize(); y++)
		for (int x = 0; x < qr.getSize(); x++)
			if (qr.getModule(x, y))
				path += QStringLiteral("M%1,%2h1v1h-1z").arg(x + border).arg(y + border);
	const QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %1 %1\" shape-rendering=\"crispEdges\">"
	                                   "<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/><path d=\"%2\" fill=\"#000\"/></svg>")
		.arg(size).arg(path);
	return QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(svg.toUtf8().toBase64());
}

} // namespace

// ---------------------------------------------------------------- state
struct PsWrapLink::Peer
{
	QString id, name;
	int height = 1080;
	std::shared_ptr<rtc::PeerConnection> pc;
	std::shared_ptr<rtc::Track> video, audio, voice;   // audio = เสียงเกม (+ไมค์เพื่อนขาเข้า) · voice = ไมค์เจ้าของเครื่อง
	std::shared_ptr<rtc::DataChannel> input;     // unordered + ไม่ส่งซ้ำ: สถานะจอยล่าสุดสำคัญกว่าของเก่า
	std::shared_ptr<rtc::DataChannel> control;   // reliable: บทบาท, ขอจอย, สถิติ
	std::atomic<bool> video_open{false}, audio_open{false}, voice_open{false};
	std::weak_ptr<LinkVideoTap> tap;             // encoder ของกลุ่มความสูงปัจจุบัน (ใต้ m — PLI อ่านจาก rtc thread)
	// GUI thread (อ่านใต้ m ด้วยเพราะ friends() อ่าน)
	QString status = QStringLiteral("connecting");
	bool staff = false;   // ทีม PS-WRAP ที่เจ้าของห้องอนุญาตให้ดู — ดูอย่างเดียว ไม่ได้จอย
	QString resume;   // รหัสต่อแท็บของหน้าเว็บ (sessionStorage) — รีเฟรชแล้วรหัสเดิม = คนเดิม
	bool wants_controller = false;
	int latency = -1;
	QString avatar;                 // PNG 64×64 data URI ที่เรา encode เอง (sanitizeAvatar)
	QString color;
	std::deque<qint64> chat_times;  // จำกัด: 5 ข้อความ / 10 วิ
	bool mic_on = false;            // เพื่อนเปิดไมค์ (หน้าเว็บบอก)
	bool mic_muted = false;         // เจ้าของเครื่องปิดเสียงคนนี้
	bool speaking = false;          // GUI thread อัปเดตจาก voice ทุก 200 ms
	double heart_tokens = 20;       // หัวใจ: token bucket 8 ดวง/วิ สะสมได้ 20
	qint64 heart_ms = 0;
};

struct PsWrapLink::Impl
{
	PsWrapRecorder *rec = nullptr;
	std::function<bool(PsWrapRecConfig *, QString *)> config_getter;
	QPointer<StreamSession> session;
	bool dev = false;
	int kbps_1080 = 8000;
	QString signal_url;
	QString host_key;
	bool beta_key = false;   // ใช้ beta key ที่ฝังมา (ห้องเต็มง่ายกว่า — ข้อความ server-full บอกต่างกัน)

	// GUI thread
	bool active = false;
	QString state = QStringLiteral("off");
	QString room, invite, qr;
	bool announce_invite = true;   // ห้องใหม่/รีเซ็ต → คัดลอก + toast
	QWebSocket *sig = nullptr;
	QTimer reconnect_timer, pad_timer, voice_timer, rumble_timer;
	// การสั่นจาก PS5 ตอนเพื่อนถือจอย (เขียนจาก thread ของ lib · rumble_timer ส่งต่อทุก 50 ms)
	std::atomic<int> rumble_l{0}, rumble_r{0};
	std::atomic<bool> rumble_haptics{false};
	std::atomic<int64_t> rumble_ms{0};
	bool rumble_sent_nonzero = false;
	PsWrapLinkVoice voice;   // เสียงไมค์เพื่อน → ลำโพงเรา
	std::vector<rtc::IceServer> ice;
	std::map<int, std::shared_ptr<LinkVideoTap>> video_taps;
	std::shared_ptr<LinkAudioTap> audio_tap;
	bool pad_held = false;
	QString view_req;            // คำขอเข้าดูจากทีมที่รอเจ้าของห้องตอบ
	QTimer view_req_timer;
	QString reclaim_resume;     // คนที่ถือจอยแล้วหลุด (รีเฟรช/เน็ตตก) — กลับมาภายในเวลา = ได้จอยคืนเอง
	qint64 reclaim_until = 0;
	QString reclaim_name;

	// ใช้ข้าม thread (rtc / tap / worker ของ recorder) ใต้ m
	mutable std::mutex m;
	std::map<QString, std::shared_ptr<Peer>> peers;
	QString controller;
	std::atomic<uint64_t> gen{0};          // เปลี่ยนทุกครั้งที่ปิด/ต่อใหม่ — callback รุ่นเก่าทิ้งตัวเอง
	std::atomic<int64_t> last_pad_us{0};
};

PsWrapLink::PsWrapLink(QObject *parent, PsWrapRecorder *recorder, std::function<bool(PsWrapRecConfig *, QString *)> config_getter)
	: QObject(parent), d(std::make_unique<Impl>())
{
	d->rec = recorder;
	d->config_getter = std::move(config_getter);
	d->dev = !qEnvironmentVariableIsEmpty("PSWRAP_LINK_DEV");
	bool ok = false;
	const int kbps = qEnvironmentVariableIntValue("PSWRAP_LINK_KBPS", &ok);
	if (ok && kbps >= 1000 && kbps <= 50000)
		d->kbps_1080 = kbps;
	d->signal_url = qEnvironmentVariableIsEmpty("PSWRAP_LINK_SIGNAL") ? QString::fromLatin1(kDefaultSignal)
	                                                                   : QString::fromUtf8(qgetenv("PSWRAP_LINK_SIGNAL")).trimmed();
	// host key: Credential Manager · env ครั้งแรก = นำเข้า (ไม่ log ค่า)
	const QString env_key = QString::fromUtf8(qgetenv("PSWRAP_LINK_HOST_KEY")).trimmed();
	if (!env_key.isEmpty()) {
		d->host_key = env_key;
		if (PsWrapSecrets::write(QString::fromLatin1(kHostKeyTarget), env_key) == PsWrapSecrets::Result::Ok)
			qCInfo(pswrapLink) << "host key saved to Credential Manager";
	} else if (!qEnvironmentVariableIsSet("PSWRAP_LINK_USE_BETA")) {   // ทดสอบเส้นทาง beta บนเครื่องที่มี key ของตัวเอง
		PsWrapSecrets::read(QString::fromLatin1(kHostKeyTarget), &d->host_key);
	}
#ifdef PSWRAP_LINK_BETA
	// ไม่มี key ของตัวเอง → beta key ที่ฝังมากับ build สาธารณะ (เซิร์ฟเวอร์จำกัดห้องของ key นี้แยก)
	if (d->host_key.isEmpty()) {
		static const char kHex[] = "0123456789abcdef";
		for (size_t i = 0; i < sizeof(kBetaKey); i++)
			d->host_key += QLatin1Char(kHex[(kBetaKey[i] ^ ((i * 7 + 3) % 16)) & 15]);
		d->beta_key = true;
	}
#endif

	d->reconnect_timer.setSingleShot(true);
	d->reconnect_timer.setInterval(3000);
	connect(&d->reconnect_timer, &QTimer::timeout, this, &PsWrapLink::connectSignal);
	d->pad_timer.setInterval(100);
	connect(&d->pad_timer, &QTimer::timeout, this, &PsWrapLink::pollPad);
	// แรงสั่น → เพื่อนที่ถือจอย ทาง "input" (unordered — ข้อความเก่าหายได้ ข้อความใหม่ทับ) · หน้าเว็บสั่นทีละ ~150 ms
	// rumble จาก event = ค้างจนเกมสั่งใหม่ → ส่งซ้ำทุกรอบ · จาก haptics = ไม่มีเฟรมใหม่ 100 ms ถือว่าหยุด
	d->view_req_timer.setSingleShot(true);
	connect(&d->view_req_timer, &QTimer::timeout, this, [this]() {   // ไม่ตอบ = หมดอายุ (เซิร์ฟเวอร์ถือว่าไม่อนุญาต)
		d->view_req.clear();
		emit changed();
	});
	d->rumble_timer.setInterval(50);
	connect(&d->rumble_timer, &QTimer::timeout, this, [this]() {
		int l = d->rumble_l, r = d->rumble_r;
		if (d->rumble_haptics && QDateTime::currentMSecsSinceEpoch() - d->rumble_ms > 100)
			l = r = 0;
		const bool nonzero = l || r;
		if (!nonzero && !d->rumble_sent_nonzero)
			return;
		d->rumble_sent_nonzero = nonzero;
		std::shared_ptr<rtc::DataChannel> in;
		{
			std::lock_guard<std::mutex> lock(d->m);
			auto it = d->peers.find(d->controller);
			if (it != d->peers.end())
				in = it->second->input;
		}
		if (in && in->isOpen())
			in->send(compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("rumble")}, {QStringLiteral("l"), l}, {QStringLiteral("r"), r}}));
	});
	// ไฟ "กำลังพูด" ในการ์ด/แผง — เช็คทุก 200 ms แจ้ง QML เฉพาะตอนเปลี่ยน
	d->voice_timer.setInterval(200);
	connect(&d->voice_timer, &QTimer::timeout, this, [this]() {
		bool dirty = false;
		{
			std::lock_guard<std::mutex> lock(d->m);
			for (auto &[id, p] : d->peers) {
				const bool s = d->voice.speaking(id);
				if (s != p->speaking) {
					p->speaking = s;
					dirty = true;
				}
			}
		}
		if (dirty)
			emit changed();
	});
	rtc::InitLogger(rtc::LogLevel::Warning);
	qCInfo(pswrapLink).noquote() << "Link ready, signaling" << d->signal_url << (d->host_key.isEmpty() ? "(no host key)" : d->beta_key ? "(beta key)" : "")
		<< (d->dev ? "· DEV (timestamp bar)" : "");
}

PsWrapLink::~PsWrapLink()
{
	stop();
	rtc::Cleanup().wait_for(std::chrono::seconds(2));   // รอ thread ของ libdatachannel ปิด ก่อน object นี้หาย
}

bool PsWrapLink::available() const { return !d->host_key.isEmpty(); }
bool PsWrapLink::active() const { return d->active; }
QString PsWrapLink::state() const { return d->state; }
QString PsWrapLink::room() const { return d->room; }
QString PsWrapLink::inviteUrl() const { return d->invite; }
QString PsWrapLink::inviteQr() const { return d->qr; }
QString PsWrapLink::controllerPeer() const
{
	std::lock_guard<std::mutex> lock(d->m);
	return d->controller;
}
int PsWrapLink::friendCount() const
{
	std::lock_guard<std::mutex> lock(d->m);
	// ทีม PS-WRAP (ดูอย่างเดียว) ไม่นับเป็นที่นั่งเพื่อน (เซิร์ฟเวอร์ก็ไม่นับ)
	return int(std::count_if(d->peers.begin(), d->peers.end(), [](const auto &kv) { return !kv.second->staff; }));
}
QVariantList PsWrapLink::friends() const
{
	QVariantList out;
	std::lock_guard<std::mutex> lock(d->m);
	for (const auto &[id, p] : d->peers) {
		QVariantMap f;
		f[QStringLiteral("peer")] = id;
		f[QStringLiteral("name")] = p->name.isEmpty() ? tr("Friend") : p->name;
		f[QStringLiteral("status")] = p->status;
		f[QStringLiteral("controller")] = id == d->controller;
		f[QStringLiteral("wantsController")] = p->wants_controller;
		f[QStringLiteral("quality")] = QStringLiteral("%1p").arg(p->height);
		f[QStringLiteral("latency")] = p->latency;
		f[QStringLiteral("avatar")] = p->avatar;
		f[QStringLiteral("color")] = p->color;
		f[QStringLiteral("mic")] = p->mic_on;
		f[QStringLiteral("staff")] = p->staff;
		f[QStringLiteral("micMuted")] = p->mic_muted;
		f[QStringLiteral("speaking")] = p->speaking;
		out << f;
	}
	return out;
}

void PsWrapLink::setState(const QString &state)
{
	if (d->state == state)
		return;
	d->state = state;
	emit changed();
}

void PsWrapLink::setInvite(const QString &room, const QString &url)
{
	d->room = room;
	d->invite = url;
	d->qr = qrDataUri(url);
	emit changed();
}

void PsWrapLink::setSession(StreamSession *session)
{
	d->session = session;
	if (!session)
		stop();   // สตรีมจบ = ปิดห้อง (เพื่อนได้ "เจ้าของเครื่องปิด Link แล้ว")
}

void PsWrapLink::toggle()
{
	if (d->active)
		stop();
	else
		start();
}

void PsWrapLink::start()
{
	if (d->active)
		return;
	if (d->host_key.isEmpty()) {
		emit toast(QStringLiteral("error"), tr("Link isn't set up"), tr("This PC has no Link host key yet."));
		return;
	}
	if (!d->session) {
		emit toast(QStringLiteral("error"), tr("Couldn't start Link"), tr("Start a stream first."));
		return;
	}
	PsWrapRecConfig cfg;
	QString err;
	if (!d->config_getter || !d->config_getter(&cfg, &err)) {
		emit toast(QStringLiteral("error"), tr("Couldn't start Link"), err.isEmpty() ? tr("Start a stream first.") : err);
		return;
	}
	// HDR: LinkVideoTap แปลงเป็น SDR ให้เพื่อนเอง (tonemapP010ToNv12)
	d->active = true;
	d->gen++;
	d->announce_invite = true;
	setState(QStringLiteral("connecting"));
	d->pad_timer.start();
	// เสียงเพื่อนผสมเข้าเสียงเกม (ลำโพง/volume/echo cancel ชุดเดียวกัน)
	StreamSession::PsWrapSetRumbleSink([](void *ctx, uint8_t l, uint8_t r, bool haptics) {
		auto *dd = static_cast<Impl *>(ctx);
		dd->rumble_l = l;
		dd->rumble_r = r;
		dd->rumble_haptics = haptics;
		dd->rumble_ms = QDateTime::currentMSecsSinceEpoch();
	}, d.get());
	d->rumble_timer.start();
	StreamSession::PsWrapSetAudioMixer([](void *ctx, int16_t *pcm, size_t frames, unsigned ch, unsigned rate) {
		static_cast<PsWrapLinkVoice *>(ctx)->mix(pcm, frames, ch, rate);
	}, &d->voice);
	d->voice_timer.start();
	connectSignal();
	qCInfo(pswrapLink) << "Link on";
	emit changed();
}

void PsWrapLink::stop()
{
	if (!d->active)
		return;
	d->active = false;
	d->gen++;
	d->reconnect_timer.stop();
	d->pad_timer.stop();
	d->voice_timer.stop();
	d->view_req_timer.stop();
	d->view_req.clear();
	releaseController(false);
	std::map<QString, std::shared_ptr<Peer>> peers;
	{
		std::lock_guard<std::mutex> lock(d->m);
		peers.swap(d->peers);
	}
	for (auto &[id, p] : peers)
		if (p->pc)
			p->pc->close();
	dropUnusedTaps();
	StreamSession::PsWrapSetAudioMixer(nullptr, nullptr);
	StreamSession::PsWrapSetRumbleSink(nullptr, nullptr);
	d->rumble_timer.stop();
	d->voice.stop();
	if (d->sig)
		d->sig->close();   // ห้องบนเซิร์ฟเวอร์ปิดตาม → เพื่อนได้ host-left
	setInvite(QString(), QString());
	setState(QStringLiteral("off"));
	qCInfo(pswrapLink) << "Link off";
	emit changed();
}

void PsWrapLink::copyInvite()
{
	if (d->invite.isEmpty())
		return;
	QGuiApplication::clipboard()->setText(d->invite);
	emit toast(QStringLiteral("success"), tr("Invite link copied"), tr("Send it to a friend. They open it in a web browser."));
}

void PsWrapLink::resetInvite()
{
	if (d->state != QLatin1String("ready"))
		return;
	d->announce_invite = true;
	sendSignal(QJsonObject{{QStringLiteral("type"), QStringLiteral("reset-invite")}});
}

// ---------------------------------------------------------------- signaling (GUI thread)
void PsWrapLink::connectSignal()
{
	// QWebSocket ไม่ใช่ rtc::WebSocket: libdatachannel บน Windows ปิดการตรวจใบรับรอง TLS ตายตัว (W01)
	// → MITM ปลอมเป็นเซิร์ฟเวอร์แล้วขโมย host key ได้ · Qt ตรวจกับที่เก็บ CA ของ Windows และไม่ยอมต่อถ้าไม่ผ่าน
	if (!d->active)
		return;
	if (!d->sig) {
		d->sig = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
		connect(d->sig, &QWebSocket::connected, this, [this]() {
			d->sig->sendTextMessage(QString::fromStdString(compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("host-hello")},
			                                                                   {QStringLiteral("key"), d->host_key}})));
		});
		connect(d->sig, &QWebSocket::textMessageReceived, this, &PsWrapLink::onSignalMessage);
		connect(d->sig, &QWebSocket::disconnected, this, [this]() {
			if (!d->active)
				return;   // stop() ปิดเอง
			// ห้องบนเซิร์ฟเวอร์หายไปกับการเชื่อมต่อ → เพื่อนทุกคนหลุด · ต่อใหม่ได้ห้องใหม่ (ลิงก์ใหม่)
			qCWarning(pswrapLink) << "signaling disconnected — retry in 3 s";
			std::map<QString, std::shared_ptr<Peer>> peers;
			{
				std::lock_guard<std::mutex> lock(d->m);
				peers.swap(d->peers);
			}
			for (auto &[id, p] : peers)
				if (p->pc)
					p->pc->close();
			releaseController(false);
			dropUnusedTaps();
			setInvite(QString(), QString());
			d->announce_invite = true;
			setState(QStringLiteral("connecting"));
			d->reconnect_timer.start();
		});
		connect(d->sig, &QWebSocket::sslErrors, this, [](const QList<QSslError> &errors) {
			for (const auto &e : errors)   // ไม่ ignore — ใบรับรองไม่ผ่าน = ไม่ต่อ
				qCWarning(pswrapLink).noquote() << "signaling TLS error:" << e.errorString();
		});
		connect(d->sig, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
			qCWarning(pswrapLink).noquote() << "signaling error:" << d->sig->errorString();
		});
	}
	d->sig->open(QUrl(d->signal_url));
}

void PsWrapLink::sendSignal(const QJsonObject &msg)
{
	// thread ใดก็ได้ → ส่งผ่าน QWebSocket บน GUI thread
	const QString text = QString::fromStdString(compact(msg));
	QMetaObject::invokeMethod(this, [this, text]() {
		if (d->sig && d->sig->state() == QAbstractSocket::ConnectedState)
			d->sig->sendTextMessage(text);
	}, Qt::QueuedConnection);
}

void PsWrapLink::onSignalMessage(const QString &text)
{
	if (!d->active)
		return;
	const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
	const QString type = o.value(QStringLiteral("type")).toString();
	if (type == QLatin1String("room")) {
		const QJsonObject turn = o.value(QStringLiteral("turn")).toObject();
		d->ice.clear();
		for (const auto &u : turn.value(QStringLiteral("urls")).toArray()) {
			rtc::IceServer s(u.toString().toStdString());
			if (s.type == rtc::IceServer::Type::Turn) {
				s.username = turn.value(QStringLiteral("username")).toString().toStdString();
				s.password = turn.value(QStringLiteral("credential")).toString().toStdString();
			}
			d->ice.push_back(s);
		}
		// ลิงก์มีรหัสเชิญ — ไม่ลง log (log ส่งให้ทีมดูได้)
		setInvite(o.value(QStringLiteral("room")).toString(), o.value(QStringLiteral("invite")).toString());
		qCInfo(pswrapLink).noquote() << "room" << d->room << "ready";
		setState(QStringLiteral("ready"));
		if (d->announce_invite) {
			d->announce_invite = false;
			QGuiApplication::clipboard()->setText(d->invite);
			emit toast(QStringLiteral("success"), tr("Link is on — invite link copied"),
			           tr("Send it to up to %1 friends. They watch first; you hand over the controller.").arg(kMaxFriends));
		}
	} else if (type == QLatin1String("join")) {
		const QString peer = o.value(QStringLiteral("peer")).toString();
		const QString name = o.value(QStringLiteral("name")).toString().left(32);
		addPeer(peer, name, o.value(QStringLiteral("h")).toInt(), o.value(QStringLiteral("kbps")).toInt(), o.value(QStringLiteral("pace")).toDouble());
		{
			static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9_-]{8,40}$"));
			const QString resume = o.value(QStringLiteral("resume")).toString();
			std::lock_guard<std::mutex> lock(d->m);
			auto it = d->peers.find(peer);
			if (it != d->peers.end() && ok.match(resume).hasMatch())
				it->second->resume = resume;
			if (it != d->peers.end())
				it->second->staff = o.value(QStringLiteral("staff")).toBool();
		}
		emit toast(QStringLiteral("info"), tr("%1 joined Link").arg(name.isEmpty() ? tr("A friend") : name), QString());
	} else if (type == QLatin1String("view-request")) {
		d->view_req = o.value(QStringLiteral("req")).toString().left(32);
		d->view_req_timer.start(std::clamp(o.value(QStringLiteral("ttl")).toInt(60), 5, 120) * 1000);
		qCInfo(pswrapLink) << "PS-WRAP team asks to view this room";
		emit toast(QStringLiteral("info"), tr("PS-WRAP team asks to view your stream"), tr("Allow or decline on the Link card."));
		emit changed();
	} else if (type == QLatin1String("signal")) {
		const QJsonObject data = o.value(QStringLiteral("data")).toObject();
		std::shared_ptr<rtc::PeerConnection> pc;
		{
			std::lock_guard<std::mutex> lock(d->m);
			auto it = d->peers.find(o.value(QStringLiteral("peer")).toString());
			if (it != d->peers.end())
				pc = it->second->pc;
		}
		if (pc && data.value(QStringLiteral("type")).toString() == QLatin1String("answer")) {
			try {
				pc->setRemoteDescription(rtc::Description(data.value(QStringLiteral("sdp")).toString().toStdString(), "answer"));
			} catch (const std::exception &e) {
				qCWarning(pswrapLink) << "bad answer:" << e.what();
			}
		}
	} else if (type == QLatin1String("leave")) {
		removePeer(o.value(QStringLiteral("peer")).toString(), false);
	} else if (type == QLatin1String("error")) {
		const QString e = o.value(QStringLiteral("error")).toString();
		qCWarning(pswrapLink).noquote() << "signaling server:" << e;
		if (e == QLatin1String("unauthorized")) {
			if (d->beta_key)   // beta key ถูกถอน/เปลี่ยน → ต้องอัปเดตแอป
				emit toast(QStringLiteral("error"), tr("Link beta is closed"), tr("This version's Link beta has ended. Check for an update in About."));
			else
				emit toast(QStringLiteral("error"), tr("Link was refused"), tr("The Link server didn't accept this PC's host key."));
			stop();
		} else if (e == QLatin1String("server-full")) {
			if (d->beta_key)
				emit toast(QStringLiteral("error"), tr("Link beta is full"), tr("Try again in a few minutes. Each internet connection can open up to 2 rooms."));
			else
				emit toast(QStringLiteral("error"), tr("Link server is busy"), tr("Try again in a few minutes."));
			stop();
		}
	}
}

// ---------------------------------------------------------------- เพื่อน
bool PsWrapLink::ensureVideoTap(int height)
{
	if (d->video_taps.count(height))
		return true;
	PsWrapRecConfig cfg;
	QString err;
	if (!d->rec || !d->config_getter || !d->config_getter(&cfg, &err))
		return false;
	auto tap = std::make_shared<LinkVideoTap>(height, kbpsFor(height, d->kbps_1080), d->dev);
	Impl *dd = d.get();
	tap->send = [dd, height](const uint8_t *data, size_t size, int64_t pts_ms) {
		std::vector<std::shared_ptr<rtc::Track>> tracks;
		{
			std::lock_guard<std::mutex> lock(dd->m);
			for (const auto &[id, p] : dd->peers)
				if (p->height == height && p->video_open)
					tracks.push_back(p->video);
		}
		for (auto &t : tracks) {
			try {
				t->sendFrame(reinterpret_cast<const std::byte *>(data), size,
				             rtc::FrameInfo(std::chrono::duration<double>(double(pts_ms) / 1000.0)));
			} catch (const std::exception &e) {
				qCWarning(pswrapLink) << "send video:" << e.what();
			}
		}
	};
	QPointer<PsWrapLink> self(this);
	LinkVideoTap *raw = tap.get();
	tap->on_detached = [self, raw, height]() {
		// recorder ถอดเอง (pipeline ปิด/encoder พัง) → ลบจากรายการนอก call stack ของ removeTap
		QMetaObject::invokeMethod(self, [self, raw, height]() {
			if (!self)
				return;
			auto it = self->d->video_taps.find(height);
			if (it != self->d->video_taps.end() && it->second.get() == raw) {
				it->second->stopThread();
				self->d->video_taps.erase(it);
			}
		}, Qt::QueuedConnection);
	};
	tap->startThread();
	if (!d->rec->addTap(cfg, tap, &err)) {
		qCWarning(pswrapLink) << "video tap failed:" << err;
		tap->stopThread();
		return false;
	}
	d->video_taps[height] = tap;
	return true;
}

void PsWrapLink::ensureAudioTap()
{
	if (d->audio_tap)
		return;
	PsWrapRecConfig cfg;
	QString err;
	if (!d->rec || !d->config_getter || !d->config_getter(&cfg, &err))
		return;
	auto tap = std::make_shared<LinkAudioTap>();
	if (!tap->open(&err)) {
		qCWarning(pswrapLink) << "audio off:" << err;
		return;
	}
	Impl *dd = d.get();
	// ส่ง packet เดียวกันให้ทุกคนที่ track เปิดแล้ว · voice = true → track ไมค์เจ้าของเครื่อง
	auto sender = [dd](bool voice) {
		return [dd, voice](const uint8_t *data, size_t size, int64_t pos) {
			std::vector<std::shared_ptr<rtc::Track>> tracks;
			{
				std::lock_guard<std::mutex> lock(dd->m);
				for (const auto &[id, p] : dd->peers)
					if (voice ? p->voice_open.load() : p->audio_open.load())
						tracks.push_back(voice ? p->voice : p->audio);
			}
			for (auto &t : tracks) {
				try {
					t->sendFrame(reinterpret_cast<const std::byte *>(data), size,
					             rtc::FrameInfo(std::chrono::duration<double>(double(pos) / 48000.0)));
				} catch (const std::exception &e) {
					qCWarning(pswrapLink) << "send audio:" << e.what();
				}
			}
		};
	};
	tap->game.send = sender(false);
	tap->voice.send = sender(true);
	QPointer<PsWrapLink> self(this);
	LinkAudioTap *raw = tap.get();
	tap->on_detached = [self, raw]() {
		QMetaObject::invokeMethod(self, [self, raw]() {
			if (self && self->d->audio_tap.get() == raw)
				self->d->audio_tap.reset();
		}, Qt::QueuedConnection);
	};
	if (!d->rec->addTap(cfg, tap, &err)) {
		qCWarning(pswrapLink) << "audio tap failed:" << err;
		return;
	}
	d->audio_tap = tap;
}

void PsWrapLink::dropUnusedTaps()
{
	// GUI thread: กลุ่มความสูงที่ไม่มีใครดูแล้ว = ถอด encoder (ไม่กิน GPU) · ไม่มีใครเลย = ถอดเสียงด้วย
	std::vector<int> heights;
	bool anyone;
	{
		std::lock_guard<std::mutex> lock(d->m);
		anyone = !d->peers.empty();
		for (const auto &[id, p] : d->peers)
			heights.push_back(p->height);
	}
	for (auto it = d->video_taps.begin(); it != d->video_taps.end();) {
		if (std::find(heights.begin(), heights.end(), it->first) != heights.end()) {
			++it;
			continue;
		}
		auto tap = it->second;
		it = d->video_taps.erase(it);
		if (d->rec)
			d->rec->removeTap(tap.get());
		tap->stopThread();
	}
	if (!anyone && d->audio_tap) {
		auto tap = d->audio_tap;
		d->audio_tap.reset();
		if (d->rec)
			d->rec->removeTap(tap.get());
	}
}

void PsWrapLink::addPeer(const QString &peer, const QString &name, int height, int kbps, double pace)
{
	Q_UNUSED(kbps)   // ตอนนี้ bitrate ตามกลุ่มความสูง (encoder ใช้ร่วมกัน)
	height = normalizeHeight(height);
	{
		std::lock_guard<std::mutex> lock(d->m);
		if (int(d->peers.size()) >= kMaxFriends || d->peers.count(peer))
			return;
	}
	if (!ensureVideoTap(height)) {
		sendSignal(QJsonObject{{QStringLiteral("type"), QStringLiteral("kick")}, {QStringLiteral("peer"), peer}});
		emit toast(QStringLiteral("error"), tr("A friend couldn't join"), tr("The video encoder for Link didn't start."));
		return;
	}
	ensureAudioTap();

	auto p = std::make_shared<Peer>();
	p->id = peer;
	p->name = name;
	p->color = nameColor(name.isEmpty() ? peer : name);
	p->height = height;
	p->tap = d->video_taps[height];
	rtc::Configuration cfg;
	cfg.iceServers = d->ice;
	cfg.disableAutoNegotiation = true;
	p->pc = std::make_shared<rtc::PeerConnection>(cfg);

	const uint64_t gen = d->gen;
	Impl *dd = d.get();
	QPointer<PsWrapLink> self(this);
	std::weak_ptr<Peer> wp = p;
	std::weak_ptr<rtc::PeerConnection> wpc = p->pc;
	auto onGui = [self, dd, gen](std::function<void(PsWrapLink *)> fn) {
		QMetaObject::invokeMethod(self, [self, dd, gen, fn]() {
			if (self && dd->gen == gen)
				fn(self);
		}, Qt::QueuedConnection);
	};

	p->pc->onStateChange([onGui, peer](rtc::PeerConnection::State st) {
		std::ostringstream s;
		s << st;
		qCInfo(pswrapLink).noquote() << "friend" << peer << "pc" << QString::fromStdString(s.str());
		if (st == rtc::PeerConnection::State::Connected) {
			onGui([peer](PsWrapLink *l) {
				{
					std::lock_guard<std::mutex> lock(l->d->m);
					auto it = l->d->peers.find(peer);
					if (it == l->d->peers.end())
						return;
					it->second->status = QStringLiteral("watching");
				}
				emit l->changed();
			});
		} else if (st == rtc::PeerConnection::State::Failed) {
			onGui([peer](PsWrapLink *l) { l->removePeer(peer, true, true); });   // เน็ตหลุด = จองจอยคืนให้ได้
		}
	});
	// ไม่ทำ trickle: รอ gather เสร็จแล้วส่ง SDP ก้อนเดียว
	p->pc->onGatheringStateChange([self, dd, gen, wpc, peer](rtc::PeerConnection::GatheringState st) {
		auto pc = wpc.lock();
		if (st != rtc::PeerConnection::GatheringState::Complete || !pc || !self || dd->gen != gen)
			return;
		auto desc = pc->localDescription();
		self->sendSignal(QJsonObject{{QStringLiteral("type"), QStringLiteral("signal")}, {QStringLiteral("peer"), peer},
		                             {QStringLiteral("data"), QJsonObject{{QStringLiteral("type"), QString::fromStdString(desc->typeString())},
		                                                                  {QStringLiteral("sdp"), QString::fromStdString(std::string(*desc))}}}});
	});

	// ---- ภาพ (คนละ stream กับเสียง — เบราว์เซอร์ไม่หน่วงภาพรอ sync)
	const uint32_t vssrc = QRandomGenerator::global()->generate() | 1;
	rtc::Description::Video vm("video", rtc::Description::Direction::SendOnly);
	vm.addH264Codec(kVideoPt, "profile-level-id=42e02a;packetization-mode=1;level-asymmetry-allowed=1");
	vm.addSSRC(vssrc, "pswrap", "pswrap-video", "video");
	vm.addExtMap(rtc::Description::Entry::ExtMap(kPlayoutDelayExtId, "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay"));
	p->video = p->pc->addTrack(vm);
	auto vrtp = std::make_shared<rtc::RtpPacketizationConfig>(vssrc, "pswrap", kVideoPt, rtc::H264RtpPacketizer::ClockRate);
	vrtp->playoutDelayId = kPlayoutDelayExtId;   // 0/0 = เบราว์เซอร์แสดงทันที ไม่หน่วงไว้ทำให้ลื่น
	vrtp->playoutDelayMin = 0;
	vrtp->playoutDelayMax = 0;
	auto vpk = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, vrtp);
	vpk->addToChain(std::make_shared<rtc::RtcpSrReporter>(vrtp));
	vpk->addToChain(std::make_shared<rtc::RtcpNackResponder>());
	auto currentTap = [dd, wp]() -> std::shared_ptr<LinkVideoTap> {
		auto p = wp.lock();
		if (!p)
			return nullptr;
		std::lock_guard<std::mutex> lock(dd->m);
		return p->tap.lock();
	};
	vpk->addToChain(std::make_shared<rtc::PliHandler>([currentTap]() {
		if (auto t = currentTap())
			t->want_idr = true;
	}));
	if (pace > 0.0)   // ทดลอง: ?pace= ในลิงก์เชิญ
		vpk->addToChain(std::make_shared<SmoothPacer>(kbpsFor(height, d->kbps_1080) * 1000.0 / 8.0 * pace));
	p->video->setMediaHandler(vpk);
	p->video->onOpen([wp, currentTap]() {
		if (auto p = wp.lock())
			p->video_open = true;
		if (auto t = currentTap())
			t->want_idr = true;   // เพื่อนใหม่ต้องเริ่มที่ keyframe
	});
	p->video->onClosed([wp]() {
		if (auto p = wp.lock())
			p->video_open = false;
	});

	// ---- เสียง
	const uint32_t assrc = QRandomGenerator::global()->generate() | 1;
	// sendrecv: เสียงเกมไปหาเพื่อน + ไมค์เพื่อนกลับมา (หน้าเว็บตั้ง transceiver เป็น sendrecv ก่อนตอบ → ได้ SSRC ใน SDP)
	rtc::Description::Audio am("audio", rtc::Description::Direction::SendRecv);
	am.addOpusCodec(kAudioPt);
	am.addSSRC(assrc, "pswrap", "pswrap-audio", "audio");
	p->audio = p->pc->addTrack(am);
	auto artp = std::make_shared<rtc::RtpPacketizationConfig>(assrc, "pswrap", kAudioPt, rtc::OpusRtpPacketizer::DefaultClockRate);
	auto apk = std::make_shared<rtc::OpusRtpPacketizer>(artp);
	apk->addToChain(std::make_shared<rtc::RtcpSrReporter>(artp));
	apk->addToChain(std::make_shared<rtc::OpusRtpDepacketizer>());   // ขาเข้า: RTP → แพ็กเก็ต Opus (ขาออกผ่านไปเฉยๆ)
	p->audio->setMediaHandler(apk);
	p->audio->onFrame([dd, gen, peer](rtc::binary data, rtc::FrameInfo info) {
		if (dd->gen != gen || data.empty())
			return;
		dd->voice.push(peer, reinterpret_cast<const uint8_t *>(data.data()), data.size(), info.timestamp);
	});
	p->audio->onOpen([wp]() {
		if (auto p = wp.lock())
			p->audio_open = true;
	});
	p->audio->onClosed([wp]() {
		if (auto p = wp.lock())
			p->audio_open = false;
	});

	// ---- ไมค์เจ้าของเครื่อง (แยกจากเสียงเกม — หน้าเว็บปรับความดังแยก) · หน้าเว็บรุ่นเก่าที่ไม่รู้จัก track นี้ก็แค่ไม่เล่น
	const uint32_t host_mic_ssrc = QRandomGenerator::global()->generate() | 1;
	rtc::Description::Audio hm("voice", rtc::Description::Direction::SendOnly);
	hm.addOpusCodec(kAudioPt);
	hm.addSSRC(host_mic_ssrc, "pswrap", "pswrap-voice", "voice");
	p->voice = p->pc->addTrack(hm);
	auto hm_rtp = std::make_shared<rtc::RtpPacketizationConfig>(host_mic_ssrc, "pswrap", kAudioPt, rtc::OpusRtpPacketizer::DefaultClockRate);
	auto hm_pk = std::make_shared<rtc::OpusRtpPacketizer>(hm_rtp);
	hm_pk->addToChain(std::make_shared<rtc::RtcpSrReporter>(hm_rtp));
	p->voice->setMediaHandler(hm_pk);
	p->voice->onOpen([wp]() {
		if (auto p = wp.lock())
			p->voice_open = true;
	});
	p->voice->onClosed([wp]() {
		if (auto p = wp.lock())
			p->voice_open = false;
	});

	// ---- จอย (ไม่ส่งซ้ำ) + ควบคุม (reliable)
	rtc::DataChannelInit input_init;
	input_init.reliability.unordered = true;
	input_init.reliability.maxRetransmits = 0;
	p->input = p->pc->createDataChannel("input", input_init);
	p->control = p->pc->createDataChannel("control");
	p->control->onOpen([onGui, peer]() {
		onGui([peer](PsWrapLink *l) { l->tryReclaim(peer); });
	});
	std::weak_ptr<rtc::DataChannel> winput = p->input;
	p->input->onMessage([self, dd, gen, winput, peer, onGui](rtc::message_variant msg) {
		auto str = std::get_if<std::string>(&msg);
		auto dc = winput.lock();
		if (!str || !dc || dd->gen != gen)
			return;
		const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(*str)).object();
		const QString type = o.value(QStringLiteral("type")).toString();
		if (type == QLatin1String("ping")) {
			dc->send(compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("pong")}, {QStringLiteral("t"), o.value(QStringLiteral("t"))},
			                             {QStringLiteral("host"), double(wallMs())}}));
		} else if (type == QLatin1String("pad")) {
			dc->send(compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("pad-ack")}, {QStringLiteral("t"), o.value(QStringLiteral("t"))}}));
			bool holder;
			{
				std::lock_guard<std::mutex> lock(dd->m);
				holder = dd->controller == peer;
			}
			if (!holder || !o.contains(QStringLiteral("b")))
				return;   // ไม่ได้ถือจอย = ดูอย่างเดียว
			const ChiakiControllerState st = padToState(o);
			dd->last_pad_us = monoUs();
			onGui([peer, st](PsWrapLink *l) {
				{
					std::lock_guard<std::mutex> lock(l->d->m);
					if (l->d->controller != peer)
						return;   // ถูกดึงคืนระหว่างทาง
				}
				if (l->d->session) {
					l->d->session->PsWrapSetLinkState(st);
					l->d->pad_held = true;
				}
			});
		} else if (type == QLatin1String("report") && self && self->d->dev) {
			QString line = QStringLiteral("[browser %1] latency ").arg(peer) + o.value(QStringLiteral("now")).toString();
			const QJsonObject rows = o.value(QStringLiteral("rows")).toObject();
			for (auto it = rows.begin(); it != rows.end(); ++it)
				line += QStringLiteral(" | ") + it.key() + QStringLiteral(": ") + it.value().toString();
			qCInfo(pswrapLink).noquote() << line;
		}
	});
	p->control->onMessage([dd, gen, peer, onGui](rtc::message_variant msg) {
		auto str = std::get_if<std::string>(&msg);
		if (!str || dd->gen != gen)
			return;
		const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(*str)).object();
		onGui([peer, o](PsWrapLink *l) { l->onPeerMessage(peer, o); });
	});
	p->control->onClosed([onGui, peer]() {
		onGui([peer](PsWrapLink *l) {
			std::lock_guard<std::mutex> lock(l->d->m);
			if (l->d->controller == peer)
				l->d->last_pad_us = 0;   // pollPad ปล่อยปุ่มรอบถัดไป
		});
	});

	{
		std::lock_guard<std::mutex> lock(d->m);
		d->peers[peer] = p;
	}
	qCInfo(pswrapLink).noquote() << "friend" << peer << "joined" << height << "p";
	p->pc->setLocalDescription();   // offer
	emit changed();
}

void PsWrapLink::setPeerHeight(const QString &peer, int height)
{
	// เพื่อนเปลี่ยนคุณภาพ: track เดิม แค่ย้ายไปรับจาก encoder กลุ่มใหม่ (เริ่มที่ keyframe — เบราว์เซอร์รับ SPS ใหม่ได้เอง)
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto it = d->peers.find(peer);
		if (it == d->peers.end() || it->second->height == height)
			return;
	}
	if (!ensureVideoTap(height))
		return;
	auto tap = d->video_taps[height];
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto it = d->peers.find(peer);
		if (it == d->peers.end())
			return;
		it->second->height = height;
		it->second->tap = tap;
	}
	tap->want_idr = true;
	dropUnusedTaps();
	qCInfo(pswrapLink).noquote() << "friend" << peer << "quality" << height << "p";
	emit changed();
}

void PsWrapLink::removePeer(const QString &peer, bool notify_server, bool allow_resume)
{
	std::shared_ptr<Peer> p;
	bool was_controller = false;
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto it = d->peers.find(peer);
		if (it == d->peers.end())
			return;
		p = it->second;
		d->peers.erase(it);
		was_controller = d->controller == peer;
	}
	d->voice.removePeer(peer);
	if (was_controller) {
		releaseController(false);
		if (allow_resume && !p->resume.isEmpty()) {
			d->reclaim_resume = p->resume;
			d->reclaim_name = p->name;
			d->reclaim_until = QDateTime::currentMSecsSinceEpoch() + kReclaimMs;
			qCInfo(pswrapLink).noquote() << "friend" << peer << "had the controller, holding it" << kReclaimMs / 1000 << "s for a rejoin";
		}
	}
	if (p->pc)
		p->pc->close();
	if (notify_server)
		sendSignal(QJsonObject{{QStringLiteral("type"), QStringLiteral("kick")}, {QStringLiteral("peer"), peer}});
	dropUnusedTaps();
	qCInfo(pswrapLink).noquote() << "friend" << peer << "left";
	emit toast(QStringLiteral("info"), tr("%1 left Link").arg(p->name.isEmpty() ? tr("A friend") : p->name), QString());
	emit changed();
}

void PsWrapLink::onPeerMessage(const QString &peer, const QJsonObject &msg)
{
	const QString type = msg.value(QStringLiteral("type")).toString();
	QString name;
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto it = d->peers.find(peer);
		if (it == d->peers.end())
			return;
		Peer &p = *it->second;
		name = p.name.isEmpty() ? tr("A friend") : p.name;
		if (type == QLatin1String("want-controller")) {
			const bool want = msg.value(QStringLiteral("on")).toBool() && d->controller != peer && !p.staff;
			if (p.wants_controller == want)
				return;
			p.wants_controller = want;
		} else if (type == QLatin1String("stats")) {
			p.latency = msg.value(QStringLiteral("lat")).toInt(-1);
		} else if (type == QLatin1String("quality")) {
			// ทำนอก lock (ensureVideoTap/dropUnusedTaps ล็อกเอง)
		} else if (type == QLatin1String("profile")) {
			const QString in = msg.value(QStringLiteral("avatar")).toString();
			p.avatar = sanitizeAvatar(in);
			if (p.avatar.isEmpty() && !in.isEmpty())   // Qt ในเครื่องไม่มี plugin WebP — หน้าเว็บต้องส่ง PNG/JPEG
				qCInfo(pswrapLink).noquote() << "friend" << peer << "avatar rejected:" << in.left(24) << in.size() << "chars";
		} else if (type == QLatin1String("mic")) {
			const bool on = msg.value(QStringLiteral("on")).toBool();
			if (p.mic_on == on)
				return;
			p.mic_on = on;
		} else if (type == QLatin1String("react")) {
			// หน้าเว็บรวมยอดส่งทุก ~250 ms · กดรัวแค่ไหนจอเราก็ลอยไม่เกิน 8 ดวง/วิ/คน (ทุกชนิดรวมกัน)
			static const QStringList kinds = {QStringLiteral("heart"), QStringLiteral("flower"), QStringLiteral("rock"), QStringLiteral("wai")};
			const QString kind = msg.value(QStringLiteral("kind")).toString();
			if (!kinds.contains(kind))
				return;
			const qint64 now = QDateTime::currentMSecsSinceEpoch();
			if (p.heart_ms)
				p.heart_tokens = std::min(20.0, p.heart_tokens + (now - p.heart_ms) * 0.008);
			p.heart_ms = now;
			const int want = std::clamp(msg.value(QStringLiteral("n")).toInt(), 0, 50);
			const int n = std::min(want, int(p.heart_tokens));
			if (n <= 0)
				return;
			p.heart_tokens -= n;
			const QString color = p.color;
			// queued = emit หลังปล่อย m (slot ฝั่ง QML ไม่ควรรันใต้ lock)
			QMetaObject::invokeMethod(this, [this, name, color, kind, n]() { emit reaction(name, color, kind, n); }, Qt::QueuedConnection);
			return;
		} else if (type == QLatin1String("chat")) {
			const qint64 now = QDateTime::currentMSecsSinceEpoch();
			while (!p.chat_times.empty() && now - p.chat_times.front() > 10000)
				p.chat_times.pop_front();
			if (p.chat_times.size() >= 5)
				return;   // สแปม — ทิ้งเงียบๆ
			p.chat_times.push_back(now);
		} else {
			return;
		}
	}
	if (type == QLatin1String("quality")) {
		setPeerHeight(peer, normalizeHeight(msg.value(QStringLiteral("h")).toInt()));
		return;
	}
	if (type == QLatin1String("chat")) {
		const QString text = cleanChat(msg.value(QStringLiteral("text")).toString());
		if (text.isEmpty())
			return;
		QString color, avatar;
		std::vector<std::shared_ptr<rtc::DataChannel>> others;
		{
			std::lock_guard<std::mutex> lock(d->m);
			auto it = d->peers.find(peer);
			if (it == d->peers.end())
				return;
			color = it->second->color;
			avatar = it->second->avatar;
			for (const auto &[id, o] : d->peers)
				if (id != peer && o->control)
					others.push_back(o->control);
		}
		emit chatMessage(name, color, text, avatar);
		// ส่งต่อให้เพื่อนคนอื่นในห้อง (ไม่ส่งรูป — แค่ชื่อ/สี/ข้อความ)
		const std::string relay = compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("chat")}, {QStringLiteral("name"), name},
		                                              {QStringLiteral("color"), color}, {QStringLiteral("text"), text}});
		for (auto &dc : others)
			if (dc->isOpen())
				dc->send(relay);
		return;
	}
	if (type == QLatin1String("profile")) {
		emit changed();
		return;
	}
	if (type == QLatin1String("want-controller") && msg.value(QStringLiteral("on")).toBool())
		emit toast(QStringLiteral("info"), tr("%1 wants the controller").arg(name), tr("Open the stream menu › Link to hand it over."));
	emit changed();
}

void PsWrapLink::giveController(const QString &peer)
{
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto pit = d->peers.find(peer);
		if (pit == d->peers.end() || d->controller == peer || pit->second->staff)   // ทีมดูอย่างเดียว
			return;
		d->reclaim_resume.clear();   // มีคนถือจอยแล้ว — การจองคืน (ถ้ามี) จบ
	}
	releaseController(true);   // คนเดิมคืนก่อน (PS5 รับจอยตัวเดียว)
	std::shared_ptr<rtc::DataChannel> ctrl;
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto it = d->peers.find(peer);
		if (it == d->peers.end())
			return;
		d->controller = peer;
		it->second->wants_controller = false;
		ctrl = it->second->control;
	}
	d->last_pad_us = monoUs();
	d->rumble_l = d->rumble_r = 0;
	if (d->session)
		d->session->PsWrapSetLinkRedirect(true);   // การสั่นไปที่จอยเพื่อน · จอยในเครื่องเงียบ/คงสีไฟ
	if (ctrl && ctrl->isOpen())
		ctrl->send(compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("role")}, {QStringLiteral("controller"), true}}));
	qCInfo(pswrapLink).noquote() << "controller to" << peer;   // ASCII ล้วน: ตัวเขียน log ไม่ได้เป็น UTF-8 (→ กลายเป็น ?)
	emit changed();
}

void PsWrapLink::takeController()
{
	d->reclaim_resume.clear();   // เจ้าของเครื่องดึงคืนเอง = ไม่ต้องจองให้ใคร
	releaseController(true);
}

void PsWrapLink::releaseController(bool notify)
{
	std::shared_ptr<rtc::DataChannel> ctrl;
	QString old;
	{
		std::lock_guard<std::mutex> lock(d->m);
		old = d->controller;
		d->controller.clear();
		auto it = d->peers.find(old);
		if (it != d->peers.end())
			ctrl = it->second->control;
	}
	if (d->pad_held && d->session) {
		ChiakiControllerState idle;
		chiaki_controller_state_set_idle(&idle);
		d->session->PsWrapSetLinkState(idle);
	}
	d->pad_held = false;
	if (d->session)
		d->session->PsWrapSetLinkRedirect(false);   // จอยในเครื่องกลับมาสั่น + สีไฟล่าสุด
	if (old.isEmpty())
		return;
	if (notify && ctrl && ctrl->isOpen())
		ctrl->send(compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("role")}, {QStringLiteral("controller"), false}}));
	qCInfo(pswrapLink).noquote() << "controller back from" << old;
	emit changed();
}

void PsWrapLink::kick(const QString &peer)
{
	removePeer(peer, true, false);   // ถูกเตะ = ไม่คืนจอยให้
}

bool PsWrapLink::viewRequest() const { return !d->view_req.isEmpty(); }

void PsWrapLink::answerViewRequest(bool allow)
{
	if (d->view_req.isEmpty())
		return;
	sendSignal(QJsonObject{{QStringLiteral("type"), QStringLiteral("view-answer")}, {QStringLiteral("req"), d->view_req}, {QStringLiteral("allow"), allow}});
	qCInfo(pswrapLink) << "view request" << (allow ? "allowed" : "declined");
	d->view_req.clear();
	d->view_req_timer.stop();
	emit changed();
}

void PsWrapLink::tryReclaim(const QString &peer)
{
	QString name;
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto it = d->peers.find(peer);
		if (d->reclaim_resume.isEmpty() || it == d->peers.end() || it->second->resume != d->reclaim_resume)
			return;
		const bool fresh = QDateTime::currentMSecsSinceEpoch() <= d->reclaim_until;
		const bool free = d->controller.isEmpty();   // ระหว่างนั้นเจ้าของเครื่องยังไม่ได้ให้คนอื่น
		d->reclaim_resume.clear();
		if (!fresh || !free)
			return;
		name = it->second->name;
	}
	giveController(peer);
	qCInfo(pswrapLink).noquote() << "friend" << peer << "rejoined, controller returned";
	emit toast(QStringLiteral("info"), tr("%1 is back with the controller").arg(name.isEmpty() ? tr("A friend") : name), tr("Take it back with Ctrl+Shift+U."));
}

void PsWrapLink::setMicMuted(const QString &peer, bool muted)
{
	std::shared_ptr<rtc::DataChannel> ctrl;
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto it = d->peers.find(peer);
		if (it == d->peers.end() || it->second->mic_muted == muted)
			return;
		it->second->mic_muted = muted;
		it->second->speaking = false;
		ctrl = it->second->control;
	}
	d->voice.setMuted(peer, muted);
	if (ctrl && ctrl->isOpen())
		ctrl->send(compact(QJsonObject{{QStringLiteral("type"), QStringLiteral("mic-muted")}, {QStringLiteral("on"), muted}}));
	qCInfo(pswrapLink).noquote() << "friend" << peer << (muted ? "mic muted" : "mic unmuted");
	emit changed();
}

void PsWrapLink::pollPad()
{
	// GUI thread: ผู้ถือจอยเงียบเกิน 500 ms (เน็ตหลุด/ปิดหน้า) → ปล่อยทุกปุ่ม กันปุ่มค้างใน PS5
	if (!d->pad_held || monoUs() - d->last_pad_us.load() <= kPadTimeoutUs)
		return;
	if (d->session) {
		ChiakiControllerState idle;
		chiaki_controller_state_set_idle(&idle);
		d->session->PsWrapSetLinkState(idle);
	}
	d->pad_held = false;
}
