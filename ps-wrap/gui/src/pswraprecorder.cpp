// PS-WRAP: อัดวิดีโอ + Instant Replay (ดู pswraprecorder.h)
#include <pswraprecorder.h>

#include <chiaki/time.h>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

Q_LOGGING_CATEGORY(pswrapRec, "pswrap.rec", QtInfoMsg)

namespace {

constexpr int kAudioRate = 48000;
constexpr int kAudioTracks = 3;            // 0 = Game + Mic, 1 = Game, 2 = Mic
constexpr size_t kMaxQueuedVideo = 6;      // เกินนี้ = encoder ตามไม่ทัน → ทิ้งเฟรม (ไม่ให้ RAM บวม)
constexpr size_t kMaxQueuedAudio = 500;    // ~5 วินาทีของ chunk 10ms
constexpr int64_t kGapFillSamples = 2880;  // เสียงเกมขาดช่วง > 60ms → เติมเงียบให้ตรงนาฬิกา
constexpr int64_t kGapSlackSamples = 960;
constexpr int64_t kLateDropSamples = 9600; // เสียงมาช้ากว่านาฬิกา > 200ms (มาเป็นก้อนหลังสะดุด) → ทิ้ง
constexpr qint64 kGameIdleUs = 250000;     // ไม่มีเสียงเกม 250ms → เดินนาฬิกาเสียงเองด้วยความเงียบ + ไมค์
constexpr int64_t kIdleLagSamples = 4800;
constexpr size_t kMicFifoMaxFrames = 9600; // ไมค์ค้างใน FIFO เกิน 200ms (นาฬิกาไมค์เร็วกว่า) → ตัดเหลือ 40ms
constexpr size_t kMicFifoKeepFrames = 1920;

// Instant Replay: เพดานแรม (4K HDR 80 Mbps × 122 วิ ≈ 1.2 GB) — เกินนี้ทิ้ง GOP เก่าสุดก่อนถึงเวลา
constexpr int64_t kReplayMaxBytes = 1536LL * 1024 * 1024;
constexpr int64_t kReplayAudioLeadUs = 150000; // เก็บเสียงก่อน keyframe แรกไว้นิดหน่อย (วิดีโอ encode ช้ากว่าเสียง)
constexpr int64_t kReplayLogEveryUs = 30000000;
constexpr size_t kRebaserMaxPending = 400;     // เสียงที่รอ keyframe แรก (~2.8 วิ × 3 track)
constexpr int64_t kMarkerMinGapMs = 1000;      // marker ห่างกันน้อยกว่านี้ = ซ้ำ (กดรัว)

const char *const kTrackTitles[kAudioTracks] = {"Game + Mic", "Game", "Mic"};

QAtomicPointer<PsWrapRecorder> g_instance;

QString avErr(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(err, buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

AVRational q(double v, int den)
{
	return av_make_q(static_cast<int>(std::lround(v * den)), den);
}

int64_t toUs(int64_t ts, AVRational tb)
{
	return av_rescale_q(ts, tb, AVRational{1, 1000000});
}

int64_t nowUs()
{
	return static_cast<int64_t>(chiaki_time_now_monotonic_us());
}

struct AudioChunk
{
	bool mic = false;
	qint64 arrival_us = 0;
	std::vector<float> stereo; // interleaved L R
};

struct VideoItem
{
	AVFrame *frame = nullptr;
	qint64 capture_us = 0;
};

struct AudioTrack
{
	AVCodecContext *enc = nullptr;
	AVAudioFifo *fifo = nullptr;
	int index = 0;   // stream index ใน layout
	int64_t next_pts = 0;
};

struct Chapter
{
	int64_t start_ms = 0;
	int64_t end_ms = 0;
	QByteArray title;
};

// marker (ms นับจากต้นไฟล์) → chapter ต่อเนื่องคลุมทั้งไฟล์ — chapter track ของ MP4 เริ่มที่ 0 เสมอ
// (ทดสอบแล้ว: ถ้า chapter แรกเริ่มที่ 1.5 วิ ffprobe/VLC อ่านได้เป็น 0) → ใส่ "Start" นำหน้าเมื่อ marker แรกไม่ได้อยู่ต้นไฟล์
std::vector<Chapter> makeChapters(std::vector<int64_t> marks_ms, int64_t duration_ms)
{
	std::vector<Chapter> out;
	std::sort(marks_ms.begin(), marks_ms.end());
	std::vector<int64_t> m;
	for (int64_t t : marks_ms) {
		t = std::max<int64_t>(t, 0);
		if (t >= duration_ms)
			continue;
		if (!m.empty() && t - m.back() < kMarkerMinGapMs)
			continue;
		m.push_back(t);
	}
	if (m.empty() || duration_ms <= 0)
		return out;
	if (m.front() < 500)
		m.front() = 0;
	else
		out.push_back(Chapter{0, m.front(), QByteArrayLiteral("Start")});
	for (size_t i = 0; i < m.size(); i++)
		out.push_back(Chapter{m[i], i + 1 < m.size() ? m[i + 1] : duration_ms, QStringLiteral("Marker %1").arg(i + 1).toUtf8()});
	return out;
}

// stream สำหรับเขียนไฟล์ (par = shared เพื่อให้ job ถือสำเนาของตัวเองได้หลัง pipeline ปิด)
struct StreamDesc
{
	std::shared_ptr<AVCodecParameters> par;
	AVRational time_base = {1, 1000};
	QByteArray title;
	int disposition = 0;
	bool video = false;
	int fps = 0;
};

std::shared_ptr<AVCodecParameters> copyPar(const AVCodecParameters *src)
{
	AVCodecParameters *p = avcodec_parameters_alloc();
	if (!p)
		return nullptr;
	if (avcodec_parameters_copy(p, src) < 0) {
		avcodec_parameters_free(&p);
		return nullptr;
	}
	return std::shared_ptr<AVCodecParameters>(p, [](AVCodecParameters *x) { avcodec_parameters_free(&x); });
}

// borrow = ไม่ copy (ใช้ระหว่างที่ pipeline ยังอยู่ — เช่นเปิดไฟล์อัดบน GUI thread)
std::vector<StreamDesc> describe(const PsWrapEncodedLayout &layout, bool deep_copy)
{
	std::vector<StreamDesc> out;
	for (int i = 0; i < layout.count; i++) {
		const PsWrapEncodedStream &s = layout.streams[i];
		StreamDesc d;
		d.par = deep_copy ? copyPar(s.par) : std::shared_ptr<AVCodecParameters>(s.par, [](AVCodecParameters *) {});
		if (!d.par)
			return {};
		d.time_base = s.time_base;
		d.title = s.title ? QByteArray(s.title) : QByteArray();
		d.disposition = s.disposition;
		d.video = s.video;
		d.fps = layout.fps;
		out.push_back(std::move(d));
	}
	return out;
}

// MP4 writer เดียวใช้ทั้ง 3 งาน: ไฟล์อัด (fragmented), ไฟล์ replay และ remux ใส่ chapter (MP4 ปกติ)
class Mp4Writer
{
public:
	~Mp4Writer() { close(); }

	bool open(const QString &path, const std::vector<StreamDesc> &streams, bool fragmented,
	          const std::vector<Chapter> &chapters, QString *error, bool *file_error)
	{
		this->path = path;
		if (file_error)
			*file_error = false;
		const QByteArray p = path.toUtf8();
		int err = avformat_alloc_output_context2(&fmt, nullptr, "mp4", p.constData());
		if (err < 0 || !fmt) {
			*error = QObject::tr("Could not create the output file: %1").arg(avErr(err));
			return false;
		}
		for (const StreamDesc &d : streams) {
			AVStream *st = avformat_new_stream(fmt, nullptr);
			if (!st || avcodec_parameters_copy(st->codecpar, d.par.get()) < 0) {
				*error = QObject::tr("Could not create the output streams.");
				return false;
			}
			st->time_base = d.time_base;
			if (d.video && d.fps > 0)
				st->avg_frame_rate = AVRational{d.fps, 1};
			st->disposition = d.disposition;
			if (!d.title.isEmpty()) {
				// title → udta "name" (VLC อ่านเป็นชื่อ track) · handler_name → hdlr (ffprobe/mpv/MPC เห็น)
				av_dict_set(&st->metadata, "title", d.title.constData(), 0);
				av_dict_set(&st->metadata, "handler_name", d.title.constData(), 0);
			}
			in_tb.push_back(d.time_base);
		}
		if (!chapters.empty()) {
			// ต้องรู้ nb_chapters ก่อน write_header (mov สร้าง chapter track ตอน init) · end ของตัวสุดท้ายแก้ได้ก่อน trailer
			fmt->chapters = static_cast<AVChapter **>(av_calloc(chapters.size(), sizeof(AVChapter *)));
			if (!fmt->chapters) {
				*error = QObject::tr("Out of memory.");
				return false;
			}
			for (size_t i = 0; i < chapters.size(); i++) {
				AVChapter *c = static_cast<AVChapter *>(av_mallocz(sizeof(AVChapter)));
				if (!c) {
					*error = QObject::tr("Out of memory.");
					return false;
				}
				c->id = static_cast<int64_t>(i) + 1;
				c->time_base = AVRational{1, 1000};
				c->start = chapters[i].start_ms;
				c->end = chapters[i].end_ms;
				av_dict_set(&c->metadata, "title", chapters[i].title.constData(), 0);
				fmt->chapters[i] = c;
				fmt->nb_chapters = static_cast<unsigned>(i + 1);
			}
		}
		av_dict_set(&fmt->metadata, "encoder", "PS-WRAP", 0);
		if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
			err = avio_open(&fmt->pb, p.constData(), AVIO_FLAG_WRITE);
			if (err < 0) {
				if (file_error)
					*file_error = true;
				*error = QObject::tr("Could not open %1 for writing: %2").arg(QDir::toNativeSeparators(path), avErr(err));
				return false;
			}
		}
		AVDictionary *opts = nullptr;
		if (fragmented)
			av_dict_set(&opts, "movflags", "+frag_keyframe+empty_moov+default_base_moof", 0);
		err = avformat_write_header(fmt, &opts);
		av_dict_free(&opts);
		if (err < 0) {
			*error = QObject::tr("Could not write the file header: %1").arg(avErr(err));
			return false;
		}
		header_written = true;
		return true;
	}

	// p อยู่ใน time_base ของ stream ตอน open · ถูก unref เสมอ
	bool write(AVPacket *p, QString *error)
	{
		const int idx = p->stream_index;
		if (idx < 0 || idx >= static_cast<int>(in_tb.size())) {
			av_packet_unref(p);
			return true;
		}
		const AVRational tb = in_tb[idx];
		if (p->pts != AV_NOPTS_VALUE)
			end_ms = std::max(end_ms, av_rescale_q(p->pts + std::max<int64_t>(p->duration, 0), tb, AVRational{1, 1000}));
		av_packet_rescale_ts(p, tb, fmt->streams[idx]->time_base);
		const int err = av_interleaved_write_frame(fmt, p);
		av_packet_unref(p);
		if (err < 0) {
			*error = QObject::tr("Writing the file failed: %1").arg(avErr(err));
			return false;
		}
		return true;
	}

	bool finish(QString *error)
	{
		if (!fmt)
			return false;
		bool ok = true;
		if (header_written) {
			if (fmt->nb_chapters > 0) {
				AVChapter *last = fmt->chapters[fmt->nb_chapters - 1];
				if (end_ms > last->start)
					last->end = end_ms;
			}
			const int err = av_write_trailer(fmt);
			if (err < 0) {
				*error = QObject::tr("Finishing the file failed: %1").arg(avErr(err));
				ok = false;
			}
		}
		close();
		return ok;
	}

	void discard()
	{
		close();
		if (!path.isEmpty())
			QFile::remove(path);
	}

	int64_t endMs() const { return end_ms; }

private:
	AVFormatContext *fmt = nullptr;
	std::vector<AVRational> in_tb;
	QString path;
	bool header_written = false;
	int64_t end_ms = 0;

	void close()
	{
		if (!fmt)
			return;
		if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
			avio_closep(&fmt->pb);
		avformat_free_context(fmt); // คืน chapters + metadata ด้วย
		fmt = nullptr;
	}
};

// เขียนชุด packet (นาฬิกา pipeline) ลงไฟล์ MP4 ปกติพร้อม chapter — ใช้กับ replay (thread ของ job)
bool writePacketsFile(const QString &path, const PsWrapEncodedLayout &layout, const std::vector<StreamDesc> &streams,
                      const std::vector<AVPacket *> &packets, const std::vector<int64_t> &markers_us,
                      const std::atomic<bool> &abort, QString *error, bool *file_error)
{
	PsWrapPacketRebaser rb(&layout);
	std::vector<AVPacket *> ready;
	ready.reserve(packets.size());
	for (const AVPacket *p : packets)
		rb.push(p, &ready);
	auto free_ready = [&ready]() {
		for (AVPacket *p : ready)
			av_packet_free(&p);
		ready.clear();
	};
	if (!rb.started()) {
		free_ready();
		*error = QObject::tr("The replay buffer is still empty.");
		return false;
	}
	std::vector<int64_t> rel;
	for (int64_t m : markers_us)
		rel.push_back((m - rb.baseUs()) / 1000);
	const auto chapters = makeChapters(rel, (rb.endUs() - rb.baseUs()) / 1000);

	Mp4Writer w;
	if (!w.open(path, streams, false, chapters, error, file_error)) {
		w.discard();
		free_ready();
		return false;
	}
	bool ok = true;
	for (AVPacket *p : ready) {
		if (abort.load()) {
			ok = false;
			*error = QObject::tr("Cancelled.");
			break;
		}
		if (!w.write(p, error)) {
			ok = false;
			break;
		}
	}
	free_ready();
	if (ok && !w.finish(error))
		ok = false;
	if (!ok)
		w.discard();
	return ok;
}

// remux ไฟล์อัด (fragmented) → MP4 ปกติที่มี chapter แบบ stream copy (ไม่ encode ใหม่ — เร็วเท่าความเร็วดิสก์)
bool remuxWithChapters(const QString &src, const QString &dst, const std::vector<Chapter> &chapters,
                       const std::atomic<bool> &abort, QString *error)
{
	AVFormatContext *in = nullptr;
	const QByteArray s = src.toUtf8();
	int err = avformat_open_input(&in, s.constData(), nullptr, nullptr);
	if (err < 0) {
		*error = QObject::tr("Could not read %1: %2").arg(QDir::toNativeSeparators(src), avErr(err));
		return false;
	}
	std::vector<StreamDesc> streams;
	for (unsigned i = 0; i < in->nb_streams; i++) {
		AVStream *st = in->streams[i];
		if (st->codecpar->codec_id == AV_CODEC_ID_AAC && st->codecpar->frame_size <= 0)
			st->codecpar->frame_size = 1024; // demuxer ไม่ใส่ให้ → muxer เตือน "codec frame size is not set"
		StreamDesc d;
		d.par = std::shared_ptr<AVCodecParameters>(st->codecpar, [](AVCodecParameters *) {});
		d.time_base = st->time_base;
		d.video = st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
		// demuxer MP4 ไม่อ่าน udta "name" กลับมา → ใส่ชื่อ track ตามลำดับที่เราเขียนเอง
		if (!d.video && i >= 1 && i <= static_cast<unsigned>(kAudioTracks))
			d.title = kTrackTitles[i - 1];
		d.disposition = st->disposition;
		if (d.video && st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0)
			d.fps = static_cast<int>(std::lround(av_q2d(st->avg_frame_rate)));
		streams.push_back(std::move(d));
	}
	Mp4Writer w;
	bool ok = w.open(dst, streams, false, chapters, error, nullptr);
	AVPacket *pkt = ok ? av_packet_alloc() : nullptr;
	if (ok && !pkt) {
		ok = false;
		*error = QObject::tr("Out of memory.");
	}
	while (ok) {
		if (abort.load()) {
			ok = false;
			*error = QObject::tr("Cancelled.");
			break;
		}
		err = av_read_frame(in, pkt);
		if (err == AVERROR_EOF)
			break;
		if (err < 0) {
			ok = false;
			*error = QObject::tr("Could not read %1: %2").arg(QDir::toNativeSeparators(src), avErr(err));
			break;
		}
		if (!w.write(pkt, error))
			ok = false;
	}
	av_packet_free(&pkt);
	if (ok && !w.finish(error))
		ok = false;
	if (!ok)
		w.discard();
	avformat_close_input(&in);
	return ok;
}

} // namespace

// ---------------------------------------------------------------- PsWrapPacketRebaser

PsWrapPacketRebaser::~PsWrapPacketRebaser()
{
	for (AVPacket *p : pending_audio)
		av_packet_free(&p);
}

bool PsWrapPacketRebaser::accept(AVPacket *p)
{
	const PsWrapEncodedStream &s = layout->streams[p->stream_index];
	const int64_t base = av_rescale_q(base_us, AVRational{1, 1000000}, s.time_base);
	if (!s.video && p->pts != AV_NOPTS_VALUE) {
		const int64_t dur = p->duration > 0 ? p->duration : 1024;
		if (p->pts + dur <= base)
			return false; // เสียงที่จบก่อนจุดเริ่ม
	}
	if (p->pts != AV_NOPTS_VALUE) {
		p->pts -= base;
		end_us = std::max(end_us, base_us + toUs(p->pts + std::max<int64_t>(p->duration, 0), s.time_base));
	}
	if (p->dts != AV_NOPTS_VALUE)
		p->dts -= base;
	return true;
}

void PsWrapPacketRebaser::push(const AVPacket *pkt, std::vector<AVPacket *> *out)
{
	if (!layout || pkt->stream_index < 0 || pkt->stream_index >= layout->count)
		return;
	AVPacket *p = av_packet_clone(pkt);
	if (!p)
		return;
	const PsWrapEncodedStream &s = layout->streams[p->stream_index];
	if (base_us < 0) {
		if (!s.video) {
			pending_audio.push_back(p);
			if (pending_audio.size() > kRebaserMaxPending) {
				av_packet_free(&pending_audio.front());
				pending_audio.erase(pending_audio.begin());
			}
			return;
		}
		if (!(p->flags & AV_PKT_FLAG_KEY) || p->pts == AV_NOPTS_VALUE) {
			av_packet_free(&p);
			return;
		}
		base_us = toUs(p->pts, s.time_base);
		end_us = base_us;
		accept(p);
		out->push_back(p);
		for (AVPacket *a : pending_audio) {
			if (accept(a))
				out->push_back(a);
			else
				av_packet_free(&a);
		}
		pending_audio.clear();
		return;
	}
	if (accept(p))
		out->push_back(p);
	else
		av_packet_free(&p);
}

// ---------------------------------------------------------------- sinks

// ไฟล์อัด: rebase ให้เริ่มที่ keyframe แล้วเขียน fragmented MP4 บน worker thread
class PsWrapRecorder::FileSink : public PsWrapPacketSink
{
public:
	Mp4Writer writer;
	PsWrapPacketRebaser rb;
	QString path;
	QString error;
	std::atomic<bool> broken{false};
	bool reported = false;            // GUI: แจ้ง error ไปแล้ว
	std::vector<int64_t> markers_us;  // GUI thread เท่านั้น (นาฬิกา pipeline)
	std::vector<AVPacket *> ready;    // worker เท่านั้น

