"""Placeholder symbols for every ability and trait, in the hand of the flora emblems and grenade
symbols: fine, calligraphic white lines on black (each swelling in the middle and tapering at both
ends) with a soft glow, organic shapes with fine inner detail (veins, gills, crystal striations),
each a simple, mostly symmetric glyph inside a frame, more a symbol than a picture.

  Frames   abilities: a ring in the flora emblem's manner; traits: a diamond in the same hand, so the
           two read apart at a glance
             VeilSpore  a double ring, its arcs offset like a crescent, long thin spikes at the four points
             IronBark   a broken ring, small diamond stars at the four points, dots beyond them
             VoltVine   a ring with lightning kinks at its sides, a line straight through the middle
             Seedburst (no flora) the IronBark ring
  Glyphs   VoltVine   leaf chevrons, lightning, loops
           VeilSpore  mushroom caps, waves, droplets
           IronBark   rhombus seeds, shields, roots, bursts

Writes Tools/art/ability_src/<ability id>.png and Tools/art/trait_src/<trait id>.png (1024 x 1024);
Tools/make_symbols.py then turns them into the UI symbols and badges like the floras' and grenades'.
Replace any of them with a real drawing of the same name and rerun make_symbols.py.
    python3 Tools/make_placeholder_icons.py      (needs numpy and Pillow)
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
S = 2048          # drawn this big, then halved (smooth edges)
W = 10.0          # a glyph's line width at its widest (at S); the emblems' lines are fine
C = np.array([S / 2, S / 2])
GLYPH_SCALE = 1.38  # the glyphs are drawn this much bigger than laid out (they fill more of the frame)


# ---------- drawing ----------

def P(x, y): return np.array([x * S, y * S], float)


def unit(v): v = np.asarray(v, float); return v / (np.linalg.norm(v) + 1e-9)


def perp(v): return np.array([-v[1], v[0]])


def bez(p0, p1, p2, p3, n=90):
    t = np.linspace(0, 1, n)[:, None]
    return (1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t * t * p2 + t ** 3 * p3


def stroke(d, pts, w0=W, w1=None):
    """A line through pts. With only w0 it swells to w0 in the middle and tapers at both ends (the
    emblems' calligraphic line); with w1 too, its width runs from w0 to w1."""
    pts = np.asarray(pts, float)
    if len(pts) < 2: return
    t = np.linspace(0, 1, len(pts))
    w = w0 * (0.25 + 0.75 * np.sin(np.pi * t) ** 0.7) if w1 is None else w0 * (1 - t) + w1 * t
    tang = np.gradient(pts, axis=0); tang /= np.linalg.norm(tang, axis=1)[:, None] + 1e-9
    nrm = np.stack([-tang[:, 1], tang[:, 0]], 1)
    left = pts + nrm * (w / 2)[:, None]; right = pts - nrm * (w / 2)[:, None]
    d.polygon([tuple(p) for p in np.concatenate([left, right[::-1]])], fill=255)
    for p, r in ((pts[0], w[0] / 2), (pts[-1], w[-1] / 2)):
        if r > 1.2: d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=255)


def curve(d, p0, p1, p2, p3, w0=W, w1=None):
    stroke(d, bez(*(np.asarray(p, float) for p in (p0, p1, p2, p3))), w0, w1)


def line(d, a, b, w0=W, w1=None):
    stroke(d, np.linspace(np.asarray(a, float), np.asarray(b, float), 20), w0, w1)


def poly(d, pts, w=W, closed=True):
    pts = [np.asarray(p, float) for p in pts]
    for i in range(len(pts) - (0 if closed else 1)):
        line(d, pts[i], pts[(i + 1) % len(pts)], w)


def arc(d, c, r, a0, a1, w=W, w1=None, ry=None):
    c = np.asarray(c, float); t = np.linspace(a0, a1, 120)
    stroke(d, np.stack([c[0] + r * np.cos(t), c[1] + (ry or r) * np.sin(t)], 1), w, w1)


def circle(d, c, r, w=W): arc(d, c, r, 0, 2 * math.pi + 0.02, w, w)


def dot(d, c, r):
    c = np.asarray(c, float); d.ellipse([c[0] - r, c[1] - r, c[0] + r, c[1] + r], fill=255)


