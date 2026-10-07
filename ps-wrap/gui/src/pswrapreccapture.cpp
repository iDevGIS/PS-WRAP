// PS-WRAP: จับภาพสำหรับอัดวิดีโอบน render thread (ดู pswrapreccapture.h)
#include <pswrapreccapture.h>

#include <chiaki/time.h>

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
}

Q_DECLARE_LOGGING_CATEGORY(pswrapRec)

namespace {

struct SourceState
{
	QMutex mutex;
	bool valid = false;
	bool hdr = false;
	int height = 0;
	pl_hdr_metadata meta = {};
};

SourceState &sourceState()
{
	static SourceState s;
	return s;
}

size_t alignUp(size_t v, size_t a)
{
	return a > 1 ? (v + a - 1) / a * a : v;
}

} // namespace

PsWrapRecCapture::PsWrapRecCapture(pl_gpu gpu, pl_log log)
	: gpu(gpu), log(log)
{
}

PsWrapRecCapture::~PsWrapRecCapture()
{
	releaseResources();
	if (renderer)
		pl_renderer_destroy(&renderer);
	if (no_slot_drops)
		qCInfo(pswrapRec) << "capture: frames skipped because every readback slot was busy:" << no_slot_drops;
}

bool PsWrapRecCapture::canDestroy() const
{
	for (const auto &s : ring)
		if (s.state.load(std::memory_order_acquire) == 2)
			return false;
	return true;
}

void PsWrapRecCapture::releaseSlot(void *opaque, uint8_t *)
{
	static_cast<Slot *>(opaque)->state.store(0, std::memory_order_release);
}

void PsWrapRecCapture::releaseResources()
{
	inflight.clear();
	for (auto &s : ring) {
		if (s.buf)
			pl_buf_destroy(gpu, &s.buf); // buffer ที่ GPU ยังเขียนอยู่ — libplacebo รอให้เสร็จก่อนคืนเอง
		s.state.store(0);
	}
	if (tex_y)
		pl_tex_destroy(gpu, &tex_y);
	if (tex_uv)
		pl_tex_destroy(gpu, &tex_uv);
	width = height = 0;
}

bool PsWrapRecCapture::ensureResources(int w, int h, bool is_hdr)
{
	if (tex_y && w == width && h == height && is_hdr == hdr)
		return true;
	releaseResources();
	if (!renderer)
		renderer = pl_renderer_create(log, gpu);
	if (!renderer) {
		qCWarning(pswrapRec) << "capture: pl_renderer_create failed";
		return false;
	}

	const int bits = is_hdr ? 16 : 8; // HDR: วาด 16-bit เต็ม แล้วส่งเป็น P010 (10 บิตบนสุด = ค่าที่ quantize แล้ว)
	const auto caps = static_cast<pl_fmt_caps>(PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE);
	pl_fmt fmt_y = pl_find_fmt(gpu, PL_FMT_UNORM, 1, bits, bits, caps);
	pl_fmt fmt_uv = pl_find_fmt(gpu, PL_FMT_UNORM, 2, bits, bits, caps);
	if (!fmt_y || !fmt_uv) {
		qCWarning(pswrapRec) << "capture: no renderable+readable" << bits << "bit formats";
		return false;
	}
	auto make_tex = [this](int tw, int th, pl_fmt fmt) {
		pl_tex_params p = {};
		p.w = tw;
		p.h = th;
		p.format = fmt;
		p.renderable = true;
		p.host_readable = true;
		p.blit_dst = (fmt->caps & PL_FMT_CAP_BLITTABLE) != 0;
		p.storable = (fmt->caps & PL_FMT_CAP_STORABLE) != 0;
		p.debug_tag = PL_DEBUG_TAG;
		return pl_tex_create(gpu, &p);
	};
	tex_y = make_tex(w, h, fmt_y);
	tex_uv = make_tex(w / 2, h / 2, fmt_uv);
	if (!tex_y || !tex_uv) {
		qCWarning(pswrapRec) << "capture: texture creation failed" << w << h;
		releaseResources();
		return false;
	}

	const size_t bpp = is_hdr ? 2 : 1;
	const size_t pitch_align = std::max<size_t>({gpu->limits.align_tex_xfer_pitch, fmt_y->texel_align, fmt_uv->texel_align, 256});
	const size_t offset_align = std::max<size_t>(gpu->limits.align_tex_xfer_offset, 256);
	pitch_y = alignUp(static_cast<size_t>(w) * bpp, pitch_align);
	pitch_uv = alignUp(static_cast<size_t>(w / 2) * 2 * bpp, pitch_align);
	off_uv = alignUp(pitch_y * h, offset_align);
	buf_size = off_uv + pitch_uv * (h / 2);
	if (gpu->limits.max_mapped_size && buf_size > gpu->limits.max_mapped_size) {
		qCWarning(pswrapRec) << "capture: readback buffer too large" << buf_size;
		releaseResources();
		return false;
	}
	for (auto &s : ring) {
		pl_buf_params bp = {};
		bp.size = buf_size;
		bp.host_mapped = true;
		bp.memory_type = PL_BUF_MEM_HOST;
		bp.debug_tag = PL_DEBUG_TAG;
		s.buf = pl_buf_create(gpu, &bp);
		if (!s.buf || !s.buf->data) {
			qCWarning(pswrapRec) << "capture: host-mapped buffer creation failed" << buf_size;
			releaseResources();
			return false;
		}
	}
	width = w;
	height = h;
	hdr = is_hdr;
	qCInfo(pswrapRec) << "capture: resources" << w << "x" << h << (is_hdr ? "P010" : "NV12")
		<< "fmt" << fmt_y->name << fmt_uv->name << "pitch" << pitch_y << pitch_uv << "ring" << kSlots;
	return true;
}