	bool write(const AVPacket *pkt) override
	{
		if (broken)
			return false;
		rb.push(pkt, &ready);
		bool ok = true;
		for (AVPacket *p : ready) {
			if (ok && !writer.write(p, &error))
				ok = false;
			av_packet_free(&p);
		}
		ready.clear();
		if (!ok)
			broken = true;
		return ok;
	}
	QString errorString() const override { return error; }
};

// Instant Replay: เก็บ packet (ref ของ buffer encoder — ไม่ copy ข้อมูล) ตัดทิ้งทีละ GOP จากหัว
class PsWrapRecorder::ReplayRing : public PsWrapPacketSink
{
public:
	explicit ReplayRing(int seconds) { setSeconds(seconds); }
	~ReplayRing() override
	{
		for (auto &it : items)
			av_packet_free(&it.p);
	}

	void setSeconds(int s)
	{
		std::lock_guard<std::mutex> lock(m);
		keep_us = int64_t(std::clamp(s, 30, 120)) * 1000000;
	}

	bool write(const AVPacket *pkt) override
	{
		const PsWrapEncodedStream &s = layout->streams[pkt->stream_index];
		if (pkt->pts == AV_NOPTS_VALUE)
			return true;
		AVPacket *p = av_packet_clone(pkt);
		if (!p)
			return true; // แรมหมด — ข้าม packet นี้ ไม่ถือว่า sink พัง
		const int64_t t = toUs(p->pts, s.time_base);
		std::lock_guard<std::mutex> lock(m);
		const uint64_t seq = next_seq++;
		items.push_back(Item{p, t, seq, s.video});
		bytes += p->size;
		if (s.video && (p->flags & AV_PKT_FLAG_KEY))
			keys.push_back(Key{seq, t});
		newest_us = std::max(newest_us, t);
		trim();
		return true;
	}