def star(d, c, r, thin=0.1):
    """A four-pointed star."""
    c = np.asarray(c, float); pts = []
    for k in range(8):
        a = math.pi / 4 * k - math.pi / 2
        rr = r if k % 2 == 0 else r * thin
        pts.append(tuple(c + [math.cos(a) * rr, math.sin(a) * rr]))
    d.polygon(pts, fill=255)


def rhombus(d, c, rx, ry, w=W, grain=True):
    """A seed / crystal: a diamond with a facet line down it and fine striations."""
    c = np.asarray(c, float)
    top, rt, bot, lt = c + [0, -ry], c + [rx, 0], c + [0, ry], c + [-rx, 0]
    for a, b in ((top, rt), (rt, bot), (bot, lt), (lt, top)):
        curve(d, a, a + (b - a) * 0.3 + (b - a)[::-1] * [1, -1] * 0.04, a + (b - a) * 0.7, b, w)
    if grain:
        line(d, top + [0, ry * 0.15], bot - [0, ry * 0.15], w * 0.45)
        for f in (-0.45, 0.45):
            line(d, top + [rx * f * 0.5, ry * 0.35], bot + [rx * f * 0.5, -ry * 0.35], w * 0.3)


def chevron(d, tip, size, dirn=(0, -1), w=W, spread=0.8):
    tip = np.asarray(tip, float); u = unit(dirn); p = perp(u)
    line(d, tip - u * size + p * size * spread, tip, w * 0.6, w)
    line(d, tip - u * size - p * size * spread, tip, w * 0.6, w)


def leaf(d, base, tip, width, w=W, rib=True):
    """A leaf glyph like the VoltVine emblem's: a gently waved outline, a rib and side veins."""
    base, tip = np.asarray(base, float), np.asarray(tip, float)
    ax = tip - base; u = unit(ax); n = perp(u) * width
    t = np.linspace(0, 1, 70)[:, None]
    for side in (1, -1):
        edge = bez(base, base + ax * 0.3 + n * side, base + ax * 0.75 + n * 0.6 * side, tip, 70)
        edge = edge + perp(u)[None] * side * (np.sin(t * math.pi * 3) * np.sin(math.pi * t) * width * 0.08)
        stroke(d, edge, w)
    if rib:
        line(d, base - u * width * 0.3, base + ax * 0.9, w * 0.6)
        for k in (0.3, 0.5, 0.7):
            for side in (1, -1):
                a = base + ax * k
                curve(d, a, a + ax * 0.05 + n * 0.25 * side, a + ax * 0.1 + n * 0.45 * side, a + ax * 0.14 + n * 0.55 * side * (1.1 - k), w * 0.35)