void PsWrapRecCapture::collect(PsWrapRecorder *rec)
{
	while (!inflight.empty()) {
		const int i = inflight.front();
		Slot &s = ring[i];
		if (pl_buf_poll(gpu, s.buf, 0))
			break; // ยังไม่เสร็จ — เก็บตามลำดับ ไม่ข้าม
		inflight.pop_front();

		AVFrame *f = av_frame_alloc();
		if (!f) {
			s.state.store(0);
			continue;
		}
		f->format = hdr ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
		f->width = width;
		f->height = height;
		f->color_range = AVCOL_RANGE_MPEG;
		f->chroma_location = AVCHROMA_LOC_LEFT;
		f->colorspace = hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
		f->color_primaries = hdr ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
		f->color_trc = hdr ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_BT709;
		uint8_t *base = s.buf->data;
		s.state.store(2, std::memory_order_release);
		f->buf[0] = av_buffer_create(base, buf_size, &PsWrapRecCapture::releaseSlot, &s, 0);
		if (!f->buf[0]) {
			s.state.store(0);
			av_frame_free(&f);
			continue;
		}
		f->data[0] = base;
		f->linesize[0] = static_cast<int>(pitch_y);
		f->data[1] = base + off_uv;
		f->linesize[1] = static_cast<int>(pitch_uv);
		rec->pushVideoFrame(f, s.capture_us); // รับเป็นเจ้าของ — unref แล้ว slot ว่างเอง
	}
}