	void addMarker(int64_t t_us)
	{
		std::lock_guard<std::mutex> lock(m);
		markers_us.push_back(t_us);
	}

	// วินาทีจากหัว buffer ถึง t (ตำแหน่งในคลิปถ้าเซฟตอนนี้)
	int secondsAt(int64_t t_us)
	{
		std::lock_guard<std::mutex> lock(m);
		const int64_t head = keys.empty() ? t_us : keys.front().t_us;
		return static_cast<int>(std::max<int64_t>(0, t_us - head) / 1000000);
	}

	// GUI thread: ref ทุก packet (ราคาถูก — ไม่ copy ข้อมูล) ให้ job เขียนไฟล์ต่อเอง
	bool snapshot(std::vector<AVPacket *> *out, std::vector<int64_t> *markers, double *span_s, int64_t *size)
	{
		std::lock_guard<std::mutex> lock(m);
		if (keys.empty())
			return false;
		out->reserve(items.size());
		for (const auto &it : items)
			if (AVPacket *c = av_packet_clone(it.p))
				out->push_back(c);
		*markers = markers_us;
		*span_s = double(newest_us - keys.front().t_us) / 1e6;
		*size = bytes;
		return true;
	}

	const PsWrapEncodedLayout *layout = nullptr;

private:
	struct Item
	{
		AVPacket *p;
		int64_t t_us;
		uint64_t seq;
		bool video;
	};
	struct Key
	{
		uint64_t seq;
		int64_t t_us;
	};
	std::mutex m;
	std::deque<Item> items;
	std::deque<Key> keys;
	std::vector<int64_t> markers_us;
	uint64_t next_seq = 0;
	int64_t bytes = 0;
	int64_t newest_us = 0;
	int64_t keep_us = 60000000;
	int64_t last_log_us = 0;

	void popFront()
	{
		bytes -= items.front().p->size;
		av_packet_free(&items.front().p);
		items.pop_front();
	}

	// หัว buffer = keyframe ล่าสุดที่เก่ากว่า keep (คลิปยาว keep..keep+GOP) · เพดานแรมทิ้ง GOP เก่าก่อนกำหนด
	void trim()
	{
		while (keys.size() >= 2 && (keys[1].t_us <= newest_us - keep_us || bytes > kReplayMaxBytes)) {
			const Key k = keys[1];
			keys.pop_front();
			// ทิ้งทุกอย่างก่อน keyframe ใหม่ ยกเว้นเสียงช่วงสั้นๆ ก่อนหน้า (packet เสียงมาถึงก่อนภาพที่ encode ช้ากว่า)
			while (!items.empty() && items.front().seq < k.seq && (items.front().video || items.front().t_us < k.t_us - kReplayAudioLeadUs))
				popFront();
		}
		if (keys.empty()) {
			// ยังไม่มี keyframe (เพิ่งเริ่ม) — เก็บไว้ไม่เกิน keep
			while (!items.empty() && items.front().t_us < newest_us - keep_us)
				popFront();
		} else {
			while (!items.empty() && items.front().seq < keys.front().seq && items.front().t_us < keys.front().t_us - kReplayAudioLeadUs)
				popFront();
			const int64_t head = keys.front().t_us;
			markers_us.erase(std::remove_if(markers_us.begin(), markers_us.end(), [head](int64_t t) { return t < head; }), markers_us.end());
		}
		if (newest_us - last_log_us >= kReplayLogEveryUs) {
			last_log_us = newest_us;
			qCInfo(pswrapRec).nospace() << "replay buffer: " << (keys.empty() ? 0.0 : double(newest_us - keys.front().t_us) / 1e6)
				<< " s, " << double(bytes) / (1024.0 * 1024.0) << " MB, " << items.size() << " packets, keep " << keep_us / 1000000 << " s";
		}
	}
};

struct PsWrapRecorder::Job
{
	quint64 id = 0;
	std::thread th;
	std::atomic<bool> abort{false};
};

// ---------------------------------------------------------------- pipeline (encoder + worker)

struct PsWrapRecorder::Impl
{
	PsWrapRecorder *owner = nullptr;
	PsWrapRecConfig cfg;
	QString encoder_name;
	PsWrapEncodedLayout layout;

	AVCodecContext *venc = nullptr;
	AudioTrack atr[kAudioTracks];
	AVPacket *pkt = nullptr;
	AVMasteringDisplayMetadata mdm = {};
	AVContentLightMetadata cll = {};
	bool has_cll = false;

	std::thread worker;
	std::mutex mtx;
	std::condition_variable cv;
	std::deque<VideoItem> vq;
	std::deque<AudioChunk> aq;
	bool accepting = false;
	bool stop_requested = false;
	std::atomic<bool> failed{false};
	std::atomic<bool> force_key{false};

	// ผู้รับ packet — worker ถือ lock นี้ตอนส่ง packet · GUI ถือตอนเพิ่ม/ถอด
	std::mutex sinks_mtx;
	std::vector<std::shared_ptr<PsWrapPacketSink>> sinks;
	// raw tap (encoder ตัวที่ 2) — worker ถือ lock นี้ตอนส่งเฟรม/เสียง · GUI ถือตอนเพิ่ม/ถอด
	std::mutex taps_mtx;
	std::vector<std::shared_ptr<PsWrapRawTap>> taps;

