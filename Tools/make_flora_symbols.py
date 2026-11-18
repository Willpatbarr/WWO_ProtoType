"""Makes the flora symbols (Tools/art/ui/) from Will's drawings (Tools/art/flora_src/<flora>.webp: white
line art on black).

For each flora two 512 x 512 PNGs:
  flora_<id>_symbol.png  the symbol alone, white on clear (the black taken out: alpha from brightness)
  flora_<id>_badge.png   the symbol over a dark shade of the flora's colour (Tools/wwo_data.json's), a soft
                         radial glow toward the middle, so the white lines read on it

Plain Python with Pillow (pip install pillow); then Tools/import_flora_symbols.py brings them into Unreal.
    python3 Tools/make_flora_symbols.py
"""
import json
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "art", "flora_src")
OUT = os.path.join(HERE, "art", "ui")
SIZE = 512
FLOOR, FULL = 24, 190   # brightness under FLOOR is background (noise in the black); FULL and up is fully opaque
EDGE_SHADE = 0.20       # the badge's edge: the flora colour this bright (sRGB)
BADGE_ART = 0.86        # how much of the badge the symbol spans: room to spare, since the Loadout tab's card
                        # (wider than tall) shows the badge scaled to fill it, its top and bottom trimmed
MIDDLE_SHADE = 0.36     # and its middle


def flora_colors():
    data = json.load(open(os.path.join(HERE, "wwo_data.json")))
    return {f["id"]: f["color"][:3] for f in data["flora"]}


def symbol(path):
    """White on clear: alpha from brightness, ramped from FLOOR to FULL; cropped square round the art."""
    src = Image.open(path).convert("RGB")
    lum = src.convert("L")
    alpha = lum.point(lambda v: 0 if v <= FLOOR else 255 if v >= FULL else int((v - FLOOR) * 255 / (FULL - FLOOR)))
    # Square crop round what's drawn, with a margin, so every symbol fills its square the same way.
    box = alpha.point(lambda v: 255 if v > 40 else 0).getbbox() or (0, 0, src.width, src.height)
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    half = max(box[2] - box[0], box[3] - box[1]) / 2 * 1.06
    crop = (int(cx - half), int(cy - half), int(cx + half), int(cy + half))
    white = Image.new("L", src.size, 255)
    out = Image.merge("RGBA", (white, white, white, alpha)).crop(crop)
    return out.resize((SIZE, SIZE), Image.LANCZOS)


def badge(sym, color):
    """The symbol over the flora's colour, dark, a little brighter toward the middle."""
    bg = Image.new("RGB", (SIZE, SIZE))
    px = bg.load()
    c = SIZE / 2
    for y in range(SIZE):
        for x in range(SIZE):
            t = min(math.hypot(x - c, y - c) / c, 1.0)  # EDGE_SHADE from the middle of each side out (the menu fills round it with that)
            k = MIDDLE_SHADE + (EDGE_SHADE - MIDDLE_SHADE) * t
            px[x, y] = tuple(int(255 * min(ch * k, 1.0)) for ch in color)
    out = bg.convert("RGBA")
    art = int(SIZE * BADGE_ART)
    out.alpha_composite(sym.resize((art, art), Image.LANCZOS), ((SIZE - art) // 2, (SIZE - art) // 2))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    colors = flora_colors()
    for flora in ("voltvine", "veilspore", "ironbark"):
        sym = symbol(os.path.join(SRC, flora + ".webp"))
        sym.save(os.path.join(OUT, "flora_%s_symbol.png" % flora))
        badge(sym, colors[flora]).save(os.path.join(OUT, "flora_%s_badge.png" % flora))
        print("%s: symbol and badge (colour %s)" % (flora, colors[flora]))


if __name__ == "__main__":
    main()