void PsWrapRecCapture::capture(PsWrapRecorder *rec, const pl_frame_mix *mix, const pl_frame *single,
                               const pl_render_params &params, const pl_frame &screen_target,
                               const pl_overlay *overlay, int screen_w, int screen_h, const pl_hook *upscaler,
                               const PsWrapVerticalLayout *vertical)
{
	if (broken || !rec || screen_w <= 0 || screen_h <= 0)
		return;
	if (!single && (!mix || mix->num_frames <= 0))
		return;
	int w = 0, h = 0;
	bool is_hdr = false;
	if (!rec->videoSpec(&w, &h, &is_hdr)) {
		// อัดจบแล้ว: เฟรมที่ค้างใน GPU ทิ้งไป (ไม่ให้หลุดไปปนไฟล์ถัดไป)
		for (int i : inflight)
			ring[i].state.store(0, std::memory_order_release);
		inflight.clear();
		return;
	}
	if (!(tex_y && w == width && h == height && is_hdr == hdr) && !canDestroy())
		return; // ไฟล์ก่อนหน้ายังถือ slot อยู่ — รอรอบหน้า
	if (!ensureResources(w, h, is_hdr)) {
		broken = true; // ไม่วนสร้างซ้ำทุกเฟรม — ไฟล์นี้จะไม่มีภาพ (log บอกแล้ว)
		return;
	}

	collect(rec);

	const qint64 now_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
	if (!rec->wantsVideoFrame(now_us))
		return;
	int slot = -1;
	for (int i = 0; i < kSlots; i++) {
		if (ring[i].state.load(std::memory_order_acquire) == 0) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		no_slot_drops++;
		return;
	}

	pl_frame t = {};
	t.num_planes = 2;
	t.planes[0].texture = tex_y;
	t.planes[0].components = 1;
	t.planes[0].component_mapping[0] = PL_CHANNEL_Y;
	t.planes[1].texture = tex_uv;
	t.planes[1].components = 2;
	t.planes[1].component_mapping[0] = PL_CHANNEL_CB;
	t.planes[1].component_mapping[1] = PL_CHANNEL_CR;
	t.repr.sys = hdr ? PL_COLOR_SYSTEM_BT_2020_NC : PL_COLOR_SYSTEM_BT_709;
	t.repr.levels = PL_COLOR_LEVELS_LIMITED;
	t.repr.alpha = PL_ALPHA_NONE;
	t.repr.bits.sample_depth = hdr ? 16 : 8;
	t.repr.bits.color_depth = hdr ? 16 : 8;
	t.repr.bits.bit_shift = 0;
	t.color.primaries = hdr ? PL_COLOR_PRIM_BT_2020 : PL_COLOR_PRIM_BT_709;
	t.color.transfer = hdr ? PL_COLOR_TRC_PQ : PL_COLOR_TRC_BT_1886;
	if (hdr) {
		const pl_frame *src = single ? single : mix->frames[0];
		if (src && pl_color_space_is_hdr(&src->color))
			t.color.hdr = src->color.hdr; // ให้ peak เท่า source → ไม่ tone-map
	}
	pl_frame_set_chroma_location(&t, PL_CHROMA_LEFT);

	bool ok = false;
	if (vertical) {
		// ภาพแนวตั้ง 9:16 — เลย์เอาต์เดียวกับหน้าต่าง preview (pswrapvertical.cpp)
		ok = pswrapRenderVertical(renderer, mix, single, params, screen_target, overlay, screen_w, screen_h, *vertical, t, nullptr);
	} else {
		// เอาเฉพาะ "กรอบวิดีโอบนจอ" (ตัดส่วนที่ล้นจอตอน Zoom) มาวางเต็มกรอบไฟล์แบบคงสัดส่วน
		// → ไม่มีขอบดำของหน้าต่าง (จอ ultrawide / หน้าต่างไม่ใช่ 16:9) · Stretch/Zoom ที่สัดส่วนไม่ใช่ 16:9 → มีขอบในไฟล์
		const pl_rect2df c = screen_target.crop;
		const bool flipped = c.y0 > c.y1; // swapchain OpenGL กลับหัว — target ของเราไม่กลับ
		const float vx0 = std::min(c.x0, c.x1), vx1 = std::max(c.x0, c.x1);
		const float vy0 = std::min(c.y0, c.y1), vy1 = std::max(c.y0, c.y1);
		const float rx0 = std::max(vx0, 0.0f), rx1 = std::min(vx1, float(screen_w));
		const float ry0 = std::max(vy0, 0.0f), ry1 = std::min(vy1, float(screen_h));
		if (rx1 - rx0 < 2.0f || ry1 - ry0 < 2.0f)
			return; // วิดีโอไม่อยู่บนจอ (หน้าต่างย่อ/ยังไม่มีภาพ)
		const float s = std::min(float(w) / (rx1 - rx0), float(h) / (ry1 - ry0));
		const float ox = (float(w) - (rx1 - rx0) * s) * 0.5f;
		const float oy = (float(h) - (ry1 - ry0) * s) * 0.5f;
		t.crop.x0 = ox + (vx0 - rx0) * s;
		t.crop.x1 = ox + (vx1 - rx0) * s;
		t.crop.y0 = oy + (vy0 - ry0) * s;
		t.crop.y1 = oy + (vy1 - ry0) * s;

		pl_overlay ov = {};
		pl_overlay_part part = {};
		if (overlay && overlay->tex && overlay->num_parts > 0) {
			ov = *overlay;
			part = overlay->parts[0];
			if (flipped)
				std::swap(part.src.y0, part.src.y1);
			// quick_tex ครอบทั้งจอ → ตัดส่วนเดียวกับกรอบวิดีโอ (src มีเครื่องหมายได้ตอนกลับหัว — สูตรเดียวกัน)
			const pl_rect2df full = part.src;
			part.src.x0 = full.x0 + (full.x1 - full.x0) * (rx0 / screen_w);
			part.src.x1 = full.x0 + (full.x1 - full.x0) * (rx1 / screen_w);
			part.src.y0 = full.y0 + (full.y1 - full.y0) * (ry0 / screen_h);
			part.src.y1 = full.y0 + (full.y1 - full.y0) * (ry1 / screen_h);
			part.dst = {ox, oy, ox + (rx1 - rx0) * s, oy + (ry1 - ry0) * s};
			ov.parts = &part;
			ov.num_parts = 1;
			t.overlays = &ov;
			t.num_overlays = 1;
		}

		pl_render_params p = params;
		p.hooks = nullptr;      // hook ของจอคิดจากขนาดจอ — ไฟล์เลือกเอง
		p.num_hooks = 0;
		// ไฟล์ใหญ่กว่า source (Output 1440p/4K) → upscaler ตามปุ่ม QUALITY (FSRCNNX/FSR) · ไม่ขยาย = ไม่ใช้
		const pl_frame *src_frame = single ? single : mix->frames[0];
		const float src_h = src_frame ? std::fabs(pl_rect_h(src_frame->crop)) : 0.0f;
		if (upscaler && src_h > 0.0f && (vy1 - vy0) * s > src_h * 1.01f) {
			p.hooks = &upscaler;
			p.num_hooks = 1;
		}
		p.background_transparency = 0.0f;
		p.info_callback = nullptr;
		ok = single ? pl_render_image(renderer, single, &t, &p)
		                       : pl_render_image_mix(renderer, mix, &t, &p);
	}
	if (!ok) {
		static bool warned = false;
		if (!warned) {
			warned = true;
			qCWarning(pswrapRec) << "capture: pl_render_image failed";
		}
		return;
	}

	Slot &sl = ring[slot];
	pl_tex_transfer_params d = {};
	d.tex = tex_y;
	d.buf = sl.buf;
	d.buf_offset = 0;
	d.row_pitch = pitch_y;
	bool dl = pl_tex_download(gpu, &d);
	d = {};
	d.tex = tex_uv;
	d.buf = sl.buf;
	d.buf_offset = off_uv;
	d.row_pitch = pitch_uv;
	dl = dl && pl_tex_download(gpu, &d);
	if (!dl) {
		qCWarning(pswrapRec) << "capture: pl_tex_download failed — video capture disabled for this recording";
		broken = true;
		return;
	}
	sl.capture_us = now_us;
	sl.state.store(1, std::memory_order_release);
	inflight.push_back(slot);
}

