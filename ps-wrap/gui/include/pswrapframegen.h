// PS-WRAP: Frame generation — สร้างเฟรมกลางระหว่างเฟรมจริง 2 เฟรม (สตรีม 60 → จอ 120) ด้วย motion estimation บน GPU
//
// ใช้กับ render thread เท่านั้น (ทุกเมธอดเรียกจาก QmlMainWindow::render)
//   push()        วาดเฟรมจริงใหม่ลง texture ขนาดจอ (crop/border/upscaler เหมือนวาดจอปกติ) — เฟรมก่อนหน้าเลื่อนเป็น "prev"
//   interpolate() หา motion prev → cur แล้วสร้างเฟรมกลางที่ t (0..1)
//   present()     วาด cur หรือเฟรมกลางลงจอเต็มกรอบ พร้อม overlay ของจอ (QML / จุด REC)
//
// motion: luma ย่อ 1/4 → 1/8 → 1/16 · block matching แบบสมมาตรกลางทาง (prev(p - v/2) เทียบ cur(p + v/2))
// ที่ 1/16 แล้ว refine ที่ 1/8 + median 3x3 · เฟรมกลางเลือก vector (bilinear / ใกล้สุด / ศูนย์) ที่ต่างน้อยสุดรายพิกเซล
// ส่วนที่บังกัน (ต่างมากทุกทาง) ใช้ภาพฝั่งเดียวแทนการผสม กันเงาซ้อน
#pragma once

#include <libplacebo/dispatch.h>
#include <libplacebo/gpu.h>
#include <libplacebo/renderer.h>

class PsWrapFrameGen
{
public:
	PsWrapFrameGen(pl_gpu gpu, pl_log log);
	~PsWrapFrameGen();
	PsWrapFrameGen(const PsWrapFrameGen &) = delete;
	PsWrapFrameGen &operator=(const PsWrapFrameGen &) = delete;

	// src = เฟรมวิดีโอ · target = เฟรมจอ (ใช้ขนาด/สี/crop) · params = render params ของจอ
	bool push(pl_renderer renderer, const pl_frame &src, const pl_frame &target, const pl_render_params &params);
	bool hasPair() const;                 // มี prev + cur ขนาดเดียวกัน → interpolate ได้
	bool interpolate(float t);
	// mid=false → เฟรมจริงล่าสุด · target ต้องมี overlay ของจอแล้ว (crop จะถูกตั้งเป็นเต็มจอ)
	bool present(pl_renderer renderer, bool mid, const pl_frame &target);
	void reset();                         // ลืมเฟรมเก่า (เช่น สตรีมเริ่มใหม่ / ขนาดจอเปลี่ยน)

private:
	bool ensure(int w, int h);
	void destroyTextures();
	pl_tex makeTex(int w, int h, pl_fmt fmt);
	bool lumaPyramid(int idx);
	bool run(pl_tex target, const char *name, const char *header, const char *body,
	         const struct pl_shader_desc *descs, int num_descs,
	         const struct pl_shader_var *vars, int num_vars);

	pl_gpu gpu;
	pl_log log;
	pl_dispatch dp = nullptr;
	pl_fmt fmt_rgba = nullptr;   // เฟรมขนาดจอ (16 bit ถ้ามี — HDR ไม่เสียความละเอียด)
	pl_fmt fmt_luma = nullptr;
	pl_fmt fmt_mv = nullptr;     // vector หน่วยพิกเซลจอ
	pl_fmt fmt_va = nullptr;     // vertex attribute vec2

	int width = 0, height = 0;
	pl_tex frame[2] = {};        // เฟรมจริงที่วาดแล้ว
	pl_tex luma4[2] = {}, luma8[2] = {}, luma16[2] = {};
	bool valid[2] = {};
	int cur = 0;                 // index ของเฟรมล่าสุด · prev = 1 - cur
	pl_tex mv16 = nullptr, mv8 = nullptr, mv8s = nullptr;
	pl_tex mid = nullptr;
	bool motion_ready = false;   // motion ของคู่ปัจจุบันคำนวณแล้ว
	bool failed = false;         // shader ใช้ไม่ได้บน GPU นี้ — เลิกพยายาม
};
