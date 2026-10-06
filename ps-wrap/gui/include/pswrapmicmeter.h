// PS-WRAP: วัดเสียงไมค์สำหรับ overlay spectrum (MicSpectrumOverlay.qml)
// รับ PCM ไมค์ตัวเดียวกับที่ส่งเข้า PS5 (หลัง speex) → FFT 1024 จุด → 32 แถบ log 60 Hz–12 kHz + waveform 64 จุด
// คำนวณเฉพาะตอน enabled (QML ตั้งตอน overlay แสดง) · ส่งค่าให้ QML ~60 Hz ผ่าน updated()

#ifndef PSWRAP_MICMETER_H
#define PSWRAP_MICMETER_H

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QMutex>
#include <QElapsedTimer>

#include <vector>

class PsWrapMicMeter : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QVariantList bands READ bands NOTIFY updated)
	Q_PROPERTY(QVariantList wave READ wave NOTIFY updated)
	Q_PROPERTY(qreal level READ level NOTIFY updated)
	Q_PROPERTY(qreal peak READ peak NOTIFY updated)
	Q_PROPERTY(bool live READ live NOTIFY liveChanged)
	Q_PROPERTY(bool gateOpen READ gateOpen NOTIFY updated)   // noise gate ของไมค์เปิดอยู่ (มีเสียงผ่าน) — อัปเดตเฉพาะตอน enabled
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
	static constexpr int kBands = 32;
	static constexpr int kWavePoints = 64;

	explicit PsWrapMicMeter(QObject *parent = nullptr);
	~PsWrapMicMeter() override;

	static PsWrapMicMeter *instance();
	// thread ใดก็ได้ — PCM s16 interleaved 48 kHz
	static void tap(const int16_t *pcm, size_t frames, unsigned channels);

	QVariantList bands() const { return bands_out; }
	QVariantList wave() const { return wave_out; }
	qreal level() const { return level_out; }
	qreal peak() const { return peak_out; }
	bool live() const { return live_out; }
	bool gateOpen() const { return gate_open_out; }
	bool enabled() const { return enabled_flag; }
	void setEnabled(bool on);

signals:
	void updated();
	void liveChanged();
	void enabledChanged();

private:
	static constexpr int kFft = 1024;

	QMutex mutex;                 // คุม ring + analysis ด้านล่าง (tap อาจมาจาก thread อื่น)
	std::vector<float> ring;      // mono ล่าสุด kFft ตัว
	int ring_pos = 0;
	int new_samples = 0;
	bool has_new = false;
	float target_bands[kBands] = {};
	float target_level = 0.0f;
	QElapsedTimer last_data;
	bool enabled_flag = false;

	// GUI thread
	QTimer timer;
	float smooth_bands[kBands] = {};
	float smooth_level = 0.0f;
	float peak_value = 0.0f;
	QVariantList bands_out;
	QVariantList wave_out;
	qreal level_out = 0.0;
	qreal peak_out = 0.0;
	bool live_out = false;
	bool gate_open_out = false;
	int band_lo[kBands] = {};
	int band_hi[kBands] = {};
	std::vector<float> window;

	void push(const int16_t *pcm, size_t frames, unsigned channels);
	void analyzeLocked();
	void onTick();
};

#endif // PSWRAP_MICMETER_H