void PsWrapRecCapture::noteSource(const pl_frame *frame)
{
	if (!frame)
		return;
	SourceState &st = sourceState();
	QMutexLocker locker(&st.mutex);
	st.valid = true;
	st.hdr = frame->color.transfer == PL_COLOR_TRC_PQ || frame->color.transfer == PL_COLOR_TRC_HLG;
	st.meta = frame->color.hdr;
	st.height = static_cast<int>(std::lround(std::fabs(pl_rect_h(frame->crop))));
}

bool PsWrapRecCapture::sourceInfo(bool *is_hdr, PsWrapRecHdrInfo *info, int *src_height)
{
	SourceState &st = sourceState();
	QMutexLocker locker(&st.mutex);
	if (!st.valid)
		return false;
	*is_hdr = st.hdr;
	*src_height = st.height;
	PsWrapRecHdrInfo out;
	const pl_hdr_metadata &m = st.meta;
	if (m.prim.red.x > 0.0f && m.prim.green.x > 0.0f && m.prim.blue.x > 0.0f && m.prim.white.x > 0.0f) {
		out.prim_r[0] = m.prim.red.x; out.prim_r[1] = m.prim.red.y;
		out.prim_g[0] = m.prim.green.x; out.prim_g[1] = m.prim.green.y;
		out.prim_b[0] = m.prim.blue.x; out.prim_b[1] = m.prim.blue.y;
		out.white[0] = m.prim.white.x; out.white[1] = m.prim.white.y;
	}
	if (m.max_luma > 0.0f)
		out.max_luma = m.max_luma;
	if (m.min_luma > 0.0f)
		out.min_luma = m.min_luma;
	out.max_cll = m.max_cll;
	out.max_fall = m.max_fall;
	out.valid = true;
	*info = out;
	return true;
}

