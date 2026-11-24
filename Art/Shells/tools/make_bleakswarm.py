# Builds the BleakThorn swarm enemy (Remnant melee) on the swarm skeleton. This script is the model.
#   Blender -b ../base/enemy_swarm_rig.blend -P make_bleakswarm.py            build, save, render the sheet
#   Blender -b ../base/enemy_swarm_rig.blend -P make_bleakswarm.py -- pose    also render a posed skinning check
#   Blender -b ../base/enemy_swarm_rig.blend -P make_bleakswarm.py -- export  also write export/SK_BleakSwarm.fbx
# Out: bleakswarm_claude.blend, renders/bleakswarm_*.png. Reference: reference/bleakswarm_concept.webp,
# reference/bleakswarm_mask_markup.jpg. Shapes, skinning, colour and export live in remnant_kit.py.
#
# The body (trunk, hands, feet, plates) is built on the LEFT and mirrored; each arm and leg is grown separately,
# and the dressing (leaf sprays, thorn arcs) is scattered per side, so the two sides differ.
# Roles: Wood (dark core), Sinew (the twisted strands), Bone (mask, plates, claws), Leaf (crimson),
# Vine (thorn arcs, stems, creepers).
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from remnant_kit import *

setup(seed=11,
      colors={"Wood": (0.028, 0.017, 0.014), "Sinew": (0.095, 0.055, 0.04), "Bone": (0.6, 0.5, 0.39),
              "Leaf": (0.17, 0.012, 0.022), "Vine": (0.13, 0.06, 0.04)},
      rough={"Wood": 0.85, "Sinew": 0.7, "Bone": 0.55, "Leaf": 0.6, "Vine": 0.6},
      vary={"Wood": (0.16, 0.035, 0.10), "Sinew": (0.16, 0.035, 0.11), "Bone": (0.06, 0.035, 0.07),
            "Leaf": (0.2, 0.09, 0.08), "Vine": (0.14, 0.06, 0.08)})
MASK = []       # the mask's surface functions, for the dressing that grows off its rim


