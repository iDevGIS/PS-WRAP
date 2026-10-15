# หาพื้นที่บน texture ที่ตรงกับขอบบนของตัวจอย (ตำแหน่งคำว่า SONY) — ใช้หาพิกัดสำหรับลบโลโก้ใน build_glb.py
import json, numpy as np
from PIL import Image
exec(open('build_glb.py', encoding='utf-8').read().split("src = {}")[0])   # acc()
for mi, m in enumerate(d['meshes']):
    p = m['primitives'][0]
    P = acc(p['attributes']['POSITION']); UV = acc(p['attributes']['TEXCOORD_0']); I = acc(p['indices']).reshape(-1, 3)
    c = P[I].mean(1)
    sel = (np.abs(c[:, 0]) < 0.2) & (c[:, 1] > 0.5) & (c[:, 2] > 0.12)
    if not sel.any():
        continue
    uv = UV[I[sel]].reshape(-1, 2)
    mat = d['materials'][p['material']]['name']
    print(mat, sel.sum(), 'uv min', uv.min(0).round(4), 'max', uv.max(0).round(4))
    img = d['images'][d['textures'][d['materials'][p['material']]['pbrMetallicRoughness']['baseColorTexture']['index']]['source']]['uri']
    im = Image.open(SRC + img)
    W, H = im.size
    u0, v0 = (uv.min(0) % 1.0); u1, v1 = (uv.max(0) % 1.0)
    box = (int(min(u0, u1) * W), int(min(v0, v1) * H), int(max(u0, u1) * W) + 1, int(max(v0, v1) * H) + 1)
    print('  image', img, 'box', box)
    im.crop(box).convert('RGB').save('test/logo_%s.png' % mat)
