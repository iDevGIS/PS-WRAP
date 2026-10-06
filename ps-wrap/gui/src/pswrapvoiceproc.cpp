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
