// PS-WRAP: อัดวิดีโอระหว่างสตรีม — ภาพที่เห็นบนจอ (วิดีโอ + overlay QML ทั้งหมด) + เสียงเกม + เสียงไมค์
//
// ภาพ: render thread วาดเฟรมซ้ำลง texture YUV (ดู pswrapreccapture.h) แล้วส่ง AVFrame มาที่ pushVideoFrame()
// เสียง: StreamSession ส่ง PCM มาที่ tapGameAudio() / tapMicAudio() (เรียกได้จากทุก thread)
// encode + เขียนไฟล์ทั้งหมดอยู่ใน worker thread ของ recorder — ไม่บล็อก thread รับสตรีม/render
//
// ไฟล์: MP4 แบบ fragmented (แอปตายกลางทางไฟล์ยังเปิดได้) · วิดีโอ SDR = H.264 NV12 BT.709, HDR = HEVC Main10 P010 PQ BT.2020
// เสียง AAC 48 kHz 3 track: "Game + Mic" (default), "Game", "Mic"

#ifndef PSWRAP_RECORDER_H
#define PSWRAP_RECORDER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QAtomicInteger>

#include <memory>
#include <mutex>

extern "C" {
struct AVFrame;
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
	QString path;
	int width = 1920;
	int height = 1080;
	int fps = 60;
	bool hdr = false;
	PsWrapRecHdrInfo hdr_info;
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

public:
	explicit PsWrapRecorder(QObject *parent = nullptr);
	~PsWrapRecorder() override;

	// ตัวเดียวทั้งแอป (สร้างโดย QmlMainWindow) — StreamSession/render thread เข้าถึงผ่านตัวนี้
	static PsWrapRecorder *instance();

	// GUI thread
	bool start(const PsWrapRecConfig &config, QString *error);
	void stop();

	bool isRecording() const { return recording.loadAcquire() != 0; }
	bool isBusy() const { return busy; }
	int seconds() const { return seconds_value; }
	QString lastPath() const { return last_path; }
	QString lastError() const { return last_error; }
	QString description() const { return description_value; }

	// render thread: ถึงเวลาจับเฟรมถัดไปหรือยัง (คุม fps ของไฟล์ ไม่ผูกกับ refresh rate จอ)
	bool wantsVideoFrame(qint64 now_us);
	// render thread: รับ AVFrame (NV12/P010) ไปเป็นเจ้าของ — data ต้องอ้าง AVBuffer ที่ปล่อย slot ตอน unref
	void pushVideoFrame(AVFrame *frame, qint64 capture_us);
	// สเปคที่ render thread ต้องวาด (คงที่ตลอดไฟล์)
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

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
	mutable std::mutex life_mutex; // คุมอายุ impl: thread เสียง/render แตะ impl ใต้ lock นี้ · GUI reset ใต้ lock นี้
	QAtomicInteger<int> recording{0};
	bool busy = false;
	int seconds_value = 0;
	QString last_path;
	QString last_error;
	QString description_value;
	QTimer tick_timer;
	qint64 start_ms = 0;

	void pushAudio(bool mic, const int16_t *pcm, size_t frames, unsigned channels, unsigned rate);
	void workerFailed(const QString &message);
};

#endif // PSWRAP_RECORDER_H
