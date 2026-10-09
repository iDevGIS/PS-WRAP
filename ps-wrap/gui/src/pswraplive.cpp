// PS-WRAP: Go Live — ดูคำอธิบายใน pswraplive.h และ docs/06-go-live.md
#include <pswraplive.h>
#include <pswraprecorder.h>
#include <pswrapsecrets.h>

#include "settings.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <tuple>
#include <mutex>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

Q_LOGGING_CATEGORY(pswrapLive, "pswrap.live", QtInfoMsg)

namespace {

constexpr int kAudioRate = 48000;
constexpr int64_t kQueueUs = 4000000;              // คิวต่อปลายทาง ~4 วิ (วัดเป็นเวลา ไม่ใช่จำนวน packet)
constexpr int64_t kQueueMaxBytes = 64LL << 20;     // กันพลาด: เวลาเพี้ยนก็ไม่กินแรมเกินนี้
constexpr int64_t kOpenTimeoutUs = 15000000;       // ต่อ + handshake + publish
constexpr int64_t kWriteTimeoutUs = 10000000;      // เขียนค้างนานกว่านี้ = สายหลุด
constexpr int64_t kCloseTimeoutUs = 500000;        // ตอนหยุด: trailer/ปิด socket ได้ไม่เกินนี้ (ไม่ให้ UI ค้าง)
constexpr int64_t kShortLivedUs = 5000000;         // ต่อได้แต่หลุดภายใน 5 วิ = "server ปิดใส่" (key ผิดบางเจ้าทำแบบนี้)
constexpr int kQuickFailLimit = 3;
constexpr int kMaxBackoffS = 30;
constexpr size_t kTapMaxVideo = 3;                 // เฟรมรอ encode — เกินนี้ทิ้ง (เฟรมอ้าง slot ของ render thread)
constexpr size_t kTapMaxAudio = 1000;              // ~10 วิของ chunk 10ms
constexpr int kMaxRenditions = 3;                  // bitrate ต่างกันได้กี่ชุด (1 ชุด = 1 NVENC session)

int64_t nowUs()
{
	return av_gettime_relative();
}

QString avErr(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(err, buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

// ---------------------------------------------------------------- platforms (ข้อมูลอยู่ที่นี่ที่เดียว — ดู docs/06 §2)
struct PlatformServer
{
	const char *label;
	const char *url;
};

struct PlatformDef
{
	const char *id;
	const char *name;
	int max_kbps;
	int default_kbps;
	bool vertical;          // ค่าเริ่มต้นของปลายทางใหม่: ส่งภาพแนวตั้ง 9:16 (สลับได้ต่อปลายทางใน Settings › Go Live)
	bool key_per_session;   // key ใหม่ทุกรอบไลฟ์
	std::vector<PlatformServer> servers;
	const char *note;
	int max_height = 1080;  // เพดานความสูงที่ ingest รับ — ภาพจาก pipeline (Output resolution) สูงกว่านี้ถูกย่อลง
};

// codec: ตอนนี้ H.264 อย่างเดียวทุกเจ้า (YouTube HEVC/AV1/HDR = งานต่อ: เพิ่ม "hevc" ที่ YouTube + rendition HEVC)
const std::vector<PlatformDef> &platformDefs()
{
	static const std::vector<PlatformDef> defs = {
		{"youtube", "YouTube", 51000, 6000, false, false,
			{{QT_TRANSLATE_NOOP("PsWrapGoLive", "Primary (RTMPS)"), "rtmps://a.rtmps.youtube.com:443/live2"},
			 {QT_TRANSLATE_NOOP("PsWrapGoLive", "Primary (RTMP)"), "rtmp://a.rtmp.youtube.com/live2"}},
			QT_TRANSLATE_NOOP("PsWrapGoLive", "Stream key: YouTube Studio › Create › Go live › Stream. A new channel must verify its phone number and wait 24 hours before the first live stream. 4K: set Output Resolution to 4K in Settings › Recording and use 35–45 Mbps."),
			2160},
		{"twitch", "Twitch", 6000, 6000, false, false,
			{{QT_TRANSLATE_NOOP("PsWrapGoLive", "Automatic (RTMPS)"), "rtmps://ingest.global-contribute.live-video.net:443/app"}},
			QT_TRANSLATE_NOOP("PsWrapGoLive", "Stream key: Twitch Creator Dashboard › Settings › Stream. When simulcasting, Twitch must get the same quality as your other destinations.")},
		{"facebook", "Facebook", 9000, 6000, false, false,
			{{QT_TRANSLATE_NOOP("PsWrapGoLive", "Facebook Live (RTMPS)"), "rtmps://live-api-s.facebook.com:443/rtmp/"}},
			QT_TRANSLATE_NOOP("PsWrapGoLive", "Facebook gives a new key for each broadcast unless you turn on \"Use a persistent stream key\" in Live Producer.")},
		{"kick", "Kick", 8000, 6000, false, false, {},
			QT_TRANSLATE_NOOP("PsWrapGoLive", "Copy the Stream URL and Stream Key from Kick › Settings › Stream.")},
		{"tiktok", "TikTok LIVE", 6000, 6000, true, true, {},
			QT_TRANSLATE_NOOP("PsWrapGoLive", "TikTok gives a new server URL and key for every LIVE — paste both before going live. Sent as vertical 9:16 using the layout from the 9:16 window.")},
		{"instagram", "Instagram Live", 6000, 4500, true, true, {},
			QT_TRANSLATE_NOOP("PsWrapGoLive", "Create the live video in Instagram's Live Producer first, then paste its server URL and key. Sent as vertical 9:16.")},
		{"x", "X", 9000, 6000, false, false, {},
			QT_TRANSLATE_NOOP("PsWrapGoLive", "Copy the RTMPS URL and stream key from X Media Studio › Producer.")},
		{"custom", QT_TRANSLATE_NOOP("PsWrapGoLive", "Custom RTMP(S)"), 51000, 6000, false, false, {},
			QT_TRANSLATE_NOOP("PsWrapGoLive", "Any RTMP or RTMPS ingest (Restream, Castr, your own server). The stream key is added to the end of the server URL."),
			2160},
	};
	return defs;
}

const PlatformDef *platformDef(const QString &id)
{
	for (const auto &d : platformDefs())
		if (id == QLatin1String(d.id))
			return &d;
	return nullptr;
}

QString platformName(const QString &id)
{
	const PlatformDef *d = platformDef(id);
	return d ? PsWrapGoLive::tr(d->name) : id;
}

// server + key → URL (key ต่อท้ายเป็น path ส่วนสุดท้าย ตามที่ทุกเจ้าใช้)
QByteArray buildUrl(const QString &server, const QString &key)
{
	QString s = server.trimmed();
	while (s.endsWith(QLatin1Char('/')))
		s.chop(1);
	return (s + QLatin1Char('/') + key.trimmed()).toUtf8();
}

// สำหรับ log/สถานะ: host + path ของ server (ไม่มี key, ไม่มี user:pass)
QString redactServer(const QString &server)
{
	const QUrl u(server.trimmed());
	if (!u.isValid() || u.host().isEmpty())
		return QStringLiteral("(invalid server)");
	return QStringLiteral("%1://%2%3").arg(u.scheme(), u.host(), u.port() > 0 ? QStringLiteral(":%1").arg(u.port()) : QString());
}

const char *stateName(PsWrapLiveState s)
{
	switch (s) {
	case PsWrapLiveState::Idle: return "idle";
	case PsWrapLiveState::Connecting: return "connecting";
	case PsWrapLiveState::Live: return "live";
	case PsWrapLiveState::Reconnecting: return "reconnecting";
	case PsWrapLiveState::Error: return "error";
	}
	return "idle";
}

// error ที่ลองซ้ำไปก็ไม่หาย (server ปฏิเสธ key / URL ผิด) — ไม่ retry
bool fatalOpenError(int err, QString *why)
{
	if (err == AVERROR_UNKNOWN || err == AVERROR(EPERM) || err == AVERROR(EACCES)
			|| err == AVERROR_HTTP_UNAUTHORIZED || err == AVERROR_HTTP_FORBIDDEN) {
		// rtmpproto: onStatus/_error "NetStream.Publish.BadName"/"Rejected" ฯลฯ → AVERROR_UNKNOWN หรือ -1
		*why = PsWrapGoLive::tr("The server rejected the stream — check the stream key.");
		return true;
	}
	if (err == AVERROR_HTTP_NOT_FOUND) {
		*why = PsWrapGoLive::tr("The server address was not found — check the server URL.");
		return true;
	}
	if (err == AVERROR_PROTOCOL_NOT_FOUND || err == AVERROR(EINVAL) || err == AVERROR_MUXER_NOT_FOUND) {
		*why = PsWrapGoLive::tr("Invalid server address (%1).").arg(avErr(err));
		return true;
	}
	return false;
}

// server ตัดการเชื่อมต่อ (ไม่ใช่เน็ตล่ม) — Twitch/Kick ปิด socket ใส่เมื่อ key ผิด
bool closedByPeer(int err)
{
	return err == AVERROR_EOF || err == AVERROR(ECONNRESET) || err == AVERROR(EPIPE) || err == -10054 /* WSAECONNRESET */
		|| err == -10053 /* WSAECONNABORTED */;
}

// ข้อความ error เครือข่ายแบบคนอ่านเข้าใจ (av_strerror ของ winsock = "Error number -10054 occurred")
QString netErr(int err)
{
	if (closedByPeer(err))
		return PsWrapGoLive::tr("closed by the server");
	if (err == AVERROR(ECONNREFUSED) || err == -10061)
		return PsWrapGoLive::tr("connection refused");
	if (err == AVERROR(ETIMEDOUT) || err == -10060 || err == AVERROR_EXIT)
		return PsWrapGoLive::tr("timed out");
	if (err == AVERROR(ENETUNREACH) || err == -10051 || err == AVERROR(EHOSTUNREACH) || err == -10065)
		return PsWrapGoLive::tr("network unreachable");
	if (err == AVERROR(EIO))
		return PsWrapGoLive::tr("network error or unknown host");
	return avErr(err);
}

} // namespace

// ================================================================ internal: sink + rendition
namespace PsWrapLiveInternal {

// ---------------------------------------------------------------- Sink — ปลายทางเดียว: คิว + thread ส่ง + reconnect
class Sink : public PsWrapPacketSink
{
public:
	struct Snapshot
	{
		PsWrapLiveState state = PsWrapLiveState::Idle;
		QString text;
		bool fatal = false;
	};

	Sink(const QString &dest_id, const QString &name, const QString &server_label, QByteArray url, const PsWrapEncodedLayout &src)
		: dest_id(dest_id), name(name), server_label(server_label), url(std::move(url))
	{
		layout.count = src.count;
		layout.fps = src.fps;
		for (int i = 0; i < src.count; i++) {
			layout.streams[i] = src.streams[i];
			layout.streams[i].par = avcodec_parameters_alloc();
			if (layout.streams[i].par)
				avcodec_parameters_copy(layout.streams[i].par, src.streams[i].par);
		}
		rb.setLayout(&layout);
	}

	~Sink() override
	{
		requestStop();
		join();
		clearQueue();
		for (int i = 0; i < layout.count; i++)
			avcodec_parameters_free(&layout.streams[i].par);
		// ล้าง URL (มี key) ออกจากแรม
		if (!url.isEmpty())
			std::memset(url.data(), 0, static_cast<size_t>(url.size()));
	}

	void start()
	{
		th = std::thread([this]() { run(); });
	}
	void requestStop()
	{
		stop_flag = true;
		cv.notify_all();
	}
	void join()
	{
		if (th.joinable())
			th.join();
	}

	// thread ของ rendition — ห้ามบล็อก: แค่ rebase + ต่อคิว + ตัดหัวคิวถ้ายาวเกิน
	bool write(const AVPacket *pkt) override
	{
		if (stop_flag || fatal)
			return true;
		ready.clear();
		rb.push(pkt, &ready);
		if (ready.empty())
			return true;
		{
			std::lock_guard<std::mutex> lock(m);
			for (AVPacket *p : ready) {
				const PsWrapEncodedStream &s = layout.streams[p->stream_index];
				const int64_t ts = p->dts != AV_NOPTS_VALUE ? p->dts : p->pts;
				Item it;
				it.p = p;
				it.video = s.video;
				it.key = s.video && (p->flags & AV_PKT_FLAG_KEY);
				it.t_us = ts == AV_NOPTS_VALUE ? newest_us : av_rescale_q(ts, s.time_base, AVRational{1, 1000000});
				it.pts_us = p->pts == AV_NOPTS_VALUE ? it.t_us : av_rescale_q(p->pts, s.time_base, AVRational{1, 1000000});
				newest_us = std::max(newest_us, it.t_us);
				queued_bytes += p->size;
				q.push_back(it);
			}
			trimLocked();
		}
		ready.clear();
		cv.notify_one();
		return true;
	}

	// GUI thread (poll ทุก 500ms)
	Snapshot snapshot()
	{
		Snapshot s;
		s.state = static_cast<PsWrapLiveState>(state.load());
		s.fatal = fatal.load();
		QString detail;
		{
			std::lock_guard<std::mutex> lock(status_m);
			detail = status_detail;
		}
		const int64_t t = nowUs();
		const quint64 b = bytes_sent.load();
		if (last_poll_us > 0 && t > last_poll_us) {
			const double inst = double(b - last_bytes) * 8.0 / (double(t - last_poll_us) / 1e6) / 1e6;
			kbps_avg = kbps_avg <= 0.0 ? inst : kbps_avg * 0.6 + inst * 0.4;
		}
		last_poll_us = t;
		last_bytes = b;
		if (s.state != PsWrapLiveState::Live)
			kbps_avg = 0.0;
		switch (s.state) {
		case PsWrapLiveState::Live: {
			s.text = PsWrapGoLive::tr("Live · %1 Mbps").arg(kbps_avg, 0, 'f', 1);
			const quint64 gops = dropped_gops.load();
			if (gops > 0)
				//: upload too slow: whole 2-second chunks of video were skipped
				s.text += QStringLiteral(" · ") + PsWrapGoLive::tr("skipped %1 s (slow upload)").arg(gops * 2);
			break;
		}
		case PsWrapLiveState::Connecting:
			s.text = PsWrapGoLive::tr("Connecting to %1…").arg(server_label);
			break;
		default:
			s.text = detail;
			break;
		}
		return s;
	}

	const QString dest_id;
	const QString name;
	const QString server_label;

	// สถิติสำหรับ log ตอนจบ
	std::atomic<quint64> bytes_sent{0};
	std::atomic<quint64> dropped_gops{0};
	std::atomic<quint64> dropped_packets{0};
	std::atomic<int> reconnects{0};

private:
	struct Item
	{
		AVPacket *p = nullptr;
		int64_t t_us = 0;     // dts (µs) — ใช้วัดความยาวคิว
		int64_t pts_us = 0;   // pts (µs) — ใช้ตัดเสียงให้ตรง keyframe
		bool video = false;
		bool key = false;
	};

	QByteArray url;   // มี key — ห้าม log
	PsWrapEncodedLayout layout;
	PsWrapPacketRebaser rb;            // thread ของ rendition เท่านั้น
	std::vector<AVPacket *> ready;     // thread ของ rendition เท่านั้น

	std::mutex m;
	std::condition_variable cv;
	std::deque<Item> q;
	int64_t newest_us = 0;
	int64_t queued_bytes = 0;
	bool need_key = true;              // ต้องเริ่มส่งที่ keyframe (ต่อใหม่/คิวว่างหลังทิ้ง)

	std::thread th;
	std::atomic<bool> stop_flag{false};
	std::atomic<bool> fatal{false};
	std::atomic<bool> closing{false};
	std::atomic<int64_t> deadline_us{0};
	std::atomic<int> state{static_cast<int>(PsWrapLiveState::Connecting)};
	std::mutex status_m;
	QString status_detail;

	// GUI thread เท่านั้น (snapshot)
	int64_t last_poll_us = 0;
	quint64 last_bytes = 0;
	double kbps_avg = 0.0;

	AVFormatContext *fmt = nullptr;
	bool header_written = false;

	static int interruptCb(void *opaque)
	{
		auto *s = static_cast<Sink *>(opaque);
		if (s->stop_flag.load() && !s->closing.load())
			return 1;
		const int64_t d = s->deadline_us.load();
		return d > 0 && nowUs() > d ? 1 : 0;
	}

	void setState(PsWrapLiveState st, const QString &detail = QString())
	{
		{
			std::lock_guard<std::mutex> lock(status_m);
			status_detail = detail;
		}
		state = static_cast<int>(st);
	}

	void clearQueue()
	{
		std::lock_guard<std::mutex> lock(m);
		dropAllLocked();
	}

	void freeItemLocked(Item &it)
	{
		queued_bytes -= it.p->size;
		av_packet_free(&it.p);
	}

	// ตัดคิวให้เริ่มที่ q[k] (keyframe): ทิ้งทุกอย่างก่อนหน้า ยกเว้นเสียงที่เวลาอยู่หลัง keyframe นั้น
	// (เสียง encode เร็วกว่าภาพ → packet เสียงของช่วงหลัง keyframe ~0.1–0.3 วิ เข้าคิวก่อน keyframe เอง — ทิ้ง = เสียงหายหัว)
	quint64 cutBeforeKeyLocked(size_t k)
	{
		const int64_t key_pts = q[k].pts_us;
		std::vector<Item> keep;
		quint64 n = 0;
		for (size_t i = 0; i < k; i++) {
			if (!q[i].video && q[i].pts_us >= key_pts) {
				keep.push_back(q[i]);
			} else {
				freeItemLocked(q[i]);
				n++;
			}
		}
		q.erase(q.begin(), q.begin() + static_cast<std::ptrdiff_t>(k));
		q.insert(q.begin(), keep.begin(), keep.end());   // ลำดับข้าม stream ไม่สำคัญ — av_interleaved_write_frame เรียงตาม dts เอง
		return n;
	}

	void dropAllLocked()
	{
		for (auto &it : q)
			freeItemLocked(it);
		q.clear();
		need_key = true;
	}

	bool hasKeyLocked() const
	{
		for (const auto &it : q)
			if (it.key)
				return true;
		return false;
	}

	// คิวยาวเกิน ~4 วิ (เน็ตขาขึ้นไม่พอ/ปลายทางค้าง) → ทิ้ง GOP เก่าสุดทั้งก้อน: ตั้งแต่หัวคิวจนถึง keyframe ถัดไป
	// ทิ้ง "ท้าย" ของ GOP ที่กำลังส่งอยู่ก็ปลอดภัย (ลำดับ decode: ส่วนที่ส่งไปแล้ว decode ได้เสมอ แล้วเริ่มใหม่ที่ keyframe)
	void trimLocked()
	{
		while (!q.empty() && (newest_us - q.front().t_us > kQueueUs || queued_bytes > kQueueMaxBytes)) {
			// keyframe แรกในคิว (หัวคิวอาจเป็นเสียงที่เก็บไว้จากการตัดครั้งก่อน) แล้วตัดไปที่ keyframe "ถัดจากนั้น"
			size_t f = 0;
			while (f < q.size() && !q[f].key)
				f++;
			size_t k = f + 1;
			while (k < q.size() && !q[k].key)
				k++;
			quint64 n = 0;
			if (k >= q.size()) {
				n = q.size();
				dropAllLocked();
			} else {
				n = cutBeforeKeyLocked(k);
			}
			if (n == 0)
				break;
			if (state.load() == static_cast<int>(PsWrapLiveState::Live)) {
				dropped_gops++;
				dropped_packets += n;
			}
		}
	}

	// ต่อเสร็จ: ข้ามของค้างในคิวไปที่ keyframe ล่าสุด (ไม่ส่งของเก่าหลายวินาทีเป็นก้อน = ดีเลย์สะสม)
	void skipToLatestKeyLocked()
	{
		for (size_t i = q.size(); i-- > 0;)
			if (q[i].key) {
				cutBeforeKeyLocked(i);
				need_key = false;
				return;
			}
		need_key = true;   // ยังไม่มี keyframe — pump รอจนมี
	}

	int open(QString *why)
	{
		header_written = false;
		int err = avformat_alloc_output_context2(&fmt, nullptr, "flv", url.constData());
		if (err < 0 || !fmt) {
			fatalOpenError(err < 0 ? err : AVERROR_MUXER_NOT_FOUND, why);
			return err < 0 ? err : AVERROR_MUXER_NOT_FOUND;
		}
		fmt->interrupt_callback.callback = &Sink::interruptCb;
		fmt->interrupt_callback.opaque = this;
		for (int i = 0; i < layout.count; i++) {
			AVStream *st = avformat_new_stream(fmt, nullptr);
			if (!st || avcodec_parameters_copy(st->codecpar, layout.streams[i].par) < 0) {
				*why = PsWrapGoLive::tr("Out of memory.");
				return AVERROR(ENOMEM);
			}
			st->codecpar->codec_tag = 0;
			st->time_base = layout.streams[i].time_base;
			if (layout.streams[i].video && layout.fps > 0)
				st->avg_frame_rate = AVRational{layout.fps, 1};
		}
		av_dict_set(&fmt->metadata, "encoder", "PS-WRAP", 0);
		AVDictionary *opts = nullptr;
		av_dict_set(&opts, "rw_timeout", "10000000", 0);   // ชั้น tcp/tls ใต้ rtmp ก็ได้ค่านี้ (ส่งต่อ options)
		// send buffer ของ socket: Windows + socket แบบ blocking (FFmpeg) ค่าเริ่มต้น ~64 KB → ส่งได้ไม่เกิน buffer ÷ RTT
		// (ไป YouTube สิงคโปร์ ~33 ms ≈ 12–15 Mbps ทั้งที่เน็ตอัปโหลดได้หลายร้อย) — 4 MB รองรับ 4K 50 Mbps แม้ RTT ~500 ms
		av_dict_set(&opts, "send_buffer_size", "4194304", 0);
		deadline_us = nowUs() + kOpenTimeoutUs;
		err = avio_open2(&fmt->pb, url.constData(), AVIO_FLAG_WRITE, &fmt->interrupt_callback, &opts);
		av_dict_free(&opts);
		if (err < 0) {
			deadline_us = 0;
			if (!fatalOpenError(err, why))
				*why = err == AVERROR_EXIT ? PsWrapGoLive::tr("Timed out connecting to %1.").arg(server_label)
				                           : PsWrapGoLive::tr("Couldn't connect to %1 (%2).").arg(server_label, netErr(err));
			return err;
		}
		AVDictionary *mux = nullptr;
		av_dict_set(&mux, "flvflags", "no_duration_filesize", 0);   // ไลฟ์: ไม่ต้อง seek กลับไปเขียน duration
		err = avformat_write_header(fmt, &mux);
		av_dict_free(&mux);
		deadline_us = 0;
		if (err < 0) {
			if (!fatalOpenError(err, why))
				*why = PsWrapGoLive::tr("The server didn't accept the stream (%1).").arg(netErr(err));
			return err;
		}
		header_written = true;
		return 0;
	}

	void closeCtx(bool graceful)
	{
		if (!fmt)
			return;
		closing = true;
		deadline_us = nowUs() + kCloseTimeoutUs;
		if (header_written && graceful)
			av_write_trailer(fmt);
		if (fmt->pb)
			avio_closep(&fmt->pb);   // rtmp: ส่ง FCUnpublish/deleteStream (จำกัดเวลาด้วย deadline)
		avformat_free_context(fmt);
		fmt = nullptr;
		header_written = false;
		deadline_us = 0;
		closing = false;
	}

	// ส่งจากคิวจนกว่า error หรือ stop · คืน 0 = stop, <0 = error
	int pump(QString *why)
	{
		for (;;) {
			Item it;
			{
				std::unique_lock<std::mutex> lock(m);
				cv.wait_for(lock, std::chrono::milliseconds(200),
				            [this] { return stop_flag.load() || (need_key ? hasKeyLocked() : !q.empty()); });
				if (stop_flag)
					return 0;
				if (need_key) {
					// เริ่มส่ง (หรือคิวว่างหลังทิ้ง GOP) ได้ที่ keyframe เท่านั้น
					size_t k = 0;
					while (k < q.size() && !q[k].key)
						k++;
					if (k >= q.size())
						continue;
					cutBeforeKeyLocked(k);
					need_key = false;
				}
				if (q.empty())
					continue;
				it = q.front();
				q.pop_front();
				queued_bytes -= it.p->size;
			}
			AVPacket *p = it.p;
			const int idx = p->stream_index;
			av_packet_rescale_ts(p, layout.streams[idx].time_base, fmt->streams[idx]->time_base);
			p->pos = -1;
			const int size = p->size;
			deadline_us = nowUs() + kWriteTimeoutUs;
			const int err = av_interleaved_write_frame(fmt, p);
			deadline_us = 0;
			av_packet_free(&p);
			if (err < 0) {
				*why = err == AVERROR_EXIT ? PsWrapGoLive::tr("Upload stalled for 10 s.")
				                           : PsWrapGoLive::tr("Connection lost (%1).").arg(netErr(err));
				return err;
			}
			bytes_sent += static_cast<quint64>(size);
		}
	}

	void run()
	{
		int attempt = 0;
		int quick_fails = 0;
		bool ever_live = false;
		while (!stop_flag) {
			setState(ever_live || attempt > 0 ? PsWrapLiveState::Reconnecting : PsWrapLiveState::Connecting,
			         PsWrapGoLive::tr("Connecting to %1…").arg(server_label));
			QString why;
			bool is_fatal = false;
			const int64_t t_open = nowUs();
			int err = open(&why);
			if (err >= 0) {
				{
					std::lock_guard<std::mutex> lock(m);
					skipToLatestKeyLocked();
				}
				qCInfo(pswrapLive).nospace().noquote() << name << ": live → " << server_label << " (connect " << (nowUs() - t_open) / 1000 << " ms"
					<< (attempt > 0 ? QStringLiteral(", after %1 retries").arg(attempt) : QString()) << ")";
				setState(PsWrapLiveState::Live);
				ever_live = true;
				const int64_t t_live = nowUs();
				err = pump(&why);
				const bool stopping = stop_flag.load();
				closeCtx(stopping);
				if (stopping)
					break;
				const bool short_lived = nowUs() - t_live < kShortLivedUs;
				qCWarning(pswrapLive).nospace().noquote() << name << ": dropped after " << (nowUs() - t_live) / 1000 << " ms: " << why;
				if (short_lived) {
					quick_fails++;
				} else {
					quick_fails = 0;
					attempt = 0;
				}
			} else {
				closeCtx(false);
				if (stop_flag)
					break;
				is_fatal = fatalOpenError(err, &why) || is_fatal;
				if (!is_fatal && closedByPeer(err))
					quick_fails++;
				qCWarning(pswrapLive).nospace().noquote() << name << ": connect failed (" << err << "): " << why;
			}
			if (!is_fatal && quick_fails >= kQuickFailLimit) {
				is_fatal = true;
				why = PsWrapGoLive::tr("The server keeps closing the connection — check the stream key and server.");
			}
			if (is_fatal) {
				fatal = true;
				setState(PsWrapLiveState::Error, why);
				clearQueue();
				qCWarning(pswrapLive).nospace().noquote() << name << ": giving up: " << why;
				std::unique_lock<std::mutex> lock(m);
				cv.wait(lock, [this] { return stop_flag.load(); });
				break;
			}
			// backoff 1 → 2 → 4 → 8 → 16 → 30 วิ (+ jitter ≤ 20%) ไม่จำกัดครั้งจนกว่าจะ stop
			const int base_s = std::min(kMaxBackoffS, 1 << std::min(attempt, 5));
			const int64_t delay_us = int64_t(base_s) * 1000000 + int64_t(QRandomGenerator::global()->bounded(base_s * 200)) * 1000;
			attempt++;
			reconnects++;
			const int64_t until = nowUs() + delay_us;
			while (!stop_flag) {
				const int64_t left = until - nowUs();
				if (left <= 0)
					break;
				//: %1 = seconds, %2 = reason
				setState(PsWrapLiveState::Reconnecting, PsWrapGoLive::tr("Reconnecting in %1 s — %2").arg((left + 999999) / 1000000).arg(why));
				std::unique_lock<std::mutex> lock(m);
				cv.wait_for(lock, std::chrono::milliseconds(std::min<int64_t>(250, left / 1000 + 1)), [this] { return stop_flag.load(); });
			}
		}
		setState(PsWrapLiveState::Idle);
	}
};

// ---------------------------------------------------------------- Rendition — encode 1 ชุด (16:9 H.264 CBR + AAC) แจกให้หลาย sink
class Rendition : public PsWrapRawTap
{
public:
	struct Spec
	{
		int video_kbps = 6000;
		int audio_kbps = 160;
		int max_height = 1080;
		int max_fps = 60;
		bool vertical = false;   // PS-WRAP: 9:16 1080x1920 จาก pipeline แนวตั้ง (PsWrapRecorder::secondary())
		// จุดต่อขยาย: canvas 9:16 (V1), codec hevc/av1, HDR (L2) — ดู docs/06 §3
	};

	explicit Rendition(const Spec &spec) : spec(spec) {}

	~Rendition() override
	{
		stopThread();
		std::lock_guard<std::mutex> lock(m);
		for (auto &it : q)
			av_frame_free(&it.frame);
		q.clear();
		if (venc)
			avcodec_free_context(&venc);
		if (aenc)
			avcodec_free_context(&aenc);
		if (fifo)
			av_audio_fifo_free(fifo);
		if (sws)
			sws_freeContext(sws);
		av_packet_free(&pkt);
		for (int i = 0; i < layout.count; i++)
			avcodec_parameters_free(&layout.streams[i].par);
	}

	// GUI thread: เปิด encoder ตามสเปคภาพของ pipeline
	bool open(int src_w, int src_h, int src_fps, QString *error)
	{
		if (spec.vertical) {
			out_h = std::max(std::min(1920, src_h) & ~1, 640);
			out_w = static_cast<int>(std::lround(out_h * 9.0 / 16.0)) & ~1;
		} else {
			out_h = std::min(spec.max_height, src_h) & ~1;
			out_h = std::max(out_h, 360);
			out_w = static_cast<int>(std::lround(out_h * 16.0 / 9.0)) & ~1;
		}
		out_fps = std::clamp(std::min(src_fps, spec.max_fps), 24, 60);
		decimate = src_fps > out_fps;
		pkt = av_packet_alloc();
		if (!pkt || !openVideo(error) || !openAudio(error)) {
			if (error->isEmpty())
				*error = PsWrapGoLive::tr("Out of memory.");
			return false;
		}
		qCInfo(pswrapLive).nospace().noquote() << "rendition: " << out_w << "x" << out_h << "@" << out_fps << " " << encoder_name
			<< " " << spec.video_kbps << " kbps CBR + AAC " << spec.audio_kbps << " kbps (source " << src_w << "x" << src_h << "@" << src_fps << ")";
		return true;
	}

	const PsWrapEncodedLayout &encodedLayout() const { return layout; }
	QString description() const { return QStringLiteral("%1x%2@%3 %4 %5k").arg(out_w).arg(out_h).arg(out_fps).arg(encoder_name).arg(spec.video_kbps); }
	int videoKbps() const { return spec.video_kbps; }

	// ก่อน startThread เท่านั้น
	void addSink(const std::shared_ptr<Sink> &s) { sinks.push_back(s); }

	void startThread()
	{
		th = std::thread([this]() { run(); });
	}
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

	// ---- PsWrapRawTap (worker ของ recorder) — ต่อคิวแล้วกลับทันที
	void videoFrame(const AVFrame *frame) override
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
			if (queued_video < kTapMaxVideo) {
				q.push_back(Item{ref, {}, 0, 0});
				queued_video++;
				ref = nullptr;
			}
		}
		if (ref) {
			tap_video_dropped++;
			av_frame_free(&ref);
			return;
		}
		cv.notify_one();
	}

	void audioMix(const float *left, const float *right, int64_t frames, int64_t pos) override
	{
		if (frames <= 0)
			return;
		Item it;
		it.audio.resize(static_cast<size_t>(frames) * 2);
		std::memcpy(it.audio.data(), left, sizeof(float) * static_cast<size_t>(frames));
		std::memcpy(it.audio.data() + frames, right, sizeof(float) * static_cast<size_t>(frames));
		it.pos = pos;
		it.n = frames;
		std::lock_guard<std::mutex> lock(m);
		if (q.size() - queued_video >= kTapMaxAudio) {
			tap_audio_dropped++;
			return;
		}
		q.push_back(std::move(it));
	}

	void detached() override
	{
		if (on_detached)
			on_detached(this);
	}
	std::function<void(Rendition *)> on_detached;   // GUI thread
	PsWrapRecorder *src_rec = nullptr;               // PS-WRAP: pipeline ที่ tap อยู่ (หลัก หรือแนวตั้ง)

	// สถิติ
	std::atomic<quint64> frames_encoded{0}, tap_video_dropped{0}, tap_audio_dropped{0}, wrong_format{0}, decimated{0};
	std::atomic<int64_t> max_fanout_us{0};
	std::atomic<bool> failed{false};

private:
	struct Item
	{
		AVFrame *frame = nullptr;      // วิดีโอ (nullptr = เสียง)
		std::vector<float> audio;      // planar: L ทั้งก้อน แล้ว R
		int64_t pos = 0;
		int64_t n = 0;
	};

	Spec spec;
	int out_w = 1920, out_h = 1080, out_fps = 60;
	bool decimate = false;
	QString encoder_name;
	PsWrapEncodedLayout layout;
	AVCodecContext *venc = nullptr;
	AVCodecContext *aenc = nullptr;
	AVAudioFifo *fifo = nullptr;
	SwsContext *sws = nullptr;
	AVPacket *pkt = nullptr;
	std::vector<std::shared_ptr<Sink>> sinks;

	std::thread th;
	std::mutex m;
	std::condition_variable cv;
	std::deque<Item> q;
	size_t queued_video = 0;
	bool quit = false;

	// thread ของ rendition เท่านั้น
	int64_t last_video_pts = -1;
	double next_due_ms = -1.0;
	int64_t audio_expected = -1;   // sample pos ถัดไปที่ควรได้ (นาฬิกา pipeline)
	int64_t audio_next_pts = 0;

	bool openVideo(QString *error)
	{
		const char *const names[] = {"h264_nvenc", "h264_amf", "libx264"};
		const int64_t bitrate = int64_t(spec.video_kbps) * 1000;
		QStringList tried;
		for (const char *n : names) {
			const AVCodec *codec = avcodec_find_encoder_by_name(n);
			if (!codec)
				continue;
			AVCodecContext *c = avcodec_alloc_context3(codec);
			if (!c)
				continue;
			const QByteArray name(n);
			c->width = out_w;
			c->height = out_h;
			c->pix_fmt = AV_PIX_FMT_NV12;
			c->time_base = AVRational{1, 1000};
			c->framerate = AVRational{out_fps, 1};
			c->gop_size = out_fps * 2;        // keyframe ทุก 2 วิ คงที่ (ทุก ingest ต้องการ)
			c->keyint_min = c->gop_size;
			c->max_b_frames = name == "h264_amf" ? 0 : 2;
			c->bit_rate = bitrate;
			c->rc_max_rate = bitrate;
			c->rc_min_rate = bitrate;
			c->rc_buffer_size = static_cast<int>(bitrate);   // 1 วิ
			c->color_range = AVCOL_RANGE_MPEG;
			c->colorspace = AVCOL_SPC_BT709;
			c->color_primaries = AVCOL_PRI_BT709;
			c->color_trc = AVCOL_TRC_BT709;
			c->chroma_sample_location = AVCHROMA_LOC_LEFT;
			c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;   // FLV ต้องการ avcC ใน extradata
			if (name == "h264_nvenc") {
				av_opt_set(c->priv_data, "preset", "p5", 0);
				av_opt_set(c->priv_data, "tune", "hq", 0);
				av_opt_set(c->priv_data, "rc", "cbr", 0);
				av_opt_set(c->priv_data, "profile", "high", 0);
				av_opt_set(c->priv_data, "spatial-aq", "1", 0);
				av_opt_set(c->priv_data, "no-scenecut", "1", 0);
				av_opt_set(c->priv_data, "forced-idr", "1", 0);
				av_opt_set(c->priv_data, "b_ref_mode", "disabled", 0);   // ไม่มี B-pyramid (บาง ingest ไม่ชอบ)
			} else if (name == "h264_amf") {
				av_opt_set(c->priv_data, "rc", "cbr", 0);
				av_opt_set(c->priv_data, "quality", "balanced", 0);
				av_opt_set(c->priv_data, "profile", "high", 0);
			} else {
				av_opt_set(c->priv_data, "preset", "veryfast", 0);
				av_opt_set(c->priv_data, "profile", "high", 0);
				av_opt_set(c->priv_data, "x264-params", "nal-hrd=cbr:scenecut=0", 0);
			}
			const int err = avcodec_open2(c, codec, nullptr);
			if (err < 0) {
				tried << QStringLiteral("%1 (%2)").arg(QString::fromUtf8(name), avErr(err));
				qCWarning(pswrapLive) << "encoder" << name << "failed:" << avErr(err);
				avcodec_free_context(&c);
				continue;
			}
			venc = c;
			encoder_name = QString::fromUtf8(name);
			break;
		}
		if (!venc) {
			*error = tried.isEmpty() ? PsWrapGoLive::tr("No usable H.264 encoder found.")
			                         : PsWrapGoLive::tr("Video encoder failed: %1").arg(tried.join(QStringLiteral(", ")));
			return false;
		}
		PsWrapEncodedStream &vs = layout.streams[0];
		vs.par = avcodec_parameters_alloc();
		if (!vs.par || avcodec_parameters_from_context(vs.par, venc) < 0)
			return false;
		vs.time_base = venc->time_base;
		vs.video = true;
		vs.disposition = AV_DISPOSITION_DEFAULT;
		layout.count = 1;
		layout.fps = out_fps;
		return true;
	}

	bool openAudio(QString *error)
	{
		const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!codec) {
			*error = PsWrapGoLive::tr("AAC encoder not available.");
			return false;
		}
		aenc = avcodec_alloc_context3(codec);
		if (!aenc)
			return false;
		aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
		aenc->sample_rate = kAudioRate;
		av_channel_layout_default(&aenc->ch_layout, 2);
		aenc->bit_rate = int64_t(spec.audio_kbps) * 1000;
		aenc->time_base = AVRational{1, kAudioRate};
		aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		const int err = avcodec_open2(aenc, codec, nullptr);
		if (err < 0) {
			*error = PsWrapGoLive::tr("Audio encoder failed: %1").arg(avErr(err));
			return false;
		}
		PsWrapEncodedStream &as = layout.streams[1];
		as.par = avcodec_parameters_alloc();
		if (!as.par || avcodec_parameters_from_context(as.par, aenc) < 0)
			return false;
		as.time_base = aenc->time_base;
		as.video = false;
		as.disposition = AV_DISPOSITION_DEFAULT;
		layout.count = 2;
		fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, kAudioRate / 5);
		return fifo != nullptr;
	}

	void fanout(int index)
	{
		pkt->stream_index = index;
		const int64_t t0 = nowUs();
		for (auto &s : sinks)
			s->write(pkt);
		const int64_t dt = nowUs() - t0;
		if (dt > max_fanout_us.load())
			max_fanout_us = dt;
	}

	bool drain(AVCodecContext *enc, int index)
	{
		for (;;) {
			const int err = avcodec_receive_packet(enc, pkt);
			if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
				return true;
			if (err < 0) {
				qCWarning(pswrapLive) << "encode failed:" << avErr(err);
				failed = true;
				return false;
			}
			fanout(index);
			av_packet_unref(pkt);
		}
	}

	// ย่อ/ขยายให้พอดี canvas 16:9 คงสัดส่วน + ขอบดำ (หน้าต่าง ultrawide/สูง ไม่ถูกบีบ)
	AVFrame *toCanvas(const AVFrame *src)
	{
		AVFrame *dst = av_frame_alloc();
		if (!dst)
			return nullptr;
		dst->format = AV_PIX_FMT_NV12;
		dst->width = out_w;
		dst->height = out_h;
		if (av_frame_get_buffer(dst, 0) < 0) {
			av_frame_free(&dst);
			return nullptr;
		}
		if (src->width == out_w && src->height == out_h) {
			av_frame_copy(dst, src);
			return dst;
		}
		int fit_w = out_w;
		int fit_h = static_cast<int>(std::lround(double(out_w) * src->height / src->width)) & ~1;
		if (fit_h > out_h) {
			fit_h = out_h;
			fit_w = static_cast<int>(std::lround(double(out_h) * src->width / src->height)) & ~1;
		}
		fit_w = std::max(fit_w, 2);
		fit_h = std::max(fit_h, 2);
		const int x0 = ((out_w - fit_w) / 2) & ~1;
		const int y0 = ((out_h - fit_h) / 2) & ~1;
		if (fit_w != out_w || fit_h != out_h) {
			for (int y = 0; y < out_h; y++)
				std::memset(dst->data[0] + y * dst->linesize[0], 16, static_cast<size_t>(out_w));
			for (int y = 0; y < out_h / 2; y++)
				std::memset(dst->data[1] + y * dst->linesize[1], 128, static_cast<size_t>(out_w));
		}
		sws = sws_getCachedContext(sws, src->width, src->height, AV_PIX_FMT_NV12, fit_w, fit_h, AV_PIX_FMT_NV12,
		                           SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (!sws) {
			av_frame_free(&dst);
			return nullptr;
		}
		uint8_t *planes[4] = {dst->data[0] + y0 * dst->linesize[0] + x0, dst->data[1] + (y0 / 2) * dst->linesize[1] + x0, nullptr, nullptr};
		int strides[4] = {dst->linesize[0], dst->linesize[1], 0, 0};
		sws_scale(sws, src->data, src->linesize, 0, src->height, planes, strides);
		return dst;
	}

	void encodeVideo(AVFrame *src)
	{
		const int64_t pts_ms = src->pts;
		if (decimate) {
			// pipeline เร็วกว่า fps ของไลฟ์ (เช่น 120 → 60) → เลือกเฟรมตามนาฬิกา
			const double interval = 1000.0 / out_fps;
			if (next_due_ms >= 0.0 && pts_ms + interval * 0.25 < next_due_ms) {
				decimated++;
				av_frame_free(&src);
				return;
			}
			next_due_ms = next_due_ms < 0.0 || pts_ms - next_due_ms > interval ? pts_ms + interval : next_due_ms + interval;
		}
		AVFrame *f = toCanvas(src);
		av_frame_free(&src);   // คืน slot ของ render thread ทันที
		if (!f)
			return;
		int64_t pts = pts_ms;
		if (pts <= last_video_pts)
			pts = last_video_pts + 1;
		last_video_pts = pts;
		f->pts = pts;
		const int err = avcodec_send_frame(venc, f);
		av_frame_free(&f);
		if (err < 0) {
			qCWarning(pswrapLive) << "video encode failed:" << avErr(err);
			failed = true;
			return;
		}
		frames_encoded++;
		drain(venc, 0);
	}

	void encodeAudioFifo()
	{
		const int fs = aenc->frame_size > 0 ? aenc->frame_size : 1024;
		while (av_audio_fifo_size(fifo) >= fs) {
			AVFrame *f = av_frame_alloc();
			if (!f)
				return;
			f->nb_samples = fs;
			f->format = AV_SAMPLE_FMT_FLTP;
			f->sample_rate = kAudioRate;
			av_channel_layout_copy(&f->ch_layout, &aenc->ch_layout);
			if (av_frame_get_buffer(f, 0) < 0) {
				av_frame_free(&f);
				return;
			}
			av_audio_fifo_read(fifo, reinterpret_cast<void **>(f->data), fs);
			f->pts = audio_next_pts;
			audio_next_pts += fs;
			const int err = avcodec_send_frame(aenc, f);
			av_frame_free(&f);
			if (err < 0) {
				qCWarning(pswrapLive) << "audio encode failed:" << avErr(err);
				failed = true;
				return;
			}
			if (!drain(aenc, 1))
				return;
		}
	}

	void encodeAudio(Item &it)
	{
		const float *l = it.audio.data();
		const float *r = l + it.n;
		int64_t n = it.n;
		if (audio_expected < 0) {
			audio_expected = it.pos;
			audio_next_pts = it.pos;   // pts = นาฬิกาเดียวกับภาพ (sample นับจาก pipeline เริ่ม)
		}
		if (it.pos > audio_expected) {
			// chunk หาย (คิวเต็ม) → เติมเงียบให้เวลาตรง · หายนานเกิน 2 วิ = เริ่มนับใหม่
			const int64_t gap = it.pos - audio_expected;
			if (gap > kAudioRate * 2) {
				av_audio_fifo_reset(fifo);
				audio_next_pts = it.pos;
			} else {
				std::vector<float> zeros(static_cast<size_t>(gap), 0.0f);
				void *z[2] = {zeros.data(), zeros.data()};
				av_audio_fifo_write(fifo, z, static_cast<int>(gap));
			}
			audio_expected = it.pos;
		} else if (it.pos < audio_expected) {
			const int64_t skip = std::min(n, audio_expected - it.pos);
			l += skip;
			r += skip;
			n -= skip;
		}
		if (n > 0) {
			void *data[2] = {const_cast<float *>(l), const_cast<float *>(r)};
			av_audio_fifo_write(fifo, data, static_cast<int>(n));
			audio_expected += n;
		}
		encodeAudioFifo();
	}

	void run()
	{
		for (;;) {
			std::deque<Item> local;
			bool stopping = false;
			{
				std::unique_lock<std::mutex> lock(m);
				cv.wait_for(lock, std::chrono::milliseconds(20), [this] { return quit || queued_video > 0; });
				local.swap(q);
				queued_video = 0;
				stopping = quit;
			}
			for (auto &it : local) {
				if (stopping || failed) {
					av_frame_free(&it.frame);
					continue;
				}
				if (it.frame)
					encodeVideo(it.frame);
				else
					encodeAudio(it);
				it.frame = nullptr;
			}
			if (stopping)
				break;
		}
		qCInfo(pswrapLive).nospace().noquote() << "rendition " << description() << " finished: frames=" << frames_encoded.load()
			<< " tap_video_dropped=" << tap_video_dropped.load() << " tap_audio_dropped=" << tap_audio_dropped.load()
			<< " decimated=" << decimated.load() << " max_fanout_us=" << max_fanout_us.load();
	}
};

} // namespace PsWrapLiveInternal