def bolt(d, a, b, w=W * 1.2):
    """A clean, symmetric lightning glyph from a to b."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    ax = b - a; n = perp(unit(ax)) * np.linalg.norm(ax) * 0.16
    pts = [a, a + ax * 0.45 + n, a + ax * 0.55 - n, b]
    for i in range(3): line(d, pts[i], pts[i + 1], w * (1 - i * 0.25), w * (0.75 - i * 0.25) + 1)
    off = perp(unit(ax)) * w * 2.2      # a fine line beside it, as the emblems double their strokes
    for i in range(2): line(d, pts[i] + off, pts[i + 1] + off * (0.6 if i == 1 else 1), w * 0.3)


def cap(d, c, wd, h, w=W, stem=True):
    """A mushroom glyph like the VeilSpore emblem's: a wide, flared cap, gills fanning down from its
    rim to a slender stem that curves away beneath."""
    c = np.asarray(c, float)
    L, R = c + [-wd / 2, h * 0.15], c + [wd / 2, h * 0.15]
    curve(d, L, c + [-wd * 0.3, -h * 1.0], c + [wd * 0.3, -h * 1.0], R, w)
    curve(d, L, c + [-wd * 0.2, -h * 0.1], c + [wd * 0.2, -h * 0.1], R, w * 0.6)
    foot = c + [wd * 0.06, h * 1.6]
    stem_top = c + [0, h * 0.05]
    if stem:
        curve(d, stem_top, stem_top + [0, h * 0.6], foot + [-wd * 0.12, -h * 0.5], foot, w)
    for k in range(7):   # gills
        x = (k + 0.5) / 7 - 0.5
        rim = c + [x * wd * 0.85, h * 0.02 - abs(x) * h * 0.1]
        curve(d, rim, rim + [0, h * 0.2], stem_top + [x * wd * 0.12, -h * 0.1], stem_top + [x * wd * 0.04, h * 0.25], w * 0.3)


def wave(d, a, b, amp, cycles=1.5, w=W, w1=None):
    a, b = np.asarray(a, float), np.asarray(b, float)
    t = np.linspace(0, 1, 120)[:, None]
    n = perp(unit(b - a))
    stroke(d, a + (b - a) * t + n * amp * np.sin(t * 2 * math.pi * cycles), w, w1)


def drop(d, c, r, w=W):
    """A droplet glyph: round at the bottom, pointed at the top."""
    c = np.asarray(c, float)
    top = c + [0, -r * 1.8]
    curve(d, top, top + [r * 0.3, r * 0.8], c + [r * 1.05, -r * 0.2], c + [r * 0.75, r * 0.7], w, w)
    curve(d, top, top + [-r * 0.3, r * 0.8], c + [-r * 1.05, -r * 0.2], c + [-r * 0.75, r * 0.7], w, w)
    arc(d, c + [0, r * 0.15], r * 0.95, 0.65, math.pi - 0.65, w)


def shield(d, c, wd, h, w=W, veins=True):
    """A shield: its outline, a fine inner one, and a rib with veins like a leaf's."""
    c = np.asarray(c, float)
    for k, (sc, ww) in enumerate(((1.0, w), (0.82, w * 0.35))):
        tl, tr, b = c + np.array([-wd / 2, -h * 0.42]) * sc, c + np.array([wd / 2, -h * 0.42]) * sc, c + np.array([0, h * 0.55]) * sc
        curve(d, tl, c + np.array([-wd * 0.18, -h * 0.5]) * sc, c + np.array([wd * 0.18, -h * 0.5]) * sc, tr, ww)
        for side, t0 in ((-1, tl), (1, tr)):
            curve(d, t0, t0 + [side * wd * 0.02 * sc, h * 0.45 * sc], b + [side * wd * 0.32 * sc, -h * 0.22 * sc], b, ww)
    if veins:
        line(d, c + [0, -h * 0.36], c + [0, h * 0.42], w * 0.4)
        for k in (-0.2, 0.0, 0.2):
            for side in (1, -1):
                a = c + [0, h * k]
                curve(d, a, a + [side * wd * 0.1, -h * 0.03], a + [side * wd * 0.2, -h * 0.08], a + [side * wd * 0.26, -h * 0.12], w * 0.25)


def arrow_head(d, tip, dirn, size):
    """An open, calligraphic arrow point: two fine strokes curving back from the tip."""
    tip = np.asarray(tip, float); u = unit(dirn); p = perp(u)
    for side in (1, -1):
        curve(d, tip, tip - u * size * 0.4 + p * size * 0.15 * side, tip - u * size * 0.8 + p * size * 0.45 * side, tip - u * size + p * size * 0.6 * side, W * 0.9, W * 0.25)


def arrow(d, a, b, size, w=W):
    """An arrow from a to b: a fine shaft, an open point, fletching at its tail."""
    a, b = np.asarray(a, float), np.asarray(b, float); u = unit(b - a); p = perp(u)
    line(d, a, b - u * size * 0.2, w * 0.8)
    arrow_head(d, b, u, size)
    for k in (0.0, 0.35):
        t0 = a + u * size * k
        for side in (1, -1):
            curve(d, t0, t0 - u * size * 0.15 + p * size * 0.2 * side, t0 - u * size * 0.3 + p * size * 0.3 * side, t0 - u * size * 0.45 + p * size * 0.35 * side, w * 0.6)


def double_chevron(d, tip, size, dirn=(0, -1), w=W, spread=0.8):
    """A chevron with a fine one tucked inside it."""
    chevron(d, tip, size, dirn, w, spread)
    chevron(d, np.asarray(tip, float) - unit(dirn) * size * 0.28, size * 0.65, dirn, w * 0.35, spread)


