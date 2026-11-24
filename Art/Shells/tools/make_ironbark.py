# Builds the IronBark tank (Remnant heavy, also the FootRace boss) on the tank skeleton. This script is the model.
#   Blender -b ../base/enemy_tank_rig.blend -P make_ironbark.py            build, save, render the sheet
#   Blender -b ../base/enemy_tank_rig.blend -P make_ironbark.py -- pose    also render a posed skinning check
#   Blender -b ../base/enemy_tank_rig.blend -P make_ironbark.py -- export  also write export/SK_IronBark.fbx
# Out: ironbark_claude.blend, renders/ironbark_*.png. Reference: reference/ironbark_concept.png,
# reference/ironbark_shell_cds.png. Shapes, skinning, colour and export live in remnant_kit.py.
# Built at the skeleton's own size (head about 2.3 m up); the game draws this mesh at 0.55.
#
# A rooted hulk: a body of thick wound roots under slabs of bark hung like shingles, each split by a crack of
# glowing amber. Shoulders and hump are piled higher than the head and bristle with standing shards; the head sits
# low between them behind a disfigured head of bark: one knot-hole eye, a wandering split, slabs and burls grown over the rest, one horn whole and one snapped. Talons, forearm
# spikes, dead branches dripping sap off the hump, a little bracket fungus on the back, and root-flare feet.
# Roles: Wood (heartwood core), Root (the wound roots, stems, antlers), Bark (slabs, mask, nails), Leaf (rust),
# Cap (bracket fungus), Amber (glows: cracks, sap, the heart, the fire behind the mask).
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from remnant_kit import *

setup(seed=37,
      colors={"Wood": (0.008, 0.006, 0.005), "Root": (0.03, 0.019, 0.014), "Bark": (0.058, 0.034, 0.024),
              "Leaf": (0.13, 0.022, 0.008), "Cap": (0.07, 0.014, 0.012), "Amber": (1.0, 0.26, 0.004)},
      rough={"Wood": 0.9, "Root": 0.8, "Bark": 0.85, "Leaf": 0.6, "Cap": 0.5, "Amber": 0.3},
      vary={"Wood": (0.16, 0.035, 0.10), "Root": (0.18, 0.04, 0.12), "Bark": (0.2, 0.05, 0.14),
            "Leaf": (0.25, 0.12, 0.08), "Cap": (0.18, 0.06, 0.08), "Amber": (0.12, 0.04, 0.05)},
      glow={"Amber": 1.6})

# the trunk's section, by height: (z, centre y, half width, half depth)
TRUNK = [(120, 0.0, 25, 19), (134, 0.0, 27, 21), (148, -0.5, 23, 19), (162, -1.5, 31, 24), (178, -2.0, 42, 29), (196, 0.0, 46, 30), (210, -2.5, 22, 18)]


HAND = 0.8                    # the hands, as a fraction of the size the skeleton's finger bones would give
SLOUCH = A(0, -15, -11)       # the whole head, mask and horns are carried this far forward and down of the head bone


def bark(base, along, nrm, L, W, bone, mirror=False, **kw):
    pl, cr = slab(A(*base), along, nrm, L, W, **kw)
    add(*pl, "Bark", bone, mirror)
    if cr: add(*cr, "Amber", bone, mirror)


def finger(f):
    return [f + "_01_l", f + "_02_l", f.capitalize() + "_03_L"]


