#version 440
// PS-WRAP facecam compositing: mode 1 = chroma key (green/blue screen), mode 2 = AI mask (selfie segmentation)
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 keyColor;
    vec4 maskRect;      // crop ของเฟรมกล้องที่มองเห็นในกรอบ (x0,y0,x1,y1) normalized
    vec2 pan;
    float tolerance;
    float softness;
    float mode;
    float mirror;
    float zoom;
    float aiThreshold;
};
layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D mask;

vec2 chroma(vec3 c) {
    float cb = -0.168736 * c.r - 0.331264 * c.g + 0.5 * c.b;
    float cr =  0.5 * c.r - 0.418688 * c.g - 0.081312 * c.b;
    return vec2(cb, cr);
}

void main() {
    vec4 p = texture(source, qt_TexCoord0);
    vec3 c = p.rgb / max(p.a, 1e-4);
    float a = 1.0;
    if (mode > 1.5) {
        // ย้อน transform ของชั้นภาพ (translate → zoom → mirror) ให้ได้พิกัดในเฟรมกล้องดิบ แล้ว map เข้า crop
        vec2 s = qt_TexCoord0 + pan * (zoom - 1.0) * 0.5;
        s = (s - 0.5) / max(zoom, 1e-3) + 0.5;
        if (mirror > 0.5) s.x = 1.0 - s.x;
        vec2 m = maskRect.xy + s * (maskRect.zw - maskRect.xy);
        float v = texture(mask, clamp(m, 0.0, 1.0)).r;
        a = smoothstep(aiThreshold - softness, aiThreshold + softness, v);
    } else if (mode > 0.5) {
        float d = distance(chroma(c), chroma(keyColor.rgb));
        a = smoothstep(tolerance, tolerance + max(softness, 0.001), d);
        float spill = 1.0 - a;
        c = mix(c, vec3(dot(c, vec3(0.299, 0.587, 0.114))), spill * 0.5);
    }
    fragColor = vec4(c * a, a) * p.a * qt_Opacity;
}