def curl_wave(d, a, b, amp, cycles=1.5, w=W, spores=True):
    """A wave ending in a little curl at each end, spore dots along it."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    wave(d, a, b, amp, cycles, w)
    u = unit(b - a); n = perp(u)
    for end, sgn in ((a, -1), (b, 1)):
        t = np.linspace(0, 1.6 * math.pi, 30); rr = amp * 1.1 * (1 - t / 7)
        stroke(d, end + (u[None] * sgn * np.sin(t)[:, None] + n[None] * (1 - np.cos(t))[:, None] * sgn) * rr[:, None], w * 0.6)
    if spores:
        for k in (0.3, 0.7):
            dot(d, a + (b - a) * k + n * amp * 2.2 * (1 if k < 0.5 else -1), w * 0.5)


def double_circle(d, c, r, w=W):
    circle(d, c, r, w)
    arc(d, c, r * 0.8, 0.3, 2 * math.pi - 0.3, w * 0.35)


def orb(d, c, r, w=W):
    """A spore orb: a ring with gill lines fanning inside and a crown of dots."""
    c = np.asarray(c, float)
    circle(d, c, r, w)
    for k in range(7):
        a = math.pi * (0.15 + 0.7 * k / 6)
        u = np.array([math.cos(a), math.sin(a)])
        line(d, c + u * r * 0.2, c + u * r * 0.85, w * 0.3)
    for k in range(3):
        a = -math.pi / 2 + (k - 1) * 0.5
        dot(d, c + np.array([math.cos(a), math.sin(a)]) * r * 1.35, w * 0.45)


def arc_arrow(d, c, r, a0, a1, w=W):
    arc(d, c, r, a0, a1, w * 0.7, w)
    t = a1; tang = np.array([-math.sin(t), math.cos(t)]) * (1 if a1 > a0 else -1)
    arrow_head(d, np.asarray(c, float) + r * np.array([math.cos(t), math.sin(t)]) + tang * r * 0.12, tang, r * 0.22)


def rays(d, c, r0, r1, n, w=W, phase=-math.pi / 2):
    c = np.asarray(c, float)
    for k in range(n):
        a = phase + 2 * math.pi * k / n
        u = np.array([math.cos(a), math.sin(a)])
        line(d, c + u * r0, c + u * (r1 if k % 2 == 0 else r0 + (r1 - r0) * 0.6), w, 1.0)


# ---------- frames ----------

def spike(d, c, u, inner, outer, w):
    """A long, thin cardinal spike: tapering to a point outward and inward, swelling where it
    crosses the ring."""
    c = np.asarray(c, float); u = np.asarray(u, float)
    stroke(d, np.linspace(c + u * inner, c + u * outer, 60), w * 1.6)


def frame(d, kind, flora):
    """The emblem round a glyph, in its flora's manner: a ring (abilities) or a diamond (traits)."""
    fw = W * 0.75
    accents(d, flora)
    if kind == "trait":
        R = 0.43 * S
        pts = [C + [0, -R], C + [R, 0], C + [0, R], C + [-R, 0]]
        for k in range(4):         # each side a slightly bowed, tapering line, doubled inside
            a, b = pts[k], pts[(k + 1) % 4]
            m = (a + b) / 2; inw = unit(C - m)
            curve(d, a, a + (b - a) * 0.3 - inw * 0.012 * S, a + (b - a) * 0.7 - inw * 0.012 * S, b, fw)
            a2, b2 = a + (C - a) * 0.08, b + (C - b) * 0.08
            curve(d, a2 + (b2 - a2) * 0.12, a2 + (b2 - a2) * 0.35, a2 + (b2 - a2) * 0.65, a2 + (b2 - a2) * 0.88, fw * 0.45)
        for p in pts:
            u = unit(p - C)
            if flora == "veilspore": spike(d, C, u, 0.38 * S, 0.49 * S, fw)
            elif flora == "voltvine": spike(d, C, u, 0.4 * S, 0.48 * S, fw)
            else: star(d, p, 0.035 * S, thin=0.12)
        if flora == "veilspore":
            for k in range(4):
                a, b = pts[k], pts[(k + 1) % 4]
                dot(d, (a + b) / 2 + unit(C - (a + b) / 2) * 0.05 * S, W * 0.45)
        for k in range(4):        # small sparkles outside the diagonal sides
            a, b = pts[k], pts[(k + 1) % 4]
            m = (a + b) / 2
            star(d, m + unit(m - C) * 0.045 * S, 0.018 * S, thin=0.15)
        return
    R = 0.38 * S
    # A fine inner ring, broken at the four points, and sparkles out at the diagonals.
    for a0 in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
        arc(d, C, R * 0.9, a0 + 0.3, a0 + math.pi / 2 - 0.3, fw * 0.4)
    for k in range(4):
        a = math.pi / 4 + math.pi / 2 * k
        star(d, C + np.array([math.cos(a), math.sin(a)]) * R * 1.2, 0.02 * S, thin=0.15)
    if flora == "veilspore":
        # Two rings a little apart, the outer one in arcs that taper off like a crescent; long thin
        # spikes through the four points.
        arc(d, C, R, 0, 2 * math.pi + 0.02, fw * 0.8, fw * 0.8)
        for a0 in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
            arc(d, C + [0.006 * S, -0.004 * S], R * 1.07, a0 + 0.18, a0 + math.pi / 2 - 0.18, fw)
        for k in range(4):
            a = math.pi / 2 * k
            spike(d, C, np.array([math.cos(a), math.sin(a)]), 0.33 * S, 0.5 * S, fw)
    elif flora == "voltvine":
        # A ring with lightning kinks at its sides and a line straight down through the middle,
        # crossing arcs at the foot.
        for side in (1, -1):
            arc(d, C, R, -math.pi / 2 + 0.05 + (0 if side > 0 else math.pi), -math.pi / 2 + math.pi - 0.05 + (0 if side > 0 else math.pi), fw * 0.8, fw * 0.8)
            k0 = C + [side * R, -0.06 * S]
            poly(d, [k0 + [-side * 0.01 * S, -0.04 * S], k0 + [side * 0.025 * S, 0], k0 + [-side * 0.01 * S, 0.03 * S], k0 + [side * 0.02 * S, 0.07 * S]], fw * 0.7, closed=False)
            curve(d, C + [side * 0.2 * S, 0.45 * S], C + [side * 0.25 * S, 0.33 * S], C + [side * 0.35 * S, 0.24 * S], C + [side * 0.44 * S, 0.2 * S], fw * 0.7)
        for u in (np.array([0, -1.0]), np.array([0, 1.0])):   # through the ring, stopping short of the glyph
            spike(d, C, u, 0.3 * S, 0.48 * S, fw * 0.7)
    else:
        # IronBark: a broken ring, small diamond stars at the four points, dots beyond them, short
        # arcs outside at the diagonals.
        for a0 in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
            arc(d, C, R, a0 + 0.1, a0 + math.pi / 2 - 0.1, fw * 0.8, fw * 0.8)
            arc(d, C, R * 1.08, a0 + math.pi / 4 - 0.35, a0 + math.pi / 4 + 0.35, fw * 0.7)
        for k in range(4):
            a = math.pi / 2 * k
            u = np.array([math.cos(a), math.sin(a)])
            star(d, C + u * R, 0.045 * S, thin=0.14)
            spike(d, C, u, 0.42 * S, 0.47 * S, fw * 0.6)
            dot(d, C + u * 0.49 * S, W * 0.5)