// ---------------------------------------------------------------- ภาพหน้าจอ (PsWrapShotCapture)

// งาน download 1 ครั้ง (SDR + PQ ถ้ามี) — ถือ QImage ที่ GPU เขียนลงตรงๆ · ลบตัวเองเมื่อ callback ครบ
struct PsWrapShotCapture::Job
{
	PsWrapShotCapture *owner = nullptr;
	int pending = 0;           // callback ของ libplacebo ไม่รันซ้อนกัน → int ธรรมดาพอ
	bool sdr_ok = false, pq_ok = false;
	QImage sdr, pq;
	DoneFn done;
};

PsWrapShotCapture::PsWrapShotCapture(pl_gpu gpu, pl_log log)
	: gpu(gpu), log(log)
{
}

PsWrapShotCapture::~PsWrapShotCapture()
{
	if (inflight.load(std::memory_order_acquire) > 0)
		pl_gpu_finish(gpu); // ให้ callback ที่ค้างยิงให้ครบก่อน (Job ชี้กลับมาที่ this)
	if (tex_sdr)
		pl_tex_destroy(gpu, &tex_sdr);
	if (tex_pq)
		pl_tex_destroy(gpu, &tex_pq);
	if (renderer)
		pl_renderer_destroy(&renderer);
}

pl_tex PsWrapShotCapture::ensureTex(pl_tex *tex, int w, int h, int bits)
{
	if (*tex && (*tex)->params.w == w && (*tex)->params.h == h)
		return *tex;
	if (*tex)
		pl_tex_destroy(gpu, tex);
	// ต้องเป็น rgba ตามลำดับ byte ของ QImage RGBX8888/RGBX64 (pl_find_fmt อาจคืน bgra)
	pl_fmt fmt = pl_find_named_fmt(gpu, bits == 16 ? "rgba16" : "rgba8");
	const auto need = static_cast<pl_fmt_caps>(PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE);
	if (!fmt || (fmt->caps & need) != need || fmt->type != PL_FMT_UNORM) {
		qCWarning(pswrapRec) << "shot: no renderable+readable rgba" << bits << "format";
		return nullptr;
	}
	pl_tex_params p = {};
	p.w = w;
	p.h = h;
	p.format = fmt;
	p.renderable = true;
	p.host_readable = true;
	p.blit_dst = (fmt->caps & PL_FMT_CAP_BLITTABLE) != 0;
	p.storable = (fmt->caps & PL_FMT_CAP_STORABLE) != 0;
	p.debug_tag = PL_DEBUG_TAG;
	*tex = pl_tex_create(gpu, &p);
	if (!*tex)
		qCWarning(pswrapRec) << "shot: texture creation failed" << w << h << bits;
	return *tex;
}

