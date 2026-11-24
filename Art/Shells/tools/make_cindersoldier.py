# Builds the CinderBloom soldier (Remnant ranged) on the soldier skeleton. This script is the model.
#   Blender -b ../base/enemy_soldier_rig.blend -P make_cindersoldier.py            build, save, render the sheet
#   Blender -b ../base/enemy_soldier_rig.blend -P make_cindersoldier.py -- pose    also render a posed skinning check
#   Blender -b ../base/enemy_soldier_rig.blend -P make_cindersoldier.py -- export  also write export/SK_CinderSoldier.fbx
# Out: cindersoldier_claude.blend, renders/cindersoldier_*.png. Reference: reference/cindersoldier_concept.webp,
# reference/cinderbloom_shell_cds.webp. Shapes, skinning, colour and export live in remnant_kit.py.
#
# A slim upright body of wound wood with a fire banked inside it: the rib cage and the head hold an ember core that
# shows between the strands and through the holes in the bone. Broad crimson leaves stand off the head as a crest,
# mantle the shoulders, and hang as a cloak and skirt; thin stems carry glowing buds. It carries no weapon: the
# game attaches one.
# Roles: Wood (dark core), Sinew (strands), Bone (mask, plates, claws), Leaf (crimson), Vine (stems, creepers),
# Ember (glows: the inner fire, leaf veins, buds, lanterns).
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from remnant_kit import *

setup(seed=23,
      colors={"Wood": (0.011, 0.008, 0.008), "Sinew": (0.05, 0.024, 0.019), "Bone": (0.5, 0.36, 0.25),      # charred, not weathered
              "Leaf": (0.1, 0.006, 0.012), "Vine": (0.2, 0.065, 0.018), "Ember": (1.0, 0.2, 0.006)},

      rough={"Wood": 0.85, "Sinew": 0.7, "Bone": 0.55, "Leaf": 0.55, "Vine": 0.6, "Ember": 0.4},
      vary={"Wood": (0.16, 0.035, 0.10), "Sinew": (0.16, 0.035, 0.11), "Bone": (0.06, 0.035, 0.07),
            "Leaf": (0.22, 0.07, 0.08), "Vine": (0.14, 0.06, 0.08), "Ember": (0.14, 0.05, 0.06)},
      glow={"Ember": 1.3})
HOT = (0.7, 0.085, 0.004)      # what a leaf burns toward at its tip and rim

# the trunk's section, by height: (z, centre y, half width, half depth)
TRUNK = [(92, 1.0, 10.5, 7.5), (100, 0.8, 11.0, 8.0), (112, 0.5, 7.6, 5.8), (124, 1.5, 9.4, 7.0), (136, 3.0, 12.8, 8.8), (147, 4.5, 13.6, 8.8), (154, 5.6, 6.2, 5.4)]
TZ = [t[0] for t in TRUNK]
def trunk_at(z): return [np.interp(z, TZ, [t[i] for t in TRUNK]) for i in (1, 2, 3)]


def leaves(n, p, d, nrm, bone, L, W, curl, spread=1.5, jitter=0.2):
    """n broad leaves from around p, each with its ember vein."""
    for _ in range(n):
        lf, vn, heat = blade(A(*p) + RNG.normal(0, spread, 3), unit(A(*d) + RNG.normal(0, jitter, 3)), A(*nrm) + RNG.normal(0, 0.2, 3),
                       RNG.uniform(*L), RNG.uniform(*W), curl=curl * RNG.uniform(0.6, 1.3))
        add(*lf, "Leaf", bone, grad=(HOT, heat)); add(*vn, "Ember", bone)


def buds(n, p, d, bone, L=(8, 15), spread=1.2, jitter=0.35, bend=0.7, toward=(0, 0, 1), bud=0.7, long=2.2):
    for _ in range(n):
        st, bd = bud_stem(A(*p) + RNG.normal(0, spread, 3), unit(A(*d) + RNG.normal(0, jitter, 3)), RNG.uniform(*L),
                          bend=bend * RNG.uniform(0.5, 1.4), toward=toward, bud=bud * RNG.uniform(0.85, 1.2), long=long * RNG.uniform(0.85, 1.2))
        add(*st, "Vine", bone); add(*bd, "Ember", bone)


