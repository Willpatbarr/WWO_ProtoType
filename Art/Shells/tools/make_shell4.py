# Shell 4: angular bone-white plate armour on the kitbashed skeleton. The whole body: head, chest, waist, shoulders, upper arms, forearms, legs and feet.
# Reference (Will, 2026-11-23): reference/shell4_concept_front.jpg and shell4_concept_side.jpg (the look), and
# reference/shell4_upper_front.jpg and shell4_chest_side.jpg, painted over the front and side views of
# renders/skeleton_sheet.png (target (0, 0, 100), span 215: 0.1536 cm per px; the paintings are those tiles at
# 0.79 px per px, so 0.194 cm per px: front x = 0 at px 262.4, z = 100 at px 498.4; side y = 0 at px 195.6,
# z = 100 at px 481.3).
#
#   /Applications/Blender.app/Contents/MacOS/Blender -b ../base/robot_frame_v2_rigged.blend -P make_shell4.py
# Writes shell4_claude.blend, renders/shell4_chest_*.png and shell4_shoulder_*.png (six views each) and
# renders/shell4_full_*.png (the whole robot, the views the paintings were made on).
#
# The chest, all rigid to spine_02 (Core). Flat plates, hard edges, each a slab (the ribs alone are curved).
# No rivets: the dots in the paintings were left in by accident (Will, 2026-11-23).
#   chest_collar  the big V over the top of the chest: from a point at the sternum up and back over the shoulders
#   chest_keel    the narrow wedge down the centre under the V's point
#   chest_ribs    three tiers each side, shingled, running down and out from the centre and round the flank: curved plates
#   chest_back    over the shoulder and down the back, ending in hanging blades (not drawn from behind: my design)
#   chest_rim     the dark band round the top and back of each arm hole
# Numbers are centimetres in rig space (robot faces -Y, +X is its left, feet at Z = 0). Left half; the right mirrors it.
import bpy, bmesh, sys, os
import numpy as np
from mathutils import Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K, skeleton_edits
from skeleton_split import classify

OUT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Shell 4's colours, read off the paintings: bone white plates, warm grey-tan ribs, near-black frame.
K.ROLES["Primary"] = dict(color=(0.66, 0.61, 0.53), rough=0.55, metal=0.05)
K.ROLES["Secondary"] = dict(color=(0.33, 0.27, 0.21), rough=0.5, metal=0.15)
K.ROLES["Frame"] = dict(color=(0.035, 0.035, 0.04), rough=0.5, metal=0.4)
K.ROLES["Glow"] = dict(color=(0.0, 0.0, 0.0), rough=0.3, metal=0.0, glow=(0.25, 1.0, 0.85), strength=1.3)

A = lambda *p: np.array(p, float)
m = lambda p: np.array((-p[0], p[1], p[2]))
CENTRE = A(0, 3, 140)            # plates are slabs inward, toward this point


def flat(a, b, c, p, ax=1):
    """p with its coordinate `ax` solved so that it lies in the plane of a, b, c: keeps a four-corner facet flat."""
    n = np.cross(b - a, c - a); p = np.array(p, float)
    p[ax] = a[ax] - sum(n[i] * (p[i] - a[i]) for i in range(3) if i != ax) / n[ax]
    return p