# =====================================================================================================
# The body (left side and centre line; mirrored)
# =====================================================================================================
def build_body():
    # --- trunk: a wasp waist under a wide rib cage
    zc = [78, 86, 97, 107, 117, 125, 131]
    ctrl = [(0, -1.2, 78), (0, -1.4, 86), (0, -1.2, 97), (0, -0.4, 107), (0, 0.8, 117), (0, 2.2, 125), (0, 1.0, 131)]
    rx = [10.8, 11.8, 8.2, 10.8, 15.0, 15.8, 7.5]; ry = [8.0, 8.5, 6.4, 8.1, 10.4, 10.0, 6.5]
    cv, sv, th = bundle(ctrl, rx, ry, strands=9, twist=0.45, n=22, k=10, strand=0.27, core=0.88, hint=(1, 0, 0), rough=0.6)
    wt = lambda v: chain_w(v, ["pelvis", "spine_01", "spine_02", "spine_03"], J["neck_01"])
    add(*cv, "Wood", wt(cv[0])); add(*sv, "Sinew", wt(sv[0]))
    hv = tube(spline([(0, 5, 104), (0, 9.5, 114), (0, 11.5, 123), (0, 7, 133)], 9), [7, 11, 12, 6.5], k=10, rb=[3.5, 5.5, 6, 3.5], hint=(1, 0, 0))
    add(*hv, "Wood", chain_w(hv[0], ["spine_02", "spine_03"], J["neck_01"]))
    for s in (1, -1):                                   # two heavy vines over the hump, shoulder to opposite hip
        P = spline([(s * 22, 8, 128), (s * 11, 14.5, 121), (0, 14, 111), (-s * 8, 9, 99), (-s * 10.5, 6, 88)], 14)
        vv = tube(P, [2.2, 2.9, 2.7, 2.2, 1.4], k=6)
        add(*vv, "Sinew", chain_w(vv[0], ["pelvis", "spine_01", "spine_02", "spine_03"], J["neck_01"]))
    # --- neck and the skull knob behind the mask
    ctrl = [(0, 1.0, 129), J["neck_01"], J["head"] + A(0, 0.5, 0)]
    cv, sv, _ = bundle(ctrl, [6.6, 5.2, 5.4], strands=5, twist=0.4, n=8, strand=0.3, core=0.85, hint=(1, 0, 0))
    wn = lambda v: chain_w(v, ["spine_03", "neck_01", "head"], (0, -2, 152), blend=3)
    add(*cv, "Wood", wn(cv[0])); add(*sv, "Sinew", wn(sv[0]))
    ctrl = [(0, 0.5, 136), (0, -1.5, 141), (0, -2.5, 147), (0, -2.0, 153), (0, 0, 158.5)]
    cv, sv, _ = bundle(ctrl, [5.5, 9.6, 11.0, 9.6, 3.0], [6.0, 9.2, 10.4, 9.0, 3.0], strands=8, twist=0.35, n=14, k=10, strand=0.24, core=0.92, hint=(1, 0, 0))
    add(*cv, "Wood", "head"); add(*sv, "Sinew", "head")
    # --- shoulder yoke
    sh = J["upperarm_l"]
    cv, sv, _ = bundle([(6, 2.5, 123), (19, 4.5, 127.5), sh + A(1, 0, 0.5)], [6.5, 6.3, 6.2], strands=5, twist=0.5, n=10, strand=0.36, rough=0.5)
    add(*cv, "Wood", "clavicle_l", True); add(*sv, "Sinew", "clavicle_l", True)
    # --- arm: long, wiry, thorned on the forearm
    el, wr = J["lowerarm_l"], J["hand_l"]
    ctrl = [sh, lerp(sh, el, 0.5) + A(1.2, 1.0, 0), el, lerp(el, wr, 0.3) + A(0.8, 1.2, 0), lerp(el, wr, 0.72), wr]
    wa = lambda v: chain_w(v, ["upperarm_l", "lowerarm_l", "hand_l"], J["middle_01_l"])
    for side in (1, -1):
        cv, sv, th = bundle(ctrl, [6.2, 5.6, 3.3, 5.3, 3.6, 2.5], strands=6, twist=1.0, n=26, thorn=3.4, rough=1.0, creep=2, stubs=3)
        for g, m in ((cv, "Wood"), (sv, "Sinew"), (th, "Vine")): add_side(*g, m, wa(g[0]), side)
        for c, r in ((el + A(1.0, 1.6, 0.5), 2.6), (el + A(-1.0, -1.2, 2.5), 1.9), (lerp(sh, el, 0.28) + A(2.4, 1.2, 0), 2.2), (lerp(el, wr, 0.62) + A(1.2, 1.0, 0), 1.6)):
            g = burl(c + RNG.normal(0, 0.7, 3), r * RNG.uniform(0.8, 1.2)); add_side(*g, "Sinew", wa(g[0]), side)
    # --- hand: a narrow palm, long fingers ending in claws
    kn = (J["index_01_l"] + J["pinky_01_l"]) / 2
    cv, sv, _ = bundle([wr, lerp(wr, kn, 0.55), lerp(wr, kn, 1.1)], [2.6, 3.5, 3.0], [2.2, 1.9, 1.2], strands=5, twist=0.25, n=9, strand=0.5,
                       core=0.8, hint=J["pinky_01_l"] - J["index_01_l"], rough=0.6)
    add(*cv, "Wood", "hand_l", True); add(*sv, "Sinew", "hand_l", True)
    for f, r, claw in (("index", 1.3, 11), ("middle", 1.38, 12.5), ("ring", 1.3, 11.5), ("pinky", 1.12, 9), ("thumb", 1.25, 8)):
        js = [J["%s_0%d_l" % (f, i)] for i in (1, 2, 3) if "%s_0%d_l" % (f, i) in J]
        d = unit(js[-1] - js[-2]); curl = unit(d - unit(js[-2] - js[-3]) if len(js) > 2 else np.cross(d, (0, 0, 1)))
        tip = js[-1] + d * 4.0
        P = spline([lerp(wr, js[0], 0.45)] + js + [tip], 4 * len(js) + 3)
        fv = tube(P, np.linspace(r * 1.15, r * 0.85, len(P)), k=5)
        bones = ["hand_l"] + ["%s_0%d_l" % (f, i + 1) for i in range(len(js))]
        add(*fv, "Sinew", chain_w(fv[0], bones, tip, blend=1.6), True)
        C = [tip - d * 1.0]; dd = d
        for i in range(6): C.append(C[-1] + dd * claw / 6); dd = unit(dd + curl * 0.16)
        add(*tube(np.array(C), r * 0.95 * (1 - np.linspace(0, 1, 7) ** 1.6) + 0.03, k=5, rb=r * 0.7 * (1 - np.linspace(0, 1, 7) ** 1.6) + 0.03, hint=curl),
            "Bone", bones[-1], True)
    # --- leg
    hip, kne, ank = J["thigh_l"], J["calf_l"], J["foot_l"]
    ctrl = [hip + A(0, 0, 1), lerp(hip, kne, 0.4) + A(0.8, -1.4, 0), kne, lerp(kne, ank, 0.3) + A(0.3, 2.6, 0), lerp(kne, ank, 0.75), ank]
    wl = lambda v: chain_w(v, ["thigh_l", "calf_l", "foot_l"], J["ball_l"])
    for side in (1, -1):
        cv, sv, th = bundle(ctrl, [8.3, 7.8, 4.3, 6.4, 3.5, 2.7], strands=7, twist=0.85, n=26, thorn=2.6, rough=1.0, creep=2, stubs=3)
        for g, m in ((cv, "Wood"), (sv, "Sinew"), (th, "Vine")): add_side(*g, m, wl(g[0]), side)
        for c, r in ((kne + A(0.5, -3.0, 0.5), 3.6), (kne + A(2.5, 1.5, -2.5), 2.6), (lerp(hip, kne, 0.3) + A(4.5, -2, 0), 3.2), (lerp(kne, ank, 0.4) + A(-1, 4.2, 0), 2.4)):
            g = burl(c + RNG.normal(0, 0.8, 3), r * RNG.uniform(0.8, 1.2)); add_side(*g, "Sinew", wl(g[0]), side)
    # --- foot: three clawed toes forward, a spur behind
    wf = lambda v: chain_w(v, ["foot_l", "ball_l"], J["ball_l"] + A(0, -14, 0), blend=2.5)
    for dx, ln in ((-4.6, 0.9), (0.2, 1.0), (5.0, 0.92)):
        a = ank + A(0, -0.5, -1.5); m = ank + A(dx * 0.55, -7.5 * ln, -5.2); e = A(ank[0] + dx, ank[1] - 14.5 * ln, 1.3)
        tv = tube(spline([a, m, e], 9), np.linspace(2.6, 1.5, 9), k=6)
        add(*tv, "Sinew", wf(tv[0]), True)
        c0 = e + A(0, 0.8, 0.2); cl = spline([c0, c0 + A(dx * 0.12, -3.5, 1.6), c0 + A(dx * 0.2, -7.5, 0.4), c0 + A(dx * 0.24, -9.5, -1.0)], 7)
        add(*tube(cl, 1.2 * (1 - np.linspace(0, 1, 7) ** 1.5) + 0.03, k=5), "Bone", "ball_l", True)
    heel = spline([ank + A(0, 0.5, -1), ank + A(0, 5.5, -4), ank + A(0, 9.5, -7.6)], 7)
    add(*tube(heel, [2.2, 2.0, 1.7, 1.3, 0.9, 0.5, 0.05], k=5), "Bone", "foot_l", True)


