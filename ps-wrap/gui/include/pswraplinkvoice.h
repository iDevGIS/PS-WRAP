// PS-WRAP Link: เสียงไมค์ของเพื่อน → ลำโพงของเจ้าของเครื่อง
// เพื่อนส่ง Opus มาทาง audio track เดิม (sendrecv) · ถอดรหัสแยกต่อคน (libopus + FEC/PLC) → jitter buffer ปรับเอง 60–200 ms → ผสมเข้าเสียงเกม
// (StreamSession::PsWrapSetAudioMixer) — ลำโพง/volume/echo cancel ชุดเดียวกับเกม · เดิมเปิด SDL device ของตัวเองบนลำโพงเดียวกัน
// แล้วลำโพงค้างทั้งตัว (2026-10-12) จึงเลิกทำ
// ใช้จาก rtc thread (push) + GUI thread (ที่เหลือ) · ปลอดภัยข้าม thread
#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>

class PsWrapLinkVoice
{
public:
	PsWrapLinkVoice();
	~PsWrapLinkVoice();

	// thread เสียงของ StreamSession: บวกเสียงเพื่อนเข้า pcm (s16 interleaved, 48 kHz)
	void mix(int16_t *pcm, size_t frames, unsigned channels, unsigned rate);
	// rtc thread: แพ็กเก็ต Opus 1 ก้อนจากเพื่อน (timestamp RTP @ 48 kHz)
	void push(const QString &peer, const uint8_t *data, size_t size, uint32_t timestamp);
	void removePeer(const QString &peer);
	void setMuted(const QString &peer, bool muted);
	// กำลังพูด (เสียงดังเกินเกณฑ์ภายใน ~300 ms ที่ผ่านมา)
	bool speaking(const QString &peer) const;
	// ความดังสูงสุด (0..1) ตั้งแต่ถามครั้งก่อน แล้วรีเซ็ต — คลื่นเสียงในการ์ด Link
	float takeLevel(const QString &peer);
	bool anyVoice() const;
	// ทิ้งทุกคน (Link ปิด)
	void stop();

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};