def solid(name, faces, role, bone, slot, inset=0.008, centre=(0.0, 0.03, 1.40), mirror=False, outward=False):
    """Flat facets exactly as given (metres): each face one plane, hard edges, no bevel; each is a slab
    `inset` deep along its own normal toward `centre` (outward: away from it). Open behind. (make_shell2's and 3's helper.)"""
    V, F = [], []
    C = np.array(centre)
    for f in faces:
        P = [np.array(p, float) for p in f]; n0 = len(V); k = len(P)
        nrm = np.zeros(3)
        for p, q in zip(P, P[1:] + P[:1]): nrm += np.cross(p, q)
        nrm /= np.linalg.norm(nrm) + 1e-12
        if (nrm.dot(C - np.mean(P, axis=0)) < 0) != outward: nrm = -nrm
        V += P + [q + nrm * inset for q in P]
        F.append(tuple(range(n0, n0 + k))); F.append(tuple(range(n0 + 2 * k - 1, n0 + k - 1, -1)))
        for i in range(k):
            p, q = n0 + i, n0 + (i + 1) % k
            F.append((p, q, q + k, p + k))
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in V], [], F); me.update()
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.recalc_face_normals(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    return K.register(name, me, role, bone, slot, bevel=0.0, smooth=1.0, mirror=mirror)


def plates(name, faces, role, thick, bone="spine_02"):
    """Left-half facets (cm) and their mirror images as one piece of slabs `thick` cm deep."""
    both = faces + [[m(p) for p in f] for f in faces]
    solid(name, [[p / 100 for p in f] for f in both], role, bone, "core", inset=thick / 100, centre=tuple((CENTRE if bone == "spine_02" else A(0, 0, 107)) / 100))


def through(pts, n):
    """n points along a smooth curve through every one of pts (Catmull-Rom), evenly spread between them."""
    P = [pts[0]] + list(pts) + [pts[-1]]; out = []
    for t in np.linspace(0, len(pts) - 1, n):
        i = min(int(t), len(pts) - 2); u = t - i; a, b, c, d = P[i], P[i + 1], P[i + 2], P[i + 3]
        out.append(0.5 * (2 * b + (c - a) * u + (2 * a - 5 * b + 4 * c - d) * u * u + (3 * b - a - 3 * c + d) * u ** 3))
    return out


RIB_BULGE, RIB_THICK = 0.8, 0.8  # how far the middle of each rib plate swells out between its edges, and its thickness


def curved(name, bands, role, bone="spine_02", slot="core", inside=lambda q: CENTRE, both=True, nu=33):
    """Curved plates (Will, 2026-11-23: the ribs are "supposed to be curved plating"). Each band is a top edge and
    a bottom edge (corner points, cm, left half); the plate runs smoothly through the corners, swells outward
    between its edges, and keeps hard edges all round. inside(point) is a point inside the body near it (cm).
    both: build the mirror image too (pieces on a bone ending _l are mirrored by the kit instead)."""
    bm = bmesh.new(); nv = 9
    for top, bot in bands:
        T, B = through(top, nu), through(bot, nu)
        for side in ((1, -1) if both else (1,)):
            rows = []
            for i in range(nu):
                du = T[min(i + 1, nu - 1)] - T[max(i - 1, 0)] + B[min(i + 1, nu - 1)] - B[max(i - 1, 0)]
                n = np.cross(du, B[i] - T[i]); n /= np.linalg.norm(n)
                if n.dot(inside((T[i] + B[i]) / 2) - (T[i] + B[i]) / 2) > 0: n = -n
                row = [T[i] + (B[i] - T[i]) * v + n * RIB_BULGE * np.sin(np.pi * v) ** 0.8 for v in np.linspace(0, 1, nv)]
                rows.append([bm.verts.new((side * q[0] / 100, q[1] / 100, q[2] / 100)) for q in row])
            for i in range(nu - 1):
                for j in range(nv - 1): bm.faces.new((rows[i][j], rows[i][j + 1], rows[i + 1][j + 1], rows[i + 1][j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    for f in bm.faces:               # each plate is an open sheet: face it away from the body, so it thickens inward
        c = f.calc_center_median() * 100; i = inside(np.array((abs(c.x), c.y, c.z)))       # inside() is given for the left half
        if f.normal.dot(Vector((i[0] if c.x >= 0 else -i[0], i[1], i[2])) - c) > 0: f.normal_flip()
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = K.register(name, me, role, bone, slot, smooth=50.0, mirror=not both)
    t = o.modifiers.new("thick", "SOLIDIFY"); t.thickness = RIB_THICK / 100; t.offset = -1
    return o


def chest():
    log = lambda n, p: K.log("chest corner %s solved to" % n, [round(float(v), 2) for v in p])
    # ---- the collar. Front view: the notch where the halves meet (cA), the V's point (cE), the outer lower
    # corner at the arm hole (cD), the top corner beside the neck (cB), the outer top corner (cC).
    # Side view: the front outline runs from the point (y -20.3, z 144) up and back to (8, 168).
    # It cannot be one flat plate (the arm hole's front edge is 13 cm in front of that plane at cD), so it
    # folds once, along cA-cD: a nearly upright triangle below, a sloping mantle above.
    cA, cE, cD, cB = A(0, -16.8, 150.6), A(0, -20.3, 144.0), A(18.0, -5.5, 156.5), A(9.7, 6.0, 170.0)
    cC = flat(cA, cD, cB, (15.0, 0, 166.6)); log("cC", cC)
    cF = A(17.2, -3.5, 162.0)                        # the arm hole's front edge stands up from cD before it turns back (side view)
    plates("chest_collar", [[cA, cE, cD], [cA, cD, cC, cB], [cD, cF, cC]], "Primary", 1.0)
    # ---- the keel: a ridge down the centre from under the V's point to a tip, its sides sloping back
    kT, kB, eT = A(0, -19.0, 146.0), A(0, -17.2, 130.6), A(4.2, -15.8, 146.5)      # its top is tucked behind the V's point
    eB = flat(kT, kB, eT, (0.7, 0, 131.4)); log("keel eB", eB)
    # its sides turn straight back to the ribs, so it reads as a block from the side and not a hanging tab
    plates("chest_keel", [[kT, kB, eB, eT], [eT, eB, A(eB[0], -12.6, eB[2]), A(eT[0], -12.4, eT[2])]], "Primary", 1.0)
    # ---- the ribs: three layered curved plates a side on the front, and three on the back (Will, 2026-11-23: they "start underneath the light tan
    # armor and wrap around the sides of the body. it shouldn't be a hard turn all of a sudden"). Each lies on a
    # rounded body: at each height an ellipse, RIB_W wide at the flank (the paintings' front outline) and reaching
    # RIB_FRONT at the centre line, centred on y = RIB_Y0. A plate is a band on it between a top and a bottom line,
    # given as (degrees round from the front centre line, height): it starts under the collar or the keel and runs
    # down and round past the flank to the back of the arm hole. Its top edge sits RIB_TUCK nearer the body, so
    # each plate goes under the one above. Front view: tier 1 ends on the line (9.2, 143.8)-(14.8, 135.6), tier 2
    # on (3.9, 137.5)-(12.1, 129.3), tier 3 on the bottom edge (0, 129)-(9.7, 120.5).
    RIB_Y0, RIB_TUCK = 2.0, 0.93
    RIB_W = [(120, 10.9), (128, 12.8), (133, 15.2), (139.5, 17.5), (144, 18.4), (150, 18.8), (156, 18.6)]
    RIB_FRONT = [(120, -11.5), (129, -12.8), (138, -13.5), (148, -13.5), (156, -13.0)]
    # The back's plates (Will, 2026-11-23: "start from the spine and wrap under the front wrapping plates"): the same
    # three tiers from the back's centre line (180 degrees) forward to 105, where they are RIB_UNDER nearer the
    # body so that they slip under the front plates' tails. Their lines are mine: nothing was drawn from behind.
    RIB_UNDER, RIB_BACK = 0.10, 1.5
    under = lambda deg: 1 - RIB_UNDER * float(np.clip((140 - deg) / 30, 0, 1))
    def on_body(deg, z, k):
        w = np.interp(z, *zip(*RIB_W)); d = RIB_Y0 - np.interp(z, *zip(*RIB_FRONT)); t = np.radians(deg)
        if deg > 90: d += RIB_BACK              # the skeleton's chest block reaches further back than the front plates are deep
        return A(w * k * np.sin(t), RIB_Y0 - d * k * np.cos(t), z)
    def tier(top, bot, k=lambda deg: 1.0):
        degs = np.linspace(top[0][0], top[-1][0], 33)
        line = lambda pts: np.interp(degs, *zip(*[(q[0], q[1]) for q in through([A(*q) for q in pts], 200)]))
        return ([on_body(t, z, RIB_TUCK * k(t)) for t, z in zip(degs, line(top))], [on_body(t, z, k(t)) for t, z in zip(degs, line(bot))])
    ribs = [tier([(20, 150.5), (40, 152.5), (62, 155.5), (72, 152.0), (82, 146.5), (100, 145.0), (125, 143.5)],      # up under the collar, then down under the arm
                 [(20, 148.3), (30, 143.8), (65, 135.6), (90, 133.0), (125, 131.5)]),
            tier([(10, 149.0), (30, 145.5), (65, 137.5), (90, 135.0), (125, 133.5)],
                 [(10, 138.6), (13, 137.5), (72, 129.3), (95, 127.5), (125, 126.5)]),
            tier([(0, 143.0), (13, 139.2), (72, 131.0), (95, 129.0), (125, 128.0)],
                 [(0, 129.0), (72, 120.5), (100, 119.5), (125, 119.5)]),
            tier([(105, 144.0), (125, 146.5), (150, 150.5), (180, 152.0)], [(105, 133.5), (150, 138.5), (180, 141.0)], under),
            tier([(105, 135.0), (150, 140.5), (180, 143.0)], [(105, 127.5), (150, 131.0), (180, 133.0)], under),
            tier([(105, 129.0), (150, 133.0), (180, 135.0)], [(105, 122.0), (150, 124.0), (180, 125.5)], under)]
    curved("chest_ribs", ribs, "Secondary")
    # ---- over the shoulder and down the back. Side view: the top peaks at (y 11.6..14.5, z 169), the back runs
    # down through (17.9, 164.5) (22.3, 157.2) (23.7, 149.9), and blades hang to tips at (20.8, 130.4) and (14, 132.4).
    # Nothing was drawn from behind, so the split into plates and blades is mine.
    bB = A(8.5, 13.0, 171.0)
    bC = flat(cB, cC, bB, (16.0, 14.5, 0), ax=2); log("bC", bC)
    n0, k0 = A(0, 14.5, 166.0), A(0, 20.6, 157.5)
    p1 = flat(n0, bB, k0, (9.0, 0, 157.4)); log("p1", p1)
    k1 = flat(bB, bC, p1, (16.5, 0, 157.2)); log("k1", k1)
    l0 = A(0, 21.2, 149.9)
    l1 = flat(k0, p1, l0, (9.5, 0, 149.5)); log("l1", l1)
    l2 = flat(p1, k1, l1, (16.5, 0, 150.5)); log("l2", l2)
    li = l0 + (l1 - l0) * 4.5 / 9.5                  # where the centre blade ends and the side blade starts
    tip_s, tip_c, tip_f = A(11.5, 20.8, 130.4), A(0, 21.8, 137.0), A(16.2, 14.0, 132.4)
    q = flat(li, l2, tip_s, (15.5, 0, 140.0)); log("blade q", q)
    g_t = A(16.3, 13.5, 160.0)                       # the side wall behind the arm hole
    g_b = flat(g_t, k1, l2, (0, 13.0, 146.0), ax=0); log("g_b", g_b)
    back = [[cB, cC, bC, bB],                        # the roof over the shoulder
            [n0, bB, p1, k0], [bB, bC, k1, p1],      # the upper back
            [k0, p1, l1, l0], [p1, k1, l2, l1],      # the band below it
            [l0, li, tip_c], [li, l2, q, tip_s],     # the blades
            [bC, k1, g_t], [g_t, k1, l2, g_b], [g_b, l2, tip_f]]      # the side wall and its blade
    plates("chest_back", back, "Primary", 1.2)
    # ---- the arm hole's rim: a dark band in the plane x = 16.3, over the top of the hole and down its back
    X = 16.3
    outer = [(-0.5, 163.9), (9.0, 164.2), (13.8, 158.0), (14.6, 150.0), (12.6, 143.2)]
    inner = [(0.8, 161.9), (8.0, 162.2), (11.8, 157.3), (12.6, 150.0), (10.7, 143.6)]
    rim = [[A(X, *outer[i]), A(X, *outer[i + 1]), A(X, *inner[i + 1]), A(X, *inner[i])] for i in range(len(outer) - 1)]
    plates("chest_rim", rim, "Frame", 1.0)


# ---- the shoulders (reference/shell4_upper_front.jpg, the two flat pink shapes over each shoulder; front view
# only, so every depth is mine). Two stacked shards over the joint, pointing out and down, riding the upper arm
# as shell 2's and 3's shoulder pieces do (Arms). Left shoulder; the right mirrors it.
#   shoulder_upper  a ridged roof: the painting's quad (15.3, 166.8) (26, 160) (30.3, 151.5) (16, 159.3), with the
#                   brown wedge under its front edge (down to (31.3, 145.5) at the tip) as its front and end faces
#   shoulder_lower  the sliver showing under it, bottom edge (18.5, 152.5)-(28.1, 147.6): a band round the front,
#                   the outer end and the back, coming out from inside the upper shard's wedge
SH_Y = 4.0                       # the shoulder joint's depth: both shards are the same in front of it and behind it


def shoulder():
    back = lambda p: A(p[0], 2 * SH_Y - p[1], p[2])
    a, b, c = A(16.2, SH_Y, 166.8), A(26.0, SH_Y, 161.4), A(30.3, SH_Y, 151.5)       # the ridge, neck end to tip
    d, cf, c2 = A(16.5, -8.0, 159.3), A(30.3, -3.5, 151.5), A(31.3, -3.5, 145.5)     # the front edge, and the wedge's lowest corner
    half = [[a, b, d], [b, cf, d], [b, c, cf], [d, cf, c2]]
    upper = half + [[back(p) for p in f] for f in half] + [[cf, back(cf), back(c2), c2]]
    centre = (0.22, SH_Y / 100, 1.50)
    solid("shoulder_upper", [[p / 100 for p in f] for f in upper], "Primary", "upperarm_l", "arms", inset=0.006, centre=centre, mirror=True)
    l1, l2, l4 = A(18.4, -7.0, 157.0), A(29.8, -2.6, 151.0), A(18.4, -7.0, 152.6)       # its top is inside the upper shard's wedge
    l3 = flat(l1, l2, l4, (28.3, 0, 147.5)); K.log("shoulder corner l3 solved to", [round(float(v), 2) for v in l3])
    lower = [[l1, l2, l3, l4], [back(p) for p in (l1, l2, l3, l4)], [l2, back(l2), back(l3), l3]]
    solid("shoulder_lower", [[p / 100 for p in f] for f in lower], "Primary", "upperarm_l", "arms", inset=0.006, centre=centre, mirror=True)


# ---- the upper arms (reference/shell4_upper_front.jpg: the dark tan plates from under the shoulder to above the
# elbow; Will, 2026-11-23: "the same kind of wrapping plate as the sides of the torso"). Two layered curved plates
# round the arm, open on the side facing the body. The arm is a tube along ARM_A-ARM_B, ARM_RP wide across the
# arm in the front view and ARM_RY deep. A plate is a band on it between a top and a bottom line, given as
# (degrees round the arm: 0 outside, 90 front, -90 back; cm down the arm from the shoulder joint). Front view: the
# painted plates run from under the shoulder to a bottom edge from (24, 139.4) inside to (33.5, 132.6) outside.
# Only the front was painted: the back is the front's mirror image. Left arm; rides the upper arm (Arms).
ARM_A, ARM_B = A(23.2, 4.6, 152.8), A(33.0, 4.6, 124.0)
ARM_RP, ARM_RY, ARM_TUCK = 6.5, 8.0, 0.92


def upper_arm():
    ax = (ARM_B - ARM_A) / np.linalg.norm(ARM_B - ARM_A); out = A(-ax[2], 0, ax[0])
    def on_arm(deg, s, k):
        t = np.radians(deg)
        return ARM_A + ax * s + out * (ARM_RP * k * np.cos(t)) + A(0, -ARM_RY * k * np.sin(t), 0)
    def tier(top, bot):
        degs = np.linspace(top[0][0], top[-1][0], 41)
        line = lambda pts: np.interp(-degs, *zip(*[(-q[0], q[1]) for q in through([A(*q) for q in pts], 200)]))
        return ([on_arm(t, s, ARM_TUCK) for t, s in zip(degs, line(top))], [on_arm(t, s, 1.0) for t, s in zip(degs, line(bot))])
    sym = lambda pts: pts + [(-d, s) for d, s in pts[-2::-1]]         # the back half mirrors the front
    # the top plate starts just below the shoulder shard's outer end (z 145.5), and behind its lower band in front
    bands = [tier(sym([(130, 4.0), (110, 2.5), (90, 3.5), (60, 9.5), (0, 11.5)]), sym([(130, 8.0), (90, 9.5), (0, 18.0)])),
             tier(sym([(130, 6.5), (90, 8.0), (0, 16.5)]), sym([(130, 12.9), (90, 14.5), (0, 22.4)]))]
    nearest = lambda q: ARM_A + ax * float(np.clip((q - ARM_A).dot(ax), 0, 30))
    curved("arm_plates", bands, "Secondary", bone="upperarm_l", slot="arms", inside=nearest, both=False)


# ---- the forearms (reference/shell4_upper_front.jpg: the flat pink leaf shape on each forearm; front view only).
# Will, 2026-11-23, on a first pass that was one blade behind the arm: "overall good shape, but the forearm armor
# should completely cover the sides of the forearms and cover the front and backs to an extent".
# Two angular bone-white shells, one on the outside of the forearm and one on the inside (the side facing the
# body), each covering its side and folding FA_WRAP degrees round onto the front and the back, so a strip of the
# arm shows down the middle of the front and of the back. Their front outline is the painted leaf: FA_OUT and
# FA_IN are how far each side stands from the arm's centre line FA_A-FA_B at (cm down the arm from the elbow).
# The outer shell runs on to a point at the wrist, the inner one ends in a point partway, as painted.
# The painted leaf is narrower than the arm's own joints in places (it was painted behind the arm), so the widths
# are the painting's or the skeleton's plus a clearance, whichever is larger. Depths (FA_DEEP) are mine. Left arm; rides the forearm (Arms).
FA_A, FA_B, FA_Y = A(33.5, 4.0, 127.0), A(46.5, 4.0, 99.0), 4.0
FA_OUT = [(2, 4.6), (5, 5.8), (8, 4.8), (12, 5.2), (15, 4.4), (18.5, 3.6), (24, 3.0), (29, 3.0)]
FA_IN = [(2, 6.8), (5, 7.0), (8, 6.2), (12, 7.0), (15, 7.0), (18.5, 6.6), (21.5, 6.0)]
FA_DEEP = [(2, 6.6), (12, 6.6), (15, 6.0), (18.5, 5.6), (24, 5.4), (29, 5.2)]
FA_WRAP, FA_FOLD, FA_POINT = 75.0, 40.0, 3.5       # where each shell's edges are, where it folds, how much shorter its edges are than its middle


def forearm():
    ax = (FA_B - FA_A) / np.linalg.norm(FA_B - FA_A); out = A(-ax[2], 0, ax[0])
    def shell(name, table, side):
        ss = [q[0] for q in table]; faces = []
        cols = [-FA_WRAP, -FA_FOLD, FA_FOLD, FA_WRAP]
        def pt(i, deg):
            s_ = ss[i]
            if abs(deg) > FA_FOLD + 1:          # the ends are pointed: the edges start later and stop sooner than the middle
                if i == 0: s_ += FA_POINT
                if i == len(ss) - 1: s_ -= FA_POINT
            t = np.radians(deg)
            return FA_A + ax * s_ + out * (side * np.interp(s_, *zip(*table)) * np.cos(t)) + A(0, -np.interp(s_, *zip(*FA_DEEP)) * np.sin(t), 0)
        for i in range(len(ss) - 1):
            for j in range(3): faces.append([pt(i, cols[j]), pt(i, cols[j + 1]), pt(i + 1, cols[j + 1]), pt(i + 1, cols[j])])
        mid = (FA_A + ax * 13) / 100
        solid(name, [[q / 100 for q in f] for f in faces], "Primary", "lowerarm_l", "arms", inset=0.006, centre=tuple(mid), mirror=True, outward=True)
    shell("forearm_outer", FA_OUT, 1)
    shell("forearm_inner", FA_IN, -1)


# ---- the head (reference/shell4_head_views.jpg, Will 2026-11-23: front, side and front 3/4, painted over the
# same three views of renders/skeleton_sheet.png at 0.848 px per px, so 0.1814 cm per px: front x = 0 at px 245.5,
# z = 100 at px 529.8; side y = 0 at px 652.5, z = 100 at px 530.5). All rigid to the head bone (Head):
#   helm_cap     bone white: a grooved strip down the middle from the brow over the top to a tip behind the head,
#                a raised ridge each side of it (the two peaks in the front view), and faceted flanks down to the
#                temples; a band carries on down each cheek beside the lights
#   helm_face    the dark recessed face, with helm_glow, two pairs of upright cyan bars, standing just proud of it
#   helm_jaw     dark: the chin block under the lights and three layered bars each side back to under the ear
#   helm_rear    dark: the back of the skull under the cap, and a fin running back from each ear disc
# The skeleton's ear discs stay bare, as painted. Left half; the right mirrors it.
HELM_C = A(0, -1, 178)


def head():
    def piece(name, left, role, thick=0.5, whole=()):
        faces = left + [[m(q) for q in f] for f in left] + list(whole)
        solid(name, [[q / 100 for q in f] for f in faces], role, "head", "head", inset=thick / 100, centre=tuple(HELM_C / 100))
    # the cap: stations front to back. (y, z) of the middle line, the ridge's crest and the flank's outer edge.
    mid = [(-14.1, 177.9), (-12.0, 182.2), (-8.9, 185.5), (-4.7, 188.4), (0.2, 189.6), (6.2, 189.0), (11.4, 188.2)]
    crest = [A(4.6, -13.2, 178.2), A(4.7, -11.2, 182.8), A(4.8, -8.3, 186.1), A(4.8, -4.4, 189.0), A(4.7, 0.3, 190.1), A(4.2, 6.0, 189.4), A(2.4, 11.2, 188.3)]
    edge = [A(7.6, -9.5, 179.4), A(8.3, -7.0, 180.8), A(8.5, -4.7, 182.2), A(8.2, -2.0, 184.6), A(7.6, 3.2, 186.4), A(5.6, 8.0, 186.2), A(3.0, 11.0, 187.0)]
    GROOVE, HALF, LIFT = 0.2, 1.7, 1.1              # LIFT: the painted top line sits on the skull; this much higher clears it
    mid = [mid[0]] + [(y, z + LIFT) for y, z in mid[1:-1]] + [mid[-1]]
    crest = [crest[0]] + [q + A(0, 0, LIFT) for q in crest[1:-1]] + [crest[-1]]
    left, whole = [], []
    for i in range(len(mid) - 1):
        a0, a1 = A(HALF, mid[i][0], mid[i][1] - GROOVE), A(HALF, mid[i + 1][0], mid[i + 1][1] - GROOVE)
        whole.append([m(a0), a0, a1, m(a1)])                                         # the grooved strip
        b0, b1 = A(HALF + 0.2, mid[i][0], mid[i][1]), A(HALF + 0.2, mid[i + 1][0], mid[i + 1][1])
        left += [[b0, crest[i], crest[i + 1], b1],                                   # the ridge
                 [crest[i], edge[i], edge[i + 1]], [crest[i], edge[i + 1], crest[i + 1]]]      # the flank, two facets
    g_top, g_bot = A(4.4, -13.3, 178.0), A(4.2, -11.9, 169.3)                        # the face's corners beside the lights
    left.append([crest[0], edge[0], A(8.0, -6.5, 176.5), A(4.3, -12.3, 172.0)])       # the cheek band
    piece("helm_cap", left, "Primary", 0.6, whole)
    piece("helm_face", [], "Frame", 0.4, [[m(g_top), g_top, g_bot, m(g_bot)]])
    # the lights: front view, bars 2.5 wide either side of the middle, z 177.6 to 169.6, each two bright lines.
    # Will, 2026-11-23: "thin to a point on their top and bottom points, instead of just being straight up and down bars"
    bars = []
    for x0, x1 in ((0.5, 1.5), (2.0, 3.0)):
        y = lambda z: -13.3 + (178.0 - z) * (1.4 / 8.7) - 0.15
        xc, zt, zb, tp = (x0 + x1) / 2, 177.6, 169.6, 2.6        # Will: each line thins to a point at its top and its bottom
        bars.append([A(xc, y(zt), zt), A(x1, y(zt - tp), zt - tp), A(x1, y(zb + tp), zb + tp), A(xc, y(zb), zb), A(x0, y(zb + tp), zb + tp), A(x0, y(zt - tp), zt - tp)])
    piece("helm_glow", bars, "Glow", 0.2)
    # the jaw: three dark bars a side, layered, each running from the face's edge up and back to under the ear
    # (the 3/4 view; Will, 2026-11-23: "Layer the jaw plates. don't make it much thicker"). Each bar's top edge is
    # tucked JAW_TUCK under the bar above, and the bars are thinner than the single plate they replace, so the
    # jaw's outline is where it was.
    JAW_TUCK = 0.35
    tuck = lambda q: q + (HELM_C - q) / np.linalg.norm(HELM_C - q) * JAW_TUCK
    chin_l, jaw_b, jaw_t = A(3.6, -12.0, 165.4), A(6.2, -4.7, 168.2), A(8.0, -6.5, 176.3)
    bars_ = [[A(4.3, -12.3, 172.0), jaw_t, A(8.35, -3.6, 174.0), A(4.2, -11.9, 169.1)],
             [tuck(A(4.2, -11.9, 169.5)), tuck(A(8.3, -3.8, 174.5)), A(7.7, -3.3, 170.9), A(3.9, -11.95, 167.1)],
             [tuck(A(3.9, -11.95, 167.5)), tuck(A(7.75, -3.35, 171.3)), jaw_b, chin_l]]
    piece("helm_jaw", bars_, "Frame", 0.4, [[m(g_bot), g_bot, chin_l, m(chin_l)]])
    r_t, r_b = A(4.5, 10.5, 185.6), A(4.0, 10.2, 175.5)
    r_s = A(8.0, 6.5, 185.4)                         # the side of the back plate starts clear of the ear disc's mount
    piece("helm_rear", [[r_s, r_t, r_b, flat(r_s, r_t, r_b, (8.0, 0, 175.5))],
                        [A(9.6, 5.2, 180.8), A(6.0, 12.6, 180.4), A(6.5, 11.2, 179.2), A(9.7, 5.6, 179.4)]], "Frame", 0.5, [[m(r_t), r_t, r_b, m(r_b)]])


# ---- the waist (reference/shell4_waist_views.jpg, Will 2026-11-23: front and side, painted over the same views
# of renders/skeleton_sheet.png at 1.686 px per px, so 0.0911 cm per px: front x = 0 at px 193.7, side y = 0 at
# px 1710.7, z = 100 at px 283.2). Bone white, flat plates, all rigid to the pelvis (Core):
#   waist_belt   each side: a hip panel sloping down to the front, a spike out from its top corner, and a panel
#                round the back of the hip; a chevron across the front between the hip panels; a plate across the back
#   waist_front  the long pointed shield hanging in front, ridged down the middle, to a tip between the thighs
#   waist_tail   the shorter blade hanging at the back (side view; I put it on the centre line)
#   waist_shelf  what joins the belt to the body: shelves in from its top and bottom edges
# Left half; the right mirrors it.
def waist():
    log = lambda n, q: K.log("waist corner %s solved to" % n, [round(float(c), 2) for c in q])
    s1, t2, p3 = A(12.4, 0.5, 116.7), A(5.6, -10.8, 113.4), A(4.6, -10.6, 105.6)        # the hip panel: top outer, top inner, bottom inner
    # the hip panel folds once, round the corner of the spine's lowest blocks (a straight panel cut through them)
    kt, p4 = A(10.8, -8.3, 115.7), A(12.0, 0.4, 112.0)
    kb = flat(kt, t2, p3, (10.3, 0, 110.6)); log("kb", kb)
    b1, bc_t, bc_b = A(10.2, 9.0, 117.0), A(0, 11.6, 117.2), A(0, 11.4, 111.5)             # round the back
    b2 = flat(b1, bc_t, bc_b, (9.8, 0, 110.5)); log("b2", b2)
    vt, vb, lb = A(0, -12.2, 111.5), A(0, -12.6, 107.6), A(5.3, -11.0, 109.2)            # the chevron's point, top and bottom
    belt = [[s1, kt, kb, p4], [kt, t2, p3, kb],
            [s1, A(14.8, 1.5, 110.0), p4],                                               # the spike
            [s1, b1, b2, p4], [b2, p4, A(10.6, 6.0, 106.0)],                             # the back of the hip, and its lower point
            [b1, bc_t, bc_b, b2],
            [t2, vt, vb, lb]]
    # Will, 2026-11-23: "it needs to actually have the material to be attached to the hip and waist area. Not just
    # floating". The belt is closed onto the body: a shelf from its top edge in to the spine's lowest blocks, and one
    # from its bottom edge in to the pelvis, so it is a hollow band sitting on the waist and not a ring standing off it.
    tri = lambda a_, b_, c_, d_: [[a_, b_, c_], [a_, c_, d_]]            # a four-corner shelf as two flat facets
    i_s, i_fc, i_fm, i_f0 = A(9.7, 0.5, 116.2), A(9.7, -7.2, 115.2), A(5.4, -7.3, 113.0), A(0, -7.4, 111.3)      # against the body, top
    i_bc, i_b0 = A(9.7, 7.3, 116.5), A(0, 7.5, 116.7)
    j_fc, j_fm, j_f0 = A(10.0, -7.3, 110.3), A(4.6, -7.7, 106.0), A(0, -7.8, 107.4)                              # against the body, bottom
    j_lb, j_bc, j_b0 = A(5.3, -7.6, 108.6), A(9.9, 7.4, 110.4), A(0, 7.6, 111.3)
    shelf = (tri(s1, kt, i_fc, i_s) + tri(kt, t2, i_fm, i_fc) + tri(t2, vt, i_f0, i_fm) + tri(s1, b1, i_bc, i_s) + tri(b1, bc_t, i_b0, i_bc)
             + [[p4, kb, j_fc]] + tri(kb, p3, j_fm, j_fc) + tri(lb, vb, j_f0, j_lb) + [[p4, b2, j_bc]] + tri(b2, bc_b, j_b0, j_bc))
    plates("waist_belt", belt, "Primary", 0.8, bone="pelvis")
    plates("waist_shelf", shelf, "Primary", 0.4, bone="pelvis")
    r0, r1, r2, r3 = A(0, -11.6, 108.2), A(0, -13.3, 104.0), A(0, -12.7, 99.6), A(0, -10.5, 92.8)      # the shield's ridge (side view's front outline)
    w1, w2, w3 = A(4.5, -9.8, 110.8), A(4.4, -11.0, 105.6), A(3.1, -10.8, 99.6)                        # its edge (front view)
    # the shield's sides turn straight back to the pelvis, so it is a block fixed to the front of the hips
    back_to = lambda q, y: A(q[0], y, q[2])
    plates("waist_front", [[r0, w1, w2, r1], [r1, w2, w3, r2], [r2, w3, r3],
                           [w1, w2, back_to(w2, -7.4), back_to(w1, -7.4)], [w2, w3, back_to(w3, -7.4), back_to(w2, -7.4)]], "Primary", 1.0, bone="pelvis")
    e0, e1 = A(3.2, 11.2, 111.0), A(2.4, 11.0, 104.5)
    plates("waist_tail", [[A(0, 12.9, 110.8), e0, e1], [A(0, 12.9, 110.8), e1, A(0, 11.3, 97.8)], [e0, e1, back_to(e1, 7.6), back_to(e0, 7.6)]], "Primary", 0.8, bone="pelvis")


# ---- the legs and feet. No sketch (Will, 2026-11-23: "try to do the legs and feet now"): designed from
# reference/shell4_concept_front.jpg and _side.jpg in the same language as the rest of the shell. Left leg (Legs).
#   thigh_blade               bone white: a long blade on the outer front of the thigh, up to a point at the hip
#   knee_cap, shin_upper, shin_lower   bone white: a knee cap, a flared two-pronged greave, a plate down to the ankle
#   thigh_plates, calf_plates dark tan wrapping plates, two tiers each, round the back, tucked under the shells
#   foot_cap, foot_heel       bone white: a flat-topped cap from the ankle down over the toes; a fin standing on
#                             the back of the sole
# A limb is a tube along a-b. LEG_* tables are how far the skeleton reaches from that line at (cm along it):
# outward, inward (toward the other leg), forward and backward. Armour sits `clear` outside those.
THIGH_A, THIGH_B = A(12.0, 0.0, 103.2), A(13.7, 2.2, 62.9)
THIGH = dict(square=0.55, out=[(4, 3.5), (9, 5.0), (13, 4.6), (17, 4.2), (21, 3.6), (25, 3.0), (29, 2.4), (33, 2.2)],
             inn=[(4, 6.2), (9, 6.2), (13, 5.6), (17, 5.2), (21, 5.7), (25, 5.8), (29, 3.6), (33, 3.0)],
             front=[(4, 7.1), (9, 9.2), (13, 9.9), (17, 8.6), (21, 7.0), (25, 6.4), (29, 5.2), (33, 4.2)],
             back=[(4, 7.1), (9, 8.1), (13, 8.4), (17, 8.5), (21, 7.4), (25, 6.0), (29, 5.0), (33, 4.0)])
CALF_A, CALF_B = A(13.7, 2.2, 62.9), A(15.7, 4.6, 16.6)
CALF = dict(square=0.8, out=[(0, 3.9), (7, 3.6), (13, 4.5), (19, 3.8), (25, 3.4), (33, 3.3), (40, 3.7), (45, 3.7)],
            inn=[(0, 4.0), (7, 3.2), (13, 3.6), (19, 3.3), (25, 3.1), (33, 2.9), (40, 3.4), (45, 3.4)],
            front=[(0, 4.7), (7, 4.6), (13, 4.5), (19, 4.0), (25, 3.2), (33, 2.5), (40, 3.5), (45, 3.5)],
            back=[(0, 7.1), (7, 7.2), (13, 9.6), (19, 9.0), (25, 8.7), (33, 6.4), (40, 4.4), (45, 4.4)])


def limb(a, b, tab, clear):
    """point(deg, s, k): the point `clear` outside the skeleton at `deg` round the limb (0 outside, 90 front, -90 back)
    and s cm along it, scaled by k toward the limb's line; and nearest(point): the nearest point of that line."""
    ax = (b - a) / np.linalg.norm(b - a); out = A(-ax[2], 0, ax[0]); L = float(np.linalg.norm(b - a))
    def point(deg, s_, k=1.0):
        t = np.radians(deg); c, sn = np.cos(t), np.sin(t)
        q_ = tab.get("square", 1.0)           # under 1: a squarer section (the thigh's block has corners)
        c, sn = np.sign(c) * abs(c) ** q_, np.sign(sn) * abs(sn) ** q_
        cl = clear(deg) if callable(clear) else clear
        r1 = np.interp(s_, *zip(*tab["out" if c >= 0 else "inn"])) * 1.08 + cl
        r2 = np.interp(s_, *zip(*tab["front" if sn >= 0 else "back"])) * 1.08 + cl
        return a + ax * s_ + out * (r1 * k * c) + A(0, -r2 * k * sn, 0)
    return point, (lambda q: a + ax * float(np.clip((q - a).dot(ax), 0, L)))


def limb_plates(name, bone, a, b, tab, tiers, clear=2.6, tuck=0.93):
    """Curved wrapping plates round a limb, like the ribs': each tier is (top line, bottom line) as (degrees, cm along)."""
    point, nearest = limb(a, b, tab, clear); bands = []
    for top, bot in tiers:
        degs = np.linspace(top[0][0], top[-1][0], 81)
        line = lambda pts: np.interp(-degs, *zip(*[(-q[0], q[1]) for q in through([A(*q) for q in pts], 200)]))
        bands.append(([point(t, s_, tuck) for t, s_ in zip(degs, line(top))], [point(t, s_) for t, s_ in zip(degs, line(bot))]))
    curved(name, bands, "Secondary", bone=bone, slot="legs", inside=nearest, both=False, nu=73)


def under(centre, half, low=1.9, high=2.6):
    """A limb plate's clearance round the limb: `low` within `half` degrees of `centre` (where the light armour
    lies over it, so the plate hugs the leg and passes underneath), easing to `high` 50 degrees further round."""
    def clear(deg):
        d = abs((deg - centre + 180) % 360 - 180); t = float(np.clip((d - half) / 50, 0, 1))
        return low + (high - low) * t * t * (3 - 2 * t)
    return clear


def legs():
    def blades(name, bone, faces, inside, thick=0.6):
        solid(name, [[q / 100 for q in f] for f in faces], "Primary", bone, "legs", inset=thick / 100, centre=tuple(np.array(inside) / 100), mirror=True, outward=True)
    def leaf(top, ridge, front, side, bottom):
        """A blade folded along a ridge: a wing from the ridge to the `front` edge and one to the `side` edge,
        both closing to a point at `top` and at `bottom`. Every facet is a flat triangle."""
        f = [[top, front[0], ridge[0]], [top, ridge[0], side[0]]]
        for i in range(len(ridge) - 1):
            for e in (front, side): f += [[ridge[i], e[i], e[i + 1]], [ridge[i], e[i + 1], ridge[i + 1]]]
        return f + [[ridge[-1], front[-1], bottom], [ridge[-1], side[-1], bottom]]
    # Will, 2026-11-23, on a first pass of plain shells round the thigh and shin: "shaping is all wrong and thigh
    # armor should go up higher"; on the second, of blades from the concept art: "way better. The thighs need a bit
    # more front coverage and the shins should be one piece. And for the dark tan curved plating on the thighs and
    # the calves it should wrap all the way underneath the light tan armor".
    # the thigh: one long blade folded round the block's front outer corner, from a point above the hip joint
    # (z 113, beside the waist's hip spike) to a point at the outside of the knee. Its front wing reaches across
    # the front of the thigh to the inner corner (his red line on the front view).
    # (height, the corner's x and y, the front wing's inner edge x, the side wing's back edge y)
    TH = [(106, 17.9, -8.4, 12.0, 2.5), (98, 19.7, -11.4, 8.0, 3.0), (92, 20.7, -13.2, 6.5, 4.5), (86, 20.1, -12.2, 6.6, 3.0), (78, 19.1, -9.6, 8.5, 1.0), (70, 17.9, -6.8, 12.5, -1.5)]
    thigh = leaf(A(17.0, -6.2, 113.0), [A(x, y, z) for z, x, y, fx, sy in TH], [A(fx, y + 0.3, z) for z, x, y, fx, sy in TH],
                 [A(x - 0.4, sy, z) for z, x, y, fx, sy in TH], A(19.2, -5.3, 65.5))
    blades("thigh_blade", "thigh_l", thigh, (12.5, 0.5, 88))
    # the knee: a small ridged cap on the front of the knee, riding the shin
    kx = 13.8
    blades("knee_cap", "calf_l", [[A(kx, -7.0, 70.5), A(kx, -7.6, 62.0), A(kx + s_ * 3.3, -5.2, 62.8), A(kx + s_ * 3.0, -4.8, 69.5)] for s_ in (1, -1)], (kx, 1, 66))
    # the shin: one greave, ridged down the front from under the knee to above the ankle, with a wing each side
    # that flares to a spike beside the knee. Will, 2026-11-23: "make it so the shin plate isn't as wide and gets closer
    # to the bone of the shin as it gets closer to the ankle".
    SR = [A(14.2, -7.2, 61.5), A(14.5, -6.4, 48.0), A(15.0, -4.0, 34.0), A(15.5, -0.9, 19.5)]
    shin = []
    for s_ in (1, -1):
        SE = [A(14.2 + s_ * 5.2, -2.6, 65.0), A(14.5 + s_ * 4.6, -3.0, 47.0), A(15.0 + s_ * 3.6, -2.4, 33.0), A(15.5 + s_ * 2.6, 0.0, 20.5)]
        for i in range(3): shin += [[SR[i], SE[i], SE[i + 1]], [SR[i], SE[i + 1], SR[i + 1]]]
    blades("shin", "calf_l", shin, (14.6, 4, 42))
    # the dark plates go right round the limb: they swell out behind and hug the leg in front, where they pass
    # underneath the light armour. Each ring's seam is at the front, under the blade's ridge.
    limb_plates("thigh_plates", "thigh_l", THIGH_A, THIGH_B, THIGH,
                [([(80, 6.0), (-10, 6.0), (-100, 7.0), (-190, 6.0), (-280, 6.0)], [(80, 17.0), (-10, 17.0), (-100, 19.0), (-190, 17.0), (-280, 17.0)]),
                 ([(80, 15.5), (-10, 15.5), (-100, 17.5), (-190, 15.5), (-280, 15.5)], [(80, 27.0), (-10, 27.0), (-100, 30.0), (-190, 27.0), (-280, 27.0)])],
                clear=under(65, 85, low=1.4))
    limb_plates("calf_plates", "calf_l", CALF_A, CALF_B, CALF,
                [([(90, 3.0), (0, 3.0), (-90, 5.0), (-180, 3.0), (-270, 3.0)], [(90, 17.0), (0, 17.0), (-90, 20.0), (-180, 17.0), (-270, 17.0)]),
                 ([(90, 15.5), (0, 15.5), (-90, 18.5), (-180, 15.5), (-270, 15.5)], [(90, 30.0), (0, 30.0), (-90, 34.0), (-180, 30.0), (-270, 30.0)])],
                clear=under(90, 80))
    # the foot: a flat-topped cap from the toe up the instep to the front of the ankle, with a wall each side down
    # to the top of the sole (the sole plate and the toe roller are wider and higher than a simple ridged cap clears)
    X = 15.8; mx = lambda q: A(2 * X - q[0], q[1], q[2]); SOLE = 4.4
    top = [A(X + 2.6, -22.6, 5.6), A(X + 4.4, -12.0, 10.4), A(X + 4.4, -2.0, 13.0)]       # the top's edge, toe to ankle
    low = [A(X + 5.6, -21.8, SOLE), A(X + 5.9, -12.0, SOLE), A(X + 5.6, -1.0, SOLE + 2.0)]
    cap = [[mx(top[0]), top[0], low[0], mx(low[0])]]                                      # the toe's front
    for i in range(2):
        cap += [[mx(top[i]), top[i], top[i + 1], mx(top[i + 1])], [top[i], low[i], low[i + 1], top[i + 1]], [mx(q) for q in (top[i], low[i], low[i + 1], top[i + 1])]]
    solid("foot_cap", [[q / 100 for q in f] for f in cap], "Primary", "foot_l", "legs", inset=0.005, centre=(X / 100, -0.08, 0.03), mirror=True, outward=True)
    # the heel: a fin standing on the back of the sole behind the ankle block (the concept's side view)
    solid("foot_heel", [[q / 100 for q in f] for f in [[A(X + 0.9, 10.0, 13.5), A(X + 0.9, 15.0, SOLE), A(X + 0.9, 10.0, SOLE)]]], "Primary", "foot_l", "legs", inset=0.018, centre=(X / 100, 0.12, 0.07), mirror=True)


# the skeleton alone, to see the armour on and to check nothing of it pokes through
body = bpy.data.objects["SK_RobotFrame"]
part = classify(body); skeleton_edits.apply(body, part=part)
bm = bmesh.new(); bm.from_mesh(body.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if part[f.index] != "Skeleton"], context="FACES")
skel = bpy.data.objects.new("SkeletonOnly", bpy.data.meshes.new("SkeletonOnly"))
for mt in body.data.materials: skel.data.materials.append(mt)
bm.to_mesh(skel.data); bm.free()
bpy.context.scene.collection.objects.link(skel); skel.matrix_world = body.matrix_world

chest()
shoulder()
upper_arm()
forearm()
head()
waist()
legs()
parts = K.finish("Shell4", base_hands=False)


def touch_check(o, what):
    """Where the skeleton cuts through a built slot's mesh."""
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()
    hits = BVHTree.FromObject(o, dg).overlap(BVHTree.FromObject(skel, dg))
    cs = [skel.data.polygons[j].center for _, j in hits]
    if not cs: K.log("skeleton through the %s: none" % what); return
    K.log("skeleton through the %s: %d faces, x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f" % ((what, len(cs)) + tuple(f(c[i] for c in cs) for i in range(3) for f in (min, max))))
    for c in cs[:: max(1, len(cs) // 12)]: K.log("   at", [round(v, 1) for v in c])


touch_check(parts["core"], "chest")
touch_check(parts["arms"], "arm armour")
touch_check(parts["head"], "helmet")
touch_check(parts["legs"], "leg armour")
if os.environ.get("WWO_CHECK_ONLY"): sys.exit(0)      # corners and the skeleton check only: nothing saved or rendered
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "shell4_claude.blend"))

cam, lights = K.studio((1000, 1000))
R = OUT + "/renders/shell4_chest_"
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (0.7, -1, 0.5), "low_q": (0.7, -1, -0.35), "back": (0, 1, 0), "back_q": (0.7, 1, 0.25)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 145), span=70)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell4_shoulder_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(26.0, 3.5, 147.0), span=60)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell4_head_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, -1, 177), span=40)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell4_waist_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 106), span=50)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
for part_, tg, sp in (("thigh", (13.0, 1.0, 84.0), 56), ("calf", (14.5, 3.5, 42.0), 60), ("foot", (15.8, -4.0, 10.0), 44)):
    R = OUT + "/renders/shell4_%s_" % part_
    for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=tg, span=sp)
    K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell4_forearm_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(39.5, 3.5, 113.0), span=50)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
cam, lights = K.studio()
R = OUT + "/renders/shell4_full_"
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (-0.62, -1, 0.12), "back": (0, 1, 0)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 100), span=215)
K.sheet([R + n + ".png" for n in v], 4, R + "sheet.png")
