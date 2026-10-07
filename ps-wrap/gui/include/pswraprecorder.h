// PS-WRAP: อัดวิดีโอระหว่างสตรีม — ภาพที่เห็นบนจอ (วิดีโอ + overlay QML ทั้งหมด) + เสียงเกม + เสียงไมค์
//
// ภาพ: render thread วาดเฟรมซ้ำลง texture YUV (ดู pswrapreccapture.h) แล้วส่ง AVFrame มาที่ pushVideoFrame()
// เสียง: StreamSession ส่ง PCM มาที่ tapGameAudio() / tapMicAudio() (เรียกได้จากทุก thread)
// encode ทั้งหมดอยู่ใน worker thread ของ "pipeline" — ไม่บล็อก thread รับสตรีม/render
//
// โครงสร้าง (2026-10-06): encoder pipeline เดียว → ผู้รับ packet ได้หลายตัว (PsWrapPacketSink)
//   - ไฟล์อัด (start/stop)           → MP4 fragmented (แอปตายกลางทางไฟล์ยังเปิดได้)
//   - Instant Replay (startReplay)   → ring ในแรม เก็บ packet ที่ encode แล้ว N วินาทีล่าสุด · saveReplay() เขียนเป็นไฟล์
//   - ผู้รับภายนอก (addSink)          → เช่น RTMP live ในอนาคต
// pipeline เปิดเมื่อมีผู้รับอย่างน้อย 1 ตัว และปิดเองเมื่อไม่เหลือใคร · สเปคภาพ (ขนาด/fps/HDR) คงที่ตลอดอายุ pipeline
//
// วิดีโอ SDR = H.264 NV12 BT.709, HDR = HEVC Main10 P010 PQ BT.2020 · GOP = fps×2 (2 วินาที)
// เสียง AAC 48 kHz 3 track: "Game + Mic" (default), "Game", "Mic"
// Marker (addMarker) → chapter ใน MP4 ("Start", "Marker 1", …) — ไฟล์อัดเป็น fragmented (moov เขียนตอนเริ่ม)
//   เลย remux แบบ stream copy ตอนหยุดอัด (เฉพาะไฟล์ที่มี marker) บน thread แยก · replay เขียนพร้อม chapter ทีเดียว

#ifndef PSWRAP_RECORDER_H
#define PSWRAP_RECORDER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QAtomicInteger>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

extern "C" {
#include <libavutil/rational.h>
struct AVFrame;
struct AVPacket;
struct AVCodecParameters;
}

struct PsWrapRecHdrInfo
{
	bool valid = false;
	// mastering display (CIE xy) + luminance (nits)
	float prim_r[2] = {0.708f, 0.292f}, prim_g[2] = {0.170f, 0.797f}, prim_b[2] = {0.131f, 0.046f}, white[2] = {0.3127f, 0.3290f};
	float min_luma = 0.0001f, max_luma = 1000.0f;
	float max_cll = 0.0f, max_fall = 0.0f;
};

struct PsWrapRecConfig
{
	QString path;      // ไฟล์อัด (startReplay ไม่ใช้)
	int width = 1920;
	int height = 1080;
	int fps = 60;
	bool hdr = false;
	PsWrapRecHdrInfo hdr_info;
};

// ---------------------------------------------------------------- packet sink (ผู้รับ packet ที่ encode แล้ว)
//
// รูปแบบ stream ของ pipeline — คงที่ตลอดอายุ pipeline (อ่านได้จาก GUI thread ระหว่าง pipeline ยังอยู่)
//   index 0 = วิดีโอ (time_base 1/1000 = ms) · 1..3 = AAC "Game + Mic" / "Game" / "Mic" (time_base 1/48000)
//   par มี extradata (global header: SPS/PPS/VPS, AudioSpecificConfig) + HDR side data (mastering/CLL) ใน coded_side_data
struct PsWrapEncodedStream
{
	AVCodecParameters *par = nullptr;   // pipeline เป็นเจ้าของ — sink ต้อง avcodec_parameters_copy ถ้าจะใช้หลัง pipeline จบ
	AVRational time_base = {1, 1000};
	const char *title = nullptr;        // ชื่อ track (nullptr = วิดีโอ)
	int disposition = 0;                // AV_DISPOSITION_*
	bool video = false;
};

struct PsWrapEncodedLayout
{
	static constexpr int kMaxStreams = 4;
	int count = 0;
	int fps = 60;
	PsWrapEncodedStream streams[kMaxStreams];
};

