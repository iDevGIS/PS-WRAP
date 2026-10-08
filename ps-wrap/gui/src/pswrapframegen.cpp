// PS-WRAP: Frame generation (ดู pswrapframegen.h)
#include <pswrapframegen.h>

#include <libplacebo/shaders/custom.h>

#include <QLoggingCategory>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

namespace {

constexpr auto kTexCaps = static_cast<pl_fmt_caps>(PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR);

pl_shader_desc sampled(const char *name, pl_tex tex, bool linear)
{
	pl_shader_desc d = {};
	d.desc.name = name;
	d.desc.type = PL_DESC_SAMPLED_TEX;
	d.binding.object = tex;
	d.binding.address_mode = PL_TEX_ADDRESS_CLAMP;
	d.binding.sample_mode = linear ? PL_TEX_SAMPLE_LINEAR : PL_TEX_SAMPLE_NEAREST;
	return d;
}

pl_shader_var vec2Var(const char *name, const float *v)
{
	pl_shader_var s = {};
	s.var = pl_var_vec2(name);
	s.data = v;
	return s;
}

pl_shader_var floatVar(const char *name, const float *v)
{
	pl_shader_var s = {};
	s.var = pl_var_float(name);
	s.data = v;
	return s;
}

// ---- shaders ---- (fg_pos = ตำแหน่งพิกเซลของ target ที่จุดกึ่งกลาง เช่น (0.5, 0.5))

// เฟรมสี → luma 1/4: เฉลี่ย 4x4 ด้วย bilinear 4 จุด
const char *kLuma4Body = R"(
	vec2 uv = fg_pos / out_size;
	vec2 px = 1.0 / src_size;
	vec3 w = vec3(0.2126, 0.7152, 0.0722);
	float l = dot(texture(tex_src, uv + px * vec2(-1.0, -1.0)).rgb, w)
	        + dot(texture(tex_src, uv + px * vec2( 1.0, -1.0)).rgb, w)
	        + dot(texture(tex_src, uv + px * vec2(-1.0,  1.0)).rgb, w)
	        + dot(texture(tex_src, uv + px * vec2( 1.0,  1.0)).rgb, w);
	color = vec4(0.25 * l, 0.0, 0.0, 1.0);
)";

// luma ย่อครึ่ง: bilinear ที่กลาง 2x2
const char *kHalfBody = R"(
	color = vec4(texture(tex_src, fg_pos / out_size).r, 0.0, 0.0, 1.0);
)";

// motion หยาบที่ 1/16: ค้นทุกจุดในรัศมี 6 texel (≈ ±96 พิกเซลจอ) · patch 3x3 · v = การเคลื่อนที่ทั้งเฟรม
const char *kCoarseHeader = R"(
float fg_cost(vec2 p, vec2 v) {
	float c = 0.0;
	for (int y = -1; y <= 1; y++)
	for (int x = -1; x <= 1; x++) {
		vec2 q = p + vec2(float(x), float(y));
		c += abs(texture(tex_a, (q - 0.5 * v) / lsize).r - texture(tex_b, (q + 0.5 * v) / lsize).r);
	}
	return c;
}
)";
const char *kCoarseBody = R"(
	vec2 p = fg_pos;
	float best = fg_cost(p, vec2(0.0)) - 0.002;
	vec2 bv = vec2(0.0);
	for (int y = -6; y <= 6; y++)
	for (int x = -6; x <= 6; x++) {
		vec2 v = vec2(float(x), float(y));
		float c = fg_cost(p, v) + 0.0015 * length(v);
		if (c < best) { best = c; bv = v; }
	}
	color = vec4(bv * scale, best, 1.0);
)";