using PsWrapLiveInternal::Rendition;
using PsWrapLiveInternal::Sink;

// ================================================================ PsWrapLiveDestinations

int PsWrapLiveDestinations::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : count();
}

QVariant PsWrapLiveDestinations::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= count())
		return {};
	const Row &r = rows[static_cast<size_t>(index.row())];
	switch (role) {
	case DestIdRole: return r.id;
	case PlatformRole: return r.platform;
	case EnabledRole: return r.enabled;
	case ServerRole: return r.server;
	case KeySavedRole: return r.key_saved;
	case VideoBitrateRole: return r.video_kbps;
	case AudioBitrateRole: return r.audio_kbps;
	case CodecRole: return r.codec;
	case VerticalRole: return r.vertical;
	case StatusRole: return QString::fromLatin1(stateName(r.status));
	case StatusTextRole: return r.status_text;
	}
	return {};
}

QHash<int, QByteArray> PsWrapLiveDestinations::roleNames() const
{
	return {
		{DestIdRole, "destId"}, {PlatformRole, "platform"}, {EnabledRole, "destEnabled"}, {ServerRole, "server"},
		{KeySavedRole, "keySaved"}, {VideoBitrateRole, "videoBitrate"}, {AudioBitrateRole, "audioBitrate"},
		{CodecRole, "codec"}, {VerticalRole, "vertical"}, {StatusRole, "status"}, {StatusTextRole, "statusText"},
	};
}