// สัญญาของ sink:
//   - addSink() (GUI thread) → pipeline ขอ keyframe ทันที (IDR) เพื่อให้ sink เริ่มได้เร็ว (NVENC/x264 ≤ 1 เฟรม, AMF ≤ 1 GOP)
//   - write() ถูกเรียกบน worker thread ของ pipeline เท่านั้น ทีละ packet ตามลำดับที่ encoder ปล่อย (ไม่ interleave ข้าม stream)
//     pkt->stream_index = index ใน layout · pts/dts อยู่ใน time_base ของ stream นั้น · นาฬิกา 0 = ตอน pipeline เริ่ม
//     (ใช้ PsWrapPacketRebaser ตัดให้เริ่มที่ keyframe + ย้ายเวลาเป็น 0) · AV_PKT_FLAG_KEY = keyframe ของวิดีโอ
//     ห้ามแก้ pkt — ต้องการเก็บ/ส่งต่อให้ av_packet_ref/clone เอง · ห้ามบล็อกนาน (encoder รอ) — งานช้าให้ทำ thread ตัวเอง
//     คืน false = sink พัง → pipeline ถอดออก แล้วเรียก detached(false) บน GUI thread
//   - removeSink() / pipeline ปิด → หลังจากนั้นไม่มี write() อีก แล้วเรียก detached(ok) บน GUI thread (ปิดไฟล์/socket ที่นี่)
//     pipeline ปิดเอง (สตรีมจบ) = encoder flush packet ที่ค้างเข้า sink ก่อน detached(true)
class PsWrapPacketSink
{
public:
	virtual ~PsWrapPacketSink() = default;
	virtual bool write(const AVPacket *pkt) = 0;   // worker thread
	virtual void detached(bool ok) { Q_UNUSED(ok); } // GUI thread
	virtual QString errorString() const { return QString(); }
};

// ---------------------------------------------------------------- raw tap (ภาพ/เสียง "ก่อน" encode)
//
// สำหรับ encoder ตัวที่ 2 ที่ต้องการสเปคต่างจากไฟล์อัด (เช่น Go Live: H.264 CBR 6 Mbps 16:9 — pswraplive.h)
// ใช้เฟรม/เสียงชุดเดียวกับ pipeline (render thread จับภาพครั้งเดียว) · นาฬิกาเดียวกัน (0 = ตอน pipeline เริ่ม)
//   - videoFrame(): worker thread · เฟรมที่กำลังจะเข้า encoder หลัก (NV12 SDR / P010 HDR) pts = ms
//     ห้ามบล็อก · จะเก็บต้อง av_frame_ref/clone เอง — เฟรมอ้าง slot ของ render thread (มี 6 slot) ต้องคืนเร็ว
//   - audioMix(): worker thread · เสียง "Game + Mic" (float planar L/R) n เฟรม เริ่มที่ sample pos (48 kHz)
//     ต่อเนื่องเสมอ (pipeline เติมเงียบเองตอนเกมเงียบ) · ห้ามบล็อก
//   - detached(): GUI thread · removeTap() หรือ pipeline ปิด (สตรีมจบ/encoder พัง) — หลังจากนี้ไม่มี callback อีก
class PsWrapRawTap
{
public:
	virtual ~PsWrapRawTap() = default;
	virtual void videoFrame(const AVFrame *frame) = 0;
	virtual void audioMix(const float *left, const float *right, int64_t frames, int64_t pos) = 0;
	virtual void detached() {}
};

// ตัดลำดับ packet ให้เริ่มที่ keyframe วิดีโอแรก แล้วย้ายเวลาทุก stream ให้ keyframe นั้น = 0
// เสียงที่มาก่อน keyframe (วิดีโอ encode ช้ากว่าเสียง) ถูกพักไว้แล้วปล่อยถ้าคาบเกี่ยวจุดเริ่ม
class PsWrapPacketRebaser
{
public:
	explicit PsWrapPacketRebaser(const PsWrapEncodedLayout *layout = nullptr) : layout(layout) {}
	~PsWrapPacketRebaser();
	void setLayout(const PsWrapEncodedLayout *l) { layout = l; }
	// รับ packet (clone เอง) → คืน packet ที่พร้อมเขียน (0..n ตัว เป็นเจ้าของ ต้อง av_packet_free) ใส่ out
	void push(const AVPacket *pkt, std::vector<AVPacket *> *out);
	bool started() const { return base_us >= 0; }
	int64_t baseUs() const { return base_us; } // เวลา (นาฬิกา pipeline, µs) ของ keyframe แรก · -1 = ยังไม่เจอ
	int64_t endUs() const { return end_us; }   // เวลาปลายสุดของ packet ที่ปล่อยแล้ว (นาฬิกา pipeline, µs)

private:
	const PsWrapEncodedLayout *layout = nullptr;
	int64_t base_us = -1;
	int64_t end_us = 0;
	std::vector<AVPacket *> pending_audio;
	bool accept(AVPacket *p);
};