def build_body():
    # --- trunk: slim, with a fire in the rib cage
    ctrl = [(0, t[1], t[0]) for t in TRUNK]; rx = [t[2] for t in TRUNK]; ry = [t[3] for t in TRUNK]
    cv, sv, th = bundle(ctrl, rx, ry, strands=9, twist=0.5, n=24, k=10, strand=0.26, core=0.86, hint=(1, 0, 0), rough=0.7, creep=2, creep_r=0.55)
    wt = lambda v: chain_w(v, ["pelvis", "spine_01", "spine_02", "spine_03"], J["neck_01"])
    add(*cv, "Wood", wt(cv[0])); add(*sv, "Sinew", wt(sv[0])); add(*th, "Ember", wt(th[0]))
    fire = tube(spline([(0, 1.2, 120), (0, 2.2, 129), (0, 3.4, 138), (0, 4.4, 146), (0, 5.0, 151)], 9), [6.5, 9.6, 12.4, 12.6, 6.5], k=10, rb=[5.2, 7.3, 8.6, 8.4, 5.2], hint=(1, 0, 0))
    add(*fire, "Ember", wt(fire[0]))
    # --- neck, and a skull of wound wood with its own fire behind the face
    cv, sv, _ = bundle([(0, 5.4, 152), J["neck_01"], J["head"] + A(0, -0.5, 1)], [5.2, 4.2, 4.4], strands=5, twist=0.4, n=8, strand=0.3, core=0.85, hint=(1, 0, 0), rough=0.4)
    wn = lambda v: chain_w(v, ["spine_03", "neck_01", "head"], (0, 0, 180), blend=3)
    add(*cv, "Wood", wn(cv[0])); add(*sv, "Sinew", wn(sv[0]))
    cv, sv, _ = bundle([(0, 3.0, 164), (0, 1.5, 169), (0, 0.6, 175), (0, 0.6, 181), (0, 1.5, 186.5)], [4.6, 8.0, 9.2, 8.0, 2.6], [5.0, 8.6, 9.8, 8.4, 2.8],
                       strands=8, twist=0.35, n=14, k=10, strand=0.24, core=0.92, hint=(1, 0, 0), rough=0.4)
    add(*cv, "Wood", "head"); add(*sv, "Sinew", "head")
    add(*ball((0, -6.2, 172.5), 7.6, 10.5, k=9, rings=6), "Ember", "head")
    # --- shoulder yoke
    sh = J["upperarm_l"]
    cv, sv, _ = bundle([(5, 4.5, 147.5), (12, 8, 151.5), sh + A(1, 0, 0.5)], [5.6, 5.3, 5.2], strands=5, twist=0.5, n=10, strand=0.36, rough=0.5)
    add(*cv, "Wood", "clavicle_l", True); add(*sv, "Sinew", "clavicle_l", True)
    # --- arms: lean; each side grown separately
    el, wr = J["lowerarm_l"], J["hand_l"]
    ctrl = [sh, lerp(sh, el, 0.5) + A(0.8, 0.8, 0), el, lerp(el, wr, 0.3) + A(0.4, 1.0, 0.6), lerp(el, wr, 0.72), wr]
    wa = lambda v: chain_w(v, ["upperarm_l", "lowerarm_l", "hand_l"], J["middle_01_l"])
    for side in (1, -1):
        cv, sv, th = bundle(ctrl, [5.0, 4.5, 3.0, 4.3, 3.1, 2.4], strands=6, twist=0.9, n=24, rough=1.0, creep=2, stubs=2, creep_r=0.55)
        for g, m in ((cv, "Wood"), (sv, "Sinew"), (th, "Ember")): add_side(*g, m, wa(g[0]), side)       # the creepers are veins of fire
        for c, r in ((el + A(0.8, 1.2, 0.5), 2.2), (lerp(sh, el, 0.3) + A(1.8, 1.0, 0), 1.9)):
            g = burl(c + RNG.normal(0, 0.6, 3), r * RNG.uniform(0.8, 1.2)); add_side(*g, "Sinew", wa(g[0]), side)
    # --- hands: built to hold a weapon, so plain fingers with short claws
    kn = (J["index_01_l"] + J["pinky_01_l"]) / 2
    cv, sv, _ = bundle([wr, lerp(wr, kn, 0.55), lerp(wr, kn, 1.08)], [2.5, 3.6, 3.2], [2.1, 1.8, 1.3], strands=5, twist=0.25, n=9, strand=0.5,
                       core=0.8, hint=J["pinky_01_l"] - J["index_01_l"], rough=0.6)
    add(*cv, "Wood", "hand_l", True); add(*sv, "Sinew", "hand_l", True)
    for f, r, claw in (("index", 1.1, 3.6), ("middle", 1.15, 4.0), ("ring", 1.1, 3.6), ("pinky", 0.95, 3.0), ("thumb", 1.2, 3.2)):
        js = [J["%s_0%d_l" % (f, i)] for i in (1, 2, 3)]
        d = unit(js[-1] - js[-2]); curl = unit(d - unit(js[-2] - js[-3]))
        tip = js[-1] + d * 2.6
        P = spline([lerp(wr, js[0], 0.5)] + js + [tip], 15)
        fv = tube(P, np.linspace(r * 1.15, r * 0.85, len(P)), k=5)
        bones = ["hand_l"] + ["%s_0%d_l" % (f, i) for i in (1, 2, 3)]
        add(*fv, "Sinew", chain_w(fv[0], bones, tip, blend=1.4), True)
        C = [tip - d * 0.8]; dd = d
        for i in range(5): C.append(C[-1] + dd * claw / 5); dd = unit(dd + curl * 0.2)
        tp = 1 - np.linspace(0, 1, 6) ** 1.6
        add(*tube(np.array(C), r * 0.9 * tp + 0.03, k=5, rb=r * 0.7 * tp + 0.03, hint=curl), "Bone", bones[-1], True)
    # --- legs
    hip, kne, ank, bal = J["thigh_l"], J["calf_l"], J["foot_l"], J["ball_l"]
    ctrl = [hip + A(0, 0, 1), lerp(hip, kne, 0.4) + A(0.8, -1.6, 0), kne, lerp(kne, ank, 0.28) + A(0.2, 2.8, 0), lerp(kne, ank, 0.75), ank]
    wl = lambda v: chain_w(v, ["thigh_l", "calf_l", "foot_l"], bal)
    for side in (1, -1):
        cv, sv, th = bundle(ctrl, [7.6, 7.0, 4.2, 5.6, 3.3, 2.8], strands=7, twist=0.8, n=26, rough=1.0, creep=2, stubs=2, creep_r=0.55)
        for g, m in ((cv, "Wood"), (sv, "Sinew"), (th, "Ember")): add_side(*g, m, wl(g[0]), side)
        for c, r in ((kne + A(0.4, -2.8, 0.5), 3.0), (lerp(hip, kne, 0.3) + A(3.8, -1.6, 0), 2.6)):
            g = burl(c + RNG.normal(0, 0.7, 3), r * RNG.uniform(0.8, 1.2)); add_side(*g, "Sinew", wl(g[0]), side)
    # --- feet: roots. Three run forward over the ball of the foot, two brace out behind
    wf = lambda v: chain_w(v, ["foot_l", "ball_l"], bal + A(0, -13, 0), blend=3)
    for dx, ln, r in ((-4.4, 0.8, 1.8), (0.4, 0.9, 2.1), (5.0, 0.76, 1.7)):
        P = spline([ank + A(0, -1, -2), lerp(ank, bal, 0.55) + A(dx * 0.35, 0, 1.2), bal + A(dx * 0.7, 0, 0.6), bal + A(dx, -9.5 * ln, -1.2), bal + A(dx * 1.25, -15.5 * ln, -1.7)], 13)
        tv = tube(P, [r * 1.1, r, r * 0.9, r * 0.6, 0.12], k=6)
        add(*tv, "Sinew", wf(tv[0]), True)
    for dx, dy in ((-6.0, 6.5), (5.5, 8.5)):
        P = spline([ank + A(0, 0.5, -3), ank + A(dx * 0.6, dy * 0.55, -8.5), ank + A(dx, dy, -12.4)], 8)
        add(*tube(P, [2.0, 1.4, 0.12], k=5), "Sinew", "foot_l", True)