int PsWrapLiveDestinations::indexOf(const QString &id) const
{
	for (size_t i = 0; i < rows.size(); i++)
		if (rows[i].id == id)
			return static_cast<int>(i);
	return -1;
}

const PsWrapLiveDestinations::Row *PsWrapLiveDestinations::find(const QString &id) const
{
	const int i = indexOf(id);
	return i < 0 ? nullptr : &rows[static_cast<size_t>(i)];
}

void PsWrapLiveDestinations::reset(std::vector<Row> new_rows)
{
	const int before = count();
	beginResetModel();
	rows = std::move(new_rows);
	endResetModel();
	if (before != count())
		emit countChanged();
}

void PsWrapLiveDestinations::append(const Row &row)
{
	beginInsertRows(QModelIndex(), count(), count());
	rows.push_back(row);
	endInsertRows();
	emit countChanged();
}

void PsWrapLiveDestinations::remove(const QString &id)
{
	const int i = indexOf(id);
	if (i < 0)
		return;
	beginRemoveRows(QModelIndex(), i, i);
	rows.erase(rows.begin() + i);
	endRemoveRows();
	emit countChanged();
}

bool PsWrapLiveDestinations::update(const QString &id, const std::function<void(Row &)> &fn)
{
	const int i = indexOf(id);
	if (i < 0)
		return false;
	fn(rows[static_cast<size_t>(i)]);
	const QModelIndex mi = index(i);
	emit dataChanged(mi, mi);
	return true;
}

