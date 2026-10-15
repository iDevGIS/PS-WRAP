# PS-WRAP: แปลงโมเดล DualSense (Sketchfab, CC-BY-4.0 AHarmlessPotato) -> dualsense.glb ที่แยกปุ่ม/สติ๊ก/ไกเป็น node มีชื่อ
# ปุ่มในไฟล์ต้นฉบับเป็นเกาะเรขาคณิตแยกกันอยู่แล้ว -> แยกด้วยตำแหน่ง (พิกัด mesh-local: x=ซ้ายขวา, y=ไปทางไก, z=หน้าปุ่ม)
# รัน: python build_glb.py  (ต้องแตก playstation_5_dualsense.zip ไว้ที่ gltf/ ก่อน)
import json, io, struct
import numpy as np
from PIL import Image

SRC = 'gltf/'
TEX = 1024
d = json.load(open(SRC + 'scene.gltf'))
buf = open(SRC + 'scene.bin', 'rb').read()


def acc(i):
    a = d['accessors'][i]
    bv = d['bufferViews'][a['bufferView']]
    n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16}[a['componentType']]
    off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = bv.get('byteStride')
    sz = n * np.dtype(dt).itemsize
    if stride and stride != sz:
        raw = np.frombuffer(buf, np.uint8, a['count'] * stride, off).reshape(-1, stride)
        return raw[:, :sz].copy().view(dt).reshape(-1, n)
    return np.frombuffer(buf, dt, a['count'] * n, off).reshape(-1, n).copy()


def classify(c):
    x, y, z = c
    if abs(abs(x) - 0.318) < 0.14 and abs(y) < 0.14 and z > 0.2:
        return 'stick_L' if x < 0 else 'stick_R'
    if -0.8 < x < -0.45 and 0.1 < y < 0.45 and z > 0.2:
        if y > 0.33: return 'dpad_up'
        if y < 0.22: return 'dpad_down'
        return 'dpad_left' if x < -0.62 else 'dpad_right'
    if 0.4 < x < 0.85 and 0.05 < y < 0.47 and z > 0.2:
        if y > 0.37: return 'btn_triangle'
        if y < 0.18: return 'btn_cross'
        return 'btn_square' if x < 0.55 else 'btn_circle'
    if abs(abs(x) - 0.614) < 0.1 and abs(y - 0.528) < 0.08 and z < 0:
        return 'L2' if x < 0 else 'R2'
    if abs(abs(x) - 0.619) < 0.1 and abs(y - 0.577) < 0.08 and z > 0:
        return 'L1' if x < 0 else 'R1'
    if abs(abs(x) - 0.467) < 0.05 and abs(y - 0.494) < 0.06:
        return 'btn_create' if x < 0 else 'btn_options'
    if abs(x) < 0.07 and abs(y) < 0.07 and z > 0.22:
        return 'btn_ps'
    if abs(x) < 0.07 and abs(y + 0.102) < 0.03 and z > 0.2:
        return 'btn_mute'
    if abs(x) < 0.1 and abs(y - 0.41) < 0.05 and z > 0.2:
        return 'touchpad'
    return 'body'


src = {}      # material -> (P, N, T, UV)
parts = {}    # name -> {material: [tri arrays]}
for m in d['meshes']:
    p = m['primitives'][0]
    at = p['attributes']
    P, N, T, UV = acc(at['POSITION']), acc(at['NORMAL']), acc(at['TANGENT']), acc(at['TEXCOORD_0'])
    I = acc(p['indices']).reshape(-1, 3)
    src[p['material']] = (P, N, T, UV)
    _, inv = np.unique(np.round(P, 5), axis=0, return_inverse=True)
    inv = inv.ravel()
    par = np.arange(inv.max() + 1)

    def f(x):
        r = x
        while par[r] != r: r = par[r]
        while par[x] != r: par[x], x = r, par[x]
        return r

    for a, b, c in inv[I]:
        ra = f(a); par[f(b)] = ra; par[f(c)] = ra
    roots = np.array([f(x) for x in range(len(par))])
    tri_comp = roots[inv][I[:, 0]]
    for lab in np.unique(tri_comp):
        tri = I[tri_comp == lab]
        vs = P[np.unique(tri)]
        parts.setdefault(classify((vs.min(0) + vs.max(0)) / 2), {}).setdefault(p['material'], []).append(tri)