	qint64 t0_us = 0;
	// render thread เท่านั้น
	qint64 next_due_us = 0;
	qint64 interval_us = 16667;

	// worker เท่านั้น
	int64_t audio_pos = 0;
	qint64 last_game_arrival_us = 0;
	std::deque<float> mic_fifo; // interleaved stereo
	int64_t last_video_pts = -1;

	// สถิติ (เขียนจากหลาย thread → atomic)
	std::atomic<quint64> video_frames{0}, video_dropped{0}, audio_dropped{0}, audio_late{0}, gap_fills{0}, mic_trims{0}, forced_keys{0};
	std::atomic<bool> warned_rate{false};
	float game_peak = 0.0f, mic_peak = 0.0f; // worker เท่านั้น — log ตอนจบไว้พิสูจน์ว่าเสียงเข้าจริง

	~Impl() { closeAll(); }

	void closeAll()
	{
		if (venc)
			avcodec_free_context(&venc);
		for (auto &t : atr) {
			if (t.enc)
				avcodec_free_context(&t.enc);
			if (t.fifo) {
				av_audio_fifo_free(t.fifo);
				t.fifo = nullptr;
			}
		}
		for (int i = 0; i < layout.count; i++)
			avcodec_parameters_free(&layout.streams[i].par);
		layout.count = 0;
		av_packet_free(&pkt);
		for (auto &v : vq)
			av_frame_free(&v.frame);
		vq.clear();
	}

	bool openVideo(QString *error)
	{
		const AVPixelFormat pix = cfg.hdr ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
		const char *const sdr_encoders[] = {"h264_nvenc", "h264_amf", "libx264"};
		const char *const hdr_encoders[] = {"hevc_nvenc", "hevc_amf"};
		const char *const *names = cfg.hdr ? hdr_encoders : sdr_encoders;
		const int count = cfg.hdr ? 2 : 3;

		// ~20 Mbps ที่ 1080p60 สเกลตามพิกเซล×fps (HDR 10-bit +25%)
		double bitrate = 20e6 * (double(cfg.width) * cfg.height * cfg.fps) / (1920.0 * 1080.0 * 60.0);
		if (cfg.hdr)
			bitrate *= 1.25;
		bitrate = std::clamp(bitrate, 8e6, 80e6);

		QStringList tried;
		for (int i = 0; i < count; i++) {
			const AVCodec *codec = avcodec_find_encoder_by_name(names[i]);
			if (!codec)
				continue;
			AVCodecContext *c = avcodec_alloc_context3(codec);
			if (!c)
				continue;
			c->width = cfg.width;
			c->height = cfg.height;
			c->pix_fmt = pix;
			c->time_base = AVRational{1, 1000};
			c->framerate = AVRational{cfg.fps, 1};
			c->gop_size = cfg.fps * 2;
			c->max_b_frames = 0;
			c->bit_rate = static_cast<int64_t>(bitrate);
			c->rc_max_rate = static_cast<int64_t>(bitrate * 1.5);
			c->rc_buffer_size = static_cast<int>(std::min(bitrate * 2.0, 2.0e9));
			c->color_range = AVCOL_RANGE_MPEG;
			c->chroma_sample_location = AVCHROMA_LOC_LEFT;
			if (cfg.hdr) {
				c->colorspace = AVCOL_SPC_BT2020_NCL;
				c->color_primaries = AVCOL_PRI_BT2020;
				c->color_trc = AVCOL_TRC_SMPTE2084;
			} else {
				c->colorspace = AVCOL_SPC_BT709;
				c->color_primaries = AVCOL_PRI_BT709;
				c->color_trc = AVCOL_TRC_BT709;
			}
			// header แยก (extradata) เสมอ — ผู้รับทุกตัว (MP4, RTMP/FLV) ต้องการแบบนี้
			c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

			const QByteArray name(names[i]);
			if (name.endsWith("_nvenc")) {
				av_opt_set(c->priv_data, "preset", "p5", 0);
				av_opt_set(c->priv_data, "tune", "hq", 0);
				av_opt_set(c->priv_data, "rc", "vbr", 0);
				av_opt_set(c->priv_data, "spatial-aq", "1", 0);
				av_opt_set(c->priv_data, "forced-idr", "1", 0); // keyframe ที่ขอ (ไฟล์อัดเริ่มกลาง replay) = IDR จริง
				if (cfg.hdr)
					av_opt_set(c->priv_data, "profile", "main10", 0);
				else
					av_opt_set(c->priv_data, "profile", "high", 0);
			} else if (name.endsWith("_amf")) {
				av_opt_set(c->priv_data, "quality", "quality", 0);
				av_opt_set(c->priv_data, "rc", "vbr_peak", 0);
				av_opt_set(c->priv_data, "forced_idr", "1", 0); // มีเฉพาะ FFmpeg ใหม่ — ไม่มีก็ไม่เป็นไร (รอ keyframe ตาม GOP)
				if (cfg.hdr)
					av_opt_set(c->priv_data, "profile", "main10", 0);
			} else if (name == "libx264") {
				av_opt_set(c->priv_data, "preset", "veryfast", 0);
				av_opt_set(c->priv_data, "forced-idr", "1", 0);
			}

			const int err = avcodec_open2(c, codec, nullptr);
			if (err < 0) {
				tried << QStringLiteral("%1 (%2)").arg(QString::fromUtf8(name), avErr(err));
				qCWarning(pswrapRec) << "encoder" << name << "failed:" << avErr(err);
				avcodec_free_context(&c);
				continue;
			}
			venc = c;
			encoder_name = QString::fromUtf8(name);
			break;
		}
		if (!venc) {
			*error = tried.isEmpty()
				? QObject::tr("No usable video encoder found.")
				: QObject::tr("Video encoder failed: %1").arg(tried.join(QStringLiteral(", ")));
			return false;
		}

		PsWrapEncodedStream &vs = layout.streams[0];
		vs.par = avcodec_parameters_alloc();
		if (!vs.par || avcodec_parameters_from_context(vs.par, venc) < 0) {
			*error = QObject::tr("Could not create the video stream.");
			return false;
		}
		vs.time_base = venc->time_base;
		vs.video = true;
		vs.disposition = AV_DISPOSITION_DEFAULT;
		layout.count = 1;
		layout.fps = cfg.fps;

		if (cfg.hdr) {
			const auto &h = cfg.hdr_info;
			mdm.display_primaries[0][0] = q(h.prim_r[0], 50000);
			mdm.display_primaries[0][1] = q(h.prim_r[1], 50000);
			mdm.display_primaries[1][0] = q(h.prim_g[0], 50000);
			mdm.display_primaries[1][1] = q(h.prim_g[1], 50000);
			mdm.display_primaries[2][0] = q(h.prim_b[0], 50000);
			mdm.display_primaries[2][1] = q(h.prim_b[1], 50000);
			mdm.white_point[0] = q(h.white[0], 50000);
			mdm.white_point[1] = q(h.white[1], 50000);
			mdm.min_luminance = q(h.min_luma, 10000);
			mdm.max_luminance = q(h.max_luma, 10000);
			mdm.has_primaries = 1;
			mdm.has_luminance = 1;
			if (AVPacketSideData *sd = av_packet_side_data_new(&vs.par->coded_side_data, &vs.par->nb_coded_side_data,
					AV_PKT_DATA_MASTERING_DISPLAY_METADATA, sizeof(mdm), 0))
				memcpy(sd->data, &mdm, sizeof(mdm));
			if (h.max_cll > 0.0f) {
				has_cll = true;
				cll.MaxCLL = static_cast<unsigned>(h.max_cll);
				cll.MaxFALL = static_cast<unsigned>(h.max_fall);
				if (AVPacketSideData *sd = av_packet_side_data_new(&vs.par->coded_side_data, &vs.par->nb_coded_side_data,
						AV_PKT_DATA_CONTENT_LIGHT_LEVEL, sizeof(cll), 0))
					memcpy(sd->data, &cll, sizeof(cll));
			}
		}
		return true;
	}