// refine ที่ 1/8: ผู้สมัครจาก vector หยาบ 3x3 ข้างเคียง แล้วขยับ ±1 / ±0.5 · patch 5x5
const char *kRefineHeader = R"(
float fg_cost(vec2 p, vec2 v) {
	float c = 0.0;
	for (int y = -2; y <= 2; y++)
	for (int x = -2; x <= 2; x++) {
		vec2 q = p + vec2(float(x), float(y));
		c += abs(texture(tex_a, (q - 0.5 * v) / lsize).r - texture(tex_b, (q + 0.5 * v) / lsize).r);
	}
	return c;
}
)";
const char *kRefineBody = R"(
	vec2 p = fg_pos;
	vec2 muv = p / lsize;
	vec2 mpx = 1.0 / mvsize;
	float best = fg_cost(p, vec2(0.0)) - 0.004;
	vec2 bv = vec2(0.0);
	for (int y = -1; y <= 1; y++)
	for (int x = -1; x <= 1; x++) {
		vec2 v = texture(tex_mv, muv + mpx * vec2(float(x), float(y))).xy / scale;
		float c = fg_cost(p, v) + 0.003 * length(v);
		if (c < best) { best = c; bv = v; }
	}
	for (int s = 0; s < 2; s++) {
		float st = s == 0 ? 1.0 : 0.5;
		vec2 base = bv;
		for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++) {
			if (x == 0 && y == 0)
				continue;
			vec2 v = base + st * vec2(float(x), float(y));
			float c = fg_cost(p, v) + 0.003 * length(v);
			if (c < best) { best = c; bv = v; }
		}
	}
	color = vec4(bv * scale, best, 1.0);
)";

// median ของ vector 3x3 (ตัวที่ห่างตัวอื่นรวมน้อยสุด) — ตัด vector หลุดเดี่ยวๆ
const char *kMedianBody = R"(
	vec2 uv = fg_pos / mvsize;
	vec2 px = 1.0 / mvsize;
	vec2 c[9];
	int k = 0;
	for (int y = -1; y <= 1; y++)
	for (int x = -1; x <= 1; x++)
		c[k++] = texture(tex_mv, uv + px * vec2(float(x), float(y))).xy;
	float bestd = 1e20;
	vec2 bv = c[4];
	for (int i = 0; i < 9; i++) {
		float d = 0.0;
		for (int j = 0; j < 9; j++)
			d += length(c[i] - c[j]);
		if (d < bestd) { bestd = d; bv = c[i]; }
	}
	color = vec4(bv, 0.0, 1.0);
)";

// เฟรมกลาง: ผู้สมัคร = vector bilinear / texel ใกล้สุด / ศูนย์ (HUD นิ่ง) · ต่างน้อยสุดชนะ (5 จุด รูปกากบาท)
const char *kInterpHeader = R"(
vec3 fg_a;
vec3 fg_b;
float fg_err(vec2 uv, vec2 v, vec2 px) {
	vec2 oa = -t_mix * v * px;
	vec2 ob = (1.0 - t_mix) * v * px;
	vec3 a = texture(tex_a, uv + oa).rgb;
	vec3 b = texture(tex_b, uv + ob).rgb;
	vec3 one = vec3(1.0);
	float e = dot(abs(a - b), one);
	vec2 dx = vec2(2.0 * px.x, 0.0);
	vec2 dy = vec2(0.0, 2.0 * px.y);
	e += dot(abs(texture(tex_a, uv + oa + dx).rgb - texture(tex_b, uv + ob + dx).rgb), one);
	e += dot(abs(texture(tex_a, uv + oa - dx).rgb - texture(tex_b, uv + ob - dx).rgb), one);
	e += dot(abs(texture(tex_a, uv + oa + dy).rgb - texture(tex_b, uv + ob + dy).rgb), one);
	e += dot(abs(texture(tex_a, uv + oa - dy).rgb - texture(tex_b, uv + ob - dy).rgb), one);
	fg_a = a;
	fg_b = b;
	return e;
}
)";
const char *kInterpBody = R"(
	vec2 px = 1.0 / full_size;
	vec2 uv = fg_pos * px;
	float e = fg_err(uv, texture(tex_mv, uv).xy, px) * 0.9;
	vec3 a = fg_a;
	vec3 b = fg_b;
	// texel ใกล้สุด = sample linear ตรงกลาง texel (libplacebo ห้าม bind texture เดียวกันสองช่อง)
	vec2 nuv = (floor(uv * mvsize) + 0.5) / mvsize;
	float e1 = fg_err(uv, texture(tex_mv, nuv).xy, px);
	if (e1 < e) { e = e1; a = fg_a; b = fg_b; }
	float e2 = fg_err(uv, vec2(0.0), px);
	if (e2 < e) { e = e2; a = fg_a; b = fg_b; }
	vec3 blend = mix(a, b, t_mix);
	vec3 side = t_mix <= 0.5 ? a : b;
	color = vec4(mix(blend, side, smoothstep(0.6, 1.8, e)), 1.0);
)";

} // namespace