# แถบไฟข้าง touchpad = สามเหลี่ยมของวัสดุ 1011 ที่ UV ตกในพื้นที่สว่างของ emissive texture
# แยกเป็นชิ้น lightbar ใช้วัสดุของตัวเอง (พื้นมืด + เรืองแสง) ไม่งั้นสีไฟบวกบนพื้นขาวแล้วกลายเป็นขาว
LIGHT_MAT = len(d['materials'])
_lm = [i for i, m in enumerate(d['materials']) if 'emissiveTexture' in m][0]
_em = np.asarray(Image.open(SRC + d['images'][d['textures'][d['materials'][_lm]['emissiveTexture']['index']]['source']]['uri']).convert('L')).astype(int)
_UV = src[_lm][3]
for _pn in ('body', 'touchpad'):   # แถบไฟติดอยู่กับเกาะของ touchpad (กว้างเท่าแผง)
    _keep = []
    for tri in parts[_pn].get(_lm, []):
        uv = _UV[tri].mean(1) % 1.0
        lit = _em[np.clip((uv[:, 1] * _em.shape[0]).astype(int), 0, _em.shape[0] - 1),
                  np.clip((uv[:, 0] * _em.shape[1]).astype(int), 0, _em.shape[1] - 1)] > 60
        if lit.any(): parts.setdefault('lightbar', {}).setdefault(LIGHT_MAT, []).append(tri[lit])
        if (~lit).any(): _keep.append(tri[~lit])
    if _lm in parts[_pn]: parts[_pn][_lm] = _keep
src[LIGHT_MAT] = src[_lm]

order = ['body', 'lightbar', 'touchpad', 'stick_L', 'stick_R', 'dpad_up', 'dpad_down', 'dpad_left', 'dpad_right',
         'btn_triangle', 'btn_circle', 'btn_cross', 'btn_square', 'L1', 'R1', 'L2', 'R2',
         'btn_create', 'btn_options', 'btn_ps', 'btn_mute']
print('missing:', [n for n in order if n not in parts], 'extra:', [n for n in parts if n not in order])
for n in order:
    if n in parts: print(' ', n, sum(len(t) for v in parts[n].values() for t in v), 'tris')

bin_ = bytearray(); bviews = []; accs = []


def pad4():
    while len(bin_) % 4: bin_.append(0)


def add(arr, target=None, minmax=False):
    arr = np.ascontiguousarray(arr)
    pad4()
    bv = {'buffer': 0, 'byteOffset': len(bin_), 'byteLength': arr.nbytes}
    if target: bv['target'] = target
    bin_.extend(arr.tobytes()); bviews.append(bv)
    t = {1: 'SCALAR', 2: 'VEC2', 3: 'VEC3', 4: 'VEC4'}[1 if arr.ndim == 1 else arr.shape[1]]
    a = {'bufferView': len(bviews) - 1, 'componentType': 5125 if arr.dtype == np.uint32 else 5126, 'count': len(arr), 'type': t}
    if minmax: a['min'] = arr.min(0).tolist(); a['max'] = arr.max(0).tolist()
    accs.append(a)
    return len(accs) - 1


meshes = []; nodes = []; children = []; pivots = {}
for name in order:
    if name not in parts: continue
    pts = np.concatenate([src[mat][0][np.unique(np.concatenate(tris))] for mat, tris in parts[name].items()])
    lo, hi = pts.min(0), pts.max(0); c = (lo + hi) / 2
    # จุดหมุน: สติ๊ก = ฐาน (z ต่ำสุด) · ไก = ขอบบนด้านหลัง · อื่นๆ = กลางชิ้น · body = 0
    if name == 'body': piv = np.zeros(3)
    elif name.startswith('stick'): piv = np.array([c[0], c[1], lo[2]])
    elif name in ('L2', 'R2'): piv = np.array([c[0], hi[1], hi[2]])
    else: piv = c
    pivots[name] = [round(float(v), 5) for v in piv]
    prims = []
    for mat, tris in parts[name].items():
        P, N, T, UV = src[mat]
        tri = np.concatenate(tris)
        used, remap = np.unique(tri, return_inverse=True)
        prims.append({'attributes': {
            'POSITION': add((P[used] - piv).astype(np.float32), 34962, True),
            'NORMAL': add(N[used].astype(np.float32), 34962),
            'TANGENT': add(T[used].astype(np.float32), 34962),
            'TEXCOORD_0': add(UV[used].astype(np.float32), 34962)},
            'indices': add(remap.astype(np.uint32).ravel(), 34963), 'material': mat})
    meshes.append({'name': name, 'primitives': prims})
    nodes.append({'name': name, 'mesh': len(meshes) - 1, 'translation': piv.tolist()})
    children.append(len(nodes) - 1)

# root = transform เดิมของ Sketchfab (node0 * node1)
M0 = np.array(d['nodes'][0]['matrix']).reshape(4, 4).T
M1 = np.array(d['nodes'][1]['matrix']).reshape(4, 4).T
nodes.append({'name': 'DualSense', 'children': children, 'matrix': (M0 @ M1).T.ravel().tolist()})

# textures -> 1024 JPEG · ทึบทั้งหมด · emissive (ไฟ) เป็นหน้ากากขาวดำ ให้แอปย้อมสีไฟเอง
imgs = []; textures = []