// ================================================================ PsWrapGoLive

struct PsWrapGoLive::Active
{
	struct Dest
	{
		QString id;
		QString platform;
		std::shared_ptr<Sink> sink;
		PsWrapLiveState last = PsWrapLiveState::Connecting;
		QString last_text;
		bool error_reported = false;
	};
	std::vector<std::shared_ptr<Rendition>> renditions;
	std::vector<Dest> dests;
	bool stopping = false;
	bool announced = false;
	qint64 last_log_ms = 0;
};

PsWrapGoLive::PsWrapGoLive(QObject *parent, PsWrapRecorder *recorder, std::function<Settings *()> settings_getter,
                           std::function<bool(PsWrapRecConfig *, QString *)> config_getter)
	: QObject(parent), recorder(recorder), settings_getter(std::move(settings_getter)), config_getter(std::move(config_getter)),
	  model(this)
{
	poll_timer.setInterval(500);
	connect(&poll_timer, &QTimer::timeout, this, &PsWrapGoLive::poll);
	load();
}

PsWrapGoLive::~PsWrapGoLive()
{
	// ปกติ teardown เรียก stop() ก่อนแล้ว · ตรงนี้ recorder อาจตายไปแล้ว → ไม่แตะ recorder หยุดแค่ thread ของเราเอง
	if (run) {
		run->stopping = true;
		for (auto &r : run->renditions)
			r->stopThread();
		for (auto &d : run->dests)
			d.sink->requestStop();
		for (auto &d : run->dests)
			d.sink->join();
	}
}