PsWrapFrameGen::PsWrapFrameGen(pl_gpu gpu, pl_log log)
	: gpu(gpu), log(log)
{
	dp = pl_dispatch_create(log, gpu);
	fmt_rgba = pl_find_fmt(gpu, PL_FMT_UNORM, 4, 16, 0, kTexCaps);
	if (!fmt_rgba)
		fmt_rgba = pl_find_fmt(gpu, PL_FMT_UNORM, 4, 8, 0, kTexCaps);
	fmt_luma = pl_find_fmt(gpu, PL_FMT_FLOAT, 1, 16, 0, kTexCaps);
	if (!fmt_luma)
		fmt_luma = pl_find_fmt(gpu, PL_FMT_UNORM, 1, 16, 0, kTexCaps);
	fmt_mv = pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 16, 0, kTexCaps);
	fmt_va = pl_find_vertex_fmt(gpu, PL_FMT_FLOAT, 2);
	if (!dp || !fmt_rgba || !fmt_luma || !fmt_mv || !fmt_va) {
		qCWarning(chiakiGui) << "PSWRAP framegen: GPU lacks required formats — disabled";
		failed = true;
	}
}

PsWrapFrameGen::~PsWrapFrameGen()
{
	destroyTextures();
	if (dp)
		pl_dispatch_destroy(&dp);
}

void PsWrapFrameGen::destroyTextures()
{
	pl_tex *all[] = {&frame[0], &frame[1], &luma4[0], &luma4[1], &luma8[0], &luma8[1], &luma16[0], &luma16[1],
	                 &mv16, &mv8, &mv8s, &mid};
	for (pl_tex *t : all)
		if (*t)
			pl_tex_destroy(gpu, t);
	width = height = 0;
	reset();
}

void PsWrapFrameGen::reset()
{
	valid[0] = valid[1] = false;
	motion_ready = false;
}

pl_tex PsWrapFrameGen::makeTex(int w, int h, pl_fmt fmt)
{
	pl_tex_params p = {};
	p.w = std::max(1, w);
	p.h = std::max(1, h);
	p.format = fmt;
	p.renderable = true;
	p.sampleable = true;
	p.debug_tag = PL_DEBUG_TAG;
	return pl_tex_create(gpu, &p);
}

bool PsWrapFrameGen::ensure(int w, int h)
{
	if (failed || w <= 0 || h <= 0)
		return false;
	if (w == width && h == height && frame[0])
		return true;
	destroyTextures();
	const int w4 = (w + 3) / 4, h4 = (h + 3) / 4;
	const int w8 = (w4 + 1) / 2, h8 = (h4 + 1) / 2;
	const int w16 = (w8 + 1) / 2, h16 = (h8 + 1) / 2;
	bool ok = true;
	for (int i = 0; i < 2; i++) {
		ok = ok && (frame[i] = makeTex(w, h, fmt_rgba));
		ok = ok && (luma4[i] = makeTex(w4, h4, fmt_luma));
		ok = ok && (luma8[i] = makeTex(w8, h8, fmt_luma));
		ok = ok && (luma16[i] = makeTex(w16, h16, fmt_luma));
	}
	ok = ok && (mv16 = makeTex(w16, h16, fmt_mv));
	ok = ok && (mv8 = makeTex(w8, h8, fmt_mv));
	ok = ok && (mv8s = makeTex(w8, h8, fmt_mv));
	ok = ok && (mid = makeTex(w, h, fmt_rgba));
	if (!ok) {
		qCWarning(chiakiGui) << "PSWRAP framegen: texture allocation failed at" << w << "x" << h;
		destroyTextures();
		return false;
	}
	width = w;
	height = h;
	return true;
}

