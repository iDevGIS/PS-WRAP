// PS-WRAP: ลดเสียงรบกวน + ตัดเสียงลำโพงย้อน (ดู pswrapvoiceproc.h)
#include <pswrapvoiceproc.h>

#include <QDebug>
#include <QMutexLocker>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

#if CHIAKI_GUI_ENABLE_SPEEX
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#endif
#ifdef PSWRAP_HAVE_RNNOISE
#include <rnnoise.h>
#endif

namespace {

std::atomic<int> g_gen{0};
std::atomic<bool> g_enabled{false};
std::atomic<int> g_noise_db{0};
std::atomic<int> g_echo_db{0};
// gain/gate — อ่านทุกเฟรม (ไม่ผ่าน g_gen: ไม่ต้องสร้าง state ใหม่ และต้องทำงานแม้ build ไม่มี speex)
std::atomic<float> g_gain_db{0.0f};
std::atomic<bool> g_gate_on{false};
std::atomic<float> g_gate_db{-50.0f};
std::atomic<bool> g_gate_open{true};

// ---- ค่าคงที่ของ PsWrapMicDynamics (48 kHz) ----
constexpr float kDynRate = float(PsWrapMicDynamics::kRate);
const float kGainCoef = 1.0f - std::exp(-1.0f / (0.015f * kDynRate));   // gain smoothing τ 15 ms
// gate วัดแบบ RMS (τ 10 ms) — peak ของ noise สูงกว่า rms ~13 dB จะทำให้ gate เปิดค้างเพราะ noise เอง · RMS ตรงกับ level ที่ meter โชว์
const float kEnvCoef = 1.0f - std::exp(-1.0f / (0.010f * kDynRate));
constexpr int kGateHold = PsWrapMicDynamics::kRate * 150 / 1000;         // ค้างเปิด 150 ms หลังเสียงหาย
constexpr float kGateAttackStep = 1.0f / (0.005f * kDynRate);            // เปิด 0→1 ใน 5 ms
constexpr float kGateReleaseStep = 1.0f / (0.100f * kDynRate);           // ปิด 1→0 ใน 100 ms
constexpr float kGateHysteresisDb = 4.0f;                                 // ปิดเมื่อต่ำกว่า threshold − 4 dB (กันกระพือ)
constexpr float kLimCeil = 0.8913f;                                       // −1 dBFS
const float kLimRelease = 1.0f - std::exp(-1.0f / (0.080f * kDynRate)); // limiter คืนตัว τ 80 ms

inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

constexpr int kTail = PsWrapVoiceProc::kRate * 300 / 1000;    // filter AEC 300 ms (upstream 100 ms)
constexpr int64_t kMargin = PsWrapVoiceProc::kRate * 60 / 1000;   // อ่าน ref "ล่วงหน้า" 60 ms (AEC ต้องได้ ref ก่อนเสียงย้อน = causal) → ทน error −60…+240 ms
constexpr double kResync = PsWrapVoiceProc::kRate * 40 / 1000.0;  // error เฉลี่ยเกิน 40 ms ค่อยขยับ read pointer
constexpr int64_t kHardJump = PsWrapVoiceProc::kRate * 100 / 1000; // anchor วัดได้ต่างจากที่คาดเกิน 100 ms = เกิด underflow/clear คิว → ตั้งใหม่
constexpr int64_t kStaleNs = 300LL * 1000 * 1000;                 // ไม่มีเสียงออกนานเกินนี้ = ลำโพงเงียบ ไม่ต้องตัด echo
constexpr size_t kRing = PsWrapVoiceProc::kRate;                   // reference ย้อนหลัง 1 วินาที
constexpr int kRnnDelay = PsWrapVoiceProc::kFrame * 2;            // หน่วงของ RNNoise 0.2 (วัดจริงด้วย cross-correlation = 960)

int64_t nowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

void PsWrapMicDynamics::reset()
{
	gain = 1.0f;
	gate = 1.0f;
	env = 0.0f;
	hold = 0;
	open = true;
	lim = 1.0f;
}

void PsWrapMicDynamics::process(int16_t *pcm, int frames, int channels, float gain_db, bool gate_on, float gate_threshold_db)
{
	if (!pcm || frames <= 0 || channels <= 0)
		return;
	const float target = dbToLin(std::clamp(gain_db, -12.0f, 24.0f));
	// ค่าเริ่มต้น (0 dB, gate ปิด) และนิ่งแล้ว = ไม่แตะ PCM เลย (bit-exact เหมือนก่อนมีฟีเจอร์นี้)
	if (!gate_on && target == 1.0f && gain == 1.0f && gate == 1.0f && lim == 1.0f) {
		open = true;
		hold = 0;
		env = 0.0f;
		return;
	}
	const float open_lin = dbToLin(std::clamp(gate_threshold_db, -80.0f, -20.0f));
	const float close_lin = open_lin * dbToLin(-kGateHysteresisDb);
	for (int i = 0; i < frames; i++) {
		int16_t *s = pcm + size_t(i) * size_t(channels);
		// gain แบบ de-zipper (snap เมื่อห่างไม่ถึง 0.01 dB ไม่งั้น float ไล่ไม่ถึงเป้า)
		gain += (target - gain) * kGainCoef;
		if (std::fabs(target - gain) < 1e-3f * target)
			gain = target;
		int peak_i = 0;
		for (int c = 0; c < channels; c++)
			peak_i = std::max(peak_i, std::abs(int(s[c])));
		const float peak = float(peak_i) * (gain / 32768.0f);   // ระดับหลัง gain (0..~16)
		env += (peak * peak - env) * kEnvCoef;                   // mean-square หลัง gain

		// gate: เปิดเมื่อ RMS ≥ threshold · เปิดอยู่ = ค้างตราบที่ ≥ threshold−4 dB แล้วนับ hold 150 ms ค่อยปิด
		if (gate_on) {
			const float lvl = open ? close_lin : open_lin;
			if (env >= lvl * lvl) {
				open = true;
				hold = kGateHold;
			} else if (hold > 0) {
				hold--;
			} else {
				open = false;
			}
		} else {
			open = true;
			hold = 0;
		}
		const float gate_target = open ? 1.0f : 0.0f;
		if (gate < gate_target)
			gate = std::min(gate_target, gate + kGateAttackStep);
		else if (gate > gate_target)
			gate = std::max(gate_target, gate - kGateReleaseStep);

		// limiter: attack ทันที (sample นี้ไม่เกินเพดานแน่นอน) · คืนตัวช้า → ไม่บิดรูปคลื่นแบบ hard-clip
		lim += (1.0f - lim) * kLimRelease;
		if (lim > 0.9999f)
			lim = 1.0f;
		const float out_peak = peak * gate;
		if (out_peak * lim > kLimCeil)
			lim = kLimCeil / out_peak;

		const float total = gain * gate * lim;
		for (int c = 0; c < channels; c++) {
			const long v = std::lrint(float(s[c]) * total);
			s[c] = int16_t(std::clamp(v, -32768L, 32767L));
		}
	}
}

void PsWrapVoiceProc::setDynamics(float gain_db, bool gate_on, float gate_threshold_db)
{
	g_gain_db.store(std::clamp(gain_db, -12.0f, 24.0f), std::memory_order_relaxed);
	g_gate_db.store(std::clamp(gate_threshold_db, -80.0f, -20.0f), std::memory_order_relaxed);
	g_gate_on.store(gate_on, std::memory_order_relaxed);
}

bool PsWrapVoiceProc::gateOpenNow()
{
	return g_gate_open.load(std::memory_order_relaxed);
}

PsWrapVoiceProc::PsWrapVoiceProc()
	: rnn_in(kFrame, 0.0f)
	, rnn_out(kFrame, 0.0f)
	, dry_delay(kRnnDelay + kFrame, 0)
	, ref_frame(kFrame, 0)
	, out_frame(kFrame, 0)
	, ring(kRing, 0)
{
}

PsWrapVoiceProc::~PsWrapVoiceProc()
{
#if CHIAKI_GUI_ENABLE_SPEEX
	if (pre)
		speex_preprocess_state_destroy(pre);
	if (echo)
		speex_echo_state_destroy(echo);
#endif
#ifdef PSWRAP_HAVE_RNNOISE
	if (rnn)
		rnnoise_destroy(rnn);
#endif
}

bool PsWrapVoiceProc::available()
{
#if CHIAKI_GUI_ENABLE_SPEEX
	return true;
#else
	return false;
#endif
}

void PsWrapVoiceProc::setParams(bool enabled, int noise_db, int echo_db)
{
	g_enabled.store(enabled, std::memory_order_relaxed);
	g_noise_db.store(std::clamp(noise_db, 0, 60), std::memory_order_relaxed);
	g_echo_db.store(std::clamp(echo_db, 0, 60), std::memory_order_relaxed);
	g_gen.fetch_add(1, std::memory_order_release);
}

bool PsWrapVoiceProc::hasRnnoise()
{
#if defined(PSWRAP_HAVE_RNNOISE) && CHIAKI_GUI_ENABLE_SPEEX
	return true;
#else
	return false;
#endif
}

bool PsWrapVoiceProc::enabled()
{
	return available() && g_enabled.load(std::memory_order_relaxed);
}

void PsWrapVoiceProc::applyParamsIfChanged()
{
#if CHIAKI_GUI_ENABLE_SPEEX
	const int gen = g_gen.load(std::memory_order_acquire);
	if (gen == applied_gen)
		return;
	applied_gen = gen;
	const bool en = g_enabled.load(std::memory_order_relaxed);
	const int noise_db = g_noise_db.load(std::memory_order_relaxed);
	const int echo_db = g_echo_db.load(std::memory_order_relaxed);

	if (en && !pre) {
		echo = speex_echo_state_init(kFrame, kTail);
		int rate = kRate;
		speex_echo_ctl(echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate); // upstream ไม่ได้ตั้ง → speex คิดว่า 8 kHz
		pre = speex_preprocess_state_init(kFrame, kRate);
	}
	if (en) {
		// ลดเสียงรบกวน: RNNoise (ดีกว่ามาก ออกแบบมาที่ 48 kHz/480 พอดี) · ไม่มีค่อยใช้ speex denoise (ที่ 48 kHz ลดได้แค่ ~7 dB)
		rnn_on = false;
#ifdef PSWRAP_HAVE_RNNOISE
		if (noise_db > 0) {
			if (!rnn) {
				rnn = rnnoise_create(nullptr);
				std::fill(dry_delay.begin(), dry_delay.end(), int16_t(0));
			}
			rnn_on = rnn != nullptr;
			rnn_dry = std::pow(10.0f, -float(noise_db) / 20.0f);
		}
#endif
		int denoise = (noise_db > 0 && !rnn_on) ? 1 : 0;
		int noise = -noise_db;
		speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
		speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noise);
		const bool aec = echo_db > 0;
		if (aec) {
			int sup = -echo_db;
			int sup_active = -std::max(echo_db / 2, 1); // ตอนเราพูดทับ กดน้อยกว่า กันเสียงพูดขาด
			speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &sup);
			speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, &sup_active);
			speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_ECHO_STATE, echo);
		} else {
			speex_preprocess_ctl(pre, SPEEX_PREPROCESS_SET_ECHO_STATE, nullptr);
		}
		if (aec && (!aec_on || !on))
			aec_needs_reset = true;
		aec_on = aec;
		pre_on = aec || denoise;
	}
	if (en != on)
		qInfo().nospace() << "PSWRAP voice: " << (en ? "on" : "off");
	if (en)
		qInfo().nospace() << "PSWRAP voice: noise -" << noise_db << " dB (" << (rnn_on ? "rnnoise" : "speex") << "), echo " << (echo_db > 0 ? QStringLiteral("-%1 dB").arg(echo_db) : QStringLiteral("off"));
	on = en;