def accents(d, flora):
    """A pair of the flora's small motifs flanking the glyph low down (as the IronBark emblem's twigs
    flank its crystals)."""
    for side in (1, -1):
        base = C + [side * 0.2 * S, 0.24 * S]
        if flora == "voltvine":          # a leaf sprig curling up
            curve(d, base, base + [side * 0.02 * S, -0.04 * S], base + [side * 0.0 * S, -0.08 * S], base + [-side * 0.02 * S, -0.11 * S], W * 0.6)
            leaf(d, base + [side * 0.005 * S, -0.05 * S], base + [side * 0.06 * S, -0.1 * S], 0.014 * S, w=W * 0.55, rib=False)
            leaf(d, base + [-side * 0.01 * S, -0.09 * S], base + [-side * 0.05 * S, -0.14 * S], 0.011 * S, w=W * 0.5, rib=False)
        elif flora == "veilspore":       # a tiny mushroom and a few spores
            cap(d, base + [0, -0.05 * S], 0.05 * S, 0.018 * S, w=W * 0.6)
            for k, o in enumerate(((0.03, -0.11), (-0.02, -0.14), (0.0, -0.18))):
                dot(d, base + np.array([side * o[0], o[1]]) * S, W * (0.55 - 0.1 * k))
        else:                            # a branching twig with crystal buds
            tip = base + [side * 0.03 * S, -0.13 * S]
            curve(d, base, base + [side * 0.0 * S, -0.05 * S], tip + [-side * 0.02 * S, 0.03 * S], tip, W * 0.6)
            for t, sgn in ((0.45, 1), (0.7, -1)):
                p = base + (tip - base) * t
                line(d, p, p + [side * sgn * 0.035 * S, -0.03 * S], W * 0.45)
                rhombus(d, p + [side * sgn * 0.042 * S, -0.038 * S], 0.008 * S, 0.013 * S, W * 0.4, grain=False)
            rhombus(d, tip + [0, -0.012 * S], 0.01 * S, 0.016 * S, W * 0.45, grain=False)