def build_plates():
    # --- the face: a bone shell wrapped round the front of the skull, drawn down to a long pointed chin.
    #     The fire behind it shows through the eyes and the smaller holes.
    prof = spline([(0, -5.0, 187.5), (0, -10.5, 182), (0, -12.8, 174.5), (0, -12.4, 167.5), (0, -11.2, 162), (0, -10.8, 157)], 200)
    vv = np.linspace(0, 1, 200)
    def C(v): return A(0, np.interp(v, vv, prof[:, 1]), np.interp(v, vv, prof[:, 2]))
    def tan(v): return unit(C(min(v + 0.01, 1)) - C(max(v - 0.01, 0)))
    def out(v): t = tan(v); return A(0, t[2], -t[1])
    rad = lambda v: lerp(11.2, 1.5, min(max(v, 0), 1) ** 1.7)
    def mask(x, v):
        v = min(max(v, 0), 1); r = rad(v); a = x / r
        return C(v) + A(1, 0, 0) * r * sin(a) - out(v) * r * (1 - cos(a))
    hw = lambda v: rad(v) * lerp(1.45, 1.1, v)
    holes = [(0, 0.1, 1.6, 0.6, 0), (0, 0.6, 2.2, 0.6, 0), (0, 0.76, 1.5, 0.45, 0)]
    for s in (1, -1):
        holes += [(s * 4.6, 0.36, 3.4, 1.6, -s * 34), (s * 9.2, 0.26, 2.0, 0.8, -s * 20), (s * 5.2, 0.13, 1.7, 0.6, -s * 50),
                  (s * 2.6, 0.56, 1.6, 0.55, -s * 14), (s * 6.0, 0.52, 1.5, 0.6, -s * 30), (s * 2.0, 0.24, 0.5, 0.5, 0),
                  (s * 9.6, 0.42, 0.5, 0.5, 0), (s * 2.2, 0.71, 0.45, 0.45, 0)]
    add(*web_plate(mask, hw, holes, thick=1.4, nu=16, nv=18), "Bone", "head")
    for s in (1, -1):                                   # the brow flares up into two horns a side, like tongues of flame
        for x, ln, lean in ((11.5, 15, 0.55), (6.0, 10, 0.25)):
            p = mask(s * x, 0.04); P = arc_path(p + A(0, 0, -1.5), unit(A(s * lean, 0.25, 1)), (1, 0, 0), ln, -0.8, 8)
            tp = 1 - np.linspace(0, 1, 8) ** 1.4
            add(*tube(P, 2.0 * tp + 0.04, k=5, rb=0.9 * tp + 0.04, hint=(1, 0, 0)), "Bone", "head")
        for v, ln in ((0.3, 4.5), (0.48, 3.8), (0.66, 3.0), (0.82, 2.2)):             # drips off the jaw line
            add(*cone(mask(s * hw(v) * 0.95, v), (s * 0.25, 0.35, -1), ln, 0.7, k=4), "Bone", "head")
    add(*cone(C(0.97), (0, -0.1, -1), 5.0, 1.4, k=5), "Bone", "head")
    # --- pauldron
    sh = J["upperarm_l"]
    def paul(x, v):
        c = lerp(sh + A(-11, -2, 5.5), sh + A(15, 3, 8.5), v) + A(0, 0, 3.5 * sin(pi * v) - 4 * v * v)
        return c + A(0.25 * x * (v - 0.4), -x, -0.07 * x * x)
    hw = vesica(8.6, 1.0, 0.62)
    add(*web_plate(paul, hw, auto_holes(hw, 28, rows=4), thick=1.0), "Bone", "clavicle_l", True)
    # --- rib plates lapping round each flank
    for top, bot, a0, a1, W, bone in ((145, 124, 118, 26, 6.0, "spine_03"), (131, 111, 124, 38, 5.0, "spine_02")):
        def rib(x, v, top=top, bot=bot, a0=a0, a1=a1):
            z = lerp(top, bot, v) + 0.85 * x; a = radians(lerp(a0, a1, v) + 1.2 * x); yc, rx, ry = trunk_at(z)
            g = 3.6 + 2.0 * sin(pi * v)
            return A((rx + g) * sin(a), yc - (ry + g) * cos(a), z)
        hw = vesica(W, 1.0, 0.75)
        add(*web_plate(rib, hw, auto_holes(hw, 30, rows=3, v0=0.25, v1=0.75, gap=6.0), thick=1.1, nv=12), "Bone", bone, True)
    # --- breast plate and hip plate
    def chest(x, v):
        z = 149 - 24 * v; yc, rx, ry = trunk_at(z)
        return A(x, yc - ry - 1.6 - 1.6 * sin(pi * min(v * 1.2, 1)) + 0.045 * x * x, z)
    hw = lambda v: 10.5 * max(1 - v, 0) ** 0.8 * (0.75 + 0.25 * min(1, v / 0.15))
    add(*web_plate(chest, hw, auto_holes(hw, 24, rows=3, v0=0.16, v1=0.7, gap=5.0), thick=1.1, nu=8), "Bone", "spine_03")
    def hipp(x, v):
        z = 104 - 19 * v; yc, rx, ry = trunk_at(max(z, 92))
        return A(x, yc - ry - 1.4 - 1.2 * sin(pi * v) + 0.03 * x * x + 3 * v * v, z)
    hw = lambda v: 8.0 * max(1 - v, 0) ** 0.9 * (0.7 + 0.3 * min(1, v / 0.2))
    add(*web_plate(hipp, hw, auto_holes(hw, 19, rows=3, v0=0.15, v1=0.6), thick=0.9), "Bone", "pelvis")
    # --- knee spike
    kne = J["calf_l"]
    add(*cone(kne + A(0.5, -5.2, 1), (0.1, -0.7, 1), 8.5, 1.7, k=5), "Bone", "calf_l", True)


