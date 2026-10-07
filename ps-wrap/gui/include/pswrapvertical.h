// PS-WRAP: ภาพแนวตั้ง 9:16 (Shorts / TikTok / Reels) — เลย์เอาต์ + ตัวเรนเดอร์ preview
//
// เรนเดอร์จากเฟรมสตรีมตรงๆ (เหมือน PsWrapRecCapture) ลง canvas 9:16 ใน render pass เดียว:
//   Split  = facecam ด้านบน + เกม (ตัดตามสัดส่วน) ด้านล่าง
//   Center = เกมเต็มจอแนวตั้ง (ตัดกลาง เลื่อนได้) + facecam มุมบน
//   Blur   = เกมเต็มภาพกลางจอ + พื้นหลังเป็นภาพเกมเบลอ (border = PL_CLEAR_BLUR) + facecam ด้านบน
// facecam = ส่วนของ quick_tex (overlay QML) ตรงกรอบ facecam บนจอ — ได้รูปทรง/เอฟเฟกต์เดียวกับที่เห็น
// overlay อื่น (จอย/stats) ไม่ใส่ในภาพแนวตั้ง

#ifndef PSWRAP_VERTICAL_H
#define PSWRAP_VERTICAL_H

#include <libplacebo/renderer.h>

#include <QImage>
#include <QRectF>
#include <QtGlobal>

#include <atomic>
#include <functional>

struct PsWrapVerticalLayout
{
	enum Mode { Split = 0, Center = 1, Blur = 2 };
	int mode = Split;
	float crop_x = 0.5f;   // 0..1 ตำแหน่งแนวนอนของส่วนเกมที่ตัด (Split/Center)
	QRectF cam;            // กรอบ facecam บนจอ (pixel ของ swapchain) · ว่าง = ไม่มี facecam
	// ตำแหน่ง facecam ที่ผู้ใช้ลากเอง (สัดส่วนของ canvas: จุดกึ่งกลาง + ความกว้าง) · cam_w <= 0 = ค่าเริ่มต้นของเลย์เอาต์
	float cam_cx = 0.5f, cam_cy = 0.2f, cam_w = 0.0f;
};

// ผลคำนวณเลย์เอาต์บน canvas ขนาด w×h (พิกัด pixel ของ canvas)
struct PsWrapVerticalGeometry
{
	pl_rect2df src_crop = {};   // ส่วนของเฟรมสตรีมที่ใช้
	pl_rect2df game_dst = {};   // ที่วางเกมบน canvas
	pl_rect2df cam_dst = {};    // ที่วาง facecam (ว่าง = ไม่วาด)
	bool blur_border = false;
};

// src = crop ของเฟรมสตรีม · cam_aspect = กว้าง/สูงของกรอบ facecam (0 = ไม่มี)
PsWrapVerticalGeometry pswrapVerticalGeometry(const PsWrapVerticalLayout &layout, float canvas_w, float canvas_h,
                                              const pl_rect2df &src, float cam_aspect);

// วาด canvas แนวตั้งลง target (ตั้ง repr/color/planes ของ target มาแล้ว) ด้วย renderer ที่ให้มา — ใช้ทั้ง preview และไฟล์/ไลฟ์
// คืนเรขาคณิตที่ใช้จริงใน *out (nullable) · false = render ล้มเหลว
bool pswrapRenderVertical(pl_renderer renderer, const struct pl_frame_mix *mix, const struct pl_frame *single,
                          const struct pl_render_params &params, const struct pl_frame &screen_target,
                          const struct pl_overlay *overlay, int screen_w, int screen_h,
                          const PsWrapVerticalLayout &layout, struct pl_frame &target, PsWrapVerticalGeometry *out);

// render thread เท่านั้น — preview ขนาดเล็ก (RGBA8 sRGB) ดาวน์โหลดแบบ async ทีละภาพ
class PsWrapVerticalPreview
{
public:
	using DoneFn = std::function<void(QImage img)>;   // เรียกจาก thread ของ GPU callback (ห้ามทำงานหนัก)

	PsWrapVerticalPreview(pl_gpu gpu, pl_log log);
	~PsWrapVerticalPreview();   // ถ้ามี download ค้าง → pl_gpu_finish ให้ callback ยิงก่อน

	// false = ยังมีภาพก่อนหน้าค้างอยู่ / เริ่มไม่ได้ (done จะไม่ถูกเรียก)
	bool capture(const struct pl_frame_mix *mix, const struct pl_frame *single, const struct pl_render_params &params,
	             const struct pl_frame &screen_target, const struct pl_overlay *overlay, int screen_w, int screen_h,
	             const PsWrapVerticalLayout &layout, int out_w, int out_h, DoneFn done);

	bool idle() const { return inflight.load(std::memory_order_acquire) == 0; }
	const PsWrapVerticalGeometry &lastGeometry() const { return last_geometry; }   // pixel ของ canvas preview

private:
	struct Job;
	PsWrapVerticalGeometry last_geometry;
	pl_gpu gpu;
	pl_log log;
	pl_renderer renderer = nullptr;
	pl_tex tex = nullptr;
	std::atomic<int> inflight{0};
	static void onDownloaded(void *priv);
};

class QQuickImageProvider;
QQuickImageProvider *pswrapCreateVerticalProvider();   // image://pswrapvertical/<n> = ภาพ preview ล่าสุด

#endif // PSWRAP_VERTICAL_H