	bool openAudio(QString *error)
	{
		const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!codec) {
			*error = QObject::tr("AAC encoder not available.");
			return false;
		}
		for (int i = 0; i < kAudioTracks; i++) {
			AudioTrack &t = atr[i];
			t.enc = avcodec_alloc_context3(codec);
			if (!t.enc)
				return false;
			t.enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
			t.enc->sample_rate = kAudioRate;
			av_channel_layout_default(&t.enc->ch_layout, 2);
			t.enc->bit_rate = i == 0 ? 192000 : 160000;
			t.enc->time_base = AVRational{1, kAudioRate};
			t.enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			const int err = avcodec_open2(t.enc, codec, nullptr);
			if (err < 0) {
				*error = QObject::tr("Audio encoder failed: %1").arg(avErr(err));
				return false;
			}
			t.index = layout.count;
			PsWrapEncodedStream &s = layout.streams[t.index];
			s.par = avcodec_parameters_alloc();
			if (!s.par || avcodec_parameters_from_context(s.par, t.enc) < 0) {
				*error = QObject::tr("Could not create the audio stream.");
				return false;
			}
			layout.count++;
			s.time_base = t.enc->time_base;
			s.title = kTrackTitles[i];
			s.disposition = i == 0 ? AV_DISPOSITION_DEFAULT : 0;
			s.video = false;
			t.fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, kAudioRate / 10);
			if (!t.fifo)
				return false;
		}
		return true;
	}

	bool open(QString *error)
	{
		pkt = av_packet_alloc();
		if (!pkt || !openVideo(error) || !openAudio(error)) {
			if (error->isEmpty())
				*error = QObject::tr("Out of memory.");
			return false;
		}
		return true;
	}

	// ---------------------------------------------------------------- worker
	void dispatch(AVPacket *p)
	{
		std::vector<std::pair<PsWrapPacketSink *, QString>> broken;
		{
			std::lock_guard<std::mutex> lock(sinks_mtx);
			for (auto it = sinks.begin(); it != sinks.end();) {
				if ((*it)->write(p)) {
					++it;
					continue;
				}
				broken.emplace_back(it->get(), (*it)->errorString());
				it = sinks.erase(it); // GUI ยังถือ shared_ptr ไว้ (ปิดไฟล์ใน sinkFailed)
			}
		}
		for (const auto &b : broken) {
			qCWarning(pswrapRec) << "sink failed:" << b.second;
			QMetaObject::invokeMethod(owner, [o = owner, s = b.first, m = b.second]() { o->sinkFailed(s, m); }, Qt::QueuedConnection);
		}
	}

	bool drainEncoder(AVCodecContext *enc, int index)
	{
		for (;;) {
			int err = avcodec_receive_packet(enc, pkt);
			if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
				return true;
			if (err < 0) {
				fail(QObject::tr("Encoding failed: %1").arg(avErr(err)));
				return false;
			}
			pkt->stream_index = index; // time_base ของ encoder = time_base ใน layout อยู่แล้ว
			dispatch(pkt);
			av_packet_unref(pkt);
		}
	}

	void fail(const QString &message)
	{
		if (failed.exchange(true))
			return;
		qCWarning(pswrapRec) << "recording failed:" << message;
		QMetaObject::invokeMethod(owner, [o = owner, message]() { o->workerFailed(message); }, Qt::QueuedConnection);
	}

	void encodeVideo(VideoItem &item)
	{
		AVFrame *frame = item.frame;
		item.frame = nullptr;
		int64_t pts = (item.capture_us - t0_us) / 1000;
		if (pts <= last_video_pts)
			pts = last_video_pts + 1;
		last_video_pts = pts;
		frame->pts = pts;
		bool has_sinks = true;
		{
			std::lock_guard<std::mutex> lock(taps_mtx);
			for (auto &t : taps)
				t->videoFrame(frame);
		}
		{
			std::lock_guard<std::mutex> lock(sinks_mtx);
			has_sinks = !sinks.empty();
		}
		if (!has_sinks) {
			// มีแต่ tap (เช่น Go Live อย่างเดียว) → ไม่ต้อง encode ภาพของไฟล์อัด (ประหยัด GPU) · ผู้รับใหม่ได้ IDR จาก force_key
			av_frame_free(&frame);
			return;
		}
		if (force_key.exchange(false)) {
			frame->pict_type = AV_PICTURE_TYPE_I; // ผู้รับใหม่ (ไฟล์อัด) เริ่มได้ทันทีไม่ต้องรอ GOP
			forced_keys++;
		}
		if (cfg.hdr) {
			if (AVFrameSideData *sd = av_frame_new_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA, sizeof(mdm)))
				memcpy(sd->data, &mdm, sizeof(mdm));
			if (has_cll)
				if (AVFrameSideData *sd = av_frame_new_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL, sizeof(cll)))
					memcpy(sd->data, &cll, sizeof(cll));
		}
		const int err = avcodec_send_frame(venc, frame);
		av_frame_free(&frame); // ปล่อย slot ของ render thread (encoder ที่ต้องเก็บไว้จะ ref เอง)
		if (err < 0) {
			fail(QObject::tr("Video encoding failed: %1").arg(avErr(err)));
			return;
		}
		video_frames++;
		drainEncoder(venc, 0);
	}

	bool encodeAudioTrack(AudioTrack &t, bool flush)
	{
		const int frame_size = t.enc->frame_size > 0 ? t.enc->frame_size : 1024;
		while (av_audio_fifo_size(t.fifo) >= frame_size || (flush && av_audio_fifo_size(t.fifo) > 0)) {
			AVFrame *f = av_frame_alloc();
			if (!f)
				return false;
			f->nb_samples = frame_size;
			f->format = AV_SAMPLE_FMT_FLTP;
			f->sample_rate = kAudioRate;
			av_channel_layout_copy(&f->ch_layout, &t.enc->ch_layout);
			if (av_frame_get_buffer(f, 0) < 0) {
				av_frame_free(&f);
				return false;
			}
			const int got = av_audio_fifo_read(t.fifo, reinterpret_cast<void **>(f->data), frame_size);
			for (int ch = 0; ch < 2 && got < frame_size; ch++) // เฟรมสุดท้ายไม่เต็ม → เติมเงียบ
				memset(reinterpret_cast<float *>(f->data[ch]) + got, 0, sizeof(float) * (frame_size - got));
			f->pts = t.next_pts;
			t.next_pts += frame_size;
			const int err = avcodec_send_frame(t.enc, f);
			av_frame_free(&f);
			if (err < 0) {
				fail(QObject::tr("Audio encoding failed: %1").arg(avErr(err)));
				return false;
			}
			if (!drainEncoder(t.enc, t.index))
				return false;
		}
		return true;
	}

	// เขียน n เฟรมลงทุก track: เกม (nullptr = เงียบ) + ไมค์จาก FIFO → mix
	void writeBlock(const float *game, int64_t n)
	{
		thread_local std::vector<float> planes;
		planes.resize(static_cast<size_t>(n) * 6);
		float *mixL = planes.data(), *mixR = mixL + n, *gL = mixR + n, *gR = gL + n, *mL = gR + n, *mR = mL + n;
		for (int64_t i = 0; i < n; i++) {
			const float l = game ? game[2 * i] : 0.0f;
			const float r = game ? game[2 * i + 1] : 0.0f;
			float ml = 0.0f, mr = 0.0f;
			if (mic_fifo.size() >= 2) {
				ml = mic_fifo.front(); mic_fifo.pop_front();
				mr = mic_fifo.front(); mic_fifo.pop_front();
			}
			gL[i] = l; gR[i] = r; mL[i] = ml; mR[i] = mr;
			mixL[i] = std::clamp(l + ml, -1.0f, 1.0f);
			mixR[i] = std::clamp(r + mr, -1.0f, 1.0f);
		}
		{
			std::lock_guard<std::mutex> lock(taps_mtx);
			for (auto &t : taps)
				t->audioMix(mixL, mixR, n, audio_pos);
		}
		void *data[kAudioTracks][2] = {{mixL, mixR}, {gL, gR}, {mL, mR}};
		for (int t = 0; t < kAudioTracks; t++) {
			if (av_audio_fifo_write(atr[t].fifo, data[t], static_cast<int>(n)) < n) {
				fail(QObject::tr("Out of memory."));
				return;
			}
			if (!encodeAudioTrack(atr[t], false))
				return;
		}
		audio_pos += n;
	}

	void writeSilence(int64_t n)
	{
		while (n > 0 && !failed) {
			const int64_t step = std::min<int64_t>(n, 960);
			writeBlock(nullptr, step);
			n -= step;
		}
	}

	int64_t usToSamples(qint64 us) const { return (us - t0_us) * kAudioRate / 1000000; }

	void processAudio(AudioChunk &c)
	{
		const int64_t frames = static_cast<int64_t>(c.stereo.size() / 2);
		for (float v : c.stereo)
			(c.mic ? mic_peak : game_peak) = std::max(c.mic ? mic_peak : game_peak, std::fabs(v));
		if (c.mic) {
			mic_fifo.insert(mic_fifo.end(), c.stereo.begin(), c.stereo.end());
			if (mic_fifo.size() / 2 > kMicFifoMaxFrames) {
				mic_fifo.erase(mic_fifo.begin(), mic_fifo.end() - kMicFifoKeepFrames * 2);
				mic_trims++;
			}
			return;
		}
		const int64_t ideal_start = usToSamples(c.arrival_us) - frames;
		if (ideal_start - audio_pos > kGapFillSamples) {
			writeSilence(ideal_start - kGapSlackSamples - audio_pos);
			gap_fills++;
		} else if (audio_pos - ideal_start > kLateDropSamples) {
			audio_late++;
			return;
		}
		last_game_arrival_us = c.arrival_us;
		writeBlock(c.stereo.data(), frames);
	}

	void idleFill(qint64 now_us)
	{
		const qint64 since = last_game_arrival_us > 0 ? now_us - last_game_arrival_us : now_us - t0_us;
		if (since < kGameIdleUs)
			return;
		const int64_t target = usToSamples(now_us) - kIdleLagSamples;
		if (target > audio_pos)
			writeSilence(target - audio_pos);
	}

	void run()
	{
		for (;;) {
			std::deque<VideoItem> vlocal;
			std::deque<AudioChunk> alocal;
			bool stopping = false;
			{
				std::unique_lock<std::mutex> lock(mtx);
				cv.wait_for(lock, std::chrono::milliseconds(20), [this] { return stop_requested || !vq.empty(); });
				vlocal.swap(vq);
				alocal.swap(aq);
				stopping = stop_requested;
			}
			for (auto &a : alocal) {
				if (failed)
					break;
				processAudio(a);
			}
			for (auto &v : vlocal) {
				if (failed)
					av_frame_free(&v.frame);
				else
					encodeVideo(v);
			}
			if (!failed)
				idleFill(static_cast<qint64>(chiaki_time_now_monotonic_us()));
			if (stopping || failed)
				break;
		}
		finish();
	}

	// pipeline ปิด: เสียงให้ยาวถึงเฟรมภาพสุดท้าย แล้ว flush ทุก encoder เข้า sink (ปิดไฟล์ทำบน GUI thread หลัง join)
	void finish()
	{
		if (!failed) {
			const int64_t video_end = (last_video_pts + 1) * kAudioRate / 1000;
			if (video_end > audio_pos && video_end - audio_pos < kAudioRate * 2)
				writeSilence(video_end - audio_pos);
			for (auto &t : atr)
				if (!failed)
					encodeAudioTrack(t, true);
			if (!failed && avcodec_send_frame(venc, nullptr) >= 0)
				drainEncoder(venc, 0);
			for (auto &t : atr)
				if (!failed && avcodec_send_frame(t.enc, nullptr) >= 0)
					drainEncoder(t.enc, t.index);
		}
		qCInfo(pswrapRec).nospace() << "pipeline finished: video_frames=" << video_frames.load()
			<< " video_dropped=" << video_dropped.load() << " audio_dropped=" << audio_dropped.load()
			<< " audio_late=" << audio_late.load() << " gap_fills=" << gap_fills.load()
			<< " mic_trims=" << mic_trims.load() << " forced_keys=" << forced_keys.load()
			<< " game_peak_db=" << 20.0 * std::log10(game_peak + 1e-9)
			<< " mic_peak_db=" << 20.0 * std::log10(mic_peak + 1e-9) << " audio_s=" << double(audio_pos) / kAudioRate
			<< " video_s=" << double(last_video_pts + 1) / 1000.0;
	}
};