def build_body():
    # --- trunk: a barrel of roots, with a hump across the shoulders
    ctrl = [(0, t[1], t[0]) for t in TRUNK]; rx = [t[2] for t in TRUNK]; ry = [t[3] for t in TRUNK]
    cv, sv, _ = bundle(ctrl, rx, ry, strands=11, twist=0.4, n=24, k=12, strand=0.24, core=0.88, hint=(1, 0, 0), rough=0.8)
    wt = lambda v: chain_w(v, ["pelvis", "spine_01", "spine_02", "spine_03"], J["neck_01"], blend=7)
    add(*cv, "Wood", wt(cv[0])); add(*sv, "Root", wt(sv[0]))
    hv = tube(spline([(0, 14, 158), (0, 27, 184), (0, 30, 212), (0, 14, 236)], 9), [26, 40, 42, 22], k=12, rb=[10, 17, 18, 10], hint=(1, 0, 0))
    add(*hv, "Wood", chain_w(hv[0], ["spine_02", "spine_03"], J["neck_01"], blend=7))
    for s in (1, -1):                                   # heavy roots over the hump
        P = spline([(s * 34, 16, 214), (s * 18, 40, 208), (0, 42, 186), (-s * 14, 30, 160), (-s * 22, 16, 138)], 14)
        vv = tube(P, [5.0, 6.5, 6.0, 5.0, 3.0], k=6)
        add(*vv, "Root", wt(vv[0]))
    # --- neck and a small head carried low and forward
    cv, sv, _ = bundle([(0, -3, 203), J["neck_01"] + A(0, -5, -3), J["head"] + SLOUCH + A(0, 2, 2)], [14, 12.5, 11.5], strands=6, twist=0.4, n=10, strand=0.3, core=0.85, hint=(1, 0, 0), rough=0.5)
    wn = lambda v: chain_w(v, ["spine_03", "neck_01", "head"], J["head"] + A(0, -3, 22), blend=5)
    add(*cv, "Wood", wn(cv[0])); add(*sv, "Root", wn(sv[0]))
    cv, sv, _ = bundle([(0, -13, 219), (0, -15, 226), (0, -16, 234), (0, -15.5, 242), (0, -14, 249)], [8, 12.5, 13.5, 11.5, 4], [8, 11.5, 12, 10.5, 4],
                       strands=8, twist=0.35, n=14, k=10, strand=0.24, core=0.92, hint=(1, 0, 0), rough=0.5)
    add(*cv, "Wood", "head"); add(*sv, "Root", "head")
    add(*ball((0, -23.5, 232), 11.0, 5.0, k=10, rings=5, axis=(0, 1, 0)), "Amber", "head")      # a disc of fire pressed behind the face
    # --- the heart: a knot of amber in the breast, framed by the chest slabs
    add(*ball((0, -24.0, 182), 6.0, 8.5, k=9, rings=6), "Amber", "spine_03")
    # --- shoulder yoke
    sh = J["upperarm_l"]
    cv, sv, _ = bundle([(12, 3, 202), (28, 7, 212), sh + A(5, 0, 5)], [20, 23, 21], strands=7, twist=0.5, n=10, strand=0.34, rough=0.6)
    add(*cv, "Wood", "clavicle_l", True); add(*sv, "Root", "clavicle_l", True)
    # --- arms: trunks; each side grown separately
    el, wr = J["lowerarm_l"], J["hand_l"]
    ctrl = [sh, lerp(sh, el, 0.5) + A(2, 1.5, 0), el, lerp(el, wr, 0.3) + A(1, 2, 1), lerp(el, wr, 0.72), wr]
    wa = lambda v: chain_w(v, ["upperarm_l", "lowerarm_l", "hand_l"], J["middle_01_l"], blend=8)
    for side in (1, -1):
        cv, sv, th = bundle(ctrl, [17.5, 15.5, 11, 18.5, 16, 11], strands=8, twist=0.7, n=24, rough=1.0, stubs=3)
        for g, m in ((cv, "Wood"), (sv, "Root"), (th, "Root")): add_side(*g, m, wa(g[0]), side)
        for c, r in ((el + A(2, 4, 1), 7.5), (lerp(sh, el, 0.3) + A(6, 3, 0), 6.5), (lerp(el, wr, 0.55) + A(4, 4, 0), 5.5)):
            g = burl(c + RNG.normal(0, 1.5, 3), r * RNG.uniform(0.8, 1.2)); add_side(*g, "Root", wa(g[0]), side)
    # --- hands: fists of root with a talon on each finger, drawn in toward the wrist (HAND) after skinning,
    #     so each part still follows the finger bone it was grown along
    h0 = len(PARTS)
    kn = (J["index_01_l"] + J["pinky_01_l"]) / 2
    cv, sv, _ = bundle([wr, lerp(wr, kn, 0.55), lerp(wr, kn, 1.08)], [9.5, 12.5, 11.5], [8, 7, 5.5], strands=6, twist=0.25, n=9, strand=0.45,
                       core=0.82, hint=J["pinky_01_l"] - J["index_01_l"], rough=0.6)
    add(*cv, "Wood", "hand_l", True); add(*sv, "Root", "hand_l", True)
    for f, r in (("index", 4.6), ("middle", 4.8), ("ring", 4.6), ("pinky", 4.0), ("thumb", 5.0)):
        bones = finger(f); js = [J[b] for b in bones]
        d = unit(js[-1] - js[-2]); curl = unit(d - unit(js[-2] - js[-3])); tip = js[-1] + d * 4.0
        P = spline([lerp(wr, js[0], 0.5)] + js + [tip], 15)
        fv = tube(P, np.linspace(r * 1.15, r * 0.9, len(P)), k=6)
        add(*fv, "Root", chain_w(fv[0], ["hand_l"] + bones, tip, blend=3.5), True)
        C = [tip - d * 3.0]; dd = d                       # a talon, hooked
        for i in range(6): C.append(C[-1] + dd * 3.3); dd = unit(dd + curl * 0.2)
        tp = 1 - np.linspace(0, 1, 7) ** 1.5
        add(*tube(np.array(C), r * 0.95 * tp + 0.06, k=6, rb=r * 0.62 * tp + 0.06, hint=curl), "Bark", bones[-1], True)
    for part in PARTS[h0:]: part["v"] = wr + (part["v"] - wr) * HAND
    # --- legs: stumps
    hip, kne, ank, bal = J["thigh_l"], J["calf_l"], J["foot_l"], J["ball_l"]
    ctrl = [hip + A(0, 0, 2), lerp(hip, kne, 0.4) + A(1.5, -2.5, 0), kne, lerp(kne, ank, 0.3) + A(0.5, 4, 0), lerp(kne, ank, 0.75), ank + A(0, 0, 2)]
    wl = lambda v: chain_w(v, ["thigh_l", "calf_l", "foot_l"], bal, blend=8)
    for side in (1, -1):
        cv, sv, th = bundle(ctrl, [20, 19, 13.5, 16.5, 13.5, 13], strands=8, twist=0.6, n=26, rough=1.0, stubs=3)
        for g, m in ((cv, "Wood"), (sv, "Root"), (th, "Root")): add_side(*g, m, wl(g[0]), side)
        for c, r in ((kne + A(1, -8, 1), 8.5), (lerp(hip, kne, 0.3) + A(10, -4, 0), 7.5), (lerp(kne, ank, 0.45) + A(-3, 8, 0), 6.0)):
            g = burl(c + RNG.normal(0, 1.5, 3), r * RNG.uniform(0.8, 1.2)); add_side(*g, "Root", wl(g[0]), side)
    # --- feet: a flare of roots gripping the ground all round
    wf = lambda v: chain_w(v, ["foot_l", "ball_l"], bal + A(0, -22, 0), blend=6)
    for ang, ln, r in ((-150, 30, 5.0), (-115, 34, 5.6), (-80, 36, 6.0), (-45, 32, 5.2), (-5, 26, 4.6), (40, 24, 4.4), (95, 26, 4.8), (150, 24, 4.4), (195, 26, 4.6)):
        a = radians(ang); o = A(cos(a), sin(a), 0)                  # -90 is straight ahead
        P = spline([ank + A(0, 0, 3) + o * 6, ank + o * ln * 0.45 + A(0, 0, -6.5), ank + o * ln * 0.8 + A(0, 0, -13.5), A(*(ank + o * ln)[:2], 1.4)], 10)
        tv = tube(P, [r * 1.15, r, r * 0.62, 0.3], k=6)
        add(*tv, "Root", wf(tv[0]) if sin(a) < -0.3 else "foot_l", True)