QVariantList PsWrapGoLive::platforms() const
{
	QVariantList out;
	for (const auto &d : platformDefs()) {
		QVariantMap m;
		m[QStringLiteral("id")] = QString::fromLatin1(d.id);
		m[QStringLiteral("name")] = tr(d.name);
		m[QStringLiteral("codecs")] = QStringList{QStringLiteral("h264")};
		m[QStringLiteral("maxVideoKbps")] = d.max_kbps;
		m[QStringLiteral("maxHeight")] = d.max_height;
		m[QStringLiteral("defaultVideoKbps")] = d.default_kbps;
		m[QStringLiteral("vertical")] = d.vertical;
		m[QStringLiteral("keyPerSession")] = d.key_per_session;
		QVariantList servers;
		for (const auto &s : d.servers)
			servers.append(QVariantMap{{QStringLiteral("label"), tr(s.label)}, {QStringLiteral("url"), QString::fromLatin1(s.url)}});
		m[QStringLiteral("servers")] = servers;
		m[QStringLiteral("note")] = tr(d.note);
		out.append(m);
	}
	return out;
}

int PsWrapGoLive::enabledCount() const
{
	int n = 0;
	for (const auto &r : model.all())
		n += r.enabled ? 1 : 0;
	return n;
}

bool PsWrapGoLive::secureStoreAvailable() const
{
	return PsWrapSecrets::isAvailable();
}

