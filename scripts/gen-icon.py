"""สร้าง pswrap.ico / pswrap.png (ดีไซน์เดียวกับ gui/res/pswrap.svg) ด้วย Pillow
ใช้: uv run --with pillow python scripts/gen-icon.py
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent / "chiaki-ng" / "gui"
BG, BORDER, ACCENT, TEXT = "#0b0f14", "#2a3441", "#00a7ff", "#e8edf2"


def render(size: int) -> Image.Image:
    s = 8  # supersample
    n = size * s
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    u = n / 256.0  # unit = 1px ของ viewBox 256
    pad, r = 8 * u, 56 * u
    d.rounded_rectangle([pad, pad, n - pad, n - pad], radius=r, fill=BG, outline=BORDER, width=max(1, int(4 * u)))
    # ring arc เหมือน SVG: เส้นกลางที่ r=76, หนา 18, เว้นช่อง 75° เริ่มที่ -60°
    # Pillow วาดความหนาเข้าด้านในจาก bounding box → ใช้ box ที่ r_outer = 76 + 9 และ cap ที่ r=76
    cx = cy = 128 * u
    rr, w = 76 * u, 18 * u
    ro = rr + w / 2
    box = [cx - ro, cy - ro, cx + ro, cy + ro]
    start, end = -60 + 75, -60 + 360  # เว้นช่อง 75°
    d.arc(box, start=start, end=end, fill=ACCENT, width=int(w))
    # round caps ที่เส้นกลาง
    for ang in (start, end):
        a = math.radians(ang)
        px, py = cx + rr * math.cos(a), cy + rr * math.sin(a)
        d.ellipse([px - w / 2, py - w / 2, px + w / 2, py + w / 2], fill=ACCENT)
    # play triangle
    d.polygon([(108 * u, 88 * u), (108 * u, 168 * u), (176 * u, 128 * u)], fill=TEXT)
    return im.resize((size, size), Image.LANCZOS)


def main() -> None:
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = {sz: render(sz) for sz in sizes}
    imgs[512] = render(512)
    imgs[512].save(ROOT / "pswrap.png")
    imgs[256].save(ROOT / "pswrap.ico", format="ICO", sizes=[(sz, sz) for sz in sizes],
                   append_images=[imgs[sz] for sz in sizes if sz != 256])
    print("wrote", ROOT / "pswrap.ico", "and pswrap.png")


if __name__ == "__main__":
    main()