def build_plates():
    # --- the head's face: a tall shell of bark wrapped round the front of the skull
    prof = spline([(0, -19.5, 252), (0, -26.5, 245.5), (0, -30, 235), (0, -29.5, 224.5), (0, -27.5, 214), (0, -26.5, 205)], 200)
    vv = np.linspace(0, 1, 200)
    def C(v): return A(0, np.interp(v, vv, prof[:, 1]), np.interp(v, vv, prof[:, 2]))
    def tan(v): return unit(C(min(v + 0.01, 1)) - C(max(v - 0.01, 0)))
    def out(v): t = tan(v); return A(0, t[2], -t[1])
    rad = lambda v: lerp(15.5, 2.2, min(max(v, 0), 1) ** 1.6)
    def mask(x, v):
        v = min(max(v, 0), 1); r = rad(v); a = x / r
        return C(v) + A(1, 0, 0) * r * sin(a) - out(v) * r * (1 - cos(a))
    hw = lambda v: rad(v) * lerp(1.4, 1.1, v)
    # Not a face. One knot-hole of an eye on its left, a split wandering down the other side, a few rot holes,
    # and nothing that could be a mouth; the fire behind shows through all of them.
    holes = [(6.6, 0.43, 4.4, 2.6, -35), (10.5, 0.3, 1.2, 1.0, 0), (12.0, 0.56, 2.0, 0.9, -50), (3.0, 0.76, 1.4, 1.0, 0), (-10.5, 0.44, 1.6, 1.3, 0),
             (-2.5, 0.2, 4.5, 0.7, 14), (-3.8, 0.38, 4.0, 0.8, -18), (-2.2, 0.55, 3.8, 0.7, 20), (-3.4, 0.7, 3.0, 0.6, -14)]
    add(*web_plate(mask, hw, holes, thick=2.0, nu=16, nv=18), "Bark", "head")
    # bark grown over it in slabs: two lapped down its right side, burying where the other eye would be,
    # a heavy lid sagging over the one eye, and a crooked piece hanging off the jaw
    bark((-9, -19.5, 255), (-0.03, -0.45, -0.89), (-0.55, -0.83, 0.1), 27, 10, "head", bend=0.6, flare=0.02, thick=3.0)
    bark((-10.5, -31.5, 237), (-0.1, 0.1, -1), (-0.6, -0.8, 0), 38, 9, "head", bend=0.6, flare=0.04, thick=3.0)
    bark((2.5, -23.5, 251), (0.78, -0.12, -0.6), (0.25, -0.9, 0.35), 17, 5.0, "head", bend=0.5, flare=0.0, thick=3.0, cracks=0)
    bark((4, -32, 223), (0.38, -0.05, -0.92), (0.4, -0.9, 0), 27, 7, "head", bend=0.5, flare=0.05, thick=2.6)
    # growths: a cluster of burls swelling out of the right temple and cheek, one under the left jaw
    for c, r in (((-13, -22, 241), 6.8), ((-15.5, -18, 231), 5.2), ((-11, -24, 248.5), 4.2), ((-14.5, -19, 222), 4.6), ((-17, -13, 238), 4.0), ((9.5, -27, 213), 4.2)):
        add(*burl(A(*c), r), "Bark", "head")
    # a crown of broken spikes, no two alike, some snapped short
    for x in (-15.5, -10, -4.5, 1.5, 6.5, 11, 16):
        ln = RNG.choice([6.0, 9.0, 14.0, 20.0, 27.0]) * RNG.uniform(0.85, 1.15)
        p = mask(x, 0.04); P = arc_path(p + A(0, 0, -2), unit(A(x * 0.04 + RNG.normal(0, 0.18), 0.5 + RNG.normal(0, 0.2), 1)), (1, 0, 0), ln, RNG.uniform(-0.8, 0.1), 8)
        tp = (1 - np.linspace(0, 1, 8) ** 1.4) if ln > 10 else np.linspace(1, 0.55, 8)        # the short ones are stumps
        add(*tube(P, 3.0 * tp + 0.05, k=5, rb=1.4 * tp + 0.05, hint=(1, 0, 0)), "Bark", "head")
    # splinters off the jaw line, uneven, most of them on the right
    for s, v, ln in ((-1, 0.4, 7), (-1, 0.52, 12), (-1, 0.64, 5), (-1, 0.76, 15), (-1, 0.88, 8), (1, 0.6, 4), (1, 0.82, 9)):
        add(*cone(mask(s * hw(v) * 0.9, v), (s * 0.2 + RNG.normal(0, 0.15), -0.15, -1), ln, 1.4, k=4), "Bark", "head")
    # horns: the left one whole, sweeping forward; the right one snapped off to a jagged stump
    P = arc_path(A(13, -12, 244), unit(A(1, 0.25, 0.45)), (-0.15, 0.3, 1), 66, 2.2, 16, grow=1.1)
    tp = 1 - np.linspace(0, 1, 16) ** 1.7
    add(*tube(P, 6.4 * tp + 0.08, k=7, rb=5.6 * tp + 0.08), "Bark", "head")
    P = arc_path(A(-13, -12, 244), unit(A(-1, 0.3, 0.35)), (0.15, 0.3, -1), 20, 0.5, 6, grow=1.0)
    add(*tube(P, np.linspace(6.6, 5.2, 6), k=7, rb=np.linspace(5.8, 4.4, 6)), "Bark", "head")
    e = unit(P[-1] - P[-2])
    for _ in range(5): add(*cone(P[-1] + RNG.normal(0, 2.2, 3), e + RNG.normal(0, 0.35, 3), RNG.uniform(5, 11), 1.8, k=4), "Bark", "head")
    add(*cone(C(0.97), (0.45, -0.15, -1), 11, 2.4, k=5), "Bark", "head")               # the chin, bent off to one side
    # --- slabs of bark, hung like shingles. Shoulders first: three lapped over each, one down the upper arm
    bark((14, 4, 226), (0.85, 0.1, -0.5), (0.45, -0.1, 0.9), 62, 24, "clavicle_l", True, bend=0.55, holes=3)
    bark((22, -15, 218), (0.75, -0.15, -0.65), (0.3, -0.85, 0.45), 52, 18, "clavicle_l", True, bend=0.5, holes=2)
    bark((20, 27, 220), (0.75, 0.2, -0.62), (0.25, 0.8, 0.55), 56, 20, "clavicle_l", True, bend=0.5, holes=2)
    bark((50, 6, 204), (0.55, 0.0, -0.83), (0.83, -0.1, 0.55), 54, 18, "upperarm_l", True, bend=0.6, holes=1)
    for b, a, n, L, W in (((34, 4, 214), (0.5, 0.05, 0.86), (0.85, -0.2, -0.5), 58, 13), ((46, 10, 208), (0.55, 0.15, 0.82), (0.8, -0.3, -0.55), 46, 11),
                          ((26, 18, 222), (0.3, 0.45, 0.84), (0.6, 0.5, -0.6), 50, 12)):
        bark(b, a, n, L, W, "clavicle_l", True, bend=0.35, flare=0.1, thick=4.0)
    for z, y, L, W in ((236, 24, 52, 13), (216, 40, 60, 15), (192, 46, 50, 13), (170, 40, 38, 11)):
        bark((0, y, z), (0, 0.72, 0.7), (0, 0.7, -0.72), L, W, "spine_03" if z > 180 else "spine_02", bend=0.25, flare=0.12, thick=4.5)
    # the hump: one great slab down the spine, two beside it, two lower
    bark((0, 30, 232), (0, 0.3, -0.95), (0, 0.95, 0.3), 74, 25, "spine_03", bend=0.5, holes=4, cracks=2)
    bark((25, 28, 222), (0.25, 0.3, -0.92), (0.35, 0.9, 0.25), 62, 20, "spine_03", True, bend=0.5, holes=2)
    bark((14, 33, 182), (0.15, 0.12, -0.98), (0.2, 0.97, 0.12), 54, 18, "spine_02", True, bend=0.45)
    # chest: a slab each side, leaning in over the heart
    bark((14, -22, 212), (-0.1, -0.08, -1), (0.3, -0.94, 0.12), 48, 17, "spine_03", True, bend=0.45, holes=1)
    bark((32, -14, 200), (0.05, -0.05, -1), (0.75, -0.65, 0.1), 50, 15, "spine_03", True, bend=0.5)
    # a skirt of slabs off the waist
    bark((13, -20, 152), (0.08, -0.12, -1), (0.35, -0.93, 0), 46, 14, "pelvis", True, bend=0.4)
    bark((27, 1, 150), (0.22, 0, -1), (1, 0, 0.1), 50, 16, "pelvis", True, bend=0.45, holes=1)
    bark((12, 21, 150), (0.05, 0.15, -1), (0.3, 0.95, 0), 48, 16, "pelvis", True, bend=0.4)
    bark((0, -23, 150), (0, -0.1, -1), (0, -1, 0), 36, 11, "pelvis", bend=0.3)
    # gauntlet: three slabs round each forearm, from above the elbow to the wrist
    el, wr = J["lowerarm_l"], J["hand_l"]; ax = unit(wr - el)
    for o, W, L in ((A(0.8, 0.5, 0.35), 15, 68), (A(-0.1, 1, 0.1), 13, 60), (A(0.3, -0.9, 0.35), 13, 58)):
        o = unit(o - (o @ ax) * ax)
        bark(el - ax * 10 + o * 20.5, ax, o, L, W + 2, "lowerarm_l", True, bend=0.7, flare=0.05, holes=1 if W > 14 else 0)
    out = unit(A(0.8, 0.5, 0.35) - (A(0.8, 0.5, 0.35) @ ax) * ax)
    for t, ln in ((0.02, 30), (0.3, 22), (0.55, 17), (0.78, 13)):         # spikes raked back along the forearm, longest at the elbow
        add(*cone(lerp(el, wr, t) + out * 19, out - ax * 0.75, ln, 4.6, k=5), "Bark", "lowerarm_l", True)
    # thigh and shin
    hip, kne, ank = J["thigh_l"], J["calf_l"], J["foot_l"]; ax = unit(kne - hip)
    for o, W in ((A(0.5, -0.85, 0), 16), (A(1, 0.2, 0), 15)):
        o = unit(o - (o @ ax) * ax)
        bark(hip + ax * 4 + o * 22, ax, o, 60, W, "thigh_l", True, bend=0.65, flare=0.05)
    ax = unit(ank - kne); o = unit(A(0.15, -1, 0) - (A(0.15, -1, 0) @ ax) * ax)
    bark(kne - ax * 9 + o * 17, ax, o, 52, 13, "calf_l", True, bend=0.6, flare=0.04)
    # --- a spike off each knee
    add(*cone(kne + A(1, -17, 2), (0.1, -0.75, 0.9), 24, 5.0, k=5), "Bark", "calf_l", True)


