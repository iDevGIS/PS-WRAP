# PS-WRAP: balsam_out/Dualsense.qml -> gui/res/pad3d/Pad3DModel.qml (+ meshes/maps) พร้อมผูกปุ่ม/สติ๊ก/ไก/ไฟกับ property
# ลำดับ: python build_glb.py → balsam (ดูด้านล่าง) → python make_pad3d.py
#   balsam: PATH=/c/msys64/mingw64/bin:$PATH balsam --outputPath balsam_out dualsense.glb
#   (ต้องมี mingw-w64-x86_64-qt6-quick3d + assimp + qt6-quicktimeline ใน MSYS2 — ใช้ตอนแปลงเท่านั้น แอปไม่ต้องมี assimp)
import io, os, re, shutil

SRC = 'balsam_out'
DST = os.path.join('..', '..', 'gui', 'res', 'pad3d')

# bitmask ปุ่มของ chiaki (lib/include/chiaki/controller.h)
PRESS = {
    'btn_cross': 1 << 0, 'btn_circle': 1 << 1, 'btn_square': 1 << 2, 'btn_triangle': 1 << 3,
    'dpad_left': 1 << 4, 'dpad_right': 1 << 5, 'dpad_up': 1 << 6, 'dpad_down': 1 << 7,
    'btn_options': 1 << 12, 'btn_create': 1 << 13, 'touchpad': 1 << 14, 'btn_ps': 1 << 15,
}
SHOULDER = {'l1': 1 << 8, 'r1': 1 << 9}
STICKS = {'stick_L': ('lx', 'ly', 1 << 10), 'stick_R': ('rx', 'ry', 1 << 11)}
TRIGGERS = {'l2': 'l2', 'r2': 'r2'}

s = io.open(os.path.join(SRC, 'Dualsense.qml'), encoding='utf-8').read()

header = '''
    // PS-WRAP: input สด (ControllerOverlay3D.qml ตั้งค่า) — ปุ่ม = bitmask CHIAKI_CONTROLLER_BUTTON_* · แกน -1..1 · ไก 0..1
    property int buttons: 0
    property real lx: 0
    property real ly: 0
    property real rx: 0
    property real ry: 0
    property real l2: 0
    property real r2: 0
    property color lightColor: "transparent"   // สีไฟที่เครื่องสั่ง · โปร่งใส/มืด = ไฟดับ
    readonly property real press: 0.03           // ปุ่มยุบลง (หน่วย mesh) — overlay เล็ก ต้องยุบให้เห็น
    // ปุ่มที่กดอยู่ = ชมพูเรืองแสง สีเดียวกับ overlay 2D (ds-B*.svg #E8185D) — ยุบอย่างเดียวมองไม่ออกตอนจอยเล็ก
    PrincipledMaterial {
        id: pressed_material
        baseColor: "#E8185D"
        emissiveFactor: Qt.vector3d(1.4, 0.15, 0.5)
        roughness: 0.4
    }
    // ไก L2/R2: เรืองแสงตามแรงกด
    PrincipledMaterial {
        id: l2_material
        baseColor: node.l2 > 0.02 ? "#E8185D" : "#26272e"
        emissiveFactor: Qt.vector3d(1.4 * node.l2, 0.15 * node.l2, 0.5 * node.l2)
        roughness: 0.5
    }
    PrincipledMaterial {
        id: r2_material
        baseColor: node.r2 > 0.02 ? "#E8185D" : "#26272e"
        emissiveFactor: Qt.vector3d(1.4 * node.r2, 0.15 * node.r2, 0.5 * node.r2)
        roughness: 0.5
    }
    readonly property bool lightOn: lightColor.a > 0 && (lightColor.r + lightColor.g + lightColor.b) > 0.3
'''
s = s.replace('    id: node\n', '    id: node\n' + header, 1)

# ไฟ: ย้อมตามสีที่เครื่องสั่ง
s = re.sub(r'(id: lightbar_material[\s\S]*?)emissiveFactor: Qt\.vector3d\([^)]*\)',
           r'\1emissiveFactor: node.lightOn ? Qt.vector3d(node.lightColor.r * 6, node.lightColor.g * 6, node.lightColor.b * 6) : Qt.vector3d(0.04, 0.04, 0.05)',
           s, count=1)

# วัสดุพลาสติก: texture metallicRoughness ของต้นฉบับทำให้ผิวเป็นโลหะ → overlay ไม่มีภาพสะท้อนรอบตัว (IBL) = ดำเงา
s = re.sub(r'        metalnessMap: [^\n]*\n', '', s)
s = s.replace('        metalness: 1\n', '        metalness: 0\n')

