// PS-WRAP: วัดเสียงไมค์สำหรับ overlay spectrum (ดู pswrapmicmeter.h)
#include <pswrapmicmeter.h>
#include <pswrapvoiceproc.h>

#include <QAtomicPointer>
#include <QMutexLocker>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <complex>

namespace {

QAtomicPointer<PsWrapMicMeter> g_meter;

constexpr float kSampleRate = 48000.0f;
constexpr float kMinHz = 60.0f;
constexpr float kMaxHz = 12000.0f;
constexpr float kFloorDb = -72.0f; // ต่ำกว่านี้ = 0
constexpr float kCeilDb = -12.0f;  // สูงกว่านี้ = 1
constexpr qint64 kLiveTimeoutMs = 300;

// FFT radix-2 แบบ in-place (n เป็นกำลังสอง)
void fft(std::vector<std::complex<float>> &a)
{
	const size_t n = a.size();
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const float ang = -2.0f * float(M_PI) / float(len);
		const std::complex<float> wl(std::cos(ang), std::sin(ang));
		for (size_t i = 0; i < n; i += len) {
			std::complex<float> w(1.0f, 0.0f);
			for (size_t k = 0; k < len / 2; k++) {
				const std::complex<float> u = a[i + k];
				const std::complex<float> v = a[i + k + len / 2] * w;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}

} // namespace

PsWrapMicMeter::PsWrapMicMeter(QObject *parent)
	: QObject(parent)
	, ring(kFft, 0.0f)
	, window(kFft)
{
	for (int i = 0; i < kFft; i++)
		window[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (kFft - 1)); // Hann
	// แถบ log-spaced → ช่วง bin [lo, hi]
	const float bin_hz = kSampleRate / kFft;
	for (int b = 0; b < kBands; b++) {
		const float f0 = kMinHz * std::pow(kMaxHz / kMinHz, float(b) / kBands);
		const float f1 = kMinHz * std::pow(kMaxHz / kMinHz, float(b + 1) / kBands);
		band_lo[b] = std::max(1, int(std::floor(f0 / bin_hz)));
		band_hi[b] = std::max(band_lo[b], int(std::ceil(f1 / bin_hz)) - 1);
	}
	for (int b = 0; b < kBands; b++)
		bands_out.append(0.0);
	for (int i = 0; i < kWavePoints; i++)
		wave_out.append(0.0);
	timer.setInterval(16);
	timer.setTimerType(Qt::PreciseTimer);
	connect(&timer, &QTimer::timeout, this, &PsWrapMicMeter::onTick);
	g_meter.storeRelease(this);
}

PsWrapMicMeter::~PsWrapMicMeter()
{
	g_meter.testAndSetOrdered(this, nullptr);
}

PsWrapMicMeter *PsWrapMicMeter::instance()
{
	return g_meter.loadAcquire();
}

void PsWrapMicMeter::tap(const int16_t *pcm, size_t frames, unsigned channels)
{
	if (PsWrapMicMeter *m = instance())
		m->push(pcm, frames, channels);
}

void PsWrapMicMeter::setEnabled(bool on)
{
	if (on == enabled_flag)
		return;
	{
		QMutexLocker locker(&mutex);
		enabled_flag = on;
	}
	if (on) {
		timer.start();
	} else {
		timer.stop();
		std::fill(std::begin(smooth_bands), std::end(smooth_bands), 0.0f);
		smooth_level = peak_value = 0.0f;
		if (live_out) {
			live_out = false;
			emit liveChanged();
		}
	}
	emit enabledChanged();
}

void PsWrapMicMeter::push(const int16_t *pcm, size_t frames, unsigned channels)
{
	if (!pcm || channels == 0)
		return;
	QMutexLocker locker(&mutex);
	if (!enabled_flag)
		return;
	for (size_t i = 0; i < frames; i++) {
		float v = 0.0f;
		for (unsigned c = 0; c < std::min(channels, 2u); c++)
			v += pcm[i * channels + c];
		ring[ring_pos] = v / (32768.0f * std::min(channels, 2u));
		ring_pos = (ring_pos + 1) % kFft;
	}
	new_samples += static_cast<int>(frames);
	last_data.start();
	if (new_samples >= kFft / 2) { // hop 512 = ~10.7ms
		new_samples = 0;
		analyzeLocked();
	}
}

void PsWrapMicMeter::analyzeLocked()
{
	std::vector<std::complex<float>> buf(kFft);
	double sq = 0.0;
	for (int i = 0; i < kFft; i++) {
		const float s = ring[(ring_pos + i) % kFft];
		sq += double(s) * s;
		buf[i] = std::complex<float>(s * window[i], 0.0f);
	}
	fft(buf);
	// Hann ลดพลังงาน ~ครึ่ง → ชดเชย ×2 · normalize ด้วย n/2
	const float norm = 2.0f * 2.0f / kFft;
	for (int b = 0; b < kBands; b++) {
		float mx = 0.0f;
		for (int k = band_lo[b]; k <= band_hi[b] && k < kFft / 2; k++)
			mx = std::max(mx, std::abs(buf[k]) * norm);
		const float db = 20.0f * std::log10(mx + 1e-9f);
		// ความถี่สูงพลังงานน้อยโดยธรรมชาติ — เอียงขึ้น ~+3 dB/octave ให้ภาพสมดุล
		const float tilt = 3.0f * std::log2(std::max(1.0f, (kMinHz * std::pow(kMaxHz / kMinHz, (b + 0.5f) / kBands)) / 500.0f));
		target_bands[b] = std::clamp((db + tilt - kFloorDb) / (kCeilDb - kFloorDb), 0.0f, 1.0f);
	}
	const float rms = float(std::sqrt(sq / kFft));
	const float rms_db = 20.0f * std::log10(rms + 1e-9f);
	target_level = std::clamp((rms_db + 60.0f) / 54.0f, 0.0f, 1.0f); // -60..-6 dBFS
	has_new = true;
}

void PsWrapMicMeter::onTick()
{
	float tb[kBands];
	float tl = 0.0f;
	bool is_live = false;
	QVariantList w;
	w.reserve(kWavePoints);
	{
		QMutexLocker locker(&mutex);
		is_live = last_data.isValid() && last_data.elapsed() < kLiveTimeoutMs;
		has_new = false;
		std::copy(std::begin(target_bands), std::end(target_bands), tb);
		tl = target_level;
		// waveform: peak แบบมีเครื่องหมายของ 64 ช่วงจาก 1024 ตัวล่าสุด
		const int per = kFft / kWavePoints;
		for (int p = 0; p < kWavePoints; p++) {
			float best = 0.0f;
			for (int i = 0; i < per; i++) {
				const float s = ring[(ring_pos + p * per + i) % kFft];
				if (std::fabs(s) > std::fabs(best))
					best = s;
			}
			w.append(is_live ? std::clamp(best * 2.5, -1.0, 1.0) : 0.0);
		}
	}
	if (!is_live) {
		std::fill(std::begin(tb), std::end(tb), 0.0f);
		tl = 0.0f;
	}
	for (int b = 0; b < kBands; b++) {
		const float target = tb[b];
		const float k = target > smooth_bands[b] ? 0.55f : 0.12f; // attack เร็ว release ช้า
		smooth_bands[b] += (target - smooth_bands[b]) * k;
		if (smooth_bands[b] < 0.002f)
			smooth_bands[b] = 0.0f;
		bands_out[b] = double(smooth_bands[b]);
	}
	smooth_level += (tl - smooth_level) * (tl > smooth_level ? 0.5f : 0.15f);
	peak_value = std::max(smooth_level, peak_value - 0.008f);
	level_out = smooth_level;
	peak_out = peak_value;
	wave_out = w;
	if (is_live != live_out) {
		live_out = is_live;
		emit liveChanged();
	}
	// สถานะ noise gate ของเฟรมไมค์ล่าสุด (PsWrapVoiceProc) — ไม่มีเสียงเข้า = ถือว่าปิด
	gate_open_out = is_live && PsWrapVoiceProc::gateOpenNow();
	emit updated();
}