// ---------------------------------------------------------------- PsWrapRecorder

PsWrapRecorder::PsWrapRecorder(QObject *parent)
	: QObject(parent)
{
	g_instance.storeRelease(this);
	tick_timer.setInterval(500);
	connect(&tick_timer, &QTimer::timeout, this, [this]() {
		const int s = static_cast<int>((QDateTime::currentMSecsSinceEpoch() - start_ms) / 1000);
		if (s != seconds_value) {
			seconds_value = s;
			emit secondsChanged();
		}
	});
}

PsWrapRecorder::~PsWrapRecorder()
{
	shutdown();
	g_instance.testAndSetOrdered(this, nullptr);
}

PsWrapRecorder *PsWrapRecorder::instance()
{
	return g_instance.loadAcquire();
}

bool PsWrapRecorder::ensurePipeline(const PsWrapRecConfig &config, QString *error)
{
	if (impl)
		return true;
	auto p = std::make_unique<Impl>();
	p->owner = this;
	p->cfg = config;
	p->cfg.width &= ~1;
	p->cfg.height &= ~1;
	p->cfg.fps = std::clamp(config.fps, 24, 120);
	if (!p->open(error))
		return false;
	p->interval_us = 1000000 / p->cfg.fps;
	p->t0_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
	p->next_due_us = p->t0_us;
	p->accepting = true;
	description_value = QStringLiteral("%1x%2 · %3 · %4").arg(p->cfg.width).arg(p->cfg.height)
		.arg(p->cfg.hdr ? QStringLiteral("HDR") : QStringLiteral("SDR"), p->encoder_name);
	qCInfo(pswrapRec) << "pipeline started:" << description_value << p->cfg.fps << "fps";
	Impl *raw = p.get();
	raw->worker = std::thread([raw]() { raw->run(); });
	{
		std::lock_guard<std::mutex> life(life_mutex);
		impl = std::move(p);
	}
	capturing.storeRelease(1);
	return true;
}

void PsWrapRecorder::attachSink(const std::shared_ptr<PsWrapPacketSink> &sink)
{
	if (!impl)
		return;
	{
		std::lock_guard<std::mutex> lock(impl->sinks_mtx);
		impl->sinks.push_back(sink);
	}
	impl->force_key = true;
}

bool PsWrapRecorder::detachSink(PsWrapPacketSink *sink)
{
	if (!impl)
		return false;
	std::lock_guard<std::mutex> lock(impl->sinks_mtx);
	auto it = std::find_if(impl->sinks.begin(), impl->sinks.end(), [sink](const auto &s) { return s.get() == sink; });
	if (it == impl->sinks.end())
		return false;
	impl->sinks.erase(it);
	return true;
}

void PsWrapRecorder::shutdownPipeline()
{
	if (!impl)
		return;
	busy = true;
	capturing.storeRelease(0);
	emit recordingChanged();
	{
		std::lock_guard<std::mutex> lock(impl->mtx);
		impl->accepting = false;
		impl->stop_requested = true;
	}
	impl->cv.notify_all();
	if (impl->worker.joinable())
		impl->worker.join(); // flush encoder เข้า sink แล้ว
	{
		std::lock_guard<std::mutex> lock(impl->sinks_mtx);
		impl->sinks.clear();
	}
	{
		std::lock_guard<std::mutex> lock(impl->taps_mtx);
		impl->taps.clear();
	}

	const bool had_replay = replay_ring != nullptr;
	replay_ring.reset();
	if (file_sink) {
		auto fs = std::move(file_sink);
		recording.storeRelease(0);
		tick_timer.stop();
		finishFile(fs, true);
	}
	auto extras = std::move(extra_sinks);
	extra_sinks.clear();
	for (auto &s : extras)
		s->detached(true);
	auto taps = std::move(raw_taps);
	raw_taps.clear();
	for (auto &t : taps)
		t->detached();
	{
		std::lock_guard<std::mutex> life(life_mutex);
		impl.reset();
	}
	busy = false;
	if (had_replay) {
		replay_active.storeRelease(0);
		emit replayActiveChanged();
	}
	emit recordingChanged();
}

void PsWrapRecorder::stopPipelineIfUnused()
{
	if (impl && !file_sink && !replay_ring && extra_sinks.empty() && raw_taps.empty())
		shutdownPipeline();
}

bool PsWrapRecorder::start(const PsWrapRecConfig &config, QString *error)
{
	QString err;
	start_file_error = false;
	if (isRecording() || file_sink || busy) {
		err = tr("Already recording.");
	} else {
		// pipeline ของ replay เป็น SDR แต่ตอนนี้ HDR (หรือกลับกัน) → ต้องเริ่มใหม่ ไม่งั้นสีผิด
		const bool had_replay = replay_ring != nullptr;
		if (impl && impl->cfg.hdr != config.hdr && extra_sinks.empty() && raw_taps.empty()) {
			qCInfo(pswrapRec) << "recording: restarting pipeline for" << (config.hdr ? "HDR" : "SDR") << "(replay buffer reset)";
			shutdownPipeline();
		}
		const bool created = !impl;
		bool file_error = false;
		if (ensurePipeline(config, &err)) {
			if (created && had_replay && !replay_ring) {
				replay_ring = std::make_shared<ReplayRing>(replay_seconds);
				replay_ring->layout = &impl->layout;
				attachSink(replay_ring);
				replay_active.storeRelease(1);
				emit replayActiveChanged();
			}
			auto fs = std::make_shared<FileSink>();
			fs->path = config.path;
			fs->rb.setLayout(&impl->layout);
			const QString dir = QFileInfo(config.path).absolutePath();
			if (!QDir().mkpath(dir) || !QFileInfo(dir).isWritable()) {
				err = tr("Can't write to %1.").arg(QDir::toNativeSeparators(dir));
				file_error = true;
			} else if (!fs->writer.open(config.path, describe(impl->layout, false), true, {}, &err, &file_error)) {
				fs->writer.discard();
			} else {
				file_sink = fs;
				attachSink(fs);
				start_ms = QDateTime::currentMSecsSinceEpoch();
				seconds_value = 0;
				last_error.clear();
				recording.storeRelease(1);
				tick_timer.start();
				qCInfo(pswrapRec) << "recording started:" << config.path << description_value << (created ? "(new pipeline)" : "(shared pipeline)");
				emit secondsChanged();
				emit lastErrorChanged();
				emit recordingChanged();
				return true;
			}
		}
		start_file_error = file_error;
#ifdef Q_OS_WIN
		// แก้ในแอปไม่ได้: Windows Security "Controlled folder access" บล็อก exe ที่ไม่รู้จักไม่ให้เขียน Videos/Documents/Desktop (พบ 2026-10-06)
		if (file_error)
			err += QStringLiteral(" ") + tr("If Windows Security \"Controlled folder access\" is on, allow PS-WRAP.exe there or choose another recording folder in Settings.");
#endif
		QFile::remove(config.path);
		if (created)
			stopPipelineIfUnused();
	}
	qCWarning(pswrapRec) << "recording start failed:" << err;
	if (error)
		*error = err;
	last_error = err;
	emit lastErrorChanged();
	return false;
}