bool PsWrapFrameGen::run(pl_tex target, const char *name, const char *header, const char *body,
                         const pl_shader_desc *descs, int num_descs, const pl_shader_var *vars, int num_vars)
{
	pl_shader sh = pl_dispatch_begin(dp);
	if (!sh)
		return false;
	const float w = float(target->params.w), h = float(target->params.h);
	const float c00[2] = {0.0f, 0.0f}, c10[2] = {w, 0.0f}, c01[2] = {0.0f, h}, c11[2] = {w, h};
	pl_shader_va va = {};
	va.attr.name = "fg_pos";
	va.attr.fmt = fmt_va;
	va.data[0] = c00;
	va.data[1] = c10;
	va.data[2] = c01;
	va.data[3] = c11;
	pl_custom_shader cs = {};
	cs.description = name;
	cs.header = header;
	cs.body = body;
	cs.input = PL_SHADER_SIG_NONE;
	cs.output = PL_SHADER_SIG_COLOR;
	cs.descriptors = descs;
	cs.num_descriptors = num_descs;
	cs.variables = vars;
	cs.num_variables = num_vars;
	cs.vertex_attribs = &va;
	cs.num_vertex_attribs = 1;
	cs.output_w = target->params.w;
	cs.output_h = target->params.h;
	if (!pl_shader_custom(sh, &cs)) {
		pl_dispatch_abort(dp, &sh);
		qCWarning(chiakiGui) << "PSWRAP framegen: shader setup failed:" << name;
		return false;
	}
	pl_dispatch_params dpp = {};
	dpp.shader = &sh;
	dpp.target = target;
	if (!pl_dispatch_finish(dp, &dpp)) {
		qCWarning(chiakiGui) << "PSWRAP framegen: dispatch failed:" << name;
		return false;
	}
	return true;
}

bool PsWrapFrameGen::lumaPyramid(int idx)
{
	{
		const float out_size[2] = {float(luma4[idx]->params.w), float(luma4[idx]->params.h)};
		const float src_size[2] = {float(width), float(height)};
		const pl_shader_desc d[] = {sampled("tex_src", frame[idx], true)};
		const pl_shader_var v[] = {vec2Var("out_size", out_size), vec2Var("src_size", src_size)};
		if (!run(luma4[idx], "pswrap fg luma4", nullptr, kLuma4Body, d, 1, v, 2))
			return false;
	}
	const pl_tex chain[3] = {luma4[idx], luma8[idx], luma16[idx]};
	for (int i = 1; i < 3; i++) {
		const float out_size[2] = {float(chain[i]->params.w), float(chain[i]->params.h)};
		const pl_shader_desc d[] = {sampled("tex_src", chain[i - 1], true)};
		const pl_shader_var v[] = {vec2Var("out_size", out_size)};
		if (!run(chain[i], "pswrap fg luma half", nullptr, kHalfBody, d, 1, v, 1))
			return false;
	}
	return true;
}

bool PsWrapFrameGen::push(pl_renderer renderer, const pl_frame &src, const pl_frame &target, const pl_render_params &params)
{
	const pl_tex tt = target.num_planes > 0 ? target.planes[0].texture : nullptr;
	if (!renderer || !tt || !ensure(tt->params.w, tt->params.h))
		return false;
	pl_dispatch_reset_frame(dp);
	cur = 1 - cur;
	valid[cur] = false;
	motion_ready = false;

	pl_frame t = target;
	t.num_planes = 1;
	t.planes[0] = {};
	t.planes[0].texture = frame[cur];
	t.planes[0].components = 4;
	t.planes[0].component_mapping[0] = PL_CHANNEL_R;
	t.planes[0].component_mapping[1] = PL_CHANNEL_G;
	t.planes[0].component_mapping[2] = PL_CHANNEL_B;
	t.planes[0].component_mapping[3] = PL_CHANNEL_A;
	t.repr = pl_color_repr_rgb;
	t.repr.alpha = PL_ALPHA_INDEPENDENT;
	t.repr.bits.sample_depth = fmt_rgba->component_depth[0];
	t.repr.bits.color_depth = fmt_rgba->component_depth[0];
	t.overlays = nullptr;
	t.num_overlays = 0;
	pl_render_params p = params;
	p.background_transparency = 0.0f;
	if (!pl_render_image(renderer, &src, &t, &p))
		return false;
	if (!lumaPyramid(cur)) {
		failed = true;
		return false;
	}
	valid[cur] = true;
	return true;
}

bool PsWrapFrameGen::hasPair() const
{
	return !failed && valid[0] && valid[1];
}