def build_dressing():
    # --- antlers: forking branches off the back of the head and the hump, hung with beads of sap
    for p, d, L, r, bone in (((22, 34, 232), (0.5, 0.6, 1), 34, 4.2, "spine_03"), ((-26, 30, 228), (-0.7, 0.5, 1), 30, 4.0, "spine_03")):
        wd, dr = branch(A(*p), d, L, r, depth=3, drip=0.6)
        add(*wd, "Root", bone)
        if dr: add(*dr, "Amber", bone)
    # --- bracket fungus: shelves on the left shoulder and the hump, sap beading under the rims
    for c, r, bone in (((-14, 44, 214), 10, "spine_03"), ((-24, 42, 204), 7.5, "spine_03"), ((16, 46, 196), 8, "spine_03")):
        c = A(*c); tilt = unit(A(0, 0, 1) + RNG.normal(0, 0.12, 3))
        add(*ball(c, r, r * 0.3, k=10, rings=5, axis=tilt), "Cap", bone)
        add(*tube([c - tilt * r * 0.9, c - tilt * r * 0.1], [r * 0.2, r * 0.42], k=6), "Root", bone)
        for _ in range(3):
            a = RNG.uniform(0, 6.3); q = c + A(cos(a), sin(a), 0) * r * 0.85 - tilt * r * 0.12; L = RNG.uniform(3, 7)
            add(*tube([q, q + A(0, 0, -L)], [0.16, 0.16], k=3), "Amber", bone); add(*ball(q + A(0, 0, -L - 1.3), 0.95, 1.8, k=5, rings=4), "Amber", bone)
    # --- a few turned leaves, on the right shoulder and the hump
    for p, d, bone in (((-30, 34, 226), (-0.6, 0.8, 0.4), "spine_03"), ((12, 40, 230), (0.3, 0.9, 0.5), "spine_03")):
        st, lv = spray(A(*p), unit(A(*d) + RNG.normal(0, 0.25, 3)), RNG.uniform(30, 44), leaves=int(RNG.integers(5, 8)), size=20, width=6.5)
        add(*st, "Root", bone); add(*lv, "Leaf", bone)


