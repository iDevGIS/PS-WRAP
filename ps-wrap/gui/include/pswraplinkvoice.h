// PS-WRAP Link: เสียงไมค์ของเพื่อน → ลำโพงของเจ้าของเครื่อง
// เพื่อนส่ง Opus มาทาง audio track เดิม (sendrecv) · ถอดรหัสแยกต่อคน → บัฟเฟอร์กันกระตุก ~60 ms → ผสมรวมเล่นออก SDL
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

	// อุปกรณ์เล่นเสียง (ชื่อเดียวกับที่สตรีมใช้ · ว่าง = ค่าเริ่มต้นของระบบ) — มีผลตอนเปิดอุปกรณ์ครั้งถัดไป
	void setDevice(const QString &name);
	// rtc thread: แพ็กเก็ต Opus 1 ก้อนจากเพื่อน (timestamp RTP @ 48 kHz)
	void push(const QString &peer, const uint8_t *data, size_t size, uint32_t timestamp);
	void removePeer(const QString &peer);
	void setMuted(const QString &peer, bool muted);
	// กำลังพูด (เสียงดังเกินเกณฑ์ภายใน ~300 ms ที่ผ่านมา)
	bool speaking(const QString &peer) const;
	bool anyVoice() const;
	// ปิดอุปกรณ์ + ทิ้งทุกคน (Link ปิด)
	void stop();

private:
	struct Impl;
	std::unique_ptr<Impl> d;
};
