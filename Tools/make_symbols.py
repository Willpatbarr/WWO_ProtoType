"""Makes the UI symbols (Tools/art/ui/) from Will's drawings (white line art on black):
  floras    Tools/art/flora_src/<flora id>.webp      (voltvine, veilspore, ironbark)
  grenades  Tools/art/grenade_src/<grenade id>.png   (volt_grenade, spore_pool, wall_grenade, ...)
  abilities Tools/art/ability_src/<ability id>.png   (placeholders: Tools/make_placeholder_icons.py)
  traits    Tools/art/trait_src/<trait id>.png       (placeholders too; a trait's colour is its flora's)

For each one two 512 x 512 PNGs, <kind>_<id>_symbol.png and <kind>_<id>_badge.png:
  symbol  the drawing alone, white on clear (the black taken out: alpha from brightness), cropped square
  badge   the symbol over a dark shade of its own colour (Tools/wwo_data.json's: the flora's, the grenade's),
          a soft glow toward the middle, so the white lines read on it

Plain Python with Pillow (pip install pillow); then Tools/import_symbols.py brings them into Unreal.
    python3 Tools/make_symbols.py
"""
import json
import math
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "art", "ui")
SIZE = 512
FLOOR, FULL = 24, 190   # brightness under FLOOR is background (noise in the black); FULL and up is fully opaque
EDGE_SHADE = 0.20       # the badge's edge: its colour this bright (sRGB)
MIDDLE_SHADE = 0.36     # and its middle
BADGE_ART = 1.0         # how much of the badge the symbol spans (the symbol's own crop leaves a 3% margin)
# (kind, its source folder, its source extension, its list in wwo_data.json)
KINDS = (("flora", "flora_src", ".webp", "flora"), ("grenade", "grenade_src", ".png", "grenades"),
         ("ability", "ability_src", ".png", "abilities"), ("trait", "trait_src", ".png", "traits"))


def colors(table):
    # Each item's colour; an item without one (the traits) takes its flora's.
    data = json.load(open(os.path.join(HERE, "wwo_data.json")))
    flora = {f["id"]: f["color"][:3] for f in data["flora"]}
    return {item["id"]: (item.get("color") or flora.get(item.get("flora"), [0.5, 0.5, 0.6]))[:3] for item in data[table]}


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
    """The symbol over its colour, dark, a little brighter toward the middle."""
    bg = Image.new("RGB", (SIZE, SIZE))
    px = bg.load()
    c = SIZE / 2
    for y in range(SIZE):
        for x in range(SIZE):
            t = min(math.hypot(x - c, y - c) / c, 1.0)  # EDGE_SHADE from the middle of each side out
            k = MIDDLE_SHADE + (EDGE_SHADE - MIDDLE_SHADE) * t
            px[x, y] = tuple(int(255 * min(ch * k, 1.0)) for ch in color)
    out = bg.convert("RGBA")
    art = int(SIZE * BADGE_ART)
    out.alpha_composite(sym.resize((art, art), Image.LANCZOS), ((SIZE - art) // 2, (SIZE - art) // 2))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    for kind, folder, ext, table in KINDS:
        palette = colors(table)
        src = os.path.join(HERE, "art", folder)
        for name in sorted(os.listdir(src)):
            if not name.endswith(ext):
                continue
            item = name[: -len(ext)]
            if item not in palette:
                raise SystemExit("%s/%s: no %s with that id in wwo_data.json" % (folder, name, kind))
            sym = symbol(os.path.join(src, name))
            sym.save(os.path.join(OUT, "%s_%s_symbol.png" % (kind, item)))
            badge(sym, palette[item]).save(os.path.join(OUT, "%s_%s_badge.png" % (kind, item)))
            print("%s %s: symbol and badge (colour %s)" % (kind, item, palette[item]))


if __name__ == "__main__":
    main()
