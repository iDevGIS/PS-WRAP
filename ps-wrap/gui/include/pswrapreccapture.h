// PS-WRAP: จับภาพสำหรับอัดวิดีโอ — ทำงานบน render thread ของ QmlMainWindow เท่านั้น
//
// หลัง render เฟรมลงจอแล้ว วาดเฟรมเดียวกัน (วิดีโอ + quick_tex = overlay QML ทั้งหมด) อีกรอบด้วย renderer แยก
// ลง texture YUV ขนาดคงที่ของไฟล์ (NV12 SDR BT.709 / P010 HDR PQ BT.2020 — libplacebo แปลงสีบน GPU)
// แล้ว pl_tex_download แบบ async ลง buffer ที่ map ไว้ฝั่ง host (ring หลาย slot) → ส่ง AVFrame ที่อ้าง buffer นั้นตรงๆ
// ให้ PsWrapRecorder (ไม่ memcpy บน render thread) · slot ถูกคืนเมื่อ encoder unref เฟรม

#ifndef PSWRAP_RECCAPTURE_H
#define PSWRAP_RECCAPTURE_H

#include <pswraprecorder.h>

#include <libplacebo/renderer.h>

#include <QtGlobal>

#include <atomic>
#include <deque>

class PsWrapRecCapture
{
public:
	PsWrapRecCapture(pl_gpu gpu, pl_log log);
	~PsWrapRecCapture();

	// render thread: เรียกหลัง pl_render_image(_mix) ลงจอสำเร็จ (ก่อน submit)
	// mix หรือ single อย่างใดอย่างหนึ่ง · screen_target = target ของจอ (ใช้ crop) · overlay = quick_tex (nullable)
	void capture(PsWrapRecorder *rec, const struct pl_frame_mix *mix, const struct pl_frame *single,
	             const struct pl_render_params &params, const struct pl_frame &screen_target,
	             const struct pl_overlay *overlay, int screen_w, int screen_h);

	// ไม่มีเฟรมไหนค้างอยู่ใน encoder → ลบได้
	bool canDestroy() const;

	// render thread: บันทึกสีของ source ล่าสุด (ใช้ตัดสิน SDR/HDR + metadata ตอนเริ่มอัด)
	static void noteSource(const struct pl_frame *frame);
	// GUI thread
	static bool sourceInfo(bool *hdr, PsWrapRecHdrInfo *info, int *height);

private:
	struct Slot
	{
		pl_buf buf = nullptr;
		std::atomic<int> state{0}; // 0 ว่าง · 1 GPU กำลังเขียน · 2 encoder ถือ
		qint64 capture_us = 0;
	};
	static constexpr int kSlots = 6;

	pl_gpu gpu;
	pl_log log;
	pl_renderer renderer = nullptr;
	pl_tex tex_y = nullptr;
	pl_tex tex_uv = nullptr;
	Slot ring[kSlots];
	std::deque<int> inflight;
	int width = 0, height = 0;
	bool hdr = false;
	size_t pitch_y = 0, pitch_uv = 0, off_uv = 0, buf_size = 0;
	bool broken = false;
	quint64 no_slot_drops = 0;

	bool ensureResources(int w, int h, bool is_hdr);
	void releaseResources();
	void collect(PsWrapRecorder *rec);
	static void releaseSlot(void *opaque, uint8_t *data);
};

#endif // PSWRAP_RECCAPTURE_H
