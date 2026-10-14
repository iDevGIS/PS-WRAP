// PS-WRAP Link: เสียงไมค์ของเพื่อน (ดู pswraplinkvoice.h) · ผสมเข้าเสียงเกมใน StreamSession (ไม่เปิดอุปกรณ์เสียงของตัวเอง)
//
// เสียงเพื่อนมาทางเน็ต (แกว่ง/หาย) แต่เล่นตามนาฬิกาเสียงเกมจาก PS5 (มาเป็นก้อนๆ ก็มี) → ต้องมี jitter buffer:
//  - บัฟเฟอร์ปรับเอง: เริ่ม 80 ms · เสียงขาด (บัฟเฟอร์หมด) = เพิ่ม 20 ms (สูงสุด 200) · นิ่ง 15 วิ = ลด 10 ms (ต่ำสุด 60)
//  - ขาด/เริ่มใหม่ = fade 2.5 ms (ไม่ดังแป๊ะ)
//  - แพ็กเก็ตหาย (RTP timestamp กระโดด): ก้อนสุดท้ายที่หายกู้จาก FEC ในแพ็กเก็ตถัดไป · ก่อนหน้านั้นใช้ PLC ของ Opus เติม
//  - นาฬิกาไมค์เพื่อนเร็วกว่านาฬิกาเสียงเกมนิดเดียว → บัฟเฟอร์โตช้าๆ: เกินเป้า 30 ms = เล่นเร็วขึ้น 0.5% (ไม่ได้ยิน) จนกลับใกล้เป้า
//  - log สถิติทุก 10 วิต่อคน (ได้/หาย/กู้/ขาด/บัฟเฟอร์) — ไล่ปัญหาเสียงกระตุกจาก log ได้
// เดิม (ถึง 2026-10-14): บัฟเฟอร์ 60 ms ตายตัว หมดเมื่อไรหยุดรอเติมใหม่ทั้ง 60 ms + ไม่กู้แพ็กเก็ตหาย → เสียงขาดเป็นช่วงๆ
#include <pswraplinkvoice.h>

#include <QDateTime>
#include <QLoggingCategory>

#include <opus/opus.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(pswrapLink)

