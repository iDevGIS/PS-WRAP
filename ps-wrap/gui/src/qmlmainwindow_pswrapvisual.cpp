// PS-WRAP: ภาพสวยขึ้นตอนสตรีม — Ambient light (ขอบว่าง = แสงเบลอจากขอบเกม), Frame generation (60 → 120), Lightbar halo (ค่าเปิด/ปิด)
// แยกไฟล์จาก qmlmainwindow.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
//
// Ambient: params.border = PL_CLEAR_BLUR (libplacebo ถมขอบด้วยภาพเบลอ) + overlay ไล่เงาบนแถบ ให้ภาพเกมเด่นกว่าแสงรอบๆ
// Frame gen: ใช้เฉพาะ Direct Mapping + Vulkan + จอเร็วกว่าสตรีม ≥ 1.3 เท่า
//   เฟรมใหม่ N มา → present เฟรมกลาง(N-1, N) ทันที → pswrap_fg_pending ทำให้ render รอบถัดไปถูกสั่ง (hasBufferedWork)
//   → present N โดย throttle ใช้ครึ่งช่วงเฟรม (pswrapFrameGenPresentInterval) = ห่างจากเฟรมกลาง ~ครึ่งเฟรม
//   latency เพิ่ม ~ครึ่งเฟรมสตรีม (8 ms ที่ 60 fps) · ไฟล์อัด/ภาพหน้าจอ/9:16 ยังได้เฟรมจริงเท่านั้น
// Halo: วาดใน QML (StreamView.qml) จากสี Chiaki.session.lightbarColor — ฝั่งนี้เก็บแค่ค่าเปิด/ปิด
#include "qmlmainwindow.h"
#include "pswrapframegen.h"
#include "chiaki/time.h"

#include <algorithm>
#include <cmath>

void QmlMainWindow::pswrapInitVisual()
{
	pswrap_ambient_on.storeRelease(settings->GetAmbientLight() ? 1 : 0);
	pswrap_fg_on.storeRelease(settings->GetFrameGen() ? 1 : 0);
}

bool QmlMainWindow::ambientLight() const { return settings->GetAmbientLight(); }

void QmlMainWindow::setAmbientLight(bool v)
{
	if (v == settings->GetAmbientLight())
		return;
	settings->SetAmbientLight(v);
	pswrap_ambient_on.storeRelease(v ? 1 : 0);
	emit ambientLightChanged();
	scheduleUpdate(true, UpdateRequestReason::RenderRequested);
}

bool QmlMainWindow::lightbarHalo() const { return settings->GetLightbarHalo(); }

void QmlMainWindow::setLightbarHalo(bool v)
{
	if (v == settings->GetLightbarHalo())
		return;
	settings->SetLightbarHalo(v);
	emit lightbarHaloChanged();
}

bool QmlMainWindow::frameGen() const { return settings->GetFrameGen(); }

void QmlMainWindow::setFrameGen(bool v)
{
	if (v == settings->GetFrameGen())
		return;
	settings->SetFrameGen(v);
	pswrap_fg_on.storeRelease(v ? 1 : 0);
	emit frameGenChanged();
}

bool QmlMainWindow::frameGenSupported() const
{
	return bypass_frame_queue && render_backend == RenderBackend::Vulkan;
}

// ---------------------------------------------------------------- ambient

namespace {

// ขนาดขอบว่างรอบกรอบภาพ (px ของ target) · c อาจกลับหัว (OpenGL)
struct Gaps
{
	float left = 0, right = 0, top = 0, bottom = 0;
	float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
	bool any() const { return left > 0.5f || right > 0.5f || top > 0.5f || bottom > 0.5f; }
};

Gaps gapsOf(const pl_frame &target, const pl_rect2df &c)
{
	Gaps g;
	const pl_tex tex = target.num_planes > 0 ? target.planes[0].texture : nullptr;
	if (!tex)
		return g;
	const float w = float(tex->params.w), h = float(tex->params.h);
	g.x0 = std::max(0.0f, std::min(c.x0, c.x1));
	g.x1 = std::min(w, std::max(c.x0, c.x1));
	g.y0 = std::max(0.0f, std::min(c.y0, c.y1));
	g.y1 = std::min(h, std::max(c.y0, c.y1));
	if (g.x1 <= g.x0 || g.y1 <= g.y0)
		return {};
	g.left = g.x0;
	g.right = w - g.x1;
	g.top = g.y0;
	g.bottom = h - g.y1;
	return g;
}

} // namespace