#endif
}

void PsWrapVoiceProc::pushPlayback(const int16_t *pcm, size_t frames, unsigned channels, int64_t pending_frames)
{
	if (!pcm || !frames || !channels || !enabled())
		return;
	QMutexLocker locker(&ref_mutex);
	for (size_t i = 0; i < frames; i++) {
		int sum = 0;
		for (unsigned c = 0; c < channels; c++)
			sum += pcm[i * channels + c];
		ring[size_t(write_idx % int64_t(kRing))] = int16_t(sum / int(channels));
		write_idx++;
	}
	// sample ที่กำลังออกลำโพงตอนนี้ — วัดจากขนาดคิวซึ่งขยับเป็นขั้นทีละ period ของอุปกรณ์ → กรองแบบ PLL
	const int64_t now = nowNs();
	const int64_t measured = write_idx - pending_frames;
	if (have_anchor) {
		const int64_t predicted = anchor_idx + (now - anchor_ns) * kRate / 1000000000LL;
		const int64_t diff = measured - predicted;
		anchor_idx = (std::llabs(diff) > kHardJump) ? measured : predicted + diff / 16;
	} else {
		anchor_idx = measured;
		have_anchor = true;
	}
	anchor_ns = now;
}

void PsWrapVoiceProc::resetPlayback()
{
	QMutexLocker locker(&ref_mutex);
	have_anchor = false;
}