build_body(); build_plates(); build_dressing()
for part in PARTS:                                     # everything that rides the head bone alone
    if list(part["w"]) == ["head"]: part["v"] = part["v"] + SLOUCH
ob = finish("SK_IronBark", "M_Iron_")
deliver(ob, "ironbark", target=(0, 6, 142), span=350,
        details=[("head", (-0.5, -1, 0.05), (0, -20, 218), 120), ("head_side", (-1, -0.1, 0), (0, 0, 218), 130),
                 ("arm", (0.8, -1, 0.1), (75, 0, 140), 150), ("leg", (-0.7, -1, 0.1), (-26, 0, 60), 150)],
        pose=[("spine_02", (1, 0, 0), -18), ("head", (1, 0, 0), 12), ("upperarm_l", (0, 1, 0), -40), ("lowerarm_l", (1, 0, 0), -60),
              ("upperarm_r", (1, 0, 0), -55), ("lowerarm_r", (1, 0, 0), -45), ("thigh_l", (1, 0, 0), -40), ("calf_l", (1, 0, 0), 50),
              ("thigh_r", (1, 0, 0), 18), ("calf_r", (1, 0, 0), 20)]
             + [(f + j, (0, 1, 0), -40) for f in ("index", "middle", "ring", "pinky") for j in ("_01_r", "_02_r")])      # a fist, to test the fingers