int PsWrapGoLive::totalKbps() const
{
	int t = 0;
	for (const auto &r : model.all())
		if (r.enabled)
			t += r.video_kbps + r.audio_kbps;
	return t;
}

bool PsWrapGoLive::twitchParityWarning() const
{
	int twitch = -1;
	for (const auto &r : model.all())
		if (r.enabled && r.platform == QLatin1String("twitch"))
			twitch = std::max(twitch, r.video_kbps);
	if (twitch < 0)
		return false;
	for (const auto &r : model.all())
		if (r.enabled && r.platform != QLatin1String("twitch") && !r.vertical && r.video_kbps > twitch)
			return true;
	return false;
}

QString PsWrapGoLive::profileName() const
{
	Settings *s = settings_getter ? settings_getter() : nullptr;
	return s ? s->GetCurrentProfile() : QString();
}

QString PsWrapGoLive::target(const QString &dest_id) const
{
	// --profile pswrap-test → "PS-WRAP/live/pswrap-test/<id>" — ทดสอบไม่ทับ key จริง
	return PsWrapSecrets::liveTarget(dest_id, profileName());
}

void PsWrapGoLive::setLastError(const QString &e)
{
	if (e == last_error)
		return;
	last_error = e;
	emit lastErrorChanged();
}

void PsWrapGoLive::load()
{
	Settings *s = settings_getter ? settings_getter() : nullptr;
	loaded_from = s;
	std::vector<PsWrapLiveDestinations::Row> rows;
	if (s) {
		const QJsonArray arr = QJsonDocument::fromJson(s->GetPsWrapLiveDestinations().toUtf8()).array();
		for (const QJsonValue &v : arr) {
			const QJsonObject o = v.toObject();
			PsWrapLiveDestinations::Row r;
			r.id = o.value(QStringLiteral("id")).toString();
			r.platform = o.value(QStringLiteral("platform")).toString();
			const PlatformDef *d = platformDef(r.platform);
			if (r.id.isEmpty() || !d)
				continue;
			r.enabled = o.value(QStringLiteral("enabled")).toBool(true);
			r.server = o.value(QStringLiteral("server")).toString();
			r.video_kbps = std::clamp(o.value(QStringLiteral("videoKbps")).toInt(d->default_kbps), 1000, d->max_kbps);
			r.audio_kbps = std::clamp(o.value(QStringLiteral("audioKbps")).toInt(160), 64, 320);
			r.codec = QStringLiteral("h264");
			r.vertical = o.value(QStringLiteral("vertical")).toBool(d->vertical);
			r.key_saved = PsWrapSecrets::exists(target(r.id));
			rows.push_back(r);
		}
	}
	model.reset(std::move(rows));
	emit destinationsChanged();
}