def patch_model(src, model_id, fn):
    m = re.search(r'(        Model \{\n            id: %s\n[\s\S]*?\n        \})' % re.escape(model_id), src)
    assert m, model_id
    block = m.group(1)
    return src.replace(block, fn(block), 1)

def pos_of(block):
    m = re.search(r'position: Qt\.vector3d\(([^,]+), ([^,]+), ([^)]+)\)', block)
    return [float(v) for v in m.groups()]


def press_materials(block, mask):
    # ทุกวัสดุของชิ้นนี้ → ชมพูเรืองแสงตอนกด
    head, sep, rest = block.partition('            materials: [\n')
    mats, sep2, tail = rest.partition('            ]')
    out = []
    for line in mats.splitlines():
        name = line.strip().rstrip(',')
        if name:
            out.append('                (node.buttons & %d) ? pressed_material : %s' % (mask, name))
    return head + sep + ',\n'.join(out) + '\n' + sep2 + tail

for mid, mask in PRESS.items():
    def f(b, mask=mask):
        x, y, z = pos_of(b)
        return press_materials(re.sub(r'position: Qt\.vector3d\([^)]*\)',
                      'position: Qt.vector3d(%g, %g, %g - ((node.buttons & %d) ? node.press : 0))' % (x, y, z, mask), b), mask)
    s = patch_model(s, mid, f)

for mid, mask in SHOULDER.items():
    def f(b, mask=mask):
        x, y, z = pos_of(b)
        return press_materials(re.sub(r'position: Qt\.vector3d\([^)]*\)',
                      'position: Qt.vector3d(%g, %g - ((node.buttons & %d) ? node.press : 0), %g)' % (x, y, mask, z), b), mask)
    s = patch_model(s, mid, f)

for mid, (ax, ay, mask) in STICKS.items():
    def f(b, ax=ax, ay=ay, mask=mask):
        x, y, z = pos_of(b)
        b = re.sub(r'position: Qt\.vector3d\([^)]*\)',
                   'position: Qt.vector3d(%g, %g, %g - ((node.buttons & %d) ? node.press : 0))' % (x, y, z, mask), b)
        # จุดหมุน = ฐานสติ๊ก (build_glb.py) · เอียงไปทางที่ดัน ไม่เกิน ~30° · กดสติ๊ก (L3/R3) = ติดสี
        b = b.replace('            source:', '            eulerRotation: Qt.vector3d(node.%s * 30, -node.%s * 30, 0)\n            source:' % (ay, ax), 1)
        return press_materials(b, mask)
    s = patch_model(s, mid, f)

for mid, prop in TRIGGERS.items():
    def f(b, prop=prop):
        # จุดหมุน = ขอบบนด้านหลังของไก — ดึงไกเข้าหาตัวจอย
        b = b.replace('            source:', '            eulerRotation.x: -node.%s * 26\n            source:' % prop, 1)
        return re.sub(r'materials: \[[^\]]*\]', 'materials: [ %s_material ]' % prop, b)   # เรืองตามแรงกด
    s = patch_model(s, mid, f)

s = s.replace('import QtQuick3D\n', 'import QtQuick3D\n\n// PS-WRAP: สร้างจาก assets-src/dualsense/make_pad3d.py — อย่าแก้ตรงนี้ (แก้สคริปต์แล้วสร้างใหม่)\n'
              '// Model: "Playstation 5 Dualsense" by AHarmlessPotato, CC-BY-4.0 (sketchfab.com/3d-models/playstation-5-dualsense-878c1f882808477ab81c2fe86d5a3936)\n', 1)

if os.path.isdir(DST):
    shutil.rmtree(DST)
os.makedirs(DST)
io.open(os.path.join(DST, 'Pad3DModel.qml'), 'w', encoding='utf-8').write(s)
files = ['Pad3DModel.qml']
for sub in ('meshes', 'maps'):
    shutil.copytree(os.path.join(SRC, sub), os.path.join(DST, sub))
    files += ['%s/%s' % (sub, f) for f in sorted(os.listdir(os.path.join(SRC, sub)))]
qrc = '<RCC>\n  <qresource prefix="/pad3d">\n' + ''.join('    <file>%s</file>\n' % f for f in files) + '  </qresource>\n</RCC>\n'
io.open(os.path.join(DST, 'pad3d.qrc'), 'w', encoding='utf-8').write(qrc)
print('wrote', DST, len(files), 'files')