void PsWrapRecorder::stop()
{
	if (!file_sink)
		return;
	if (!replay_ring && extra_sinks.empty() && raw_taps.empty()) {
		shutdownPipeline(); // ไม่มีใครใช้ pipeline ต่อ → flush encoder ลงไฟล์ให้ครบ (เหมือนเดิม)
		return;
	}
	auto fs = std::move(file_sink);
	file_sink.reset();
	detachSink(fs.get());
	recording.storeRelease(0);
	tick_timer.stop();
	finishFile(fs, false);
	emit recordingChanged();
}

void PsWrapRecorder::finishFile(const std::shared_ptr<FileSink> &fs, bool flushed)
{
	QString err;
	const bool broken = fs->broken.load();
	const bool closed = fs->writer.finish(&err);
	const QString path = fs->path;
	if (broken) {
		if (!fs->reported) {
			fs->reported = true;
			last_error = fs->error;
			emit lastErrorChanged();
			emit failed(fs->error);
		}
		return;
	}
	if (!fs->rb.started()) {
		// ไม่ได้ keyframe สักเฟรม (หยุดทันทีหลังเริ่ม) — ไฟล์ไม่มีภาพ ไม่ต้องเก็บ
		QFile::remove(path);
		emit failed(tr("Nothing was recorded."));
		return;
	}
	if (!closed) {
		last_error = err;
		emit lastErrorChanged();
		emit failed(err);
		return;
	}
	const int64_t base = fs->rb.baseUs();
	std::vector<int64_t> rel;
	for (int64_t m : fs->markers_us)
		rel.push_back((m - base) / 1000);
	const auto chapters = makeChapters(rel, (fs->rb.endUs() - base) / 1000);
	qCInfo(pswrapRec) << "recording finished:" << path << (flushed ? "(pipeline flushed)" : "(pipeline continues)")
		<< "markers" << fs->markers_us.size();
	if (chapters.empty()) {
		last_path = path;
		emit lastPathChanged();
		emit saved(path);
		return;
	}
	// ใส่ chapter = remux บน thread แยก (ไฟล์ใหญ่ใช้เวลา) · ไฟล์เดิมยังอยู่จนกว่าไฟล์ใหม่เสร็จ
	runJob([this, path, chapters](const std::atomic<bool> &abort) {
		const QString tmp = path + QStringLiteral(".markers.tmp");
		QString e;
		QElapsedTimer timer;
		timer.start();
		bool ok = remuxWithChapters(path, tmp, chapters, abort, &e);
		if (ok && !(QFile::remove(path) && QFile::rename(tmp, path))) {
			ok = false;
			e = tr("Could not replace %1.").arg(QDir::toNativeSeparators(path));
			QFile::remove(tmp);
		}
		qCInfo(pswrapRec) << "markers: remux" << (ok ? "ok" : "failed") << path << chapters.size() << "chapters" << timer.elapsed() << "ms" << e;
		if (abort.load())
			return;
		QMetaObject::invokeMethod(this, [this, path, ok, e]() {
			last_path = path;
			emit lastPathChanged();
			if (!ok)
				emit notice(tr("The recording was saved, but its markers couldn't be added: %1").arg(e), path);
			emit saved(path);
		}, Qt::QueuedConnection);
	});
}

bool PsWrapRecorder::startReplay(const PsWrapRecConfig &config, int seconds, QString *error)
{
	replay_seconds = std::clamp(seconds, 30, 120);
	if (replay_ring)
		return true;
	QString err;
	if (busy || !ensurePipeline(config, &err)) {
		if (err.isEmpty())
			err = tr("The recorder is busy.");
		replay_fault = true;
		qCWarning(pswrapRec) << "replay start failed:" << err;
		if (error)
			*error = err;
		return false;
	}
	replay_ring = std::make_shared<ReplayRing>(replay_seconds);
	replay_ring->layout = &impl->layout;
	attachSink(replay_ring);
	replay_active.storeRelease(1);
	qCInfo(pswrapRec) << "replay started:" << replay_seconds << "s" << description_value;
	emit replayActiveChanged();
	return true;
}

void PsWrapRecorder::stopReplay()
{
	if (!replay_ring)
		return;
	auto ring = std::move(replay_ring);
	replay_ring.reset();
	detachSink(ring.get());
	ring.reset(); // คืนแรมทันที (job ที่กำลังเซฟถือ ref ของตัวเอง)
	replay_active.storeRelease(0);
	qCInfo(pswrapRec) << "replay stopped";
	emit replayActiveChanged();
	stopPipelineIfUnused();
}

void PsWrapRecorder::setReplaySeconds(int seconds)
{
	replay_seconds = std::clamp(seconds, 30, 120);
	if (replay_ring)
		replay_ring->setSeconds(replay_seconds);
}

bool PsWrapRecorder::saveReplay(const QString &path, const QString &fallback_path)
{
	if (!replay_ring || !impl) {
		emit failed(tr("Instant Replay is off."));
		return false;
	}
	std::vector<AVPacket *> packets;
	std::vector<int64_t> markers;
	double span_s = 0.0;
	int64_t size = 0;
	auto streams = describe(impl->layout, true);
	if (streams.empty() || !replay_ring->snapshot(&packets, &markers, &span_s, &size)) {
		for (AVPacket *p : packets)
			av_packet_free(&p);
		emit failed(tr("The replay buffer is still empty."));
		return false;
	}
	qCInfo(pswrapRec).nospace() << "replay save: " << span_s << " s, " << double(size) / (1024.0 * 1024.0) << " MB, "
		<< packets.size() << " packets, " << markers.size() << " markers → " << path;
	// layout สำหรับ job: ชี้ par ที่ copy แล้ว (pipeline ปิดระหว่างเขียนได้)
	auto job_layout = std::make_shared<PsWrapEncodedLayout>();
	job_layout->count = static_cast<int>(streams.size());
	job_layout->fps = impl->layout.fps;
	for (int i = 0; i < job_layout->count; i++) {
		job_layout->streams[i] = impl->layout.streams[i];
		job_layout->streams[i].par = streams[i].par.get();
	}
	auto shared_packets = std::make_shared<std::vector<AVPacket *>>(std::move(packets));
	runJob([this, path, fallback_path, streams, job_layout, shared_packets, markers](const std::atomic<bool> &abort) {
		QString e, used = path;
		bool file_error = false;
		QDir().mkpath(QFileInfo(path).absolutePath());
		bool ok = writePacketsFile(path, *job_layout, streams, *shared_packets, markers, abort, &e, &file_error);
		bool fell_back = false;
		if (!ok && file_error && !fallback_path.isEmpty()
				&& QDir::cleanPath(QFileInfo(fallback_path).absolutePath()) != QDir::cleanPath(QFileInfo(path).absolutePath())) {
			QDir().mkpath(QFileInfo(fallback_path).absolutePath());
			QString e2;
			ok = writePacketsFile(fallback_path, *job_layout, streams, *shared_packets, markers, abort, &e2, &file_error);
			if (ok) {
				used = fallback_path;
				fell_back = true;
			}
		}
		for (AVPacket *p : *shared_packets)
			av_packet_free(&p);
		shared_packets->clear();
		qCInfo(pswrapRec) << "replay save" << (ok ? "ok" : "failed") << used << e;
		if (abort.load())
			return;
		const QString blocked_dir = QDir::toNativeSeparators(QFileInfo(path).absolutePath());
		const QString fallback_dir = QDir::toNativeSeparators(QFileInfo(fallback_path).absolutePath());
		QMetaObject::invokeMethod(this, [this, ok, used, e, fell_back, blocked_dir, fallback_dir]() {
			if (!ok) {
				emit failed(tr("Couldn't save the replay: %1").arg(e));
				return;
			}
			if (fell_back)
				emit notice(tr("Windows blocked saving to %1, so this replay is saved to %2. To keep using your folder, allow PS-WRAP in Windows Security → Ransomware protection → Allow an app, or pick another folder in Settings.")
					.arg(blocked_dir, fallback_dir), used);
			emit replaySaved(used);
		}, Qt::QueuedConnection);
	});
	return true;
}