void PsWrapVoiceProc::fetchReference(int16_t *out, int64_t capture_lag_frames, bool *have_ref)
{
	QMutexLocker locker(&ref_mutex);
	const int64_t now = nowNs();
	if (!have_anchor || now - anchor_ns > kStaleNs) {
		std::fill(out, out + kFrame, int16_t(0));
		read_valid = false;
		*have_ref = false;
		return;
	}
	int64_t play_now = anchor_idx + (now - anchor_ns) * kRate / 1000000000LL;
	play_now = std::min(play_now, write_idx);
	const int64_t target = play_now - capture_lag_frames + kMargin;

	if (!read_valid) {
		read_end = target;
		err_avg = 0.0;
		warm_frames = 0;
		read_valid = true;
	} else {
		read_end += kFrame;
		const double err = double(target - read_end);
		const double alpha = warm_frames < 100 ? 0.1 : 0.02;
		warm_frames++;
		err_avg += alpha * (err - err_avg);
		if (std::fabs(err_avg) > kResync) {
			const int64_t step = std::llround(err_avg);
			read_end += step;
			err_avg = 0.0;
			resyncs++;
			if (resyncs <= 10 || resyncs % 50 == 0)
				qInfo().nospace() << "PSWRAP voice: echo reference re-sync " << (step * 1000 / kRate) << " ms (#" << resyncs << ")";
		}
	}

	const int64_t oldest = write_idx - int64_t(kRing);
	for (int i = 0; i < kFrame; i++) {
		const int64_t idx = read_end - kFrame + i;
		out[i] = (idx < 0 || idx < oldest || idx >= write_idx) ? int16_t(0) : ring[size_t(idx % int64_t(kRing))];
	}
	*have_ref = true;
}