void QmlMainWindow::pswrapAmbientParams(pl_render_params &params, const pl_frame &target_frame) const
{
	if (pswrap_ambient_on.loadAcquire() == 0 || stream_session_active.loadAcquire() == 0)
		return;
	const Gaps g = gapsOf(target_frame, target_frame.crop);
	if (!g.any())
		return;
	const pl_tex tex = target_frame.planes[0].texture;
	params.border = PL_CLEAR_BLUR;
	params.blur_radius = std::max(16.0f, 0.05f * float(std::min(tex->params.w, tex->params.h)));
}

void QmlMainWindow::pswrapAmbientDecorate(pl_frame &target_frame, const pl_rect2df &video_rect)
{
	if (pswrap_ambient_on.loadAcquire() == 0 || stream_session_active.loadAcquire() == 0 || target_frame.num_overlays > 1)
		return;
	const Gaps g = gapsOf(target_frame, video_rect);
	if (!g.any())
		return;
	pl_gpu gpu = placeboGpu();
	if (!gpu)
		return;
	// mask 128 จุด: ครึ่งแรก = นอกสุด → ชิดภาพ (แถบซ้าย/บน) · ครึ่งหลัง = ชิดภาพ → นอกสุด (แถบขวา/ล่าง)
	// ชิดภาพหรี่น้อย (แสงจากขอบเกมยังสว่าง) · ขอบจอหรี่มาก (จางหายเหมือนไฟหลังทีวี)
	constexpr int kN = 128;
	if (!pswrap_ambient_tex_x || !pswrap_ambient_tex_y) {
		pl_fmt fmt = pl_find_fmt(gpu, PL_FMT_UNORM, 1, 8, 8, static_cast<pl_fmt_caps>(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR));
		if (!fmt)
			return;
		static uint8_t mask[kN];
		for (int i = 0; i < kN; i++) {
			const float d = i < kN / 2 ? 1.0f - float(i) / float(kN / 2 - 1) : float(i - kN / 2) / float(kN / 2 - 1);
			mask[i] = static_cast<uint8_t>(std::clamp(0.28f + 0.52f * std::pow(d, 1.4f), 0.0f, 1.0f) * 255.0f + 0.5f);
		}
		pl_tex_params tp = {};
		tp.format = fmt;
		tp.sampleable = true;
		tp.initial_data = mask;
		tp.debug_tag = PL_DEBUG_TAG;
		if (!pswrap_ambient_tex_x) {
			tp.w = kN;
			tp.h = 1;
			pswrap_ambient_tex_x = pl_tex_create(gpu, &tp);
		}
		if (!pswrap_ambient_tex_y) {
			tp.w = 1;
			tp.h = kN;
			pswrap_ambient_tex_y = pl_tex_create(gpu, &tp);
		}
		if (!pswrap_ambient_tex_x || !pswrap_ambient_tex_y)
			return;
	}
	const pl_tex tex = target_frame.planes[0].texture;
	const float w = float(tex->params.w), h = float(tex->params.h);
	const float half = float(kN / 2);
	// OpenGL: target กลับหัว → แถบบนอยู่ท้าย texture
	const bool flip_y = target_frame.crop.y0 > target_frame.crop.y1;
	auto part = [](pl_overlay_part &p, pl_rect2df src, pl_rect2df dst) {
		p = {};
		p.src = src;
		p.dst = dst;
		p.color[0] = p.color[1] = p.color[2] = 0.0f;
		p.color[3] = 1.0f;
	};
	pl_overlay ov = {};
	ov.mode = PL_OVERLAY_MONOCHROME;
	ov.repr = pl_color_repr_rgb;
	ov.color = pl_color_space_srgb;
	int n = 0;
	int np = 0;
	const pl_overlay quick = target_frame.overlays[0];
	if (g.left > 0.5f || g.right > 0.5f) {
		const int first = np;
		if (g.left > 0.5f)
			part(pswrap_ambient_parts[np++], {0, 0, half, 1}, {0, 0, g.x0, h});
		if (g.right > 0.5f)
			part(pswrap_ambient_parts[np++], {half, 0, 2 * half, 1}, {g.x1, 0, w, h});
		pl_overlay &o = pswrap_screen_overlays[n++];
		o = ov;
		o.tex = pswrap_ambient_tex_x;
		o.parts = &pswrap_ambient_parts[first];
		o.num_parts = np - first;
	}
	if (g.top > 0.5f || g.bottom > 0.5f) {
		const int first = np;
		const pl_rect2df top_src = flip_y ? pl_rect2df{0, half, 1, 2 * half} : pl_rect2df{0, 0, 1, half};
		const pl_rect2df bottom_src = flip_y ? pl_rect2df{0, 0, 1, half} : pl_rect2df{0, half, 1, 2 * half};
		// แถบบน/ล่างอยู่ระหว่างแถบซ้าย/ขวา (ไม่ซ้อนมุมให้มืดเกิน)
		if (g.top > 0.5f)
			part(pswrap_ambient_parts[np++], top_src, {g.x0, 0, g.x1, g.y0});
		if (g.bottom > 0.5f)
			part(pswrap_ambient_parts[np++], bottom_src, {g.x0, g.y1, g.x1, h});
		pl_overlay &o = pswrap_screen_overlays[n++];
		o = ov;
		o.tex = pswrap_ambient_tex_y;
		o.parts = &pswrap_ambient_parts[first];
		o.num_parts = np - first;
	}
	pswrap_screen_overlays[n++] = quick;   // QML อยู่บนสุด
	target_frame.overlays = pswrap_screen_overlays;
	target_frame.num_overlays = n;
}