bool PsWrapRecorder::addMarker()
{
	if (!impl || (!file_sink && !replay_ring)) {
		emit failed(tr("Start recording or turn on Instant Replay to add markers."));
		return false;
	}
	const int64_t t = nowUs() - impl->t0_us; // นาฬิกา pipeline
	int seconds = -1;
	if (file_sink) {
		file_sink->markers_us.push_back(t);
		seconds = static_cast<int>(std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - start_ms) / 1000);
	}
	if (replay_ring) {
		replay_ring->addMarker(t);
		if (seconds < 0)
			seconds = replay_ring->secondsAt(t);
	}
	qCInfo(pswrapRec) << "marker at" << seconds << "s" << (file_sink ? "(recording)" : "(replay)");
	emit markerAdded(seconds);
	return true;
}

bool PsWrapRecorder::addSink(const std::shared_ptr<PsWrapPacketSink> &sink)
{
	if (!impl || !sink || busy)
		return false;
	extra_sinks.push_back(sink);
	attachSink(sink);
	return true;
}

void PsWrapRecorder::removeSink(PsWrapPacketSink *sink)
{
	auto it = std::find_if(extra_sinks.begin(), extra_sinks.end(), [sink](const auto &s) { return s.get() == sink; });
	if (it == extra_sinks.end())
		return;
	auto keep = *it;
	extra_sinks.erase(it);
	detachSink(sink);
	keep->detached(true);
	stopPipelineIfUnused();
}

const PsWrapEncodedLayout *PsWrapRecorder::layout() const
{
	return impl ? &impl->layout : nullptr;
}

bool PsWrapRecorder::addTap(const PsWrapRecConfig &config, const std::shared_ptr<PsWrapRawTap> &tap, QString *error)
{
	QString err;
	if (!tap || busy) {
		err = tr("The recorder is busy.");
	} else if (ensurePipeline(config, &err)) {
		raw_taps.push_back(tap);
		{
			std::lock_guard<std::mutex> lock(impl->taps_mtx);
			impl->taps.push_back(tap);
		}
		qCInfo(pswrapRec) << "tap added:" << description_value << (raw_taps.size() > 1 ? "(more taps)" : "");
		return true;
	}
	qCWarning(pswrapRec) << "tap start failed:" << err;
	if (error)
		*error = err;
	return false;
}

void PsWrapRecorder::removeTap(PsWrapRawTap *tap)
{
	auto it = std::find_if(raw_taps.begin(), raw_taps.end(), [tap](const auto &t) { return t.get() == tap; });
	if (it == raw_taps.end())
		return;
	auto keep = *it;
	raw_taps.erase(it);
	if (impl) {
		std::lock_guard<std::mutex> lock(impl->taps_mtx); // หลังปล่อย lock worker ไม่เรียก tap นี้อีก
		impl->taps.erase(std::remove(impl->taps.begin(), impl->taps.end(), keep), impl->taps.end());
	}
	keep->detached();
	stopPipelineIfUnused();
}

int PsWrapRecorder::pipelineFps() const
{
	return impl ? impl->cfg.fps : 0;
}

void PsWrapRecorder::sinkFailed(PsWrapPacketSink *sink, const QString &message)
{
	if (file_sink && file_sink.get() == sink) {
		auto fs = std::move(file_sink);
		file_sink.reset();
		recording.storeRelease(0);
		tick_timer.stop();
		fs->reported = true;
		last_error = message;
		emit lastErrorChanged();
		emit failed(message);
		finishFile(fs, false); // ปิดไฟล์ให้เปิดได้ถึงจุดที่เขียนสำเร็จ
		emit recordingChanged();
		stopPipelineIfUnused();
		return;
	}
	auto it = std::find_if(extra_sinks.begin(), extra_sinks.end(), [sink](const auto &s) { return s.get() == sink; });
	if (it != extra_sinks.end()) {
		auto keep = *it;
		extra_sinks.erase(it);
		keep->detached(false);
		stopPipelineIfUnused();
	}
}

void PsWrapRecorder::workerFailed(const QString &message)
{
	last_error = message;
	if (replay_ring)
		replay_fault = true;
	emit lastErrorChanged();
	emit failed(message);
	shutdownPipeline();
}

void PsWrapRecorder::runJob(std::function<void(const std::atomic<bool> &)> work)
{
	auto job = std::make_unique<Job>();
	job->id = next_job_id++;
	Job *raw = job.get();
	jobs.push_back(std::move(job));
	raw->th = std::thread([this, raw, work = std::move(work)]() {
		work(raw->abort);
		const quint64 id = raw->id;
		QMetaObject::invokeMethod(this, [this, id]() { jobFinished(id); }, Qt::QueuedConnection);
	});
}

void PsWrapRecorder::jobFinished(quint64 id)
{
	auto it = std::find_if(jobs.begin(), jobs.end(), [id](const auto &j) { return j->id == id; });
	if (it == jobs.end())
		return;
	if ((*it)->th.joinable())
		(*it)->th.join();
	jobs.erase(it);
}

void PsWrapRecorder::shutdown()
{
	stopReplay();
	shutdownPipeline();
	for (auto &j : jobs)
		j->abort = true;
	for (auto &j : jobs)
		if (j->th.joinable())
			j->th.join();
	jobs.clear();
}

bool PsWrapRecorder::wantsVideoFrame(qint64 now_us)
{
	std::lock_guard<std::mutex> life(life_mutex);
	if (!isCapturing() || !impl)
		return false;
	Impl &p = *impl;
	constexpr qint64 tolerance_us = 3000; // render ที่ 165 Hz มาทุก ~6ms — ยอมเร็วกว่ากำหนดเล็กน้อย
	if (now_us + tolerance_us < p.next_due_us)
		return false;
	p.next_due_us += p.interval_us;
	if (now_us - p.next_due_us > p.interval_us)
		p.next_due_us = now_us + p.interval_us;
	return true;
}

bool PsWrapRecorder::videoSpec(int *width, int *height, bool *hdr) const
{
	std::lock_guard<std::mutex> life(life_mutex);
	if (!isCapturing() || !impl)
		return false;
	*width = impl->cfg.width;
	*height = impl->cfg.height;
	*hdr = impl->cfg.hdr;
	return true;
}

void PsWrapRecorder::pushVideoFrame(AVFrame *frame, qint64 capture_us)
{
	std::lock_guard<std::mutex> life(life_mutex);
	if (!impl) {
		av_frame_free(&frame);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(impl->mtx);
		if (impl->accepting && impl->vq.size() < kMaxQueuedVideo) {
			impl->vq.push_back(VideoItem{frame, capture_us});
			frame = nullptr;
		}
	}
	if (frame) {
		impl->video_dropped++;
		av_frame_free(&frame);
		return;
	}
	impl->cv.notify_one();
}

void PsWrapRecorder::tapGameAudio(const int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	PsWrapRecorder *r = instance();
	if (r && r->isCapturing())
		r->pushAudio(false, pcm, frames, channels, rate);
}

void PsWrapRecorder::tapMicAudio(const int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	PsWrapRecorder *r = instance();
	if (r && r->isCapturing())
		r->pushAudio(true, pcm, frames, channels, rate);
}

void PsWrapRecorder::pushAudio(bool mic, const int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	std::lock_guard<std::mutex> life(life_mutex); // shutdownPipeline reset impl ใต้ lock นี้ → ไม่มี use-after-free
	Impl *p = impl.get();
	if (!p || !pcm || frames == 0 || channels == 0)
		return;
	if (rate != static_cast<unsigned>(kAudioRate)) {
		if (!p->warned_rate.exchange(true))
			qCWarning(pswrapRec) << "unsupported audio rate" << rate << (mic ? "(mic)" : "(game)") << "- skipped";
		return;
	}
	AudioChunk c;
	c.mic = mic;
	c.arrival_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
	c.stereo.resize(frames * 2);
	constexpr float scale = 1.0f / 32768.0f;
	for (size_t i = 0; i < frames; i++) {
		const int16_t *s = pcm + i * channels;
		c.stereo[2 * i] = s[0] * scale;
		c.stereo[2 * i + 1] = (channels > 1 ? s[1] : s[0]) * scale;
	}
	std::lock_guard<std::mutex> lock(p->mtx); // ลำดับ lock: life_mutex → Impl::mtx เสมอ
	if (!p->accepting)
		return;
	if (p->aq.size() >= kMaxQueuedAudio) {
		p->audio_dropped++;
		return;
	}
	p->aq.push_back(std::move(c));
}