LOGO_BOX = (1855, 1890, 1940, 2215)   # คำว่า SONY บนขอบบน (texture 1002 ขนาด 4096) — เครื่องหมายการค้า ลบออก (หาด้วย find_logo.py)


def unbrand(uri, im):
    if '1002_baseColor' in uri:
        a = np.asarray(im.convert('RGB')).copy()
        x0, y0, x1, y1 = LOGO_BOX
        a[y0:y1, x0:x1] = np.median(a[y0 - 60:y1 + 60, x0 - 120:x0 - 20].reshape(-1, 3), axis=0).astype(np.uint8)
        return Image.fromarray(a)
    if '1002_normal' in uri:
        a = np.asarray(im.convert('RGB')).copy()
        x0, y0, x1, y1 = LOGO_BOX
        a[y0:y1, x0:x1] = (128, 128, 255)   # ผิวเรียบ (ไม่นูนเป็นตัวหนังสือ)
        return Image.fromarray(a)
    return im


def addimg(uri, gray=False):
    im = unbrand(uri, Image.open(SRC + uri)).convert('L' if gray else 'RGB').resize((TEX, TEX), Image.LANCZOS).convert('RGB')   # หน้ากากขาวดำก็เก็บเป็น RGB (JPEG ช่องเดียว Qt อ่านเป็นช่องแดงอย่างเดียว)
    b = io.BytesIO(); im.save(b, 'JPEG', quality=88)
    pad4()
    bviews.append({'buffer': 0, 'byteOffset': len(bin_), 'byteLength': len(b.getvalue())}); bin_.extend(b.getvalue())
    imgs.append({'bufferView': len(bviews) - 1, 'mimeType': 'image/jpeg'})
    textures.append({'sampler': 0, 'source': len(imgs) - 1})
    return len(textures) - 1


def uri(k): return d['images'][d['textures'][k]['source']]['uri']


mats = []
for m in d['materials']:
    pbr = m['pbrMetallicRoughness']
    nm = {'name': m['name'], 'pbrMetallicRoughness': {
        'baseColorTexture': {'index': addimg(uri(pbr['baseColorTexture']['index']))},
        'metallicRoughnessTexture': {'index': addimg(uri(pbr['metallicRoughnessTexture']['index']))}},
        'normalTexture': {'index': addimg(uri(m['normalTexture']['index']))}}
    if 'emissiveTexture' in m:
        nm['emissiveTexture'] = {'index': addimg(uri(m['emissiveTexture']['index']), gray=True)}
        nm['emissiveFactor'] = [0.0, 0.65, 1.0]
    mats.append(nm)
# วัสดุแถบไฟ: พื้นมืด ไม่มีลาย · emissiveFactor = สีไฟ (แอปเปลี่ยนตามเกม) · ใช้ emissive texture เดิมเป็นหน้ากากความสว่าง
mats.append({'name': 'lightbar', 'pbrMetallicRoughness': {'baseColorFactor': [0.02, 0.02, 0.03, 1.0], 'metallicFactor': 0.0, 'roughnessFactor': 0.35},
             'emissiveTexture': mats[_lm]['emissiveTexture'], 'emissiveFactor': [1.0, 0.18, 0.33]})
mats[_lm].pop('emissiveTexture'); mats[_lm].pop('emissiveFactor')   # ตัวจอยไม่เรืองแสงแล้ว (ไฟย้ายไป lightbar)

out = {'asset': {'version': '2.0', 'generator': 'PS-WRAP build_glb.py',
                 'copyright': 'Playstation 5 Dualsense by AHarmlessPotato, CC-BY-4.0, sketchfab.com/3d-models/playstation-5-dualsense-878c1f882808477ab81c2fe86d5a3936'},
       'scene': 0, 'scenes': [{'nodes': [len(nodes) - 1]}], 'nodes': nodes, 'meshes': meshes, 'materials': mats,
       'textures': textures, 'images': imgs,
       'samplers': [{'magFilter': 9729, 'minFilter': 9987, 'wrapS': 10497, 'wrapT': 10497}],
       'accessors': accs, 'bufferViews': bviews, 'buffers': [{'byteLength': 0}]}
pad4()
out['buffers'][0]['byteLength'] = len(bin_)
js = json.dumps(out, separators=(',', ':')).encode()
js += b' ' * ((4 - len(js) % 4) % 4)
with open('dualsense.glb', 'wb') as fo:
    fo.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_)))
    fo.write(struct.pack('<II', len(js), 0x4E4F534A)); fo.write(js)
    fo.write(struct.pack('<II', len(bin_), 0x004E4942)); fo.write(bin_)
json.dump(pivots, open('pivots.json', 'w'), indent=1)
print('wrote dualsense.glb', len(bin_) // 1024, 'KB')