// ---------------------------------------------------------------- frame generation

double QmlMainWindow::pswrapFrameGenPresentInterval(double stream_interval_s) const
{
	return pswrap_fg_active_render ? stream_interval_s * 0.5 : stream_interval_s;
}

void QmlMainWindow::pswrapFrameGenSetActive(bool active)
{
	if (active == pswrap_fg_active_render)
		return;
	pswrap_fg_active_render = active;
	qCInfo(chiakiGui) << "PSWRAP framegen" << (active ? "active" : "inactive");
	QMetaObject::invokeMethod(this, [this, active]() {
		if (pswrap_fg_active_reported == active)
			return;
		pswrap_fg_active_reported = active;
		emit frameGenActiveChanged();
	}, Qt::QueuedConnection);
}

bool QmlMainWindow::pswrapFrameGenRender(const pl_frame *direct_frame, bool new_frame, const pl_render_params &params,
                                         pl_frame &target_frame, const pl_rect2df &video_rect, double stream_interval_s,
                                         double refresh_interval_s)
{
	Q_UNUSED(video_rect);   // target_frame.crop = กรอบภาพอยู่แล้ว (push ใช้ตรงนั้น · present ตั้งเต็มจอเอง)
	pswrap_fg_new_image = new_frame;   // วาดซ้ำ (UI เปลี่ยน) = ไม่ใช่ภาพใหม่ · เฟรมจริงที่รอต่อจากเฟรมกลางตั้งเป็น true ด้านล่าง
	const bool want = pswrap_fg_on.loadAcquire() != 0 &&
		render_backend == RenderBackend::Vulkan &&
		stream_session_active.loadAcquire() != 0 &&
		direct_frame && direct_frame->num_planes > 0 &&
		stream_interval_s > 0.0 && refresh_interval_s > 0.0 &&
		refresh_interval_s * 1.3 <= stream_interval_s;
	if (!want) {
		if (pswrap_fg)
			pswrap_fg->reset();
		pswrap_fg_pending.storeRelease(0);
		pswrapFrameGenSetActive(false);
		return false;
	}
	pl_gpu gpu = placeboGpu();
	if (!gpu || !placebo_renderer)
		return false;
	if (!pswrap_fg)
		pswrap_fg = new PsWrapFrameGen(gpu, placebo_log);

	if (new_frame) {
		if (!pswrap_fg->push(placebo_renderer, *direct_frame, target_frame, params)) {
			pswrap_fg->reset();
			pswrap_fg_pending.storeRelease(0);
			pswrapFrameGenSetActive(false);
			return false;
		}
		if (pswrap_fg->hasPair() && pswrap_fg->interpolate(0.5f) && pswrap_fg->present(placebo_renderer, true, target_frame)) {
			pswrap_fg_pending.storeRelease(1);
			pswrap_fg_mid_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
			pswrapFrameGenSetActive(true);
			return true;
		}
		// เฟรมแรก / สร้างเฟรมกลางไม่ได้ → แสดงเฟรมจริงเลย
		pswrap_fg_pending.storeRelease(0);
		return pswrap_fg->present(placebo_renderer, false, target_frame);
	}
	// ไม่มีเฟรมใหม่: เฟรมจริงที่รอต่อจากเฟรมกลาง (throttle เว้นครึ่งเฟรมให้แล้ว) หรือวาดซ้ำเพราะ UI เปลี่ยน
	pswrap_fg_new_image = pswrap_fg_pending.fetchAndStoreRelaxed(0) != 0;
	return pswrap_fg->present(placebo_renderer, false, target_frame);
}

void QmlMainWindow::pswrapDestroyVisual()
{
	delete pswrap_fg;
	pswrap_fg = nullptr;
	if (pl_gpu gpu = placeboGpu()) {
		if (pswrap_ambient_tex_x)
			pl_tex_destroy(gpu, &pswrap_ambient_tex_x);
		if (pswrap_ambient_tex_y)
			pl_tex_destroy(gpu, &pswrap_ambient_tex_y);
	}
}
