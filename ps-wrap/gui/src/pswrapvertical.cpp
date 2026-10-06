// PS-WRAP: ภาพแนวตั้ง 9:16 — เลย์เอาต์ + preview (ดู pswrapvertical.h)
#include <pswrapvertical.h>

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(pswrapRec)

namespace {

pl_rect2df rect(float x0, float y0, float x1, float y1) { return pl_rect2df{x0, y0, x1, y1}; }

// วางกล่องสัดส่วน aspect ให้ใหญ่สุดใน region (ไม่ตัด) กึ่งกลาง
pl_rect2df containIn(const pl_rect2df &r, float aspect)
{
	const float rw = r.x1 - r.x0, rh = r.y1 - r.y0;
	if (rw <= 0 || rh <= 0 || aspect <= 0)
		return {};
	float w = rw, h = rw / aspect;
	if (h > rh) {
		h = rh;
		w = rh * aspect;
	}
	const float x = r.x0 + (rw - w) * 0.5f, y = r.y0 + (rh - h) * 0.5f;
	return rect(x, y, x + w, y + h);
}

// ส่วนของ src ที่ตัดมาเต็มกล่องสัดส่วน dst_aspect (cover) · crop_x เลื่อนแนวนอน
pl_rect2df coverSlice(const pl_rect2df &src, float dst_aspect, float crop_x)
{
	const float sw = std::fabs(src.x1 - src.x0), sh = std::fabs(src.y1 - src.y0);
	const float sx = std::min(src.x0, src.x1), sy = std::min(src.y0, src.y1);
	if (sw <= 0 || sh <= 0 || dst_aspect <= 0)
		return src;
	if (sw / sh > dst_aspect) {
		const float w = sh * dst_aspect;
		const float x = sx + std::clamp(crop_x, 0.0f, 1.0f) * (sw - w);
		return rect(x, sy, x + w, sy + sh);
	}
	const float h = sw / dst_aspect;
	const float y = sy + (sh - h) * 0.5f;
	return rect(sx, y, sx + sw, y + h);
}

} // namespace

PsWrapVerticalGeometry pswrapVerticalGeometry(const PsWrapVerticalLayout &layout, float W, float H,
                                              const pl_rect2df &src, float cam_aspect)
{
	PsWrapVerticalGeometry g;
	const float m = H * 0.02f;   // ระยะขอบ facecam
	const bool cam = cam_aspect > 0.0f;
	switch (layout.mode) {
	case PsWrapVerticalLayout::Blur: {
		const float sw = std::fabs(src.x1 - src.x0), sh = std::fabs(src.y1 - src.y0);
		const float band = sh > 0 ? W * sh / sw : H;
		const float y0 = (H - band) * 0.5f;
		g.src_crop = src;
		g.game_dst = rect(0, y0, W, y0 + band);
		g.blur_border = true;
		if (cam)
			g.cam_dst = containIn(rect(m, m, W - m, y0 - m), cam_aspect);
		break;
	}
	case PsWrapVerticalLayout::Center:
		g.game_dst = rect(0, 0, W, H);
		g.src_crop = coverSlice(src, W / H, layout.crop_x);
		if (cam) {
			const float cw = W * 0.42f;
			g.cam_dst = containIn(rect((W - cw) * 0.5f, H * 0.04f, (W + cw) * 0.5f, H * 0.04f + cw), cam_aspect);
		}
		break;
	case PsWrapVerticalLayout::Split:
	default: {
		const float top = cam ? H * 0.40f : 0.0f;
		g.game_dst = rect(0, top, W, H);
		g.src_crop = coverSlice(src, W / (H - top), layout.crop_x);
		if (cam)
			g.cam_dst = containIn(rect(m, m, W - m, top - m), cam_aspect);
		break;
	}
	}
	return g;
}

// ---------------------------------------------------------------- preview