class PsWrapRecorder : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
	Q_PROPERTY(bool busy READ isBusy NOTIFY recordingChanged)
	Q_PROPERTY(int seconds READ seconds NOTIFY secondsChanged)
	Q_PROPERTY(QString lastPath READ lastPath NOTIFY lastPathChanged)
	Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
	Q_PROPERTY(QString description READ description NOTIFY recordingChanged)   // เช่น "1920x1080 · HDR · hevc_nvenc"
	Q_PROPERTY(bool replayActive READ isReplayActive NOTIFY replayActiveChanged) // Instant Replay กำลังเก็บ buffer

public:
	// primary = ตัวหลัก (instance()) · false = ตัวที่ 2 สำหรับภาพแนวตั้ง 9:16 (secondary()) — เสียงเกม/ไมค์ป้อนให้ทั้งคู่
	explicit PsWrapRecorder(QObject *parent = nullptr, bool primary = true);
	~PsWrapRecorder() override;

	// ตัวหลักทั้งแอป (สร้างโดย QmlMainWindow) — StreamSession/render thread เข้าถึงผ่านตัวนี้
	static PsWrapRecorder *instance();
	static PsWrapRecorder *secondary();   // PS-WRAP: pipeline แนวตั้ง (อัดคลิป 9:16 / ไลฟ์แนวตั้ง) · nullptr = ไม่มี

	// ---- GUI thread: ไฟล์อัด
	// pipeline ทำงานอยู่แล้ว (replay) → ใช้ pipeline เดิม (ขนาด/fps ของ pipeline) ยกเว้น SDR/HDR ไม่ตรง = เริ่ม pipeline ใหม่ (buffer replay หาย)
	bool start(const PsWrapRecConfig &config, QString *error);
	void stop();   // ปิดไฟล์ · pipeline ยังเดินต่อถ้า replay/sink อื่นยังใช้ · saved(path) อาจมาทีหลัง (remux marker)

	// ---- GUI thread: Instant Replay
	bool startReplay(const PsWrapRecConfig &config, int seconds, QString *error);   // config.path ไม่ใช้
	void stopReplay();
	void setReplaySeconds(int seconds);   // 30..120 · มีผลทันทีกับ buffer ที่เดินอยู่
	// เขียน buffer ลงไฟล์บน thread แยก → replaySaved(path) หรือ failed(msg) · เขียน path ไม่ได้ → ลอง fallback_path (+ notice)
	bool saveReplay(const QString &path, const QString &fallback_path);
	bool replayFaulted() const { return replay_fault; } // replay เริ่ม/เดินไม่ได้ — อย่าลองซ้ำจนกว่า clearReplayFault()
	void clearReplayFault() { replay_fault = false; }

	// ---- GUI thread: marker ณ ตอนนี้ (ไฟล์อัด และ/หรือ replay) → markerAdded(seconds) · ไม่มีอะไรเดินอยู่ = false
	bool addMarker();

	// ---- GUI thread: ผู้รับภายนอก (เช่น RTMP) — ต้องมี pipeline เดินอยู่ (ดู layout()) · ถือ shared_ptr ไว้จนกว่า detached()
	bool addSink(const std::shared_ptr<PsWrapPacketSink> &sink);
	void removeSink(PsWrapPacketSink *sink);
	const PsWrapEncodedLayout *layout() const;   // nullptr = ไม่มี pipeline · ใช้ได้จนกว่า pipeline ปิด

	// ---- GUI thread: raw tap (encoder ตัวที่ 2 เช่น Go Live) — เปิด pipeline ให้ถ้ายังไม่มี (config ใช้เฉพาะตอนเปิดใหม่)
	// pipeline มีอยู่แล้ว = ใช้สเปคเดิม (ดู videoSpec) · ถือ shared_ptr ไว้จนกว่า detached()
	// pipeline ปิดเองเมื่อไม่เหลือผู้ใช้ (ไฟล์/replay/sink/tap)
	bool addTap(const PsWrapRecConfig &config, const std::shared_ptr<PsWrapRawTap> &tap, QString *error);
	void removeTap(PsWrapRawTap *tap);
	int pipelineFps() const;   // 0 = ไม่มี pipeline

	// ---- GUI thread: ปิดแอป — หยุดทุกอย่างทันที รอ thread ทั้งหมดจบ (remux ที่ค้างถูกยกเลิก ไฟล์เดิมยังอยู่)
	void shutdown();

	bool isRecording() const { return recording.loadAcquire() != 0; }
	bool isReplayActive() const { return replay_active.loadAcquire() != 0; }
	bool isCapturing() const { return capturing.loadAcquire() != 0; } // pipeline เดินอยู่ = render thread ต้องจับภาพ
	bool isBusy() const { return busy; }
	int seconds() const { return seconds_value; }
	QString lastPath() const { return last_path; }
	QString lastError() const { return last_error; }
	QString description() const { return description_value; }
	bool lastStartFileError() const { return start_file_error; }   // start() ล้มเพราะเขียนไฟล์/โฟลเดอร์ไม่ได้ (เช่น Controlled folder access)

	// render thread: ถึงเวลาจับเฟรมถัดไปหรือยัง (คุม fps ของ pipeline ไม่ผูกกับ refresh rate จอ)
	bool wantsVideoFrame(qint64 now_us);
	// render thread: รับ AVFrame (NV12/P010) ไปเป็นเจ้าของ — data ต้องอ้าง AVBuffer ที่ปล่อย slot ตอน unref
	void pushVideoFrame(AVFrame *frame, qint64 capture_us);
	// สเปคที่ render thread ต้องวาด (คงที่ตลอด pipeline) · false = ไม่มี pipeline
	bool videoSpec(int *width, int *height, bool *hdr) const;

	// thread ใดก็ได้ — PCM s16 interleaved
	static void tapGameAudio(const int16_t *pcm, size_t frames, unsigned channels, unsigned rate);
	static void tapMicAudio(const int16_t *pcm, size_t frames, unsigned channels, unsigned rate);