def build_plates():
    # --- the mask is the skull's crown and face in one: ribs of bone that start at the back of the head, sweep
    #     forward over the brow and gather into a hooked beak. Slots between the ribs show the head inside.
    prof = spline([(0, 6.5, 152), (0, 0.5, 159.5), (0, -8, 160.5), (0, -16, 156.5), (0, -22.5, 149), (0, -26.5, 140.5), (0, -27, 132.5)], 200)
    vv = np.linspace(0, 1, 200); LEN = float(np.linalg.norm(np.diff(prof, axis=0), axis=1).sum())
    def C(v): return A(0, np.interp(v, vv, prof[:, 1]), np.interp(v, vv, prof[:, 2]))
    def tan(v): return unit(C(min(v + 0.01, 1)) - C(max(v - 0.01, 0)))
    def out(v): t = tan(v); return A(0, t[2], -t[1])
    rad = lambda v: lerp(12.5, 1.6, min(max(v, 0), 1) ** 1.5)
    def mask(x, v):
        v = min(max(v, 0), 1); r = rad(v); a = x / r                         # x is distance round the shell
        return C(v) + A(1, 0, 0) * r * sin(a) - out(v) * r * (1 - cos(a))
    hw = lambda v: rad(v) * lerp(1.95, 1.2, min(v * 1.3, 1))
    RIB = 0.42                                                               # radians between ribs
    holes = []
    for k in (0.5, 1.5, 2.5, 3.5):
        for s in (1, -1):
            for v0, v1 in ((0.06, 0.3), (0.36, 0.57), (0.63, 0.79)):
                vm = (v0 + v1) / 2; r = rad(vm)
                if k * RIB * r > hw(vm) - 1.5: continue
                eye = k == 0.5 and 0.3 < v0 < 0.6
                holes.append((s * k * RIB * r, vm, LEN * (v1 - v0) / 2 * 0.88, max(0.55, r * (0.2 if eye else 0.14)), 0))
    add(*web_plate(mask, hw, holes, thick=1.5, nu=20, nv=20), "Bone", "head")
    MASK[:] = [mask, hw, C, tan, out, rad, RIB]
    t1 = tan(0.97)                                                           # the hook
    P = arc_path(C(0.95) - out(0.95) * 0.9, t1, (1, 0, 0), 8.5, 0.75, 8)
    add(*tube(P, 1.9 * (1 - np.linspace(0, 1, 8) ** 1.5) + 0.03, k=6, rb=1.5 * (1 - np.linspace(0, 1, 8) ** 1.5) + 0.03, hint=(1, 0, 0)), "Bone", "head")
    for k in range(-4, 5):                                                   # each rib runs on past the rim as a tine
        p = mask(k * RIB * rad(0.03), 0.03); radial = unit(p - (C(0.03) - out(0.03) * rad(0.03)))
        d = unit(-tan(0.0) + radial * 0.35)
        P = arc_path(p - d * 1.5, d, np.cross(d, radial), 9 + 2.2 * abs(k), -0.55, 8)
        add(*tube(P, 1.6 * (1 - np.linspace(0, 1, 8) ** 1.4) + 0.04, k=5, rb=0.85 * (1 - np.linspace(0, 1, 8) ** 1.4) + 0.04, hint=radial), "Bone", "head")
    for s in (1, -1):                                                        # barbs along the lower rim
        for v, ln in ((0.1, 6), (0.24, 5.5), (0.38, 4.6), (0.54, 3.8), (0.7, 3.0)):
            add(*cone(mask(s * hw(v) * 0.96, v), (s * 0.3, 0.8, -0.6), ln, 0.8, k=4), "Bone", "head")
    # the snout: dark matter filling the beak, kept just inside the shell
    vs = (0.4, 0.55, 0.7, 0.82, 0.9); rb = [rad(v) * 0.5 for v in vs]
    sn = tube(spline([C(v) - out(v) * (d + 1.7) for v, d in zip(vs, rb)], 11), [rad(v) * 0.72 for v in vs], k=8, rb=rb, hint=(1, 0, 0))
    add(*sn, "Wood", "head")
    # --- pauldron: a pierced blade lying over the shoulder, its point thrown out and up
    sh = J["upperarm_l"]
    def paul(x, v):
        c = lerp(sh + A(-15, -1.5, 7.5), sh + A(23, 5, 15), v) + A(0, 0, 4.5 * sin(pi * v) - 5 * v * v)
        return c + A(0.25 * x * (v - 0.4), -x, -0.065 * x * x)
    hw = vesica(11.5, 1.0, 0.62)
    add(*web_plate(paul, hw, auto_holes(hw, 38, rows=5), thick=1.1), "Bone", "shoulderplate_l", True)
    def paul2(x, v):                                                               # a smaller one beneath, on the arm
        c = lerp(sh + A(-2, 0, 1), sh + A(15, 4, -17), v) + unit(A(1, 0, 0.5)) * (5.2 + 2.5 * sin(pi * v))
        return c + A(-0.05 * x * x, -x, 0.03 * x * x)
    hw = vesica(7.5, 1.0, 0.7)
    add(*web_plate(paul2, hw, auto_holes(hw, 26, rows=4), thick=0.9), "Bone", "upperarm_l", True)
    # --- breast plate: a pierced wedge over the ribs, pointing at the waist
    def chest(x, v):
        return A(x, -9.6 - 2.4 * sin(pi * min(v * 1.2, 1)) + 2.0 * v * v + 0.05 * x * x, 126 - 27 * v)
    hw = lambda v: 13.0 * max(1 - v, 0) ** 0.8 * (0.75 + 0.25 * min(1, v / 0.15))
    add(*web_plate(chest, hw, auto_holes(hw, 27, rows=3, v0=0.16, v1=0.7, gap=6.2), thick=1.2, nu=8), "Bone", "spine_03")
    # --- rib plates: pierced shards lapping round each flank, from under the arm down toward the navel
    for top, bot, a0, a1, W in ((121, 101, 118, 28, 6.2), (110, 91, 125, 40, 5.4)):
        def rib(x, v, top=top, bot=bot, a0=a0, a1=a1):
            z = lerp(top, bot, v) + 0.85 * x; a = radians(lerp(a0, a1, v) + 1.2 * x)
            rx = np.interp(z, [86, 97, 107, 117, 125], [11.8, 8.2, 10.8, 15.0, 15.8]) + 2.6 + 2.0 * sin(pi * v)
            ry = np.interp(z, [86, 97, 107, 117, 125], [8.5, 6.4, 8.1, 10.4, 10.0]) + 2.6 + 2.0 * sin(pi * v)
            return A(rx * sin(a), -ry * cos(a), z)
        hw = vesica(W, 1.0, 0.75)
        add(*web_plate(rib, hw, auto_holes(hw, 30, rows=2, v0=0.3, v1=0.7, gap=6.0), thick=1.1, nv=12), "Bone", "spine_02" if top > 115 else "spine_01", True)
    # --- hip plate over the groin
    def hipp(x, v):
        return A(x, -9.2 - 1.5 * sin(pi * v) + 0.03 * x * x + 3 * v * v, 93 - 20 * v)
    hw = lambda v: 8.5 * max(1 - v, 0) ** 0.9 * (0.7 + 0.3 * min(1, v / 0.2))
    add(*web_plate(hipp, hw, auto_holes(hw, 20, rows=3, v0=0.15, v1=0.6), thick=0.9), "Bone", "pelvis")
    # --- thigh and shin guards
    hip, kne, ank = J["thigh_l"], J["calf_l"], J["foot_l"]
    def thigh(x, v):
        c = lerp(hip + A(6.6, -3.5, 7), kne + A(5.1, -4, 6), v) + A(1.2, -1.2, 0) * sin(pi * v)
        o = unit(A(1, -0.9, 0)); side = unit(np.cross(o, (0, 0, 1)))
        return c + side * x - o * 0.045 * x * x
    hw = vesica(7.4, 1.1, 0.62)
    add(*web_plate(thigh, hw, auto_holes(hw, 33, rows=4), thick=0.9), "Bone", "thigh_l", True)
    add(*cone(kne + A(0.5, -6.0, 1), (0.1, -0.75, 1), 10.5, 2.0, k=5), "Bone", "kneecap_l", True)            # knee spike
    # --- elbow blades: three bone thorns raked back off the forearm
    el, wr = J["lowerarm_l"], J["hand_l"]; ax = unit(wr - el)
    for t, ln in ((0.02, 13), (0.2, 10), (0.38, 7.5)):
        b = lerp(el, wr, t) + A(1.6, 3.6, 0)
        P = arc_path(b, unit(A(0.45, 1, 0.25) - ax * 0.15), (1, -0.4, 0), ln, 0.7, 7)
        add(*tube(P, 1.5 * (1 - np.linspace(0, 1, 7) ** 1.4) + 0.03, k=5, rb=0.8 * (1 - np.linspace(0, 1, 7) ** 1.4) + 0.03, hint=ax), "Bone", "lowerarm_l", True)
    # --- thorns down the spine
    for z, ln, bone in ((127, 9, "spine_03"), (120, 10.5, "spine_03"), (112, 9, "spine_02"), (104, 7, "spine_02"), (96, 5.5, "spine_01"), (88, 6.5, "pelvis")):
        y = np.interp(z, [86, 97, 107, 117, 125], [7, 5.5, 11, 15.5, 16.5])
        add(*cone((0, y - 0.5, z), (0, 1, 0.55), ln * 1.2, 1.8, k=5), "Bone", bone)