struct PsWrapVerticalPreview::Job
{
	PsWrapVerticalPreview *owner = nullptr;
	QImage img;
	bool ok = false;
	DoneFn done;
};

PsWrapVerticalPreview::PsWrapVerticalPreview(pl_gpu gpu, pl_log log)
	: gpu(gpu), log(log)
{
}

PsWrapVerticalPreview::~PsWrapVerticalPreview()
{
	if (inflight.load(std::memory_order_acquire) > 0)
		pl_gpu_finish(gpu);
	if (tex)
		pl_tex_destroy(gpu, &tex);
	if (renderer)
		pl_renderer_destroy(&renderer);
}

bool PsWrapVerticalPreview::capture(const pl_frame_mix *mix, const pl_frame *single, const pl_render_params &params,
                                    const pl_frame &screen_target, const pl_overlay *overlay, int screen_w, int screen_h,
                                    const PsWrapVerticalLayout &layout, int W, int H, DoneFn done)
{
	if (!idle() || screen_w <= 0 || screen_h <= 0 || W <= 0 || H <= 0)
		return false;
	if (!single && (!mix || mix->num_frames <= 0))
		return false;
	const pl_frame *src = single ? single : mix->frames[0];
	if (!src)
		return false;
	if (!renderer)
		renderer = pl_renderer_create(log, gpu);
	if (!renderer)
		return false;
	if (!tex || tex->params.w != W || tex->params.h != H) {
		if (tex)
			pl_tex_destroy(gpu, &tex);
		pl_fmt fmt = pl_find_named_fmt(gpu, "rgba8");   // ลำดับ byte ตรงกับ QImage RGBX8888
		const auto need = static_cast<pl_fmt_caps>(PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE);
		if (!fmt || (fmt->caps & need) != need)
			return false;
		pl_tex_params p = {};
		p.w = W;
		p.h = H;
		p.format = fmt;
		p.renderable = true;
		p.host_readable = true;
		p.blit_dst = (fmt->caps & PL_FMT_CAP_BLITTABLE) != 0;
		p.debug_tag = PL_DEBUG_TAG;
		tex = pl_tex_create(gpu, &p);
		if (!tex)
			return false;
	}

	// กรอบ facecam บนจอ ∩ จอ — ใช้เป็นทั้งสัดส่วนและส่วนที่ตัดจาก quick_tex
	QRectF cam = layout.cam.intersected(QRectF(0, 0, screen_w, screen_h));
	const bool has_cam = overlay && overlay->tex && overlay->num_parts > 0 && cam.width() >= 2 && cam.height() >= 2;
	const PsWrapVerticalGeometry g = pswrapVerticalGeometry(layout, float(W), float(H), src->crop,
	                                                        has_cam ? float(cam.width() / cam.height()) : 0.0f);

	pl_frame t = {};
	t.num_planes = 1;
	t.planes[0].texture = tex;
	t.planes[0].components = 4;
	t.planes[0].component_mapping[0] = PL_CHANNEL_R;
	t.planes[0].component_mapping[1] = PL_CHANNEL_G;
	t.planes[0].component_mapping[2] = PL_CHANNEL_B;
	t.planes[0].component_mapping[3] = PL_CHANNEL_A;
	t.repr = pl_color_repr_rgb;
	t.repr.alpha = PL_ALPHA_INDEPENDENT;
	t.repr.bits.sample_depth = 8;
	t.repr.bits.color_depth = 8;
	t.color = pl_color_space_srgb;   // สตรีม HDR → tone-map ลง SDR
	t.crop = g.game_dst;

	pl_overlay ov = {};
	pl_overlay_part part = {};
	if (has_cam && g.cam_dst.x1 > g.cam_dst.x0) {
		ov = *overlay;
		part = overlay->parts[0];
		const pl_rect2df c = screen_target.crop;
		if (c.y0 > c.y1)   // swapchain OpenGL กลับหัว
			std::swap(part.src.y0, part.src.y1);
		const pl_rect2df full = part.src;   // quick_tex ครอบทั้งจอ
		part.src.x0 = full.x0 + (full.x1 - full.x0) * float(cam.left() / screen_w);
		part.src.x1 = full.x0 + (full.x1 - full.x0) * float(cam.right() / screen_w);
		part.src.y0 = full.y0 + (full.y1 - full.y0) * float(cam.top() / screen_h);
		part.src.y1 = full.y0 + (full.y1 - full.y0) * float(cam.bottom() / screen_h);
		part.dst = g.cam_dst;
		ov.parts = &part;
		ov.num_parts = 1;
		t.overlays = &ov;
		t.num_overlays = 1;
	}

	pl_render_params p = params;
	p.hooks = nullptr;
	p.num_hooks = 0;
	p.info_callback = nullptr;
	p.background_transparency = 0.0f;
	p.border = g.blur_border ? PL_CLEAR_BLUR : PL_CLEAR_COLOR;
	p.blur_radius = float(W) * 0.04f;
	p.background_color[0] = p.background_color[1] = p.background_color[2] = 0.0f;

	// ตัด source ตามเลย์เอาต์ — สำเนาเฟรม (signature ผสม crop กัน cache ของ renderer ใช้ภาพที่ตัดคนละแบบ)
	bool ok = false;
	const uint64_t salt = (uint64_t(g.src_crop.x0 * 16) << 32) ^ uint64_t(g.src_crop.x1 * 16) ^ (uint64_t(layout.mode) << 56);
	if (single) {
		pl_frame f = *single;
		f.crop = g.src_crop;
		ok = pl_render_image(renderer, &f, &t, &p);
	} else {
		std::vector<pl_frame> frames(size_t(mix->num_frames));
		std::vector<const pl_frame *> ptrs(size_t(mix->num_frames));
		std::vector<uint64_t> sigs(size_t(mix->num_frames));
		for (int i = 0; i < mix->num_frames; i++) {
			frames[size_t(i)] = *mix->frames[i];
			frames[size_t(i)].crop = g.src_crop;
			ptrs[size_t(i)] = &frames[size_t(i)];
			sigs[size_t(i)] = mix->signatures[i] ^ salt;
		}
		pl_frame_mix m = *mix;
		m.frames = ptrs.data();
		m.signatures = sigs.data();
		ok = pl_render_image_mix(renderer, &m, &t, &p);
	}
	if (!ok) {
		static bool warned = false;
		if (!warned) {
			warned = true;
			qCWarning(pswrapRec) << "vertical: render failed";
		}
		return false;
	}

	auto *job = new Job;
	job->owner = this;
	job->done = std::move(done);
	job->img = QImage(W, H, QImage::Format_RGBX8888);
	if (job->img.isNull()) {
		delete job;
		return false;
	}
	pl_tex_transfer_params d = {};
	d.tex = tex;
	d.ptr = job->img.bits();
	d.row_pitch = size_t(job->img.bytesPerLine());
	const bool async = gpu->limits.callbacks;
	inflight.fetch_add(1, std::memory_order_acq_rel);
	if (async) {
		job->ok = true;
		d.callback = &PsWrapVerticalPreview::onDownloaded;
		d.priv = job;
		if (!pl_tex_download(gpu, &d)) {
			job->ok = false;
			onDownloaded(job);   // ไม่มี callback → ปิดงานเอง
		}
		return true;
	}
	job->ok = pl_tex_download(gpu, &d);
	onDownloaded(job);
	return true;
}

void PsWrapVerticalPreview::onDownloaded(void *priv)
{
	auto *job = static_cast<Job *>(priv);
	if (!job)
		return;
	PsWrapVerticalPreview *owner = job->owner;
	if (job->done && job->ok)
		job->done(std::move(job->img));
	delete job;
	owner->inflight.fetch_sub(1, std::memory_order_acq_rel);
}