# ---------- the glyphs ----------
# Each draws in the frame's middle, about 0.24 S round C.

GLYPHS = {}


def glyph(kind, name):
    def reg(f):
        GLYPHS[(kind, name)] = f; return f
    return reg


def at(x, y): return C + np.array([x, y]) * S


# VoltVine.

@glyph("ability", "overgrowth")
def _(d):
    line(d, at(0, .22), at(0, -.2), W * 1.3, W * .5)
    for k, y in enumerate((.12, .0, -.12)):
        s = 1 - k * 0.2
        leaf(d, at(0, y), at(-.13 * s, y - .08 * s), .03 * S * s)
        leaf(d, at(0, y), at(.13 * s, y - .08 * s), .03 * S * s)
    chevron(d, at(0, -.24), .05 * S)


@glyph("ability", "thornfist")
def _(d):
    rhombus(d, C, .13 * S, .17 * S, W * 1.2)
    rhombus(d, C, .06 * S, .08 * S, W * .6)
    for u in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        u = unit(u); base = C + u * np.array([.075, .095]) * S
        line(d, base, base + u * .1 * S, W * 1.1, 1.0)


@glyph("ability", "melee_guard")
def _(d):
    for side in (1, -1):
        curve(d, at(0, .2), at(side * .2, .1), at(side * .2, -.12), at(side * .06, -.2), W * 1.2, W * .5)
    bolt(d, at(0, -.17), at(0, .14), W)


@glyph("ability", "static_charge")
def _(d):
    double_circle(d, C, .14 * S, W)
    bolt(d, at(.0, -.12), at(.0, .12), W * 1.1)
    for a in (-0.6, 0.6, math.pi - 0.6, math.pi + 0.6):
        u = np.array([math.cos(a), math.sin(a)])
        arc(d, C, .22 * S, a - .25, a + .25, W * .7)
        line(d, C + u * .17 * S, C + u * .2 * S, W * .5, 1)


@glyph("trait", "extra_speed")
def _(d):
    for k in range(3):
        double_chevron(d, at(.08 + (k - 1) * .09, 0), .09 * S, dirn=(1, 0), w=W * (1.2 - k * .2), spread=.95)
    for k, y in enumerate((-.04, 0, .04)):
        line(d, at(-.22 + .02 * abs(k - 1), y), at(-.12, y), W * .5)


@glyph("trait", "faster_shield_recovery")
def _(d):
    shield(d, at(0, .01), .2 * S, .26 * S, W)
    arc_arrow(d, C, .21 * S, 2.5, 4.3, W * .8)
    arc_arrow(d, C, .21 * S, -0.6, 1.2, W * .8)


@glyph("trait", "more_melee_damage")
def _(d):
    rhombus(d, C, .08 * S, .11 * S, W)
    for a in np.arange(4) * math.pi / 2 + math.pi / 4:
        u = np.array([math.cos(a), math.sin(a)])
        line(d, C + u * .1 * S, C + u * .2 * S, W * 1.1, 1)
    star(d, C, .05 * S)


@glyph("trait", "faster_melee")
def _(d):
    for k in (-1, 0, 1):
        o = np.array([k * .07, 0]) * S
        line(d, at(-.08, .14) + o, at(.08, -.14) + o, W * (1.1 if k == 0 else .7), 1)
    chevron(d, at(.17, -.15), .05 * S, dirn=(1, -1))


@glyph("trait", "longer_snare")
def _(d):
    double_circle(d, at(-.1, 0), .07 * S, W)
    dot(d, at(-.1, 0), W * .8)
    arrow(d, at(-.01, 0), at(.23, 0), .06 * S)
    line(d, at(-.2, -.1), at(-.2, .1), W * .5)


# VeilSpore.

