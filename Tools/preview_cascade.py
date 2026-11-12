"""Pictures of the Cascade layout as the builder will place it: a plan and the mockup's seven
sections, drawn from Tools/cascade_layout.py's boxes (not from the JSON), so any difference from
~/Documents/WWO_PvP_Mockups/Cascade_Plan.png and Cascade_Sec_*.png is a bug in cascade_layout.py.

Needs matplotlib (optional; nothing else in the project does): python3 Tools/preview_cascade.py [out folder]
Default out folder: ~/Documents/WWO_PvP_Mockups/cascade_build/
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cascade_layout as L  # noqa: E402

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Polygon as MPoly
except ImportError:
    raise SystemExit("preview_cascade.py needs matplotlib (pip install matplotlib)")

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Documents/WWO_PvP_Mockups/cascade_build")
COLORS = {
    "floor": "#a59e8f", "floor_z3": "#c9c2b0", "floor_z5": "#e6d9a8", "balcony": "#e6d9a8", "dais": "#bdb08a", "plinth": "#8f8878",
    "landing": "#9a9384", "bridge": "#b9b2a2", "ramp": "#7c7768", "steps": "#8c8676", "hall_wall": "#6f6a5c", "body": "#5e5a4f",
    "wall": "#4a4740", "roof": "#57534a", "tunnel_wall": "#6b6658", "tunnel_roof": "#8b8574", "gallery_wall": "#6b6658",
    "gallery_roof": "#8b8574", "rail": "#e8c95a", "pier": "#4f4b43", "islet": "#55524a", "arch": "#7d776a", "bracket": "#5b574d",
    "low_cover": "#5f8a4f", "block": "#4f6f8f", "crate": "#b07a3c", "pillar": "#2b2b2b", "monolith": "#2b2b2b", "column": "#e08a2c",
    "trim": "#ff8a26", "ivy": "#3f6b2f", "giant_arch": "#6d675b", "mesa": "#5c574c",
}
SKY, WATER = "#d8e6ef", "#2a6a80"


def footprint(b):
    """Plan outline of a box (pitched boxes: their horizontal extent)."""
    if b.shape == "cylinder":
        r = b.length * 0.5
        return [(b.x + r * math.cos(a * math.pi / 16), b.y + r * math.sin(a * math.pi / 16)) for a in range(32)]
    t = math.radians(b.yaw)
    p = math.radians(b.pitch)
    hl = b.length * 0.5 * abs(math.cos(p)) + b.height * 0.5 * abs(math.sin(p))
    hw = b.thickness * 0.5
    return [(b.x + su * hl * math.cos(t) - sv * hw * math.sin(t), b.y + su * hl * math.sin(t) + sv * hw * math.cos(t))
            for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1))]


def plan(out, path):
    fig, ax = plt.subplots(figsize=(18, 15))
    ax.set_facecolor("#173b47")
    under = ("arch", "pier", "bracket", "islet")    # drawn first: they are under the decks
    order = sorted((b for b in out["boxes"] if b.kind not in ("giant_arch", "mesa", "steps", "ramp")),
                   key=lambda b: (b.kind not in under, b.z + b.height * 0.5, b.kind == "trim"))
    for b in order:
        ax.add_patch(MPoly(footprint(b), closed=True, fc=COLORS.get(b.kind, "#ff00ff"), ec="#00000040", lw=0.2,
                           alpha=0.55 if b.kind in ("tunnel_roof", "gallery_roof", "roof", "body") else 1.0))
    for b in out["boxes"]:
        if b.kind == "steps":       # stairs on top, hatched like the mockup
            ax.add_patch(MPoly(footprint(b), closed=True, fc="#8c8676", ec="#3a362e", lw=0.3, hatch="///"))
    for team, col in (("blue", "#3a78e0"), ("orange", "#f08a2a")):
        xs = [s[0] for s in out["spawns"][team]]
        ys = [s[1] for s in out["spawns"][team]]
        ax.scatter(xs, ys, s=140, c=col, edgecolors="white", zorder=20)
    ax.scatter([e[0] for e in out["pve"]["enemies"]], [e[1] for e in out["pve"]["enemies"]], marker="X", s=90, c="#d83a3a", edgecolors="white", zorder=21)
    ax.scatter([l[0] for l in out["lights"]], [l[1] for l in out["lights"]], marker="*", s=70, c="#ffb347", edgecolors="#7a4a00", lw=0.4, zorder=22)
    for k, x, y, z in out["plants"]:
        if k == "maple":
            ax.scatter([x], [y], s=260, c="#cc3333", edgecolors="#3a1010", zorder=19)
    for w in out["waterfalls"]:
        ax.scatter([w["at"][0]], [w["at"][1]], marker="v", s=60, c="#9fe3ff", zorder=19)
    k = out["data"]["map"].get("plan_scale", 1.0)
    ax.set_xlim(-76 * k, 76 * k)
    ax.set_ylim(-70 * k, 70 * k)
    ax.set_aspect("equal")
    ax.grid(True, color="#ffffff22", lw=0.5)
    ax.set_title("Cascade as built from Tools/cascade_layout.py's boxes (plan, +y north). %d boxes" % len(out["boxes"]), fontsize=13)
    handles = [plt.Rectangle((0, 0), 1, 1, fc=COLORS[k]) for k in ("floor", "floor_z3", "floor_z5", "bridge", "tunnel_roof", "hall_wall", "body",
                                                                     "low_cover", "block", "crate", "pillar", "rail", "trim")]
    ax.legend(handles, ["z 0 deck", "z 3 deck", "z 5 deck / balcony", "bridge", "tunnel roof (translucent)", "hall wall", "building",
                        "low cover", "block", "crate", "pillar / monolith", "rail", "orange trim"], loc="upper left", bbox_to_anchor=(1.01, 1), fontsize=9)
    fig.savefig(path, dpi=100, bbox_inches="tight")
    plt.close(fig)


def cut(boxes, p0, p1, step=0.05):
    """Each box the line p0 -> p1 crosses, as an outline in (distance along the line, z)."""
    (x0, y0), (x1, y1) = p0, p1
    Ln = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / Ln, (y1 - y0) / Ln
    shapes = []
    for b in boxes:
        reach = math.hypot(b.length, b.thickness) * 0.5 + b.height
        # distance from the box's center to the line, and where along it
        s_c = (b.x - x0) * ux + (b.y - y0) * uy
        d = abs(-(b.x - x0) * uy + (b.y - y0) * ux)
        if d > reach or s_c < -reach or s_c > Ln + reach:
            continue
        lo, hi = max(0.0, s_c - reach), min(Ln, s_c + reach)
        tops, bots = [], []
        n = int((hi - lo) / step) + 1
        for i in range(n + 1):
            s = lo + (hi - lo) * i / max(n, 1)
            x, y = x0 + ux * s, y0 + uy * s
            if L.covers2d(b, x, y):
                tops.append((s, L.top_at(b, x, y)))
                bots.append((s, L.bottom_at(b, x, y)))
        if tops:
            shapes.append((b, tops + bots[::-1]))
    return shapes


def sections(out, folder):
    data = out["data"]
    boxes = [b for b in out["boxes"] if b.kind not in ("mesa",)]
    w = data["tunnels"][0]["outer_wall"]["windows"][0]
    ang1 = (w["a0"] + w["a1"]) / 2
    k = data["map"].get("plan_scale", 1.0)              # the mockup's 1.0x cuts, scaled with the layout
    gal = [t for t in data["tunnels"] if t.get("bridge_gallery")][0]["bridge_gallery"]
    cg = L.polar(gal["walls"][0]["window"]["at_r"], 45.0)
    hw = gal["bridge_width"] * 0.5 + 4.0
    BR = {b["name"]: b for b in data["bridges"]}

    def along(name, s_in, beyond):
        b = BR[name]
        d = (b["end"][0] - b["start"][0], b["end"][1] - b["start"][1])
        n = math.hypot(*d)
        u = (d[0] / n, d[1] / n)
        return (b["start"][0] - u[0] * s_in, b["start"][1] - u[1] * s_in), (b["end"][0] + u[0] * beyond, b["end"][1] + u[1] * beyond)

    bal = data["citadel"]["balcony"]["doors"][1]["angle_deg"]
    figs = [
        ("Cascade_Build_Sec_1_TunnelRadial.png", [("1. Radial cut through Tunnel_NNW at its first window (%.0f deg). Left = citadel, right = chasm" % ang1,
                                                   (0, 0), L.polar(36 * k, ang1), (8 * k, 36 * k), (-6, 13), 0.0)], (17, 8)),
        ("Cascade_Build_Sec_2_GalleryAcross.png", [("2. Across the NE bridge gallery at r 34 (looking toward the node; + = left)",
                                                    (cg[0] + hw * math.sqrt(0.5), cg[1] - hw * math.sqrt(0.5)), (cg[0] - hw * math.sqrt(0.5), cg[1] + hw * math.sqrt(0.5)),
                                                    (-hw, hw), (-8, 10), -hw)], (11, 9)),
        ("Cascade_Build_Sec_3_DiagonalTunnelGallery.png", [("3a. NE diagonal (45 deg): ring tunnel, gallery, LOW node z 0", (0, 0), L.polar(60 * k, 45), (8 * k, 60 * k), (-6, 13), 0.0),
                                                           ("3b. NW diagonal (135 deg): open ring, radial ramp to the HIGH node z 5", (0, 0), L.polar(60 * k, 135), (8 * k, 60 * k), (-6, 13), 0.0)], (17, 12)),
        ("Cascade_Build_Sec_4_BaseLinks.png", [("4a. East base -> NE node: base link, flat (z 0)",) + along("BaseLink_EN", 9.0, 20.0) + ((0, 29.0 + BR["BaseLink_EN"]["run_m"]), (-6, 13), 0.0),
                                               ("4b. East base -> SE node: base link climbing to the HIGH node (z 5)",) + along("BaseLink_ES", 9.0, 20.0) + ((0, 29.0 + BR["BaseLink_ES"]["run_m"]), (-6, 13), 0.0)], (17, 12)),
        ("Cascade_Build_Sec_5_NorthSouthAxis.png", [("5. North-south axis (x = 0): terminal z 3 - arm stair - arm z 0 - ring - hub stair - hall - building",
                                                     (0, -64 * k), (0, 64 * k), (-64 * k, 64 * k), (-14, 42), -64.0 * k)], (18, 11)),
        ("Cascade_Build_Sec_6_BaseRoomAcross.png", [("6. Across the east base room through both side doors (looking west; north on the right)",
                                                     (BR["BaseLink_EN"]["start"][0], -24 * k), (BR["BaseLink_EN"]["start"][0], 24 * k), (-24 * k, 24 * k), (-5, 11), -24.0 * k)], (17, 8)),
        ("Cascade_Build_Sec_7_Citadel.png", [("7a. Citadel through two hall doors (east-west axis)", (-40 * k, 0), (40 * k, 0), (-40 * k, 40 * k), (-3, 42), -40.0 * k),
                                             ("7b. Through a balcony door (%.1f deg): hall -> stair -> door -> z 5 balcony over the ring lane" % bal,
                                              (0, 0), L.polar(36 * k, bal), (0, 36 * k), (-3, 16), 0.0)], (17, 17)),
    ]
    written = []
    for name, panels, size in figs:
        fig, axes = plt.subplots(len(panels), 1, figsize=size)
        axes = axes if len(panels) > 1 else [axes]
        for ax, (title, p0, p1, xlim, ylim, s0) in zip(axes, panels):
            ax.set_facecolor(SKY)
            ax.axhspan(-60, L.WATER_Z, color=WATER)
            for b, poly in sorted(cut(boxes, p0, p1), key=lambda bp: bp[0].kind in ("trim", "rail")):
                hatch = "///" if b.kind == "steps" else None
                alpha = 0.35 if b.kind in L.NO_COLLISION and b.kind not in ("steps", "trim") else 1.0
                ax.add_patch(MPoly([(s + s0, z) for s, z in poly], closed=True, fc=COLORS.get(b.kind, "#ff00ff"), ec="#00000080", lw=0.4,
                                   hatch=hatch, alpha=alpha))
            ax.set_xlim(*xlim)
            ax.set_ylim(*ylim)
            ax.set_aspect("equal")
            ax.grid(True, color="#00000018", lw=0.5)
            ax.set_title(title + "  [from the boxes]", fontsize=11)
            ax.set_xlabel("distance along the cut (m)")
            ax.set_ylabel("z (m)")
        fig.savefig(os.path.join(folder, name), dpi=100, bbox_inches="tight")
        plt.close(fig)
        written.append(name)
    return written


def main():
    os.makedirs(OUT, exist_ok=True)
    out = L.build()
    plan(out, os.path.join(OUT, "Cascade_Build_Plan.png"))
    names = ["Cascade_Build_Plan.png"] + sections(out, OUT)
    for n in names:
        print(os.path.join(OUT, n))


if __name__ == "__main__":
    main()