def build_dressing():
    """Leaf sprays and thorn arcs. Scattered, not mirrored."""
    def sprays(n, p, d, bone, L=(22, 34), leaves=(6, 9), size=11, jitter=0.35, spread=3.0):
        for _ in range(n):
            st, lv = spray(A(*p) + RNG.normal(0, spread, 3), unit(A(*d) + RNG.normal(0, jitter, 3)), RNG.uniform(*L),
                           leaves=int(RNG.integers(leaves[0], leaves[1] + 1)), size=size * RNG.uniform(0.85, 1.15), width=size * 0.2)
            add(*st, "Vine", bone); add(*lv, "Leaf", bone)
    for s in (1, -1):
        b = "_l" if s > 0 else "_r"
        sprays(3, (s * 30, 8, 128), (s * 0.7, 0.8, 0.5), "clavicle" + b, L=(22, 36), leaves=(5, 8), size=14, jitter=0.45)
        sprays(2, (s * 13, 15, 121), (s * 0.3, 1, 0.1), "spine_03", L=(24, 34), leaves=(5, 7), size=12)
        sprays(1, (s * 46, 9, 98), (s * 0.5, 1, 0.3), "lowerarm" + b, L=(16, 22), leaves=(4, 6), size=9, spread=1.0)
        sprays(2, (s * 12, 0, 92), (s * 0.8, 0.2, -1), "pelvis", L=(14, 20), leaves=(4, 6), size=10, spread=2.0)
        sprays(1, (s * 15, 4, 40), (s * 0.4, 1, 0.2), "calf" + b, L=(12, 16), leaves=(3, 5), size=8, spread=1.0)
    # tendrils grow from the mask itself: off the tines at the back of the skull and along its lower rim
    mask, hw, C, tan, out, rad, RIB = MASK
    def rim(k):
        p = mask(k * RIB * rad(0.02), 0.02); radial = unit(p - (C(0.02) - out(0.02) * rad(0.02)))
        return p, unit(-tan(0.0) + radial * 0.6)
    for k in (-4.4, -3.5, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5, 3.5, 4.4):
        p, d = rim(k)
        sprays(1, p, d + A(0, 0.25, -0.15), "head", L=(22, 36), leaves=(5, 8), size=12.5, spread=0.4, jitter=0.22)
    for k, L, bend in ((-3, 50, 1.9), (-1, 62, 2.3), (1, 56, 2.1), (3, 48, 1.8)):
        p, d = rim(k); d = unit(d + A(0, 0.3, 0.35))
        add(*thorn_arc(p, d, np.cross(d, (np.sign(k) * 0.25, 0.3, -1)), L, 1.25, bend, n=18), "Vine", "head")
    for s in (1, -1):
        for v, dz in ((0.2, -0.1), (0.36, -0.35), (0.52, -0.6)):
            sprays(1, mask(s * hw(v) * 0.94, v), (s * 0.55, 1, dz), "head", L=(20, 30), leaves=(5, 7), size=11.5, spread=0.4, jitter=0.2)
        p = mask(s * hw(0.28) * 0.92, 0.28); d = unit(A(s * 0.6, 1, 0.1))
        add(*thorn_arc(p, d, np.cross(d, (s * 0.3, 0.2, -1)), 42, 1.1, 1.7, n=16), "Vine", "head")
    sprays(3, (0, -10, 88), (0, -0.5, -1), "pelvis", L=(14, 22), leaves=(4, 6), size=10.5, spread=3.0, jitter=0.4)   # loin fall
    sprays(3, (0, 10, 90), (0, 0.6, -1), "pelvis", L=(16, 24), leaves=(4, 6), size=10.5, spread=3.0, jitter=0.4)
    # thorn arcs: thrown up from the back and shoulders, curling forward over the head
    arcs = [((7, 13, 124), (0.25, 0.75, 1), (1, 0.1, 0), 105, 2.3, 3.3, "spine_03"),
            ((-9, 13, 122), (-0.35, 0.8, 1), (1, -0.25, 0.1), 88, 2.0, 3.0, "spine_03"),
            ((26, 8, 128), (0.9, 0.5, 0.9), (-0.5, 0.2, 0.85), 70, 1.2, 2.6, "clavicle_l"),
            ((-27, 8, 128), (-0.8, 0.6, 1), (-0.7, 0.3, -0.7), 82, 1.3, 2.8, "clavicle_r"),
            ((-6, 11, 112), (-0.7, 1, 0.3), (0.1, 0.5, 1), 55, 1.3, 1.5, "spine_02"),
            ((6, 9, 106), (0.7, 1, 0.1), (0.2, -0.6, 1), 50, 1.2, -1.6, "spine_02")]
    for p, d, ax, L, r, bend, bone in arcs:
        add(*thorn_arc(A(*p), d, ax, L, r, bend, n=22), "Vine", bone)


build_body(); build_plates(); build_dressing()
ob = finish("SK_BleakSwarm", "M_Bleak_")
deliver(ob, "bleakswarm", target=(0, 2, 92), span=215,
        details=[("head", (-0.5, -1, 0.05), (0, 0, 138), 80), ("head_side", (-1, -0.1, 0), (0, 0, 138), 80),
                 ("arm", (0.8, -1, 0.1), (45, 0, 85), 95), ("leg", (-0.7, -1, 0.1), (-13, 0, 45), 100)],
        pose=[("spine_02", (1, 0, 0), -28), ("head", (1, 0, 0), 18), ("upperarm_l", (0, 1, 0), -55), ("lowerarm_l", (1, 0, 0), -70),
              ("upperarm_r", (1, 0, 0), -60), ("lowerarm_r", (1, 0, 0), -50), ("thigh_l", (1, 0, 0), -55), ("calf_l", (1, 0, 0), 75),
              ("thigh_r", (1, 0, 0), 25), ("calf_r", (1, 0, 0), 30), ("index_02_r", (1, 0, 0), -40), ("middle_02_r", (1, 0, 0), -40)])