@glyph("ability", "spore_fog_damage")
def _(d):
    cap(d, at(0, .04), .26 * S, .1 * S)
    for k, y in enumerate((-.12, -.18, -.24)):
        curl_wave(d, at(-.1 + k * .02, y), at(.1 - k * .02, y), .012 * S, cycles=1.5, w=W * (.9 - k * .15), spores=k == 1)


@glyph("ability", "spore_dash")
def _(d):
    cap(d, at(.07, 0), .22 * S, .09 * S)
    for k, y in enumerate((-.03, .03, .09)):
        line(d, at(-.24 + k * .03, y), at(-.08, y), 1, W * .9)


@glyph("ability", "spore_patches")
def _(d):
    arc(d, at(0, .15), .2 * S, 0, 2 * math.pi + .02, W * .7, ry=.05 * S)
    for x, y, s in ((-.1, .1, .8), (0, .02, 1.0), (.1, .1, .8)):
        cap(d, at(x, y), .12 * S * s, .045 * S * s, W * .9)


@glyph("trait", "longer_fog")
def _(d):
    for k, y in enumerate((-.08, 0, .08)):
        L = .17 if k == 1 else .11
        curl_wave(d, at(-L, y), at(L, y), .013 * S, cycles=2, w=W * (1.1 if k == 1 else .7), spores=k != 1)
    arrow_head(d, at(.25, 0), (1, 0), .05 * S)
    arrow_head(d, at(-.25, 0), (-1, 0), .05 * S)


@glyph("trait", "no_radar")
def _(d):
    for r in (.07, .14, .21):
        arc(d, at(0, .1), r * S, math.pi * 1.2, math.pi * 1.8, W * .8)
    dot(d, at(0, .1), W * 1.2)
    wave(d, at(-.22, .02), at(.22, -.14), .02 * S, cycles=1.5, w=W * 1.2)


@glyph("trait", "bouncier_grenades")
def _(d):
    orb(d, at(-.15, -.13), .05 * S, W)
    t = np.linspace(0, 1, 60)
    xs = -.15 + .1 * t; ys = -.07 + .19 * t ** 2           # the drop
    stroke(d, np.stack([C[0] + xs * S, C[1] + ys * S], 1), W * .8, W * .8)
    for x0, x1, h in ((-.05, .1, .17), (.1, .2, .08)):    # and the bounces, each lower
        xs = x0 + (x1 - x0) * t; ys = .12 - h * 4 * t * (1 - t)
        stroke(d, np.stack([C[0] + xs * S, C[1] + ys * S], 1), W * .8, W * .8)
    line(d, at(-.2, .15), at(.22, .15), W * .6)


@glyph("trait", "longer_throws")
def _(d):
    t = np.linspace(0, 1, 13)
    for k in range(12):
        if k % 2: continue
        a, b = t[k], t[k + 1]
        pa = at(-.2 + .36 * a, .1 - .3 * 4 * a * (1 - a)); pb = at(-.2 + .36 * b, .1 - .3 * 4 * b * (1 - b))
        line(d, pa, pb, W, W)
    orb(d, at(.2, .13), .045 * S, W)
    arrow_head(d, at(.2, .04), (0.3, 1), .05 * S)


@glyph("trait", "stronger_poison")
def _(d):
    drop(d, at(0, .04), .09 * S, W * 1.1)
    drop(d, at(0, .05), .04 * S, W * .6)
    for x in (-.17, .17):
        line(d, at(x, -.12), at(x, -.04), W * .7); line(d, at(x - .04, -.08), at(x + .04, -.08), W * .7)


# IronBark.

@glyph("ability", "root_bombs")
def _(d):
    rhombus(d, at(0, -.08), .08 * S, .11 * S, W * 1.1)
    for side in (-1, 1):
        rhombus(d, at(side * .1, -.1), .03 * S, .05 * S, W * .5)
    for side in (-1, 0, 1):
        curve(d, at(0, .03), at(side * .04, .1), at(side * .14, .12), at(side * .16, .2), W * (1.0 if side == 0 else .8), 1)
    line(d, at(0, .03), at(0, .2), W * .9, 1)


@glyph("ability", "shield_share")
def _(d):
    shield(d, C, .2 * S, .27 * S, W * 1.2)
    rhombus(d, at(0, -.01), .04 * S, .06 * S, W * .6)
    for side in (-1, 1):
        arrow(d, at(side * .14, 0), at(side * .27, 0), .055 * S)