namespace {

constexpr int kRate = 48000;
constexpr int kCh = 2;
constexpr size_t kMs = kRate * kCh / 1000;                 // ตัวอย่าง (interleaved) ต่อ 1 ms
constexpr size_t kTargetStart = 80 * kMs;
constexpr size_t kTargetMin = 60 * kMs;
constexpr size_t kTargetMax = 200 * kMs;
constexpr size_t kTargetStepUp = 20 * kMs;
constexpr size_t kTargetStepDown = 10 * kMs;
constexpr qint64 kCalmMs = 15000;                           // ไม่ขาดเลยนานเท่านี้ = ลดบัฟเฟอร์ลง
constexpr size_t kOverflow = 200 * kMs;                     // เกินเป้า + 200 ms = ช้าไปแล้ว → ตัดเหลือเป้า
constexpr int kFade = kRate / 400;                          // 2.5 ms (ต่อช่อง)
constexpr int kMaxFrame = kRate * 120 / 1000;               // Opus ก้อนใหญ่สุด 120 ms
constexpr int kMaxConceal = 6;                              // หายเกินนี้ (เช่นเพื่อนปิดไมค์ชั่วคราว) = ไม่เติม
constexpr float kSpeakLevel = 0.04f;                        // peak เกินนี้ = กำลังพูด (หลัง AGC/NS ของเบราว์เซอร์)
constexpr qint64 kSpeakHoldMs = 300;
constexpr qint64 kStatsMs = 10000;
constexpr size_t kCatchUpOn = 30 * kMs;                      // เกินเป้าเท่านี้ = เริ่มเร่ง
constexpr size_t kCatchUpOff = 5 * kMs;                      // เหลือเกินเป้าแค่นี้ = เลิกเร่ง
constexpr double kCatchUpRate = 1.005;
constexpr size_t kFwdStart = kRate * 60 / 1000;             // ส่งต่อ: เริ่มเมื่อมี 60 ms (โมโน)
constexpr size_t kFwdMax = kRate * 250 / 1000;              // ค้างเกิน 250 ms → ตัดเหลือ 80 ms
constexpr size_t kFwdTrimTo = kRate * 80 / 1000;

struct Voice
{
	OpusDecoder *dec = nullptr;
	std::vector<float> fifo;   // interleaved stereo · อ่านจากหัว (read) — กันย้ายหน่วยความจำบ่อย
	std::vector<float> scratch = std::vector<float>(size_t(kMaxFrame) * kCh);
	size_t read = 0;
	size_t target = kTargetStart;
	bool playing = false;
	int fade_in = 0;           // ตัวอย่าง (ต่อช่อง) ที่ยังต้องค่อยๆ ดังขึ้น
	bool catch_up = false;     // กำลังเล่นเร็วขึ้นเล็กน้อยเพื่อลดบัฟเฟอร์
	std::vector<float> fwd;    // โมโน · ส่งต่อให้เพื่อนคนอื่น (อ่านจากนาฬิกาตัวอัด)
	size_t fwd_read = 0;
	bool fwd_play = false;
	double frac = 0.0;         // ตำแหน่งอ่านเศษส่วน (ตอนเร่ง)
	bool muted = false;
	bool has_ts = false;
	uint32_t last_ts = 0;
	int last_frame = kRate / 50;   // ขนาดก้อนล่าสุด (ต่อช่อง) — ปกติ 20 ms
	qint64 loud_ms = 0;
	qint64 last_underrun_ms = 0;
	qint64 last_adjust_ms = 0;
	float level = 0.0f;        // peak สูงสุดตั้งแต่ takeLevel ครั้งก่อน
	// สถิติรอบ 10 วิ
	qint64 stats_ms = 0;
	qint64 quiet_log_ms = 0;   // log ตอนไม่มีปัญหาครั้งล่าสุด
	int st_pkts = 0, st_lost = 0, st_fec = 0, st_plc = 0, st_late = 0, st_underrun = 0, st_trim = 0, st_catchup = 0, st_mix = 0;
	double st_buf_sum = 0;
	int st_buf_n = 0;

