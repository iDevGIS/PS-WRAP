// PS-WRAP: ประมวลผลเสียงไมค์ก่อนส่งเข้า PS5 — ตัดเสียงลำโพงย้อน (speexdsp AEC) + ลดเสียงรบกวน (RNNoise ถ้ามี ไม่งั้น speex)
// ใช้ร่วมกันทั้งตอนสตรีม (StreamSession) และหน้าทดสอบไมค์ (QmlMainWindow::startMicPreview)
//
// ค่าตั้งเป็นของกลาง (setParams) — เปลี่ยนเมื่อไหร่ ทุก instance รับไปใช้ในเฟรมถัดไป (ไม่ต้องเริ่มสตรีมใหม่)
//
// จัดจังหวะ echo reference (ทำไมต้องมี): AEC ของ speex ต้องได้ "เสียงที่ลำโพงเล่นอยู่ตอนไมค์อัดเฟรมนั้น"
// ที่มาก่อนเสียงย้อนจริงไม่เกินความยาว filter · เดิม upstream เอา reference ตอน decode (ก่อน ring + คิว SDL + บัฟเฟอร์การ์ดเสียง)
// และ filter ยาวแค่ 100 ms → ลำโพงหน่วงเกินนั้นเมื่อไหร่ AEC ใช้ไม่ได้เลย
// ที่นี่: pushPlayback() เรียกหลังส่งเข้า SDL พร้อมบอกว่ายังค้างกี่ sample ก่อนถึงลำโพง → รู้ว่า sample ไหนกำลังเล่น "ตอนนี้"
// process() รู้ว่าเฟรมไมค์อัดไปแล้วกี่ sample → อ่าน reference ช่วงที่เล่นตอนนั้น "ล่วงหน้า" margin (ref ต้องมาก่อนเสียงย้อน) · read pointer เดินทีละเฟรมเท่ากันเสมอ
// (ref กระตุก = filter ล้ม) re-sync เฉพาะเมื่อ error เฉลี่ยเกิน threshold

#ifndef PSWRAP_VOICEPROC_H
#define PSWRAP_VOICEPROC_H

#include <QMutex>

#include <cstddef>
#include <cstdint>
#include <vector>

#if CHIAKI_GUI_ENABLE_SPEEX
struct SpeexEchoState_;
struct SpeexPreprocessState_;
#endif
#ifdef PSWRAP_HAVE_RNNOISE
struct DenoiseState;
#endif

class PsWrapVoiceProc
{
public:
	static constexpr int kRate = 48000;
	static constexpr int kFrame = 480;          // 10 ms mono — ขนาดเดียวกับ MICROPHONE_SAMPLES ของ StreamSession

	PsWrapVoiceProc();
	~PsWrapVoiceProc();
	PsWrapVoiceProc(const PsWrapVoiceProc &) = delete;
	PsWrapVoiceProc &operator=(const PsWrapVoiceProc &) = delete;

	// มี speexdsp ใน build นี้ไหม
	static bool available();
	// ลดเสียงรบกวนด้วย RNNoise (ถ้า build มี) — ไม่มี = speex denoise
	static bool hasRnnoise();
	// thread ใดก็ได้ · noise/echo = dB ที่ลด (0 = ปิดขั้นนั้น, echo 0 = ไม่ทำ AEC)
	static void setParams(bool enabled, int noise_db, int echo_db);
	static bool enabled();

	// thread เสียงออก: PCM s16 interleaved 48 kHz ที่เพิ่งส่งเข้าอุปกรณ์ (หลังปรับ volume)
	// pending_frames = จำนวน sample ที่ยังไม่ออกลำโพง "หลัง" ส่งชุดนี้แล้ว (คิว SDL + บัฟเฟอร์อุปกรณ์)
	void pushPlayback(const int16_t *pcm, size_t frames, unsigned channels, int64_t pending_frames);
	// ล้าง reference (ปิดเสียง/เปลี่ยนอุปกรณ์/mute)
	void resetPlayback();

	// thread ไมค์: ประมวลผลในที่ · mono kFrame sample
	// capture_lag_frames = sample สุดท้ายของเฟรมนี้ถูกอัดไปแล้วกี่ sample ก่อน "ตอนนี้"
	// คืน false ถ้าไม่ได้ทำอะไร (ปิดอยู่)
	bool process(int16_t *mono, int64_t capture_lag_frames);

private:
	void applyParamsIfChanged();
	void fetchReference(int16_t *out, int64_t capture_lag_frames, bool *have_ref);

#if CHIAKI_GUI_ENABLE_SPEEX
	SpeexEchoState_ *echo = nullptr;
	SpeexPreprocessState_ *pre = nullptr;
#endif
#ifdef PSWRAP_HAVE_RNNOISE
	DenoiseState *rnn = nullptr;
#endif
	bool rnn_on = false;
	bool pre_on = false;            // ต้องรัน speex preprocess ไหม (residual echo / speex denoise)
	float rnn_dry = 0.0f;           // สัดส่วนเสียงเดิมที่ผสมกลับ = เพดานการลด (High ลดมากสุด)
	std::vector<float> rnn_in, rnn_out;
	std::vector<int16_t> dry_delay; // เสียงเดิมหน่วงเท่า RNNoise ก่อนผสม (กัน comb filter)
	int applied_gen = -1;
	bool on = false;
	bool aec_on = false;
	bool aec_needs_reset = true;
	std::vector<int16_t> ref_frame;
	std::vector<int16_t> out_frame;

	// reference ring (mono) — คุมด้วย mutex (เขียนจาก thread เสียงออก อ่านจาก thread ไมค์)
	QMutex ref_mutex;
	std::vector<int16_t> ring;
	int64_t write_idx = 0;          // sample ถัดไปที่จะเขียน (นับสะสม)
	int64_t anchor_idx = 0;         // sample ที่กำลังออกลำโพงตอน anchor_ns
	int64_t anchor_ns = 0;
	bool have_anchor = false;

	// ฝั่งอ่าน (thread ไมค์เท่านั้น)
	int64_t read_end = 0;
	bool read_valid = false;
	double err_avg = 0.0;
	int warm_frames = 0;
	int resyncs = 0;
};

#endif // PSWRAP_VOICEPROC_H