void PsWrapGoLive::store()
{
	Settings *s = settings_getter ? settings_getter() : nullptr;
	if (!s)
		return;
	QJsonArray arr;
	for (const auto &r : model.all()) {
		QJsonObject o;
		o[QStringLiteral("id")] = r.id;
		o[QStringLiteral("platform")] = r.platform;
		o[QStringLiteral("enabled")] = r.enabled;
		o[QStringLiteral("server")] = r.server;
		o[QStringLiteral("videoKbps")] = r.video_kbps;
		o[QStringLiteral("audioKbps")] = r.audio_kbps;
		o[QStringLiteral("codec")] = r.codec;
		o[QStringLiteral("vertical")] = r.vertical;
		arr.append(o);
	}
	s->SetPsWrapLiveDestinations(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void PsWrapGoLive::reloadIfSettingsChanged()
{
	if (!active && settings_getter && settings_getter() != loaded_from)
		load();
}

QString PsWrapGoLive::addDestination(const QString &platform_id)
{
	const PlatformDef *d = platformDef(platform_id);
	if (!d || active)
		return QString();
	QString id;
	do {
		id = QStringLiteral("%1-%2").arg(platform_id).arg(QRandomGenerator::global()->bounded(0x10000), 4, 16, QLatin1Char('0'));
	} while (model.indexOf(id) >= 0);
	PsWrapLiveDestinations::Row r;
	r.id = id;
	r.platform = platform_id;
	r.server = d->servers.empty() ? QString() : QString::fromLatin1(d->servers.front().url);
	r.video_kbps = d->default_kbps;
	r.vertical = d->vertical;
	r.key_saved = false;
	PsWrapSecrets::remove(target(id));   // กันของค้างจาก id ซ้ำในอดีต
	model.append(r);
	store();
	emit destinationsChanged();
	return id;
}

void PsWrapGoLive::removeDestination(const QString &dest_id)
{
	if (active || model.indexOf(dest_id) < 0)
		return;
	QString err;
	if (PsWrapSecrets::remove(target(dest_id), &err) != PsWrapSecrets::Result::Ok)
		setLastError(tr("Couldn't remove the stream key: %1").arg(err));
	model.remove(dest_id);
	store();
	emit destinationsChanged();
}

void PsWrapGoLive::setDestinationValue(const QString &dest_id, const QString &key, const QVariant &value)
{
	const PsWrapLiveDestinations::Row *row = model.find(dest_id);
	if (active || !row)
		return;
	const PlatformDef *d = platformDef(row->platform);
	const int max_kbps = d ? d->max_kbps : 6000;
	const bool ok = model.update(dest_id, [&](PsWrapLiveDestinations::Row &r) {
		if (key == QLatin1String("enabled"))
			r.enabled = value.toBool();
		else if (key == QLatin1String("server"))
			r.server = value.toString().trimmed();
		else if (key == QLatin1String("videoBitrate"))
			r.video_kbps = std::clamp(value.toInt() / 100 * 100, 1000, max_kbps);
		else if (key == QLatin1String("audioBitrate"))
			r.audio_kbps = std::clamp(value.toInt(), 64, 320);
		else if (key == QLatin1String("codec"))
			r.codec = QStringLiteral("h264");   // จุดต่อขยาย: hevc/av1 เมื่อมี rendition ให้
		else if (key == QLatin1String("vertical"))
			r.vertical = value.toBool();        // ภาพแนวตั้ง 9:16 จาก pipeline แนวตั้ง
		if (r.status == PsWrapLiveState::Error) {
			r.status = PsWrapLiveState::Idle;   // แก้ค่าแล้ว = ข้อความ error เก่าไม่เกี่ยวแล้ว
			r.status_text.clear();
		}
	});
	if (!ok)
		return;
	store();
	emit destinationsChanged();
}

bool PsWrapGoLive::setStreamKey(const QString &dest_id, const QString &key)
{
	if (active || model.indexOf(dest_id) < 0)
		return false;
	QString err;
	if (PsWrapSecrets::write(target(dest_id), key.trimmed(), &err) != PsWrapSecrets::Result::Ok) {
		setLastError(tr("Couldn't save the stream key: %1").arg(err));
		return false;
	}
	setLastError(QString());
	model.update(dest_id, [](PsWrapLiveDestinations::Row &r) {
		r.key_saved = true;
		if (r.status == PsWrapLiveState::Error) {
			r.status = PsWrapLiveState::Idle;
			r.status_text.clear();
		}
	});
	return true;
}

void PsWrapGoLive::clearStreamKey(const QString &dest_id)
{
	if (active || model.indexOf(dest_id) < 0)
		return;
	QString err;
	if (PsWrapSecrets::remove(target(dest_id), &err) != PsWrapSecrets::Result::Ok) {
		setLastError(tr("Couldn't remove the stream key: %1").arg(err));
		return;
	}
	model.update(dest_id, [](PsWrapLiveDestinations::Row &r) { r.key_saved = false; });
}

QString PsWrapGoLive::revealStreamKey(const QString &dest_id)
{
	// จออาจถูกจับภาพออกไลฟ์อยู่ → ห้ามเผยตอนไลฟ์ / streamer mode
	Settings *s = settings_getter ? settings_getter() : nullptr;
	if (active || (s && s->GetStreamerMode()) || model.indexOf(dest_id) < 0)
		return QString();
	QString key;
	if (PsWrapSecrets::read(target(dest_id), &key) != PsWrapSecrets::Result::Ok)
		return QString();
	return key;
}

void PsWrapGoLive::toggle()
{
	if (active)
		stop();
	else
		start();
}

bool PsWrapGoLive::start()
{
	if (active)
		return true;
	PsWrapRecorder *rec = recorder ? recorder : PsWrapRecorder::instance();
	auto fail = [this](const QString &msg) {
		qCWarning(pswrapLive) << "go live: start failed:" << msg;
		setLastError(msg);
		emit toast(QStringLiteral("error"), tr("Couldn't go live"), msg);
		return false;
	};
	if (!rec)
		return fail(tr("The recorder is not available."));

	// ปลายทางที่พร้อม (เปิดอยู่ + มี server + มี key)
	struct Pick
	{
		QString id;
		QString platform;
		QString server;
		QString key;
		int video_kbps;
		int audio_kbps;
		int max_height;
		bool vertical;
	};
	std::vector<Pick> picks;
	for (const auto &r : model.all()) {
		if (!r.enabled)
			continue;
		QString problem;
		QString key;
		const QString scheme = QUrl(r.server.trimmed()).scheme().toLower();
		if (r.server.trimmed().isEmpty())
			problem = tr("Add the server URL first.");
		else if (scheme != QLatin1String("rtmp") && scheme != QLatin1String("rtmps"))
			problem = tr("The server must start with rtmp:// or rtmps://.");
		else if (PsWrapSecrets::read(target(r.id), &key) != PsWrapSecrets::Result::Ok || key.trimmed().isEmpty())
			problem = tr("No stream key saved.");
		if (!problem.isEmpty()) {
			model.update(r.id, [&](PsWrapLiveDestinations::Row &x) {
				x.status = PsWrapLiveState::Error;
				x.status_text = problem;
			});
			continue;
		}
		const PlatformDef *pd = platformDef(r.platform);
		picks.push_back(Pick{r.id, r.platform, r.server.trimmed(), key, r.video_kbps, r.audio_kbps, pd ? pd->max_height : 1080, r.vertical});
	}
	if (picks.empty())
		return fail(enabledCount() == 0 ? tr("Add a destination in Settings › Go Live first.")
		                                : tr("No destination is ready — check the server and stream key in Settings › Go Live."));

	PsWrapRecConfig cfg;
	QString err;
	if (!config_getter || !config_getter(&cfg, &err))
		return fail(err.isEmpty() ? tr("Start a stream first.") : err);
	int w = cfg.width, h = cfg.height, fps = cfg.fps;
	bool hdr = cfg.hdr;
	if (rec->videoSpec(&w, &h, &hdr))   // pipeline เดินอยู่แล้ว (อัด/replay) → ใช้สเปคของมัน
		fps = rec->pipelineFps();
	bool any_landscape = false, any_vertical = false;
	for (const auto &p : picks)
		(p.vertical ? any_vertical : any_landscape) = true;
	if (hdr && any_landscape)
		return fail(tr("Go Live needs an SDR stream for now, and this stream is HDR. Turn off HDR on the PS5 (Settings › Screen and Video › Video Output › HDR) and try again."));

	// PS-WRAP: ปลายทางแนวตั้ง → pipeline ตัวที่ 2 (1080x1920 SDR เสมอ — tone-map จาก HDR ได้) เลย์เอาต์ตามหน้าต่าง 9:16
	PsWrapRecorder *vrec = PsWrapRecorder::secondary();
	PsWrapRecConfig vcfg = cfg;
	vcfg.width = 1080;
	vcfg.height = 1920;
	vcfg.hdr = false;
	vcfg.hdr_info = PsWrapRecHdrInfo();
	int vw = 1080, vh = 1920, vfps = cfg.fps;
	if (any_vertical) {
		if (!vrec)
			return fail(tr("Vertical live isn't available."));
		bool vhdr = false;
		if (vrec->videoSpec(&vw, &vh, &vhdr))   // กำลังอัดคลิปแนวตั้งอยู่ → ใช้สเปคเดิม
			vfps = vrec->pipelineFps();
	}

	// ปลายทางที่ bitrate/เสียง/เพดานความสูง/แนวตั้ง เท่ากัน = rendition เดียวกัน (encode ครั้งเดียว)
	std::map<std::tuple<int, int, int, bool>, std::vector<const Pick *>> groups;
	for (const auto &p : picks)
		groups[{p.video_kbps, p.audio_kbps, p.max_height, p.vertical}].push_back(&p);
	if (static_cast<int>(groups.size()) > kMaxRenditions)
		return fail(tr("Use at most %1 different bitrate and resolution combinations at once (each one is a separate encode).").arg(kMaxRenditions));

	auto a = std::make_unique<Active>();
	for (const auto &g : groups) {
		Rendition::Spec spec;
		spec.video_kbps = std::get<0>(g.first);
		spec.audio_kbps = std::get<1>(g.first);
		spec.max_height = std::get<2>(g.first);
		spec.vertical = std::get<3>(g.first);
		auto r = std::make_shared<Rendition>(spec);
		r->src_rec = spec.vertical ? vrec : rec;
		if (!(spec.vertical ? r->open(vw, vh, vfps, &err) : r->open(w, h, fps, &err)))
			return fail(err);
		for (const Pick *p : g.second) {
			Active::Dest d;
			d.id = p->id;
			d.platform = p->platform;
			d.sink = std::make_shared<Sink>(p->id, platformName(p->platform), redactServer(p->server), buildUrl(p->server, p->key),
			                                 r->encodedLayout());
			r->addSink(d.sink);
			a->dests.push_back(std::move(d));
		}
		a->renditions.push_back(std::move(r));
	}
	for (auto &p : picks)
		p.key.fill(QChar(u'\0'));   // ไม่ถือ key ค้าง (URL อยู่ใน sink ตัวเดียว)

	// ต่อ server ก่อน (ใช้เวลา) แล้วค่อยเริ่ม encode — ระหว่างต่อ packet รอในคิว (≤ 4 วิ)
	for (auto &d : a->dests)
		d.sink->start();
	for (auto &r : a->renditions)
		r->startThread();
	QPointer<PsWrapGoLive> self(this);
	for (size_t i = 0; i < a->renditions.size(); i++) {
		auto &r = a->renditions[i];
		r->on_detached = [self](Rendition *x) {
			// recorder เรียกบน GUI thread ระหว่าง removeTap/shutdownPipeline → เลื่อนไปทำนอก call stack นั้น
			QMetaObject::invokeMethod(self, [self, x]() { if (self) self->tapDetached(x); }, Qt::QueuedConnection);
		};
		if (!r->src_rec->addTap(r->src_rec == rec ? cfg : vcfg, r, &err)) {
			for (size_t k = 0; k < i; k++)
				a->renditions[k]->src_rec->removeTap(a->renditions[k].get());
			a->stopping = true;
			for (auto &x : a->renditions)
				x->stopThread();
			for (auto &d : a->dests)
				d.sink->requestStop();
			for (auto &d : a->dests)
				d.sink->join();
			return fail(err);
		}
	}

	QStringList names;
	for (auto &d : a->dests) {
		names << QStringLiteral("%1 → %2").arg(d.sink->name, d.sink->server_label);
		model.update(d.id, [](PsWrapLiveDestinations::Row &x) {
			x.status = PsWrapLiveState::Connecting;
			x.status_text = tr("Connecting…");
		});
	}
	QStringList rends;
	for (auto &r : a->renditions)
		rends << r->description();
	qCInfo(pswrapLive).noquote() << "go live: start" << names.join(QStringLiteral(", ")) << "| renditions:" << rends.join(QStringLiteral(", "))
		<< "| profile" << (profileName().isEmpty() ? QStringLiteral("(default)") : profileName());

	run = std::move(a);
	active = true;
	start_ms = QDateTime::currentMSecsSinceEpoch();
	seconds_value = 0;
	setLastError(QString());
	state_value = QStringLiteral("connecting");
	summary_value = tr("Connecting…");
	poll_timer.start();
	emit liveChanged();
	emit stateChanged();
	emit secondsChanged();
	return true;
}

void PsWrapGoLive::stop()
{
	if (!active)
		return;
	finishStop(false, QString());
}

void PsWrapGoLive::tapDetached(Rendition *r)
{
	if (!run || run->stopping)
		return;
	for (auto &x : run->renditions)
		if (x.get() == r) {
			// pipeline ปิดเอง (สตรีมจบ / encoder พัง) ทั้งที่ยังไลฟ์อยู่
			finishStop(true, tr("The capture stopped (the stream ended or the encoder failed)."));
			return;
		}
}

void PsWrapGoLive::finishStop(bool notify_failure, const QString &reason)
{
	std::unique_ptr<Active> a = std::move(run);
	active = false;
	poll_timer.stop();
	if (a) {
		a->stopping = true;
		for (auto &r : a->renditions)
			if (r->src_rec)
				r->src_rec->removeTap(r.get());   // หลังจากนี้ worker ของ recorder ไม่เรียก tap อีก (pipeline ปิดเองถ้าไม่มีใครใช้)
		for (auto &r : a->renditions)
			r->stopThread();
		for (auto &d : a->dests)
			d.sink->requestStop();
		for (auto &d : a->dests)
			d.sink->join();   // ≤ 0.5 วิ (deadline ตอนปิด)
		const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - start_ms) / 1000;
		for (auto &d : a->dests) {
			qCInfo(pswrapLive).nospace().noquote() << "go live: " << d.sink->name << " finished after " << secs << " s: sent "
				<< double(d.sink->bytes_sent.load()) / (1024.0 * 1024.0) << " MB, dropped " << d.sink->dropped_gops.load() << " GOPs ("
				<< d.sink->dropped_packets.load() << " packets), reconnects " << d.sink->reconnects.load();
			const auto snap = d.sink->snapshot();
			model.update(d.id, [&](PsWrapLiveDestinations::Row &x) {
				// error ค้างไว้ให้อ่าน · ที่เหลือกลับเป็น idle
				x.status = snap.fatal ? PsWrapLiveState::Error : PsWrapLiveState::Idle;
				x.status_text = snap.fatal ? snap.text : QString();
			});
		}
	}
	state_value = QStringLiteral("off");
	summary_value.clear();
	emit liveChanged();
	emit stateChanged();
	if (notify_failure) {
		setLastError(reason);
		emit toast(QStringLiteral("error"), tr("Go Live stopped"), reason);
	}
}

void PsWrapGoLive::poll()
{
	if (!run)
		return;
	const int secs = static_cast<int>((QDateTime::currentMSecsSinceEpoch() - start_ms) / 1000);
	if (secs != seconds_value) {
		seconds_value = secs;
		emit secondsChanged();
	}
	int n_live = 0, n_error = 0, n_reconnecting = 0;
	QStringList went_live;
	QString first_error;
	const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
	const bool log_now = now_ms - run->last_log_ms >= 30000;
	if (log_now)
		run->last_log_ms = now_ms;
	for (auto &d : run->dests) {
		const auto s = d.sink->snapshot();
		if (log_now)
			qCInfo(pswrapLive).nospace().noquote() << d.sink->name << ": " << stateName(s.state) << " · " << s.text;
		if (s.state != d.last || s.text != d.last_text) {
			const PsWrapLiveState prev = d.last;
			d.last = s.state;
			d.last_text = s.text;
			model.update(d.id, [&](PsWrapLiveDestinations::Row &x) {
				x.status = s.state;
				x.status_text = s.text;
			});
			if (s.state == PsWrapLiveState::Live && prev != PsWrapLiveState::Live)
				went_live << d.sink->name;
			if (s.state == PsWrapLiveState::Reconnecting && prev == PsWrapLiveState::Live)
				emit toast(QStringLiteral("info"), tr("Reconnecting to %1…").arg(d.sink->name), QString());
		}
		if (s.state == PsWrapLiveState::Error) {
			n_error++;
			if (!d.error_reported) {
				d.error_reported = true;
				emit toast(QStringLiteral("error"), tr("%1: can't go live").arg(d.sink->name), s.text);
			}
			if (first_error.isEmpty())
				first_error = QStringLiteral("%1: %2").arg(d.sink->name, s.text);
		} else if (s.state == PsWrapLiveState::Live) {
			n_live++;
		} else if (s.state == PsWrapLiveState::Reconnecting) {
			n_reconnecting++;
		}
	}
	if (!went_live.isEmpty() && !run->announced) {
		run->announced = true;
		emit toast(QStringLiteral("success"), tr("You're live"), went_live.join(QStringLiteral(", ")));
	}
	const int total = static_cast<int>(run->dests.size());
	if (n_error == total) {
		// ทุกปลายทางเลิกลองแล้ว — ไม่ต้อง encode ทิ้งต่อ
		finishStop(false, QString());
		setLastError(first_error);
		return;
	}
	QString st;
	if (n_live == total - n_error)
		st = QStringLiteral("live");
	else if (n_reconnecting > 0)
		st = QStringLiteral("reconnecting");
	else
		st = QStringLiteral("connecting");
	QString sum = n_live > 0 ? tr("Live on %1 of %2").arg(n_live).arg(total)
	                         : (st == QLatin1String("reconnecting") ? tr("Reconnecting…") : tr("Connecting…"));
	if (st != state_value || sum != summary_value) {
		state_value = st;
		summary_value = sum;
		emit stateChanged();
	}
}