	~Voice()
	{
		if (dec)
			opus_decoder_destroy(dec);
	}
	size_t available() const { return fifo.size() - read; }
	void compact()
	{
		if (read > 0 && read * 2 > fifo.size()) {
			fifo.erase(fifo.begin(), fifo.begin() + static_cast<std::ptrdiff_t>(read));
			read = 0;
		}
	}
	void clear()
	{
		fifo.clear();
		read = 0;
		playing = false;
		fade_in = 0;
		catch_up = false;
		frac = 0.0;
		fwd.clear();
		fwd_read = 0;
		fwd_play = false;
	}
	// ถอด 1 ก้อน (data = nullptr → PLC · fec = กู้ก้อนก่อนหน้าจาก FEC ในแพ็กเก็ตนี้) ต่อท้าย fifo · คืน peak
	float decode(const uint8_t *data, size_t size, int frame, bool fec)
	{
		const int n = opus_decode_float(dec, data, static_cast<opus_int32>(size), scratch.data(), std::min(frame, kMaxFrame), fec ? 1 : 0);
		if (n <= 0)
			return 0.0f;
		float peak = 0.0f;
		for (int i = 0; i < n * kCh; i++)
			peak = std::max(peak, std::fabs(scratch[size_t(i)]));
		fifo.insert(fifo.end(), scratch.begin(), scratch.begin() + n * kCh);
		for (int i = 0; i < n; i++)
			fwd.push_back(0.5f * (scratch[size_t(i) * kCh] + scratch[size_t(i) * kCh + 1]));
		if (!fec && data)
			last_frame = n;
		return peak;
	}
};

std::unique_ptr<Voice> newVoice()
{
	auto v = std::make_unique<Voice>();
	int err = 0;
	v->dec = opus_decoder_create(kRate, kCh, &err);   // เพื่อนส่งโมโน — decoder ขยายเป็นสเตอริโอให้เอง
	if (err != OPUS_OK || !v->dec)
		return nullptr;
	return v;
}

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

struct PsWrapLinkVoice::Impl
{
	mutable std::mutex m;
	std::map<QString, std::unique_ptr<Voice>> voices;
};

PsWrapLinkVoice::PsWrapLinkVoice() : d(std::make_unique<Impl>()) {}

PsWrapLinkVoice::~PsWrapLinkVoice() { stop(); }

// thread เสียงของ StreamSession: บวกเสียงเพื่อนเข้าเสียงเกม (s16 interleaved) — นาฬิกาเล่น = เสียงเกมที่มาจาก PS5
void PsWrapLinkVoice::mix(int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	if (!pcm || !frames || channels == 0 || rate != unsigned(kRate))
		return;
	const qint64 now = nowMs();
	std::lock_guard<std::mutex> lock(d->m);
	for (auto &[id, vp] : d->voices) {
		Voice &v = *vp;
		if (v.muted)
			continue;
		// นิ่งนานพอ → ลดบัฟเฟอร์ (หน่วงน้อยลง)
		if (v.target > kTargetMin && now - v.last_underrun_ms > kCalmMs && now - v.last_adjust_ms > kCalmMs) {
			v.target = std::max(kTargetMin, v.target - kTargetStepDown);
			v.last_adjust_ms = now;
		}
		if (!v.playing) {
			if (v.available() < v.target)
				continue;
			v.playing = true;
			v.fade_in = kFade;
		}
		v.st_buf_sum += double(v.available()) / kMs;
		v.st_buf_n++;
		if (!v.catch_up && v.available() > v.target + kCatchUpOn)
			v.catch_up = true;
		else if (v.catch_up && v.available() <= v.target + kCatchUpOff)
			v.catch_up = false;
		const double r = v.catch_up ? kCatchUpRate : 1.0;
		v.st_mix++;
		if (v.catch_up)
			v.st_catchup++;
		const size_t have = v.available() / kCh;
		// ออกได้กี่เฟรม: ตำแหน่งสุดท้าย frac + (n-1)·r ต้องไม่เกินเฟรมสุดท้ายที่มี
		size_t can = 0;
		if (have > 0)
			can = size_t(std::floor((double(have - 1) - v.frac) / r)) + 1;
		const size_t n = std::min(frames, can);
		const bool underrun = n < frames;
		const int fade_out_from = underrun ? int(n) - std::min<int>(kFade, int(n)) : int(n);
		const float *src = v.fifo.data() + v.read;
		for (size_t i = 0; i < n; i++) {
			float g = 1.0f;
			if (v.fade_in > 0) {
				g = 1.0f - float(v.fade_in) / kFade;
				v.fade_in--;
			}
			if (int(i) >= fade_out_from)
				g *= float(int(n) - int(i)) / float(int(n) - fade_out_from + 1);
			const double p = v.frac + double(i) * r;
			const size_t i0 = size_t(p);
			const float t = float(p - double(i0));
			const size_t i1 = std::min(i0 + 1, have - 1);
			for (unsigned c = 0; c < channels; c++) {
				const unsigned sc = std::min<unsigned>(c, kCh - 1);
				const float a = src[i0 * kCh + sc], b = src[i1 * kCh + sc];
				const float f = (a + (b - a) * t) * g;
				const int x = int(pcm[i * channels + c]) + int(std::lround(f * 32767.0f));
				pcm[i * channels + c] = int16_t(std::clamp(x, -32768, 32767));
			}
		}
		const double end = v.frac + double(n) * r;
		const size_t used = std::min(have, size_t(end));
		v.frac = used < have ? end - double(used) : 0.0;
		v.read += used * kCh;
		if (underrun) {
			// บัฟเฟอร์หมดกลางคัน (เน็ตเพื่อนสะดุด หรือเสียงเกมมาเป็นก้อน) → รอเติมใหม่ และเผื่อบัฟเฟอร์มากขึ้น
			v.playing = false;
			v.catch_up = false;
			v.frac = 0.0;
			v.st_underrun++;
			v.last_underrun_ms = now;
			v.target = std::min(kTargetMax, v.target + kTargetStepUp);
		}
		v.compact();
	}
}

void PsWrapLinkVoice::push(const QString &peer, const uint8_t *data, size_t size, uint32_t timestamp)
{
	if (!data || size == 0 || size > 1500)
		return;
	std::lock_guard<std::mutex> lock(d->m);
	auto &slot = d->voices[peer];
	if (!slot) {
		slot = newVoice();
		if (!slot) {
			qCWarning(pswrapLink) << "voice: no Opus decoder";
			d->voices.erase(peer);
			return;
		}
		qCInfo(pswrapLink).noquote() << "friend" << peer << "voice: first packet";
	}
	Voice &v = *slot;
	if (v.muted)
		return;
	const qint64 now = nowMs();
	if (!v.stats_ms)
		v.stats_ms = now;
	v.st_pkts++;
	float peak = 0.0f;
	if (v.has_ts) {
		const int32_t ahead = static_cast<int32_t>(timestamp - v.last_ts);
		if (ahead <= 0) {
			v.st_late++;   // มาช้ากว่าก้อนที่ถอดไปแล้ว — ทิ้ง (ถูกเติมด้วย PLC/FEC ไปแล้ว)
			return;
		}
		const int frame = std::max(v.last_frame, 1);
		const int missing = ahead / frame - 1;   // ก้อนที่หายระหว่างก้อนก่อนกับก้อนนี้
		if (missing > 0 && missing <= kMaxConceal) {
			for (int k = 0; k < missing - 1; k++) {
				peak = std::max(peak, v.decode(nullptr, 0, frame, false));
				v.st_plc++;
			}
			peak = std::max(peak, v.decode(data, size, frame, true));   // ก้อนสุดท้ายที่หาย: FEC จากแพ็กเก็ตนี้ (ไม่มี FEC = libopus ทำ PLC ให้)
			v.st_fec++;
			v.st_lost += missing;
		} else if (missing > kMaxConceal) {
			v.st_lost += missing;   // ช่องว่างยาว (ปิดไมค์ชั่วคราว/เน็ตหลุด) — ไม่เติม
		}
	}
	v.has_ts = true;
	v.last_ts = timestamp;
	peak = std::max(peak, v.decode(data, size, kMaxFrame, false));
	// ค้างเกินเป้ามาก = เสียงช้ากว่าภาพแล้ว → ตัดเหลือเป้า
	if (v.available() > v.target + kOverflow) {
		v.read = v.fifo.size() - v.target;
		v.frac = 0.0;
		v.compact();
		v.st_trim++;
	}
	if (v.fwd.size() - v.fwd_read > kFwdMax) {
		v.fwd_read = v.fwd.size() - kFwdTrimTo;
	}
	if (v.fwd_read > 0 && v.fwd_read * 2 > v.fwd.size()) {
		v.fwd.erase(v.fwd.begin(), v.fwd.begin() + std::ptrdiff_t(v.fwd_read));
		v.fwd_read = 0;
	}
	if (peak > kSpeakLevel)
		v.loud_ms = now;
	v.level = std::max(v.level, peak);

	if (now - v.stats_ms >= kStatsMs) {
		// เงียบ (ไม่มีปัญหา) ไม่ต้อง log ทุกรอบ — log เมื่อมีหาย/ขาด/ตัด หรือทุก 60 วิ
		const bool trouble = v.st_lost || v.st_underrun || v.st_trim || v.st_late;
		if (trouble || now - v.quiet_log_ms >= 60000) {
			if (!trouble)
				v.quiet_log_ms = now;
			qCInfo(pswrapLink).noquote()
				<< QStringLiteral("friend %1 voice %2s: pkts %3 lost %4 (fec %5 plc %6) late %7 underrun %8 trim %9 buffer avg %10 ms target %11 ms catch-up %12%")
					   .arg(peer).arg((now - v.stats_ms) / 1000).arg(v.st_pkts).arg(v.st_lost).arg(v.st_fec).arg(v.st_plc).arg(v.st_late)
					   .arg(v.st_underrun).arg(v.st_trim).arg(v.st_buf_n ? int(v.st_buf_sum / v.st_buf_n) : 0).arg(int(v.target / kMs))
					   .arg(v.st_mix ? v.st_catchup * 100 / v.st_mix : 0);
		}
		v.stats_ms = now;
		v.st_pkts = v.st_lost = v.st_fec = v.st_plc = v.st_late = v.st_underrun = v.st_trim = v.st_catchup = v.st_mix = 0;
		v.st_buf_sum = 0;
		v.st_buf_n = 0;
	}
}

void PsWrapLinkVoice::removePeer(const QString &peer)
{
	std::lock_guard<std::mutex> lock(d->m);
	d->voices.erase(peer);
}

void PsWrapLinkVoice::setMuted(const QString &peer, bool muted)
{
	std::lock_guard<std::mutex> lock(d->m);
	auto it = d->voices.find(peer);
	if (it == d->voices.end()) {
		if (!muted)
			return;
		// ยังไม่เคยพูด → สร้างช่องไว้จำว่าปิดเสียง
		auto v = newVoice();
		if (!v)
			return;
		it = d->voices.emplace(peer, std::move(v)).first;
	}
	Voice &v = *it->second;
	v.muted = muted;
	if (muted) {
		v.clear();
		v.has_ts = false;   // เปิดกลับมา = เริ่มนับ timestamp ใหม่ (ไม่เติม PLC ช่วงที่ปิด)
		v.loud_ms = 0;
	}
}

bool PsWrapLinkVoice::speaking(const QString &peer) const
{
	std::lock_guard<std::mutex> lock(d->m);
	auto it = d->voices.find(peer);
	return it != d->voices.end() && !it->second->muted && nowMs() - it->second->loud_ms < kSpeakHoldMs;
}

float PsWrapLinkVoice::takeLevel(const QString &peer)
{
	std::lock_guard<std::mutex> lock(d->m);
	auto it = d->voices.find(peer);
	if (it == d->voices.end() || it->second->muted)
		return 0.0f;
	const float l = it->second->level;
	it->second->level = 0.0f;
	return l;
}

void PsWrapLinkVoice::takeForward(size_t n, std::vector<std::pair<QString, std::vector<float>>> &out)
{
	out.clear();
	std::lock_guard<std::mutex> lock(d->m);
	for (auto &[id, vp] : d->voices) {
		Voice &v = *vp;
		if (v.muted)
			continue;
		const size_t avail = v.fwd.size() - v.fwd_read;
		if (!v.fwd_play) {
			if (avail < kFwdStart)
				continue;
			v.fwd_play = true;
		}
		std::vector<float> s(n, 0.0f);
		const size_t k = std::min(n, avail);
		std::copy(v.fwd.begin() + std::ptrdiff_t(v.fwd_read), v.fwd.begin() + std::ptrdiff_t(v.fwd_read + k), s.begin());
		v.fwd_read += k;
		if (k < n)
			v.fwd_play = false;   // หมด → รอสะสมใหม่ (ช่วงที่ขาดเป็นเงียบ — ไมค์เจ้าของเครื่องยังต่อเนื่อง)
		if (v.fwd_read > 0 && v.fwd_read * 2 > v.fwd.size()) {
			v.fwd.erase(v.fwd.begin(), v.fwd.begin() + std::ptrdiff_t(v.fwd_read));
			v.fwd_read = 0;
		}
		out.emplace_back(id, std::move(s));
	}
}

bool PsWrapLinkVoice::anyVoice() const
{
	std::lock_guard<std::mutex> lock(d->m);
	return !d->voices.empty();
}

void PsWrapLinkVoice::stop()
{
	std::lock_guard<std::mutex> lock(d->m);
	d->voices.clear();
}
