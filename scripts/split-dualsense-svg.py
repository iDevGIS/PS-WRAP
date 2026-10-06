"""แตก SVG DualSense (จาก BudToZaiDualSenseTracker index-v2.html) เป็นชิ้นต่อปุ่ม สำหรับ QML overlay
ผลลัพธ์ใน chiaki-ng/gui/res/overlay/:
  ds-base.svg          ตัวจอยทั้งหมด ยกเว้นสติ๊ก (AXIS-0-1 / AXIS-2-3)
  ds-axis-l.svg/-r.svg สติ๊กซ้าย/ขวา (เลื่อนด้วย translate ใน QML)
  ds-B0..B17.svg       เฉพาะปุ่มนั้น เติมสี pressed (#E8185D) — ซ้อนทับ base ที่ขนาดเดียวกันจะตรงตำแหน่งพอดี
ทุกไฟล์ใช้ viewBox เดิม (1138×765) และคง transform ของ ancestor ไว้
ใช้: python scripts/split-dualsense-svg.py
"""
from __future__ import annotations

import copy
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "chiaki-ng/gui/res/overlay/dualsense-full.svg"
OUT = ROOT / "chiaki-ng/gui/res/overlay"
NS = "http://www.w3.org/2000/svg"
PRESSED = "#E8185D"
ET.register_namespace("", NS)

tree = ET.parse(SRC)
root = tree.getroot()
view_box = root.get("viewBox")
print("viewBox", view_box)

parent_of: dict[ET.Element, ET.Element] = {c: p for p in root.iter() for c in p}


def q(tag: str) -> str:
    return f"{{{NS}}}{tag}"


def find_by_id(el_id: str) -> ET.Element | None:
    for el in root.iter():
        if el.get("id") == el_id:
            return el
    return None


def ancestors(el: ET.Element) -> list[ET.Element]:
    chain = []
    cur = parent_of.get(el)
    while cur is not None and cur is not root:
        chain.append(cur)
        cur = parent_of.get(cur)
    return list(reversed(chain))


def new_svg() -> ET.Element:
    svg = ET.Element(q("svg"), {"viewBox": view_box, "width": root.get("width", "1138"), "height": root.get("height", "765"), "fill": "none"})
    # คง <defs> (clipPath) ของต้นฉบับ
    for d in root.findall(q("defs")):
        svg.append(copy.deepcopy(d))
    return svg


def wrap_with_ancestors(el: ET.Element, keep_attrs=("transform", "clip-path")) -> tuple[ET.Element, ET.Element]:
    """สร้างโครง <g> ซ้อนตาม ancestor (เฉพาะ attr ที่กระทบตำแหน่ง) แล้วใส่ deep copy ของ el"""
    svg = new_svg()
    cur = svg
    for anc in ancestors(el):
        g = ET.SubElement(cur, q("g"), {k: v for k, v in anc.attrib.items() if k in keep_attrs})
        cur = g
    cur.append(copy.deepcopy(el))
    return svg, cur


def recolor(el: ET.Element, color: str) -> None:
    for node in el.iter():
        if node.get("fill") not in (None, "none"):
            node.set("fill", color)
        node.attrib.pop("fill-opacity", None)
        if node.get("stroke") not in (None, "none"):
            node.set("stroke", color)


def write(svg: ET.Element, name: str) -> None:
    data = ET.tostring(svg, encoding="unicode")
    (OUT / name).write_text('<?xml version="1.0" encoding="UTF-8"?>\n' + data, encoding="utf-8")


# ---- base: ทุกอย่างยกเว้นสติ๊ก ----
base = copy.deepcopy(root)
base_parent = {c: p for p in base.iter() for c in p}
for el in list(base.iter()):
    if el.get("id") in ("AXIS-0-1", "AXIS-2-3"):
        base_parent[el].remove(el)
write(base, "ds-base.svg")

# ---- สติ๊ก ----
for el_id, name in (("AXIS-0-1", "ds-axis-l.svg"), ("AXIS-2-3", "ds-axis-r.svg")):
    el = find_by_id(el_id)
    assert el is not None, el_id
    svg, _ = wrap_with_ancestors(el)
    write(svg, name)

# ---- ปุ่ม B0..B17 ----
found = []
for i in range(18):
    el = find_by_id(f"B{i}")
    if el is None:
        print("missing", f"B{i}")
        continue
    svg, holder = wrap_with_ancestors(el)
    recolor(holder[-1], PRESSED)
    write(svg, f"ds-B{i}.svg")
    found.append(i)
print("buttons:", found)

# maxOffset ที่ JS ใช้เลื่อนสติ๊ก
html = (ROOT / "_deps/BudToZaiDualSenseTracker/BudToZaiDualSenseTracker/Resources/index-v2.html").read_text(encoding="utf-8")
m = re.search(r"maxOffset\s*=\s*([0-9.]+)", html)
print("maxOffset", m.group(1) if m else "?")