bool PsWrapShotCapture::renderTo(pl_tex tex, bool pq, const pl_frame *src_hint, const pl_frame_mix *mix,
                                 const pl_frame *single, const pl_render_params &params,
                                 const pl_frame &screen_target, const pl_overlay *overlay,
                                 int screen_w, int screen_h)
{
	const int w = tex->params.w, h = tex->params.h;
	pl_frame t = {};
	t.num_planes = 1;
	t.planes[0].texture = tex;
	t.planes[0].components = 4;
	t.planes[0].component_mapping[0] = PL_CHANNEL_R;
	t.planes[0].component_mapping[1] = PL_CHANNEL_G;
	t.planes[0].component_mapping[2] = PL_CHANNEL_B;
	t.planes[0].component_mapping[3] = PL_CHANNEL_A;
	t.repr = pl_color_repr_rgb;
	t.repr.alpha = PL_ALPHA_INDEPENDENT; // background_transparency = 0 → alpha = 1 ทั้งภาพ (ตรงกับ RGBX ของ QImage)
	t.repr.bits.sample_depth = pq ? 16 : 8;
	t.repr.bits.color_depth = pq ? 16 : 8;
	if (pq) {
		t.color = pl_color_space_hdr10; // BT.2020 + PQ
		if (src_hint && pl_color_space_is_hdr(&src_hint->color))
			t.color.hdr = src_hint->color.hdr; // peak เท่า source → ไม่ tone-map
	} else {
		t.color = pl_color_space_srgb; // สตรีม HDR → libplacebo tone-map ลง SDR ตาม params ของจอ
	}

	// วาง "ทั้งจอ" ลงภาพแบบคงสัดส่วน (ปกติ s = 1 = ขนาด swapchain พอดี)
	const float s = std::min(float(w) / float(screen_w), float(h) / float(screen_h));
	const float ox = (float(w) - screen_w * s) * 0.5f;
	const float oy = (float(h) - screen_h * s) * 0.5f;
	const pl_rect2df c = screen_target.crop;
	const bool flipped = c.y0 > c.y1; // swapchain OpenGL กลับหัว — texture ของเราไม่กลับ
	t.crop.x0 = ox + std::min(c.x0, c.x1) * s;
	t.crop.x1 = ox + std::max(c.x0, c.x1) * s;
	t.crop.y0 = oy + std::min(c.y0, c.y1) * s;
	t.crop.y1 = oy + std::max(c.y0, c.y1) * s;

	pl_overlay ov = {};
	pl_overlay_part part = {};
	if (overlay && overlay->tex && overlay->num_parts > 0) {
		ov = *overlay;
		part = overlay->parts[0];
		if (flipped)
			std::swap(part.src.y0, part.src.y1);
		part.dst = {ox, oy, ox + screen_w * s, oy + screen_h * s};
		ov.parts = &part;
		ov.num_parts = 1;
		t.overlays = &ov;
		t.num_overlays = 1;
	}

	pl_render_params p = params;
	p.hooks = nullptr; // user shader hooks มี state ผูกกับ renderer หลัก — ไม่แชร์
	p.num_hooks = 0;
	p.background_transparency = 0.0f;
	p.info_callback = nullptr;
	return single ? pl_render_image(renderer, single, &t, &p)
	              : pl_render_image_mix(renderer, mix, &t, &p);
}