signals:
	void recordingChanged();
	void secondsChanged();
	void lastPathChanged();
	void lastErrorChanged();
	void saved(const QString &path);
	void failed(const QString &message);
	void notice(const QString &message, const QString &path);   // ข้อมูลที่ผู้ใช้ควรรู้ (เช่น ย้ายไปเซฟโฟลเดอร์สำรอง)
	void replayActiveChanged();
	void replaySaved(const QString &path);
	void markerAdded(int seconds);   // วินาทีในไฟล์อัด (ถ้ากำลังอัด) ไม่งั้นวินาทีใน buffer replay

private:
	struct Impl;
	class FileSink;
	class ReplayRing;
	struct Job;
	std::unique_ptr<Impl> impl;
	mutable std::mutex life_mutex; // คุมอายุ impl: thread เสียง/render แตะ impl ใต้ lock นี้ · GUI reset ใต้ lock นี้
	QAtomicInteger<int> recording{0};
	QAtomicInteger<int> replay_active{0};
	QAtomicInteger<int> capturing{0};
	bool busy = false;
	bool replay_fault = false;
	int replay_seconds = 60;
	int seconds_value = 0;
	QString last_path;
	QString last_error;
	QString description_value;
	bool start_file_error = false;
	QTimer tick_timer;
	qint64 start_ms = 0;
	std::shared_ptr<FileSink> file_sink;
	std::shared_ptr<ReplayRing> replay_ring;
	std::vector<std::shared_ptr<PsWrapPacketSink>> extra_sinks;
	std::vector<std::shared_ptr<PsWrapRawTap>> raw_taps;   // GUI thread (worker เห็นสำเนาใน Impl::taps)
	std::vector<std::unique_ptr<Job>> jobs;
	quint64 next_job_id = 1;

	bool ensurePipeline(const PsWrapRecConfig &config, QString *error);
	void shutdownPipeline();          // flush encoder → sink ทุกตัว → detached
	void stopPipelineIfUnused();
	void attachSink(const std::shared_ptr<PsWrapPacketSink> &sink);
	bool detachSink(PsWrapPacketSink *sink); // true = ถอดสำเร็จ (worker จะไม่เรียก write อีก)
	void finishFile(const std::shared_ptr<FileSink> &sink, bool flushed);
	void runJob(std::function<void(const std::atomic<bool> &abort)> work);
	void jobFinished(quint64 id);
	void pushAudio(bool mic, const int16_t *pcm, size_t frames, unsigned channels, unsigned rate);
	void workerFailed(const QString &message);
	void sinkFailed(PsWrapPacketSink *sink, const QString &message);
};

#endif // PSWRAP_RECORDER_H