@glyph("ability", "extra_grenade")
def _(d):
    rhombus(d, at(-.06, .03), .08 * S, .12 * S, W)
    rhombus(d, at(.07, -.02), .08 * S, .12 * S, W * 1.1)
    rhombus(d, at(-.17, .1), .03 * S, .045 * S, W * .5)
    star(d, at(.17, -.17), .06 * S, thin=.15)
    for k in range(3): dot(d, at(.1 + .04 * k, .16 - .03 * k), W * .45)


@glyph("ability", "seedburst")
def _(d):
    rhombus(d, C, .06 * S, .09 * S, W * 1.1)
    for k in range(8):
        a = -math.pi / 2 + k * math.pi / 4
        u = np.array([math.cos(a), math.sin(a)])
        c = C + u * .2 * S
        rhombus(d, c, .018 * S, .018 * S, W * .6)
        line(d, C + u * .11 * S, C + u * .16 * S, W * .5, 1)


@glyph("trait", "faster_grenade_recharge")
def _(d):
    rhombus(d, C, .07 * S, .1 * S, W)
    arc_arrow(d, C, .18 * S, 2.0, 2.0 + 4.6, W * .9)


@glyph("trait", "longer_walls")
def _(d):
    for x in (-.08, 0, .08):
        poly(d, [at(x - .03, .12), at(x - .03, -.08), at(x, -.13), at(x + .03, -.08), at(x + .03, .12)], W * .8, closed=False)
    line(d, at(-.15, .12), at(.15, .12), W * .8)
    for side in (-1, 1):
        arrow(d, at(side * .14, .02), at(side * .26, .02), .05 * S)


@glyph("trait", "impact_grenades")
def _(d):
    rhombus(d, at(0, -.06), .06 * S, .09 * S, W)
    line(d, at(-.2, .12), at(.2, .12), W)
    for side in (-1, 1):
        line(d, at(side * .05, .07), at(side * .14, .02), W * .7, 1)
        line(d, at(side * .08, .1), at(side * .18, .08), W * .6, 1)


@glyph("trait", "bigger_explosions")
def _(d):
    rays(d, C, .06 * S, .24 * S, 8, W * 1.2, phase=-math.pi / 2)
    circle(d, C, .05 * S, W * .8)
    star(d, C, .04 * S)


@glyph("trait", "more_durability")
def _(d):
    shield(d, C, .24 * S, .3 * S, W * 1.1)
    shield(d, at(0, .01), .15 * S, .19 * S, W * .7)
    rhombus(d, at(0, -.01), .03 * S, .045 * S, W * .6)


# ---------- output ----------

def finish(img, path):
    """Halve it (smooth edges) and add the emblems' soft glow."""
    small = img.resize((S // 2, S // 2), Image.LANCZOS)
    glow = small.filter(ImageFilter.GaussianBlur(9)).point(lambda v: int(v * 0.5))
    out = np.maximum(np.asarray(small, np.float32), np.asarray(glow, np.float32))
    Image.fromarray(out.astype(np.uint8)).convert("RGB").save(path)


def flora_of(kind, name):
    data = json.load(open(os.path.join(HERE, "wwo_data.json")))
    for item in data["abilities" if kind == "ability" else "traits"]:
        if item["id"] == name: return item.get("flora") or "none"
    return "none"


def main():
    for kind in ("ability", "trait"):
        os.makedirs(os.path.join(HERE, "art", kind + "_src"), exist_ok=True)
    for (kind, name), draw in sorted(GLYPHS.items()):
        img = Image.new("L", (S, S), 0)
        frame(ImageDraw.Draw(img), kind, flora_of(kind, name))
        # The glyph is drawn on its own and blown up a little round the middle (its lines with it),
        # so it reads at the small sizes the HUD and the tiles show it at.
        g = Image.new("L", (S, S), 0)
        draw(ImageDraw.Draw(g))
        big = int(S * GLYPH_SCALE)
        g = g.resize((big, big), Image.LANCZOS).crop(((big - S) // 2, (big - S) // 2, (big - S) // 2 + S, (big - S) // 2 + S))
        img = Image.fromarray(np.maximum(np.asarray(img), np.asarray(g)))
        finish(img, os.path.join(HERE, "art", kind + "_src", name + ".png"))
        print("%s %s" % (kind, name))


if __name__ == "__main__":
    main()