bool PsWrapVoiceProc::process(int16_t *mono, int64_t capture_lag_frames)
{
	const bool did = processSpeech(mono, capture_lag_frames);
	// gain + gate + limiter ท้ายสุด (หลัง RNNoise) — ก่อน AEC จะเปลี่ยนสัดส่วน echo/ref ทำให้ filter ต้องเรียนใหม่ทุกครั้งที่ปรับ gain
	if (mono) {
		dyn.process(mono, kFrame, 1, g_gain_db.load(std::memory_order_relaxed), g_gate_on.load(std::memory_order_relaxed),
			g_gate_db.load(std::memory_order_relaxed));
		g_gate_open.store(dyn.gateOpen(), std::memory_order_relaxed);
	}
	return did;
}

bool PsWrapVoiceProc::processSpeech(int16_t *mono, int64_t capture_lag_frames)
{
#if CHIAKI_GUI_ENABLE_SPEEX
	applyParamsIfChanged();
	if (!on || !pre || !mono)
		return false;
	if (aec_on) {
		bool have_ref = false;
		fetchReference(ref_frame.data(), capture_lag_frames, &have_ref);
		if (aec_needs_reset) {
			speex_echo_state_reset(echo);
			aec_needs_reset = false;
		}
		speex_echo_cancellation(echo, mono, ref_frame.data(), out_frame.data());
		std::memcpy(mono, out_frame.data(), kFrame * sizeof(int16_t));
	}
	if (pre_on)
		speex_preprocess_run(pre, mono);
#ifdef PSWRAP_HAVE_RNNOISE
	if (rnn_on) {
		// เสียงเดิมหน่วง kRnnDelay ให้ตรงกับ output ของ RNNoise แล้วผสม dry ตามระดับที่เลือก
		std::memmove(dry_delay.data(), dry_delay.data() + kFrame, kRnnDelay * sizeof(int16_t));
		std::memcpy(dry_delay.data() + kRnnDelay, mono, kFrame * sizeof(int16_t));
		for (int k = 0; k < kFrame; k++)
			rnn_in[k] = float(mono[k]);
		rnnoise_process_frame(rnn, rnn_out.data(), rnn_in.data());
		const float wet = 1.0f - rnn_dry;
		for (int k = 0; k < kFrame; k++) {
			const float v = rnn_out[k] * wet + float(dry_delay[k]) * rnn_dry;
			mono[k] = int16_t(std::clamp(v, -32768.0f, 32767.0f));
		}
	}
#endif
	return true;
#else
	Q_UNUSED(mono);
	Q_UNUSED(capture_lag_frames);
	return false;
#endif
}