def build_dressing():
    hc = A(0, 0.6, 176)
    # --- crest: leaves standing off the crown and streaming back, like a flame laid over by the wind
    for i, ang in enumerate(np.radians([-72, -48, -24, 0, 24, 48, 72])):
        o = unit(A(sin(ang) * 0.95, 0.42, cos(ang)))
        leaves(1, hc + o * 8.8, A(sin(ang) * 0.6, 1.0, 0.55 * cos(ang) + 0.2), o, "head", L=(32, 48), W=(6.5, 8.5), curl=0.8, spread=0.5)
    for ang in np.radians([-95, -55, -18, 18, 55, 95]):
        o = unit(A(sin(ang), 0.8, 0.25 * cos(ang)))
        leaves(1, hc + o * 8.6 + A(0, 0, -3), A(sin(ang) * 0.5, 1.0, -0.15), o + A(0, 0, 0.6), "head", L=(24, 34), W=(5.5, 7), curl=0.6, spread=0.5)
    buds(5, hc + A(0, 3, 9), (0, 0.5, 1), "head", L=(9, 16), spread=2.5, jitter=0.5, toward=(0, 1, 0))
    for s in (1, -1):
        b = "_l" if s > 0 else "_r"; sh = J["upperarm_l"] * (s, 1, 1)
        # --- mantle: leaves lapped over each shoulder
        for ang in np.radians([-35, -5, 25, 55, 85, 115, 145]):                   # round the shoulder, front to back
            d = A(s * (0.55 + 0.45 * abs(cos(ang))), sin(ang) * 0.9 + 0.1, -0.25 + 0.3 * cos(ang))
            leaves(1, sh + A(-s * 3, 0.5, 4.5), d, (0, 0, 1), "clavicle" + b, L=(22, 34), W=(6, 8), curl=-0.6, spread=1.2)
        leaves(3, sh + A(-s * 7, 2, 5.5), (s * 0.45, 0.7, 0.75), (0, -0.4, 1), "clavicle" + b, L=(20, 30), W=(5.5, 7), curl=0.6, spread=1.5, jitter=0.35)
        buds(5, sh + A(-s * 4, 1, 6), (s * 0.5, 0.2, 1), "clavicle" + b, L=(9, 17), spread=2.0, jitter=0.5, toward=(s, 0, 0))
        # --- forearm sprig: leaves raked back toward the elbow, buds among them
        el, wr = J["lowerarm_l"] * (s, 1, 1), J["hand_l"] * (s, 1, 1); ax = unit(el - wr); outw = unit(A(s * 0.7, 0.7, 0.2))
        leaves(3, lerp(el, wr, 0.62) + outw * 3.2, ax * 0.9 + outw * 0.6, outw, "lowerarm" + b, L=(15, 21), W=(5, 6.5), curl=0.45, spread=1.0, jitter=0.3)
        buds(4, lerp(el, wr, 0.5) + outw * 3.2, ax * 0.6 + outw, "lowerarm" + b, L=(7, 12), spread=1.5, toward=ax)
        # --- skirt: leaves hanging from the waist, open at the front
        for ang in np.radians([44, 66, 88, 110, 132, 154, 174]):
            yc, rx, ry = trunk_at(101); o = A(s * sin(ang), -cos(ang), 0)
            leaves(1, A(s * rx * sin(ang), yc - ry * cos(ang), 101), o * 0.42 + A(0, 0, -1), o, "pelvis", L=(26, 46), W=(6.5, 8.5), curl=0.55, spread=0.8)
        # --- calf sprig
        kne, ank = J["calf_l"] * (s, 1, 1), J["foot_l"] * (s, 1, 1); outw = unit(A(s * 0.6, 0.8, 0))
        leaves(3, lerp(kne, ank, 0.6) + outw * 3.4, A(s * 0.15, 0.35, 1) + outw * 0.4, outw, "calf" + b, L=(18, 25), W=(5.5, 7), curl=0.4, spread=1.0, jitter=0.3)
        buds(4, lerp(kne, ank, 0.5) + outw * 3.4, A(0, 0.2, 1) + outw, "calf" + b, L=(8, 13), spread=1.5)
        # --- a lantern hung from the mantle
        buds(1, sh + A(s * 6, 3, 3), (s * 0.9, 0.3, 0.2), "clavicle" + b, L=(13, 17), spread=0.5, jitter=0.15, bend=1.7, toward=(0, 0, -1), bud=1.5, long=3.6)
    # --- cloak: long leaves down the back
    for x in (-12, -8.5, -5, -1.5, 2, 5.5, 9, 12.5):
        yc, rx, ry = trunk_at(144)
        leaves(1, (x, yc + ry + 0.5, 145 - 0.35 * abs(x)), (x * 0.025, 0.42, -1), (0, 1, 0.25), "spine_03", L=(36, 56), W=(7, 9), curl=0.6, spread=0.8, jitter=0.14)
    for x in (-10, -6, -2, 2, 6, 10):
        yc, rx, ry = trunk_at(138)
        leaves(1, (x, yc + ry + 2.5, 139), (x * 0.03, 0.7, -0.75), (0, 1, 0.5), "spine_03", L=(24, 34), W=(6, 7.5), curl=0.65, spread=0.8, jitter=0.18)
    buds(4, (0, 9, 150), (0, 0.9, 0.7), "spine_03", L=(10, 18), spread=4, jitter=0.5, toward=(0, 1, 0))
    # --- two lanterns swung from the crest, and a curling branch of seed pods off the left of the head
    for s in (1, -1):
        buds(1, hc + A(s * 7, 6, 4), (s * 0.8, 0.7, 0.3), "head", L=(16, 20), spread=0.4, jitter=0.15, bend=1.9, toward=(0, 0, -1), bud=1.6, long=3.8)
    for p, d, L, bend in (((9, 12, 150), (0.5, 0.8, 1), 44, 3.6), ((12, 12, 147), (1, 0.7, 0.5), 32, 3.2)):
        P = arc_path(A(*p), unit(d), (-1, 0.3, 0.2), L, bend, 14, grow=1.3)
        add(*tube(P, np.linspace(1.3, 0.5, 14), k=5), "Sinew", "spine_03")
        c = P[-1] + unit(P[-1] - P[-2]) * 2.6
        add(*ball(c, 2.6, 2.4, k=8, rings=5, axis=unit(P[-1] - P[-2])), "Sinew", "spine_03")
        for _ in range(7):
            o = unit(RNG.normal(0, 1, 3)); add(*ball(c + o * 2.3, 0.6, 0.6, k=5, rings=3, axis=o), "Ember", "spine_03")


build_body(); build_plates(); build_dressing()
ob = finish("SK_CinderSoldier", "M_Cinder_")
deliver(ob, "cindersoldier", target=(0, 4, 106), span=240,
        details=[("head", (-0.5, -1, 0.05), (0, 2, 176), 80), ("head_side", (-1, -0.1, 0), (0, 4, 176), 80),
                 ("torso", (0.6, -1, 0.1), (0, 0, 132), 95), ("leg", (-0.7, -1, 0.1), (-15, 0, 48), 110)],
        pose=[("spine_02", (1, 0, 0), -14), ("spine_03", (0, 0, 1), 22), ("head", (0, 0, 1), -18),
              ("upperarm_l", (1, 0, 0), -65), ("lowerarm_l", (0, 0, 1), 70), ("upperarm_r", (1, 0, 0), -50), ("lowerarm_r", (1, 0, 0), -75),
              ("thigh_l", (1, 0, 0), -40), ("calf_l", (1, 0, 0), 55), ("thigh_r", (1, 0, 0), 22), ("calf_r", (1, 0, 0), 25)])