bool PsWrapFrameGen::interpolate(float t)
{
	if (!hasPair())
		return false;
	const int prev = 1 - cur;
	if (!motion_ready) {
		const float l16[2] = {float(luma16[0]->params.w), float(luma16[0]->params.h)};
		const float l8[2] = {float(luma8[0]->params.w), float(luma8[0]->params.h)};
		const float s16[2] = {float(width) / l16[0], float(height) / l16[1]};
		const float s8[2] = {float(width) / l8[0], float(height) / l8[1]};
		{
			const pl_shader_desc d[] = {sampled("tex_a", luma16[prev], true), sampled("tex_b", luma16[cur], true)};
			const pl_shader_var v[] = {vec2Var("lsize", l16), vec2Var("scale", s16)};
			if (!run(mv16, "pswrap fg motion coarse", kCoarseHeader, kCoarseBody, d, 2, v, 2)) {
				failed = true;
				return false;
			}
		}
		{
			const pl_shader_desc d[] = {sampled("tex_a", luma8[prev], true), sampled("tex_b", luma8[cur], true),
			                            sampled("tex_mv", mv16, false)};
			const pl_shader_var v[] = {vec2Var("lsize", l8), vec2Var("scale", s8), vec2Var("mvsize", l16)};
			if (!run(mv8, "pswrap fg motion refine", kRefineHeader, kRefineBody, d, 3, v, 3)) {
				failed = true;
				return false;
			}
		}
		{
			const pl_shader_desc d[] = {sampled("tex_mv", mv8, false)};
			const pl_shader_var v[] = {vec2Var("mvsize", l8)};
			if (!run(mv8s, "pswrap fg motion median", nullptr, kMedianBody, d, 1, v, 1)) {
				failed = true;
				return false;
			}
		}
		motion_ready = true;
	}
	const float full[2] = {float(width), float(height)};
	const float tm = std::clamp(t, 0.0f, 1.0f);
	const float mvs[2] = {float(mv8s->params.w), float(mv8s->params.h)};
	const pl_shader_desc d[] = {sampled("tex_a", frame[prev], true), sampled("tex_b", frame[cur], true),
	                            sampled("tex_mv", mv8s, true)};
	const pl_shader_var v[] = {vec2Var("full_size", full), floatVar("t_mix", &tm), vec2Var("mvsize", mvs)};
	if (!run(mid, "pswrap fg interpolate", kInterpHeader, kInterpBody, d, 3, v, 3)) {
		failed = true;
		return false;
	}

	return true;
}

bool PsWrapFrameGen::present(pl_renderer renderer, bool use_mid, const pl_frame &target)
{
	const pl_tex src_tex = use_mid ? mid : frame[cur];
	const pl_tex tt = target.num_planes > 0 ? target.planes[0].texture : nullptr;
	if (!renderer || !src_tex || !valid[cur] || !tt || tt->params.w != width || tt->params.h != height)
		return false;   // ขนาดจอเปลี่ยนหลังวาดเฟรมไว้ → ให้ทางปกติวาดแทน
	pl_frame s = {};
	s.num_planes = 1;
	s.planes[0].texture = src_tex;
	s.planes[0].components = 4;
	s.planes[0].component_mapping[0] = PL_CHANNEL_R;
	s.planes[0].component_mapping[1] = PL_CHANNEL_G;
	s.planes[0].component_mapping[2] = PL_CHANNEL_B;
	s.planes[0].component_mapping[3] = PL_CHANNEL_A;
	s.repr = pl_color_repr_rgb;
	s.repr.alpha = PL_ALPHA_NONE;
	s.repr.bits.sample_depth = fmt_rgba->component_depth[0];
	s.repr.bits.color_depth = fmt_rgba->component_depth[0];
	s.color = target.color;   // วาดไว้ใน color space ของจอแล้ว → ไม่แปลงซ้ำ
	s.crop = {0.0f, 0.0f, float(width), float(height)};
	pl_frame t = target;
	t.crop = {0.0f, 0.0f, float(width), float(height)};
	pl_render_params p = pl_render_fast_params;
	p.dither_params = &pl_dither_default_params;
	p.background_transparency = 0.0f;
	return pl_render_image(renderer, &s, &t, &p);
}