bool PsWrapShotCapture::capture(const pl_frame_mix *mix, const pl_frame *single,
                                const pl_render_params &params, const pl_frame &screen_target,
                                const pl_overlay *overlay, int screen_w, int screen_h, DoneFn done)
{
	if (screen_w <= 0 || screen_h <= 0)
		return false;
	if (!single && (!mix || mix->num_frames <= 0))
		return false;
	if (!renderer)
		renderer = pl_renderer_create(log, gpu);
	if (!renderer) {
		qCWarning(pswrapRec) << "shot: pl_renderer_create failed";
		return false;
	}

	// ขนาดภาพ = ขนาด swapchain (ทุก pixel ตรงกับจอ) · ย่อคงสัดส่วนเฉพาะเมื่อเกินขีด GPU / 8192
	int w = screen_w, h = screen_h;
	const int max_dim = static_cast<int>(std::min<uint32_t>(gpu->limits.max_tex_2d_dim ? gpu->limits.max_tex_2d_dim : 8192, 8192));
	if (w > max_dim || h > max_dim) {
		const double k = std::min(double(max_dim) / w, double(max_dim) / h);
		w = std::max(2, int(w * k));
		h = std::max(2, int(h * k));
	}

	const pl_frame *src = single ? single : mix->frames[0];
	const bool want_pq = src && pl_color_space_is_hdr(&src->color);

	auto *job = new Job;
	job->owner = this;
	job->done = std::move(done);

	pl_tex sdr = ensureTex(&tex_sdr, w, h, 8);
	if (!sdr || !renderTo(sdr, false, src, mix, single, params, screen_target, overlay, screen_w, screen_h)) {
		qCWarning(pswrapRec) << "shot: SDR render failed";
		delete job;
		return false;
	}
	job->sdr = QImage(w, h, QImage::Format_RGBX8888);
	pl_tex pqt = nullptr;
	if (want_pq) {
		pqt = ensureTex(&tex_pq, w, h, 16);
		if (pqt && renderTo(pqt, true, src, mix, single, params, screen_target, overlay, screen_w, screen_h))
			job->pq = QImage(w, h, QImage::Format_RGBX64);
		else
			pqt = nullptr; // ได้แค่ SDR (log ไว้แล้วถ้า format ไม่มี)
	}
	if (job->sdr.isNull() || (pqt && job->pq.isNull())) {
		qCWarning(pswrapRec) << "shot: out of memory for" << w << "x" << h;
		delete job;
		return false;
	}

	const bool async = gpu->limits.callbacks;
	job->pending = pqt ? 2 : 1;
	inflight.fetch_add(1, std::memory_order_acq_rel);

	auto download = [&](pl_tex tex, QImage &img, bool *ok_flag) {
		pl_tex_transfer_params d = {};
		d.tex = tex;
		d.ptr = img.bits();
		d.row_pitch = static_cast<size_t>(img.bytesPerLine());
		if (async) {
			// ok_flag ตั้งก่อน — ถ้า pl_tex_download คืน false จะไม่มี callback → แก้ด้านล่าง
			*ok_flag = true;
			d.callback = &PsWrapShotCapture::onDownloaded;
			d.priv = job;
		}
		const bool ok = pl_tex_download(gpu, &d);
		if (!async)
			*ok_flag = ok;
		return ok;
	};

	// ลำดับสำคัญ: เมื่อ download ตัวสุดท้ายเข้าคิวแล้ว callback อาจลบ job ได้ทุกเมื่อ — ห้ามแตะ job หลังจากนั้น
	if (pqt && !download(pqt, job->pq, &job->pq_ok)) {
		job->pq_ok = false;
		job->pq = QImage();
		job->pending--; // ไม่มี callback ของ PQ
		qCWarning(pswrapRec) << "shot: PQ download failed — SDR only";
	}
	const bool sdr_queued = download(sdr, job->sdr, &job->sdr_ok);
	if (async) {
		if (!sdr_queued) {
			// ไม่มี callback ของ SDR → นับส่วนนี้ว่าจบเอง (ถ้า PQ จบไปแล้ว onDownloaded จะปิดงาน + เรียก done ด้วยภาพว่าง)
			qCWarning(pswrapRec) << "shot: SDR download failed";
			job->sdr_ok = false;
			onDownloaded(job);
		}
		return true;
	}
	// ไม่มี GPU callback: download ข้างบนบล็อกจนเสร็จแล้ว → ปิดงานทันที
	job->pending = 1;
	onDownloaded(job);
	return true;
}

void PsWrapShotCapture::onDownloaded(void *priv)
{
	auto *job = static_cast<Job *>(priv);
	if (!job)
		return;
	if (job->pending > 0 && --job->pending > 0)
		return;
	if (!job->sdr_ok)
		job->sdr = QImage();
	if (!job->pq_ok)
		job->pq = QImage();
	if (job->sdr.isNull())
		job->pq = QImage(); // PQ อย่างเดียวไม่ส่ง (ผู้ใช้คาดหวังภาพ SDR เสมอ)
	PsWrapShotCapture *owner = job->owner;
	if (job->done)
		job->done(std::move(job->sdr), std::move(job->pq));
	delete job;
	owner->inflight.fetch_sub(1, std::memory_order_acq_rel);
}
