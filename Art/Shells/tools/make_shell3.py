# Shell 3: a new robot built from scratch on the skeleton. So far: the head's mask, the chest and the forearms.
# Reference: reference/mask_sketch.jpg (Will, 2026-11-20), drawn over renders/skeleton_head_sheet.png
# (front and side views: target (0, -1, 175), span 40 cm; the sketch is that sheet at 0.563 px per px).
#
#   /Applications/Blender.app/Contents/MacOS/Blender -b ../base/robot_frame_v2_rigged.blend -P make_shell3.py
# Writes shell3_claude.blend and renders/shell3_head_*.png (the mask on the skeleton, same views as the sheet).
#
# The mask: a flat oval face with an LED ring in it, and one curved piece round it that swells out from the
# oval's edge, wraps back to the jaw line at the sides and sweeps up over the head to a tip behind it.
# (The sketch draws a line across the forehead; Will, 2026-11-20: "My drawing was wrong ... It should all
# look like one piece", so there is no brow edge and no separate roof.)
# Numbers below are centimetres in rig space (robot faces -Y, +X is its left, feet at Z = 0).
import bpy, bmesh, sys, os
import numpy as np
from mathutils import Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K, skeleton_edits
from skeleton_split import classify

OUT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Shell 3's colours (Will, 2026-11-20): the ring glows a warm yellow-orange; the mask is a warm off-white / light tan.
K.ROLES["Glow"] = dict(color=(0.0, 0.0, 0.0), rough=0.3, metal=0.0, glow=(1.0, 0.33, 0.0), strength=1.3)
K.ROLES["Primary"] = dict(color=(0.74, 0.57, 0.36), rough=0.5, metal=0.1)
# the chest's under-armour (Will, 2026-11-20): "a very dark charcoal/yellow grey"
K.ROLES["Frame"] = dict(color=(0.040, 0.036, 0.024), rough=0.55, metal=0.2)
# the flat oval's face: a darker, warmer shade of the mask's colour
K.ROLES["Secondary"] = dict(color=(0.30, 0.19, 0.09), rough=0.45, metal=0.1)

# ---- the flat oval (front view: centre and radii; side view: its plane, leaning 2 degrees) ----
OVAL_Z, OVAL_RX, OVAL_RY = 177.7, 5.34, 6.1
def oval_y(z): return -13.65 + (z - OVAL_Z) * 0.0336
RING = (0.805, 0.895)            # the LED ring, as fractions of the oval's radii: 0.5 cm wide
# ---- the outline, robot's left half: from the tip behind the head, down the roof's edge to the corner
# in front of the ear disc (K), then down the jaw line to the chin's centre ----
TIP = (0.0, 11.9, 188.5)
K_CORNER = (9.0, -0.1, 181.7)
JAW = [(9.0, -2.8, 179.5), (9.0, -2.6, 177.6), (7.1, -6.1, 173.2), (4.1, -7.2, 165.7), (2.2, -9.5, 164.3), (0.0, -11.1, 163.6)]
# ---- the curve from the oval's edge to the outline, as fractions of the straight distance between them.
# Fitted to the side view's top line: brow corner (-13.4, 185.4), (-5.3, 188.7), (5, 189.9), tip.
LEAVE = 0.155                    # it leaves the oval in the oval's own plane for this far
SWELL_AT, SWELL = 0.57, 0.18     # then swells outward by this much, this far along
THICK = 0.5                      # not drawn except at the tip: the mask is seen from behind too

HEAD_C = Vector((0, -2, 177))
# How much narrower than first built the chest's top layer is, cm per side (Will picked 2 from
# renders/options/chest_slim_sheet.png; the under-armour is left as it is and shows past the loops).
# The chest's depth is skeleton_edits.CHEST_DEPTH, because the skeleton's trim has to follow it.
# WWO_TRIAL=<name> in the environment (with WWO_CHEST_SLIM / WWO_CHEST_DEPTH = cm) builds a trial instead:
# it only writes renders/options/<name>_*.png and saves nothing.
CHEST_SLIM = float(os.environ.get("WWO_CHEST_SLIM", 2.0))
CHEST_TAPER = 0.2
TRIAL = os.environ.get("WWO_TRIAL")
cy = lambda p: np.array((p[0], skeleton_edits.chest_y(p[1]), p[2]))


def surface(name, grid, role, thick=0.0, closed=False):
    """A smooth sheet through a grid of points (cm), facing away from the head, `thick` cm thick inward."""
    bm = bmesh.new()
    vs = [[bm.verts.new((x / 100, y / 100, z / 100)) for x, y, z in row] for row in grid]
    n = len(vs)
    for i in range(n if closed else n - 1):
        a, b = vs[i], vs[(i + 1) % n]
        for j in range(len(a) - 1):
            try: bm.faces.new((a[j], a[j + 1], b[j + 1], b[j]))
            except ValueError: pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    K.log('soft', name, 'cage verts', len(bm.verts), 'min', [round(min(v.co[i] for v in bm.verts)*100,1) for i in range(3)], 'max', [round(max(v.co[i] for v in bm.verts)*100,1) for i in range(3)])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if sum(f.normal.dot(f.calc_center_median() * 100 - HEAD_C) * f.calc_area() for f in bm.faces) < 0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = K.register(name, me, role, "head", "head", smooth=50.0, mirror=False)
    if thick:
        m = o.modifiers.new("thick", "SOLIDIFY"); m.thickness = thick / 100; m.offset = -1
    return o


def edge():
    """The outline's points, left half, top to bottom."""
    k = np.array(K_CORNER); t = np.array(TIP)
    roof = [(k[0] * np.sqrt(1 - q * q), k[1] + (t[1] - k[1]) * q, k[2] + (t[2] - k[2]) * q) for q in np.linspace(1, 0, 25)]
    return np.array(roof + JAW)


def outline(theta, pts):
    """The outline's point at angle theta (degrees round the oval's centre in the front view, 90 = top)."""
    ang = np.degrees(np.arctan2(pts[:, 2] - OVAL_Z, pts[:, 0]))
    return np.array([np.interp(-theta, -ang, pts[:, k]) for k in range(3)])


def head():
    # the flat oval: centre, ring, rim
    n = 72
    def loop(k):
        return [(k * OVAL_RX * np.cos(a), oval_y(OVAL_Z + k * OVAL_RY * np.sin(a)), OVAL_Z + k * OVAL_RY * np.sin(a))
                for a in np.linspace(0, 2 * np.pi, n, endpoint=False)]
    for nm, k0, k1, role in (("mask_face", 0.0, RING[0], "Secondary"), ("mask_ring", RING[0], RING[1], "Glow"), ("mask_face_rim", RING[1], 1.0, "Secondary")):
        a, b = loop(k0), loop(k1)
        surface(nm, [[a[i], b[i]] for i in range(n)], role, closed=True)
    # the one curved piece: from the oval's edge out and back to the outline
    pts = edge()
    corners = np.degrees(np.arctan2(pts[25:, 2] - OVAL_Z, pts[25:, 0]))
    half = sorted(set(np.round(np.concatenate([np.linspace(90, -90, 73), corners]), 3)), reverse=True)
    grid = []
    for side, thetas in ((1, half), (-1, half[-2:0:-1])):
        for th in thetas:
            a = np.radians(th)
            z0 = OVAL_Z + OVAL_RY * np.sin(a)
            out = np.array([np.cos(a), 0.0336 * np.sin(a), np.sin(a)])
            p0 = np.array([OVAL_RX * np.cos(a), oval_y(z0), z0]); p3 = outline(th, pts)
            L = np.linalg.norm(p3 - p0)
            p1 = p0 + out * LEAVE * L
            p2 = p0 + (p3 - p0) * SWELL_AT + out * SWELL * L
            row = [(1 - u) ** 3 * p0 + 3 * u * (1 - u) ** 2 * p1 + 3 * u * u * (1 - u) * p2 + u ** 3 * p3 for u in np.linspace(0, 1, 22)]
            grid.append([(side * p[0], p[1], p[2]) for p in row])
    surface("mask_shell", grid, "Primary", THICK, closed=True)


def solid(name, faces, role, bone, slot, inset=0.008, centre=(0.0, -0.04, 1.68)):
    """Flat facets exactly as given (metres): each face one plane, hard edges, no bevel; each is a slab
    `inset` deep along its own normal toward `centre`. Open behind. (Same helper as make_shell2's.)"""
    V, F = [], []
    C = np.array(centre)
    for f in faces:
        P = [np.array(p, float) for p in f]; n0 = len(V); k = len(P)
        nrm = np.zeros(3)
        for p, q in zip(P, P[1:] + P[:1]): nrm += np.cross(p, q)
        nrm /= np.linalg.norm(nrm) + 1e-12
        if nrm.dot(C - np.mean(P, axis=0)) < 0: nrm = -nrm
        V += P + [q + nrm * inset for q in P]
        F.append(tuple(range(n0, n0 + k))); F.append(tuple(range(n0 + 2 * k - 1, n0 + k - 1, -1)))
        for i in range(k):
            p, q = n0 + i, n0 + (i + 1) % k
            F.append((p, q, q + k, p + k))
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in V], [], F); me.update()
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.recalc_face_normals(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    return K.register(name, me, role, bone, slot, bevel=0.0, smooth=1.0, mirror=False)


# ---- the chest, first layer (reference/chest_sketch_1.jpg, Will 2026-11-20): flat facets, one per shade,
# drawn on the front view of renders/skeleton_chest_sheet.png (target (0, 0, 138), span 70; the sketch is
# 19.35 px per cm, x = 0 at px 415, z = 153.6 at px 381.5). Only the front was drawn, so every depth (Y)
# here is a choice; the skeleton's chest block is trimmed behind the plates (skeleton_edits.trim_chest). Robot's left half; the right mirrors it.
CAM_D = 204.2                    # the sheet's camera distance: a point nearer than the target is drawn larger
def drawn(xd, zd, y):
    """The rig point that the front view draws at (xd, zd) when it is at depth y."""
    k = (CAM_D + y) / CAM_D
    return np.array((xd * k, y, 138 + (zd - 138) * k))
def on_plane(a, b, c, xd, zd):
    """The point drawn at (xd, zd) that lies in the plane of a, b, c: keeps a four-corner facet flat."""
    n = np.cross(b - a, c - a); y = a[1]
    for _ in range(20):
        p = drawn(xd, zd, y); y = a[1] - (n[0] * (p[0] - a[0]) + n[2] * (p[2] - a[2])) / n[1]
    return drawn(xd, zd, y)
CHEST_W = 15.66                  # drawn half width
MID_Y = 0.0                      # "the halfway line of the skeleton": the spine's centre line in the side view


def chest():
    W = CHEST_W
    r0 = drawn(0, 157.55, -16.2)             # the centre ridge, top to bottom. Will's second mark-up: r0..r2 forward, 3 cm at the top, 1 cm at the bottom
    r1 = drawn(0, 148.0, -15.9)             # r1, r2, bot: moved back 2.5 cm (Will's red line, 2026-11-20)
    r2 = drawn(0, 141.9, -15.7)
    bot = drawn(0, 129.75, -10.0)            # the bottom point: back 3 cm (second mark-up)
    o0 = drawn(W, 156.9, -2.1)               # the outer edge: moved back to the arm opening (Will's yellow line)
    pd = drawn(W, 161.4, -0.5)
    pa = drawn(W, 162.4, 3.5)
    # third mark-up (side view): the corners beside the neck go back 4 cm and the outer edge follows the
    # drawn arm hole: (y, z) = (4.7, 164) (0, 161.9) (-2.1, 157.2) (-2.3, 152) (-1.1, 148.3) (0.3, 146.2)
    pc = drawn(3.9, 162.4, -10.0)            # the corner beside the neck
    pe = on_plane(r0, o0, pc, W, 158.7)
    global CHEST_UNDER; CHEST_UNDER = (pe, pc, pd)     # the facet the top layer's neckline rests on
    f2 = on_plane(pe, pc, pd, 8.5, 163.65)
    pb = on_plane(pd, f2, pa, 9.05, 164.3)   # the shoulder's peak
    o1 = on_plane(r0, o0, r1, W, 152.6)
    o2 = on_plane(o1, r1, r2, W, 149.4)
    a = on_plane(o2, r2, bot, 15.25, 146.3)
    c = drawn(9.3, 129.75, MID_Y)            # the darkest facet's back corner, at the halfway line
    for n, p in (("pe", pe), ("f2", f2), ("pb", pb), ("o1", o1), ("o2", o2), ("a", a)):
        K.log("chest corner %s solved to" % n, [round(float(v), 2) for v in p])
    half = [[pd, f2, pb, pa],                # the top of the shoulder (the white sliver)
            [pd, pe, pc, f2],
            [pe, o0, r0, pc],
            [o0, o1, r1, r0],
            [o1, o2, r2, r1],
            [o2, a, bot, r2],
            [a, c, bot]]                     # the darkest: back to the halfway line
    # ---- the back half (Will, 2026-11-20: "complete the back half similarly", with "a flat surface at the
    # middle instead of focusing to a point"; it is under-armour, so it sits close to the skeleton).
    # Not drawn: every corner here is a choice. One flat centre panel leaning 8 degrees, facets from its edges
    # round to the arm holes, and a facet under each arm joining the front's lowest facet to the back's.
    panel = lambda x, z: np.array((x, 16.8 - (156 - z) * 0.1417, z))
    def plane_y(p, q, r, x, z):
        n = np.cross(q - p, r - p); return np.array((x, p[1] - (n[0] * (x - p[0]) + n[2] * (z - p[2])) / n[1], z))
    pt, pm, pbm = panel(5.0, 157), panel(7.5, 147), panel(4.5, 133)      # the panel's left edge, top to bottom
    nk = np.array((4.0, 10.5, 165.0))            # the corner behind the neck
    qe = np.array((15.6, 9.5, 160.5))            # the arm hole's back edge, top to bottom
    q0 = np.array((15.55, 10.5, 156.0))
    qm = plane_y(pt, pm, q0, 15.5, 150.5)
    qa = plane_y(pm, pbm, qm, 15.2, 146.3)
    back = [[pb, nk, pa], [pa, nk, qe],          # the top of the shoulder, behind the peak
            [nk, pt, qe], [pt, q0, qe],          # the back of the shoulder
            [pt, pm, qm, q0], [pm, pbm, qa, qm], # the panel's edge round to the arm hole
            [pbm, c, qa],                        # the back's lowest facet, to the same halfway corner as the front's
            [a, c, qa]]                          # under the arm
    for f in half[1:] + back:                    # for skeleton_edits.CHEST_TRIM (as drawn, before CHEST_DEPTH): each plate's outward normal, a point, a floor
        n = np.cross(f[1] - f[0], f[2] - f[0]); n /= np.linalg.norm(n)
        if n.dot(np.array((0, 2, 148.0)) - f[0]) > 0: n = -n
        K.log("chest plane ((%.3f, %.3f, %.3f), (%.2f, %.2f, %.2f), %s)," % (*n, *f[0], 150 if min(p[2] for p in f) > 150 else 129.5))
    m = lambda p: np.array((-p[0], p[1], p[2]))
    half += back
    faces = half + [[m(p) for p in f] for f in half] + [[pc, r0, m(pc)]]
    faces += [[pt, pm, pbm, m(pbm), m(pm), m(pt)], [nk, pt, m(pt), m(nk)]]     # the collar is above; the back's flat panel and the facet above it
    faces = [[cy(p) for p in f] for f in faces]
    solid("chest_front", [[p / 100 for p in f] for f in faces], "Frame", "spine_02", "core", inset=0.006, centre=(0.0, 0.02, 1.48))


def soft(name, faces, role, bone, slot, thick=0.6, centre=(0, 2, 148)):
    """A rounded, smooth sheet (like the mask) over a cage of flat faces (cm): the cage's creases are rounded
    off, its outline's corners stay where they are. `thick` cm thick, inward toward `centre`."""
    bm = bmesh.new()
    for f in faces: bm.faces.new([bm.verts.new(tuple(np.array(p) / 100)) for p in f])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    K.log('soft', name, 'cage verts', len(bm.verts), 'min', [round(min(v.co[i] for v in bm.verts)*100,1) for i in range(3)], 'max', [round(max(v.co[i] for v in bm.verts)*100,1) for i in range(3)])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    c = Vector(centre)
    if sum(f.normal.dot(f.calc_center_median() * 100 - c) * f.calc_area() for f in bm.faces) < 0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = K.register(name, me, role, bone, slot, smooth=50.0, mirror=False)
    for kind, lv in (("SIMPLE", 1), ("CATMULL_CLARK", 3)):
        s = o.modifiers.new(kind, "SUBSURF"); s.subdivision_type = kind; s.levels = s.render_levels = lv
        s.boundary_smooth = "PRESERVE_CORNERS"
    t = o.modifiers.new("thick", "SOLIDIFY"); t.thickness = thick / 100; t.offset = -1
    return o


# ---- the chest's top layer (reference/chest_sketch_2.jpg and chest_sketch_2_bands.jpg, Will 2026-11-20):
# a breastplate with a V neck and a pointed bottom (rounded and smooth like the mask, not faceted), and a band each side that runs down the front, under the
# arm and up the back to the shoulder again. Drawn on a front view only: 26.1 px per cm, x = 0 at px 845,
# z = 153.4 at px 498 (from the two shoulder discs). Depths, the bands' backs and under-arm parts are choices.
def chest_top():
    cages = {}
    D = lambda px, py, y: drawn((px - 845) / 26.1, 153.4 - (py - 498) / 26.1, y)
    P = lambda p, q, r, px, py: on_plane(p, q, r, (px - 845) / 26.1, 153.4 - (py - 498) / 26.1)
    m = lambda p: np.array((-p[0], p[1], p[2]))
    # the breastplate: a centre ridge, a crease across it where the sketch's shade changes
    v, sr, sc = D(845, 375, -18.6), D(845, 710, -18.8), D(1064, 710, -10.8)
    # The top leans back from a fold level with the under-armour's own fold, so the neckline rests on the
    # under-armour instead of standing in front of it (Will). tm is where that fold meets the outer edge.
    tm = P(v, sr, sc, 1093, 391)
    pe_, pc_, pd_ = CHEST_UNDER
    un = np.cross(pc_ - pe_, pd_ - pe_)
    under = lambda x, z: pe_[1] - (un[0] * (x - pe_[0]) + un[2] * (z - pe_[2])) / un[1]
    K.log("chest top corner tc was", [round(float(c), 2) for c in P(v, sr, sc, 1111, 198)])
    tc = D(1111, 198, -8.0)                      # the top corner at the shoulder, 1.2 cm in front of the under-armour
    for _ in range(4): tc = D(1111, 198, under(tc[0], tc[2]) - 1.2)
    bp = D(845, 1378, -9.5)                      # the bottom point
    lc = P(sr, sc, bp, 1088, 965)
    # Material added above the drawn V (Will: no big gap at the neck): the plate carries on up the under-armour's
    # collar to its top edge beside the neck, 1.2 cm in front of it, keeping a shallow dip at the centre.
    nc = np.array((4.0, under(4.0, 162.6) - 1.2, 162.6))
    n0 = np.array((0.0, -16.2 + (161.0 - 157.55) * 6.2 / 4.85 - 1.2, 161.0))
    for n, p in (("nc", nc), ("n0", n0)): K.log("chest top neck corner %s" % n, [round(float(c), 2) for c in p])
    plate = [[tc, nc, v], [nc, n0, v], [tc, v, tm], [tm, v, sr, sc], [sc, sr, bp, lc]]
    plate += [[m(p) for p in f] for f in plate]
    cages["chest_plate"] = plate
    # the back plate (Will: "an almost flat version of that front piece to the back under the loops", not as far
    # down as the front). Not drawn: the front plate's outline with the bottom point at z 133 instead of 120.5,
    # about 1.2 cm behind the under-armour's back, folding forward to the neck above z 157 as the under-armour does.
    A = np.array
    b_tc, b_nc, b_n0 = A((9.9, 11.8, 163.5)), A((4.0, 13.6, 162.6)), A((0.0, 14.9, 161.0))
    b_v, b_tm = A((0.0, 18.1, 157.0)), A((9.3, 16.0, 156.6))
    b_sr, b_sc = A((0.0, 16.9, 148.0)), A((8.6, 16.0, 148.0))
    b_bp, b_lc = A((0.0, 14.8, 133.0)), A((8.8, 14.6, 138.0))      # 3 cm lower than the first pass (Will)
    bplate = [[b_tc, b_nc, b_v], [b_nc, b_n0, b_v], [b_tc, b_v, b_tm], [b_tm, b_v, b_sr, b_sc], [b_sc, b_sr, b_bp, b_lc]]
    bplate = [[p + A((0.0, 1.0, 0.0)) for p in f] for f in bplate]      # the rounding pulls the sheet in; 1 cm more keeps it clear
    bplate += [[m(p) for p in f] for f in bplate]
    cages["chest_backplate"] = bplate
    # the band, front: inner edge 1.5 cm in front of the breastplate, outer edge solved to keep each facet flat
    ia = D(1119, 190, tc[1] - 1.5)
    ib = D(1077, 480, P(v, sr, sc, 1077, 480)[1] - 1.5)
    ic = D(1059, 800, P(sr, sc, bp, 1059, 800)[1] - 1.5)
    idn = D(1129, 1100, -4.0)
    oa = D(1289, 270, -4.0)
    it = D(1093, 391, tm[1] - 1.5)               # the band folds back where the breastplate does
    ot = P(ia, oa, it, 1275, 391)
    ob = P(it, ot, ib, 1265, 480)
    oc = P(ib, ob, ic, 1304, 700)
    od = D(1341, 1080, 3.5)                      # the loop's bottom hangs under the shoulder joint (y 3.7), like cloth (Will)
    tip = D(1260, 1330, 3.5)                    # the loop's lowest point: the front and the back both run down to it
    # the band's back (Will's mark-up of the first pass): up over the shoulder, down the back, then on down and
    # forward under the arm to the same point as the front, so the band is one closed loop. Nothing joins the
    # front to the back straight under the arm.
    def py(p, q, r, x, z):
        n = np.cross(q - p, r - p); return np.array((x, p[1] - (n[0] * (x - p[0]) + n[2] * (z - p[2])) / n[1], z))
    A = np.array
    ja, ka = A((ia[0], 14.5, ia[2])), A((oa[0], 13.0, oa[2]))      # 1.5 cm behind the back plate
    jb = A((ib[0], 18.9, ib[2])); kb = py(ja, ka, jb, ob[0], ob[2])
    jc = A((ic[0], 17.2, 137.0)); kc = py(jb, kb, jc, 17.9, 137.0)
    im, om = A((ia[0], 2.0, ia[2] + 2.2)), A((oa[0], 2.0, oa[2] + 2.4))        # over the shoulder
    for n, p in (("tc", tc), ("tm", tm), ("ot", ot), ("lc", lc), ("ob", ob), ("oc", oc), ("od", od), ("kb", kb), ("kc", kc)):
        K.log("chest top corner %s solved to" % n, [round(float(c), 2) for c in p])
    band = [[ia, oa, ot, it], [it, ot, ob, ib], [ib, ob, oc, ic], [ic, oc, od, idn], [idn, od, tip],
            [ia, oa, om, im], [im, om, ka, ja],
            [ja, ka, kb, jb], [jb, kb, kc, jc],
            [jc, kc, od, tip]]                   # joined to the front along the tail's outer edge (od to tip), not only at the tip
    band += [[m(p) for p in f] for f in band]
    cages["chest_bands"] = band
    # CHEST_SLIM (Will: "the outer armor is too wide"): the whole top layer is narrowed toward the centre line so
    # that its widest point, the loops' outer edge, comes in by that many cm on each side.
    wide = max(abs(p[0]) for f in band for p in f); k = (wide - CHEST_SLIM) / wide
    K.log("chest top layer: cage half width %.2f cm, slimmed by %.1f cm a side (x scaled by %.3f)" % (wide, CHEST_SLIM, k))
    # CHEST_TAPER (Will: "more of a trapezoid shape ... the bottom loop pieces need to taper in towards the bottom
    # more"): the loops alone are also narrowed going down, from nothing at z 150 to this fraction at their tips.
    lean = lambda name, z: 1 - CHEST_TAPER * float(np.clip((150.0 - z) / 29.0, 0, 1)) if name == "chest_bands" else 1.0
    for name, cage in cages.items():
        soft(name, [[cy((p[0] * k * lean(name, p[2]), p[1], p[2])) for p in f] for f in cage], "Primary", "spine_02", "core")


# ---- the forearm (reference/shell3_forearm_sketch.jpg and ..._lines.jpg, Will 2026-11-20), drawn on the front
# and side views of renders/skeleton_forearm_sheet.png (target (39.5, 3.5, 110), span 50: 0.05 cm per px; the
# sketch is that sheet at 0.967 px per px, its corner at sheet px (265.8, 4.1)). Two curved pieces, left arm:
#   forearm_plate  light tan, wraps the outer side from the elbow (forked round the elbow's bolt) to the wrist
#   forearm_band   dark tan (the mask's oval colour), wraps the inner side, tucked under the plate's rims
# Outlines below are sheet pixels inside each view's 1000 px tile, top to bottom.
FA_D = 145.8
FA_SIDE_L = [(438, 138), (432, 149), (428, 170), (425, 190), (421, 211), (418, 232), (416, 252), (414, 273), (412.6, 294), (412.6, 335),
             (414, 356), (416, 376), (417, 397), (420, 418), (422, 438), (425, 459), (427, 480), (429, 500), (435, 521), (443, 542),
             (449, 563), (455, 583), (460, 604), (464, 625), (466, 645), (468.5, 666), (468.5, 687), (478, 707), (482, 716)]
FA_SIDE_R = [(587, 138), (592.6, 149), (597, 170), (601, 190), (604, 211), (607, 232), (609, 252), (611, 273), (612, 294), (613, 314),
             (613, 335), (612, 356), (610, 376), (608, 397), (606, 418), (604, 438), (601, 459), (599, 480), (596, 500), (590.5, 521),
             (583, 542), (577, 563), (571, 583), (566, 604), (563, 625), (560.5, 645), (557, 666), (556, 687), (548, 707), (545, 716)]
FA_NOTCH_L = [(438, 138), (445, 149), (451, 170), (456, 190), (460, 211), (465, 232), (470.5, 252), (476, 273), (484, 294), (493, 314), (512, 328)]
FA_NOTCH_R = [(587, 138), (582, 149), (575, 170), (570, 190), (565, 211), (560.5, 232), (555, 252), (549, 273), (540, 294), (530.5, 314), (512, 328)]
FA_NOTCH_PY = 328
FA_RIM = [(436, 112), (444, 150), (446, 211), (444, 232), (440.5, 273), (441, 290), (503, 460), (525, 480), (543, 500), (557, 521),
          (570, 542), (581, 563), (591.5, 583), (603, 604), (613, 625), (627, 645), (643, 666), (664, 687), (669, 692)]
FA_SIL = [(436, 112), (446, 128), (458, 149), (470.5, 170), (482, 190), (494, 211), (506, 232), (518, 252), (530.5, 273), (538, 294),
          (544, 314), (549, 335), (555, 356), (561.5, 376), (567, 397), (573, 418), (578, 438), (582, 459), (586, 480), (591.5, 500),
          (596, 521), (601, 542), (606, 563), (613, 583), (620.5, 604), (630, 625), (640, 645), (652.5, 666), (669, 687), (672, 692)]
FA_BAND = dict(A=(441, 290), B=(388, 418), C=(450, 555), D=(503, 460))    # front view: A-D is the line it meets the plate on
FA_Q = 0.7                       # how boxy the cross-sections are (1 = an ellipse)
# The forearm armour's size against the drawing (Will: "a tiny bit bigger", then "still need to be bigger
# overall"): FA_BIG scales its cross-section; its length grows by a third as much, about its middle, along the arm.
# WWO_FA_BIG=<number> in the environment tries another value.
FA_BIG = float(os.environ.get("WWO_FA_BIG", 1.25))
FA_LONG = 1 + (FA_BIG - 1) / 3
def fa_long(p):
    z = 114.4 + (p[2] - 114.4) * FA_LONG
    return (p[0] - 0.40 * (z - p[2]), p[1], z)                 # the forearm leans 0.40 cm outward per cm down
FA_TUCK = 0.5                    # the plate is this much wider than the band, each side, where they meet (Will)
ARM_C = Vector((40.5, 4.2, 114))


def sheet_mesh(name, grids, role, bone, slot, thick, centre):
    """A smooth sheet through grids of points (cm), facing away from `centre`, `thick` cm thick inward."""
    bm = bmesh.new()
    for grid in grids:
        vs = [[bm.verts.new((x / 100, y / 100, z / 100)) for x, y, z in row] for row in grid]
        for a, b in zip(vs, vs[1:]):
            for j in range(len(a) - 1): bm.faces.new((a[j], a[j + 1], b[j + 1], b[j]))
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    if sum(f.normal.dot(f.calc_center_median() * 100 - centre) * f.calc_area() for f in bm.faces) < 0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = K.register(name, me, role, bone, slot, smooth=50.0)
    m = o.modifiers.new("thick", "SOLIDIFY"); m.thickness = thick / 100; m.offset = -1
    return o


def forearm():
    row = lambda line, py: float(np.interp(py, [p[1] for p in line], [p[0] for p in line]))
    def front_x(line, z, y):         # the front view's line at height z, for a point at depth y
        f = FA_D / (FA_D - (3.5 - y)); return 39.5 + (row(line, 500 - (z - 110) / 0.05 * f) - 500) * 0.05 / f
    def side_y(line, z, x):          # the side view's line at height z, for a point at x
        f = FA_D / (FA_D - (x - 39.5)); return 3.5 + (row(line, 500 - (z - 110) / 0.05 * f) - 500) * 0.05 / f
    def fpt(px, py, y):
        f = FA_D / (FA_D - (3.5 - y)); return np.array((39.5 + (px - 500) * 0.05 / f, 110 - (py - 500) * 0.05 / f))
    g = lambda u: np.sin(np.pi * u) ** FA_Q
    def rims(z):
        xf = 39.0
        for _ in range(4):
            yf, yb = side_y(FA_SIDE_L, z, xf), side_y(FA_SIDE_R, z, xf); xf = front_x(FA_RIM, z, yf)
        return xf, yf, yb
    # the plate: one cross-section every half centimetre, from the front rim over the crown to the back rim
    zs = np.linspace(128.7, 100.1, 58); sec = []
    for z in zs:
        xf, yf, yb = rims(z); w = max(yb - yf, 1e-4)
        f = FA_D / (FA_D - (xf - 39.5))
        if 500 - (z - 110) / 0.05 * f < FA_NOTCH_PY:
            nl, nr = side_y(FA_NOTCH_L, z, xf), side_y(FA_NOTCH_R, z, xf)
            ul, ur = [np.arccos(np.clip(1 - 2 * (n - yf) / w, -1, 1)) / np.pi for n in (nl, nr)]
            ul, ur = min(ul, 0.5), max(ur, 0.5)
        else: nl, ul, ur = (yf + yb) / 2, 0.5, 0.5
        depth = float(np.clip((front_x(FA_SIL, z, nl) - xf) / max(g(ul), 0.4), 0.05, 5.5))
        sec.append([xf, yf, yb, ul, ur, depth])
    sec = np.array(sec)
    sec[1:-1, 5] = (sec[:-2, 5] + 2 * sec[1:-1, 5] + sec[2:, 5]) / 4          # the crown's line, evened out
    mid = (sec[:, 1] + sec[:, 2]) / 2                    # FA_BIG (Will: "a tiny bit bigger"): wider front to back and deeper, same length
    sec[:, 1], sec[:, 2], sec[:, 5] = mid - (mid - sec[:, 1]) * FA_BIG, mid + (sec[:, 2] - mid) * FA_BIG, sec[:, 5] * FA_BIG
    pt = lambda s, z, u: (s[0] + s[5] * g(u), s[1] + (s[2] - s[1]) * (1 - np.cos(np.pi * u)) / 2, z)
    m = 10
    fr = [[pt(s, z, u) for u in np.linspace(0, s[3], m + 1)] for s, z in zip(sec, zs)]
    bk = [[pt(s, z, u) for u in np.linspace(s[4], 1, m + 1)] for s, z in zip(sec, zs)]
    fr, bk = [[fa_long(q) for q in r] for r in fr], [[fa_long(q) for q in r] for r in bk]
    sheet_mesh("forearm_plate", [fr, bk], "Primary", "lowerarm_l", "arms", 0.35, ARM_C)
    for i in (0, 16, 20, 28, 40, 50, 57): K.log("forearm plate z %.1f: rim x %.2f, y %.2f to %.2f, crown x %.2f" % (zs[i], sec[i][0], sec[i][1], sec[i][2], sec[i][0] + sec[i][5]))
    # the band: its front edge is under the plate's front rim, it goes round the inner side to the back rim.
    # Only its front was drawn; the back half mirrors the front half about the arm's middle.
    A, D = fpt(*FA_BAND["A"], -0.4), fpt(*FA_BAND["D"], -0.2)
    B, C = fpt(*FA_BAND["B"], 4.15), fpt(*FA_BAND["C"], 4.15)
    for n, p in zip("ABCD", (A, B, C, D)): K.log("forearm band corner %s: x %.2f z %.2f" % (n, p[0], p[1]))
    B[0] -= 2.7 * (FA_BIG - 1); C[0] -= 2.7 * (FA_BIG - 1)                           # the band grows with the plate
    A = A + (A - B) * 1.0 / (A[0] - B[0]); D = D + (D - C) * 1.0 / (D[0] - C[0])     # 1 cm further, under the plate
    grid = []
    for s in np.linspace(0, 1, 15):
        S, I = A + (D - A) * s, B + (C - B) * s
        _, yf, yb = rims(S[1]); ym, h = (yf + yb) / 2, (yb - yf) / 2 * FA_BIG - FA_TUCK
        grid.append([(S[0] + (I[0] - S[0]) * g(t), ym - h * np.cos(np.pi * t), S[1] + (I[1] - S[1]) * g(t)) for t in np.linspace(0, 1, 29)])
    grid = [[fa_long(q) for q in r] for r in grid]
    sheet_mesh("forearm_band", [grid], "Secondary", "lowerarm_l", "arms", 0.25, ARM_C)


# ---- the shoulder cap (reference/shell3_shoulder_sketch.jpg, Will 2026-11-20), drawn on the front and side views
# of renders/shell3_shoulder_sheet.png (target (24, 3.5, 148), span 50: 0.05 cm per px; the sketch is that sheet
# at 1.258 px per px). One curved light-tan shield over the outside of the shoulder joint, rounded at the top
# and pointed at the bottom. It rides the upper arm (as shell 2's pauldron does). Outlines are sheet pixels
# inside each view's 1000 px tile, top to bottom: the front view's inner edge and outer edge, the side view's
# front edge and back edge.
SH_RIM = [(396, 172), (392, 180), (392.7, 204), (393.5, 216), (394.3, 228), (396, 240), (399, 252), (408, 264), (416.5, 276), (425, 288),
          (437, 300), (450, 312), (462, 323.5), (474, 335.5), (484, 347), (493, 359), (501, 371), (510, 383), (515, 395), (521, 407),
          (526, 419), (532.6, 431), (537.4, 443), (545, 455), (551, 466.6), (557, 478.5), (563.6, 490.5), (572, 502), (581, 514), (590, 526)]
SH_SIL = [(404, 172), (431.6, 180), (452, 192), (477, 204), (496, 216), (513.5, 228), (528, 240), (544, 252), (557, 264), (570, 276),
          (579.5, 288), (587, 300), (593, 312), (598, 323.5), (603, 335.5), (606.5, 347), (611, 359), (614.5, 371), (617, 383), (617.6, 395),
          (616, 407), (616, 419), (617, 431), (617.6, 443), (617, 455), (614, 466.6), (611, 478.5), (608, 490.5), (604, 502), (601, 514), (592, 526)]
SH_SIDE_L = [(470, 172), (424, 180), (400, 192), (387, 204), (377, 216), (369, 228), (362.5, 240), (358, 252), (352, 264), (347, 276),
             (342.6, 288), (334.7, 300), (334.7, 312), (337, 323.5), (337, 335.5), (339.4, 347), (340, 359), (342.6, 371), (343.4, 383), (343.4, 395),
             (346, 407), (358, 419), (369, 431), (382, 443), (395, 455), (409, 466.6), (422, 478.5), (437, 490.5), (459, 502), (477, 514), (509, 527)]
SH_SIDE_R = [(540, 172), (582, 180), (621, 192), (633, 204), (641.5, 216), (650, 228), (657, 240), (659, 252), (662, 264), (666, 276),
             (671, 288), (678, 300), (680, 312), (680, 323.5), (680, 335.5), (678, 347), (677, 359), (675, 371), (673, 383), (673, 395),
             (671, 407), (659, 419), (648, 431), (635, 443), (621.6, 455), (606.5, 466.6), (595, 478.5), (579.5, 490.5), (560, 502), (542, 514), (509, 527)]


SH_BULGE = 2.0


def shoulder():
    row = lambda line, py: float(np.interp(py, [p[1] for p in line], [p[0] for p in line]))
    def front_x(line, z, y):
        f = FA_D / (FA_D - (3.5 - y)); return 24.0 + (row(line, 500 - (z - 148) / 0.05 * f) - 500) * 0.05 / f
    def side_y(line, z, x):
        f = FA_D / (FA_D - (x - 24.0)); return 3.5 + (row(line, 500 - (z - 148) / 0.05 * f) - 500) * 0.05 / f
    g = lambda u: np.sin(np.pi * u) ** FA_Q
    zs = np.linspace(164.7, 146.7, 73); sec = []
    for z in zs:
        xr = 22.0
        for _ in range(4):
            yf, yb = side_y(SH_SIDE_L, z, xr), side_y(SH_SIDE_R, z, xr); xr = front_x(SH_RIM, z, (yf + yb) / 2)
        sec.append([xr, yf, yb, max(front_x(SH_SIL, z, (yf + yb) / 2), xr + 0.05)])
    # the traced outlines are a little shaky, which showed as wrinkles (Will): even each one out along the cap
    sec = np.array(sec); k = np.exp(-0.5 * (np.arange(-9, 10) / 3.0) ** 2); k /= k.sum()
    ends = sec[[0, -1]].copy()
    sec = np.stack([np.convolve(np.pad(c, 9, mode="edge"), k, mode="valid") for c in sec.T], axis=1)
    sec[[0, -1]] = ends
    # 2 cm longer down the upper arm (Will): the tapering part below z 152.6 is stretched along the arm's own
    # slope (the inner edge's: 0.47 cm outward per cm down), so the point ends 1.8 cm lower and 1.35 cm further out
    # Will, later: "extend a tiny bit further down the upper arm, bulge out more by like 2 cm": 1 cm more length
    # (3 in all), and the crown 2 cm further out at the middle, easing to nothing at the top and the point.
    sec[:, 3] += SH_BULGE * np.sin(np.pi * np.linspace(0, 1, len(sec))) ** 0.8
    SH_TAPER, SH_MORE = 152.6, 3.0
    grid = []
    for (xr, yf, yb, xc), z in zip(sec, zs):
        if z < SH_TAPER:
            t = (SH_TAPER - z) / (SH_TAPER - zs[-1]); dz = SH_MORE * 0.905 * t
            dx = 0.47 * dz + 0.5 * t          # and 0.5 cm more at the point, to clear the rod down the arm's outer side
            z, xr, xc = z - dz, xr + dx, xc + dx
        grid.append([(xr + (xc - xr) * g(u), yf + (yb - yf) * (1 - np.cos(np.pi * u)) / 2, z) for u in np.linspace(0, 1, 25)])
        if len(grid) in (1, 12, 28, 48, 73): K.log("shoulder cap z %.1f: inner edge x %.2f, y %.2f to %.2f, crown x %.2f" % (z, xr, yf, yb, xc))
    sheet_mesh("shoulder_cap", [grid], "Primary", "upperarm_l", "arms", 0.4, Vector((22.0, 3.7, 153.0)))


# ---- the waist (reference/shell3_waist_sketch.jpg, Will 2026-11-20), drawn on the front and side views of
# renders/shell3_waist_sheet.png (target (0, 0, 108), span 50: 0.05 cm per px; the sketch is that sheet at
# 0.910 px per px, its corner at sheet px (107.7, 4.4)). Two pieces, both rigid to the pelvis:
#   waist_plate  light tan, curved: a V down the front to a point between the legs, up over each hip, and
#                the same down the back (the back is not drawn; the side view gives its outline)
#   waist_under  under-armour like the chest's: four flat facets with a centre ridge, filling the V's top
# Outlines are sheet pixels inside each view's 1000 px tile. Each edge is given in both views, start to end.
WA_TOP_F = [(500, 552), (497, 545), (492, 532), (487, 519), (480, 505.5), (474, 492), (466, 479), (457, 466), (448, 453), (438.5, 440),
            (427.5, 426), (416.5, 413), (407, 400), (394.5, 387), (382, 374), (370, 360), (357, 347), (342, 334), (324, 321), (303, 308), (299, 303)]
WA_TOP_S = [(317.6, 552), (319, 545), (322, 532), (324, 519), (327.5, 505.5), (332, 492), (336, 479), (341, 466), (347, 453), (353, 440),
            (360, 426), (368, 413), (377, 400), (386, 387), (393, 374), (403, 360), (414, 347), (429, 334), (444, 321), (465, 308), (523, 300)]
WA_TOP_SB = [(708, 552), (707.7, 532), (706.6, 519), (706.6, 505.5), (704, 492), (702, 479), (699, 466), (697, 453), (693, 440), (689, 426),
             (682, 413), (677, 400), (669, 387), (661.5, 374), (652, 360), (640, 347), (625, 334), (608, 321), (581, 308), (523, 300)]
WA_BOT_F = [(500, 768), (486, 756), (475, 743), (466, 730), (456, 716.5), (446, 703), (437, 690), (430, 677), (424, 664), (418, 651),
            (412, 637), (405.5, 624), (400, 611), (392, 598), (385, 585), (377, 571), (367, 558), (358, 545), (347, 532), (337, 519),
            (326, 505.5), (314, 492), (301, 479), (285, 466), (263, 453), (240, 449), (222, 446)]
WA_BOT_S = [(322, 765), (329, 756), (332, 743), (334, 730), (336, 716.5), (338.5, 703), (342, 690), (345, 677), (346, 664), (349.5, 651),
            (353, 637), (356, 624), (359, 611), (363, 598), (368, 585), (374, 571), (380, 558), (387, 545), (396, 532), (404, 519),
            (415, 505.5), (427.5, 492), (440, 479), (454, 466), (471, 453), (510, 446)]
WA_BOT_SB = [(696, 765), (688, 756), (688, 743), (689, 730), (689, 651), (687, 637), (686, 624), (685, 611), (681, 598), (678, 585),
             (676, 571), (672.5, 558), (668, 545), (663, 532), (658, 519), (647, 505.5), (635, 492), (615, 479), (587, 466), (548, 453), (510, 446)]


def waist():
    def curve(front, side, n=41):
        """One edge in 3D from its two drawn views: the same fraction of the way along each."""
        out = []
        for u in np.linspace(0, 1, n):
            def at(line):
                py = line[0][1] + (line[-1][1] - line[0][1]) * u
                q = sorted(line, key=lambda p: p[1]); return float(np.interp(py, [p[1] for p in q], [p[0] for p in q])), py
            (fx, fpy), (sx, spy) = at(front), at(side)
            x, y = 5.0, 0.0
            for _ in range(5):
                ff, fs = FA_D / (FA_D + y), FA_D / (FA_D - x)
                x, y = abs(fx - 500) * 0.05 / ff, (sx - 500) * 0.05 / fs
            out.append((x, y, (108 - (fpy - 500) * 0.05 / ff + 108 - (spy - 500) * 0.05 / fs) / 2))
        return np.array(out)
    def loop(front, side_f, side_b):
        c = np.concatenate([curve(front, side_f), curve(front, side_b)[::-1][1:]])
        k = np.exp(-0.5 * (np.arange(-6, 7) / 1.2) ** 2); k /= k.sum(); ends = c[[0, -1]].copy(); raw = c.copy()
        c = np.stack([np.convolve(np.pad(col, 6, mode="edge"), k, mode="valid") for col in c.T], axis=1)   # the tracing's shake, evened out
        c[[0, -1]] = ends; c[[0, -1], 0] = 0.0
        c[38:43] += (raw[40] - c[40]) * np.array([0.3, 0.7, 1.0, 0.7, 0.3])[:, None]     # the hip's corner stays where it was drawn
        return c
    top, bot = loop(WA_TOP_F, WA_TOP_S, WA_TOP_SB), loop(WA_BOT_F, WA_BOT_S, WA_BOT_SB)
    for n, c in (("top edge", top), ("bottom edge", bot)):
        for i, w in ((0, "front centre"), (40, "hip"), (80, "back centre")): K.log("waist plate %s, %s: %s" % (n, w, [round(float(v), 2) for v in c[i]]))
    from mathutils.bvhtree import BVHTree
    # the under-armour, left half: P the top of the ridge, M the ridge's front-most point (on the crease), N its
    # bottom (1 cm below the V's notch, hidden); C and E the top corner and the crease's end, each carried
    # 1.5 cm further out than drawn so they end under the tan piece.
    A = np.array
    # As drawn it lies inside the spine's lowest ribs, so (WA_UNDER_FWD) the whole piece sits 0.9 cm further
    # forward than drawn and (WA_UNDER_CORNER) its top corners are no further back than y = -6.6.
    WA_UNDER_FWD, WA_UNDER_CORNER = 0.9, -6.6
    P, M, N = A((0.0, -6.7, 117.6)), A((0.0, -8.39, 113.5)), A((0.0, -7.36, 104.5))
    C = P + (A((6.72, -3.88, 115.8)) - P) * 1.223
    E = M + (A((5.83, -5.22, 114.55)) - M) * 1.257
    for q in (P, M, N, C, E): q[1] -= WA_UNDER_FWD
    C[1] = min(C[1], WA_UNDER_CORNER)
    nrm = np.cross(M - P, C - P); Ey = P[1] - (nrm[0] * (E[0] - P[0]) + nrm[2] * (E[2] - P[2])) / nrm[1]
    K.log("waist under-armour: crease end E moved %.2f cm in depth to keep the upper facet flat" % (Ey - E[1])); E[1] = Ey
    mx = lambda p: A((-p[0], p[1], p[2]))
    faces = [[P, C, E, M], [M, E, N]]; faces += [[mx(p) for p in f] for f in faces]
    under_tree = BVHTree.FromPolygons([Vector(tuple(q)) for f in faces for q in f], [tuple(range(sum(len(g) for g in faces[:i]), sum(len(g) for g in faces[:i + 1]))) for i in range(len(faces))])
    solid("waist_under", [[p / 100 for p in f] for f in faces], "Frame", "pelvis", "core", inset=0.006, centre=(0.0, 0.0, 1.10))
    left = np.array([[a + (b - a) * t for t in np.linspace(0, 1, 13)] for a, b in zip(top, bot)])
    # Built straight from the two outlines the plate cuts through the pelvis's top ring and the spine's lowest
    # ribs, which stand up to WA_PUSH_MAX cm outside it between the front and the hips. The skeleton is left
    # alone: each point of the plate is moved straight out from the body's centre line until it clears the
    # skeleton, and the under-armour it lies over, by WA_CLEAR cm, and the amounts are evened out so the plate stays smooth.
    tree = BVHTree.FromObject(skel, bpy.context.evaluated_depsgraph_get())
    WA_CLEAR, WA_REACH = 0.8, 6.0
    push = np.zeros(left.shape[:2])
    for i in range(left.shape[0]):
        for j in range(left.shape[1]):
            x, y, z = left[i, j]; r = float(np.hypot(x, y - 0.5))
            if r < 1e-6: continue
            d = Vector((x / r, (y - 0.5) / r, 0.0))
            for dz in (-0.6, 0.0, 0.6):
                o = Vector((0.0, 0.5, z + dz)) + d * (r + WA_REACH)
                for tr in (tree, under_tree):              # the skeleton, and the under-armour the plate lies over
                    hit = tr.ray_cast(o, -d, WA_REACH + WA_CLEAR)
                    if hit[0] is not None: push[i, j] = max(push[i, j], WA_REACH - hit[3] + WA_CLEAR)
    K.log("waist plate: pushed out by up to %.2f cm to clear the skeleton (%.0f%% of it moved)" % (push.max(), 100 * (push > 0).mean()))
    need = push.copy()                                   # a smooth swell that is nowhere less than what is needed
    blur = lambda f: sum(np.pad(f, ((2, 2), (1, 1)), mode="edge")[a:a + f.shape[0], b:b + f.shape[1]] for a in range(5) for b in range(3)) / 15
    for _ in range(40): push = np.maximum(need, blur(push))
    push = blur(push)
    for i in range(left.shape[0]):
        for j in range(left.shape[1]):
            x, y, z = left[i, j]; r = float(np.hypot(x, y - 0.5))
            if r > 1e-6: left[i, j] = (x + x / r * push[i, j], y + (y - 0.5) / r * push[i, j], z)
    left = [[tuple(q) for q in r] for r in left]
    right = [[(-x, y, z) for x, y, z in r] for r in left]
    sheet_mesh("waist_plate", [left, right], "Primary", "pelvis", "core", 0.4, Vector((0.0, 0.5, 106.0)))

# ---- the thighs (Will, 2026-11-20: "based on what i have been doing for the rest of the body so far, do the
# thighs"). No sketch: this is my design in the same language as his pieces, left leg:
#   thigh_plate  light tan, curved: a leaf round the front, outer side and back, narrowing to a point at the
#                knee that kicks out a little (as the forearm's does)
#   thigh_band   dark tan, round the back and inner side, tucked under the plate's rims and dropping as it
#                goes round (as the forearm's does)
# Both follow the skeleton's thigh at a set distance. Angles are degrees round the thigh bone seen from above:
# 0 = front, 90 = outer side, 180 = back, 270 = inner side. Heights are cm.
# Will's mark-up of the first pass (reference/shell3_thigh_markup.jpg, blue outline on the thigh sheet: target
# (13, 1, 84), span 56): bigger, and "cover more of the back of the thigh". Read off his outline, the plate now
# runs from an apex over the outside of the hip joint down to a point at the knee, and wraps from the inner
# front corner right round the back to the inner back corner. Its two edges, as (height, angle) top to bottom:
TH_FRONT = [(107.5, 90), (104, 5), (100, -25), (96, -40), (93, -45), (80, -38), (72, -28), (66, 10), (61.5, 57)]
TH_BACK = [(107.5, 90), (104.8, 170), (100, 200), (88, 218), (77, 200), (68.4, 165), (64.5, 110), (61.5, 57)]
TH_KICK = 1.8                        # how far the point kicks out
TH_BAND = (90.0, 82.0, 3.0)          # the band's top and bottom where it meets the plate; how far it drops round the inside
TH_PLATE_OFF, TH_BAND_OFF = 1.4, 0.8 # distance from the skeleton to each piece's outer face


def thighs():
    from mathutils.bvhtree import BVHTree
    tree = BVHTree.FromObject(skel, bpy.context.evaluated_depsgraph_get())
    head, tail = np.array((12.0, 0.0, 103.2)), np.array((13.7, 2.2, 62.9))           # the thigh bone
    axis = lambda z: head + (tail - head) * (head[2] - z) / (head[2] - tail[2])
    way = lambda a: np.array((np.sin(np.radians(a)), -np.cos(np.radians(a)), 0.0))
    # the skeleton's own radius round the bone, every 5 degrees and half centimetre, then a smooth swell over it
    angs, hts, REACH = np.arange(0, 360, 5.0), np.arange(56.0, 110.5, 0.5), 13.0
    rad = np.full((len(hts), len(angs)), 3.0)
    for i, z in enumerate(hts):
        for j, a in enumerate(angs):
            o = axis(z) + way(a) * REACH
            hit = tree.ray_cast(Vector(tuple(o)), Vector(tuple(-way(a))), REACH)
            if hit[0] is not None: rad[i, j] = max(3.0, REACH - hit[3])
    need = rad.copy()
    def blur(f):
        g = np.pad(np.concatenate([f[:, -2:], f, f[:, :2]], axis=1), ((2, 2), (0, 0)), mode="edge")
        return sum(g[a:a + f.shape[0], b:b + f.shape[1]] for a in range(5) for b in range(5)) / 25
    for _ in range(60): rad = np.maximum(need, blur(rad))
    rad = blur(rad)
    def radius(a, z):
        fi = float(np.clip((z - hts[0]) / 0.5, 0, len(hts) - 1.001)); fj = (a % 360) / 5.0
        i, j = int(fi), int(fj); u, w = fi - i, fj - j; j1 = (j + 1) % len(angs)
        return (rad[i, j] * (1 - w) + rad[i, j1] * w) * (1 - u) + (rad[i + 1, j] * (1 - w) + rad[i + 1, j1] * w) * u
    at = lambda a, z, off: tuple(axis(z) + way(a) * (radius(a, z) + off))
    zs = np.linspace(TH_FRONT[0][0], TH_FRONT[-1][0], 93)
    def edge(pts):
        e = np.interp(-zs, [-p[0] for p in pts], [p[1] for p in pts])
        k = np.exp(-0.5 * (np.arange(-8, 9) / 3.0) ** 2); k /= k.sum(); ends = e[[0, -1]].copy()
        e = np.convolve(np.pad(e, 8, mode="edge"), k, mode="valid"); e[[0, -1]] = ends
        return e
    ef, eb = edge(TH_FRONT), edge(TH_BACK)
    grid = []
    for z, f, b in zip(zs, ef, eb):
        t = (zs[0] - z) / (zs[0] - zs[-1]); kick = TH_KICK * max(0.0, (t - 0.75) / 0.25) ** 2
        grid.append([at(f + (b - f) * q, z, TH_PLATE_OFF + kick) for q in np.linspace(0, 1, 49)])
    sheet_mesh("thigh_plate", [grid], "Primary", "thigh_l", "legs", 0.4, Vector(tuple(axis(83.0))))
    zt, zb, drop = TH_BAND; zm = (zt + zb) / 2
    a0, a1 = float(np.interp(-zm, -zs, eb)) - 14, float(np.interp(-zm, -zs, ef)) + 14 + 360     # 14 degrees under each rim
    grid = [[at(a0 + (a1 - a0) * u, zt + (zb - zt) * v - drop * np.sin(np.pi * u), TH_BAND_OFF) for u in np.linspace(0, 1, 41)] for v in np.linspace(0, 1, 9)]
    sheet_mesh("thigh_band", [grid], "Secondary", "thigh_l", "legs", 0.25, Vector(tuple(axis(83.0))))
    K.log("thigh: skeleton radius %.1f to %.1f cm; band from %.0f to %.0f degrees" % (need.min(), need.max(), a0, a1 - 360))


# ---- the shins and calves (Will's mark-up of the whole-robot sheet, reference/shell3_calf_markup.jpg,
# 2026-11-20: "something similar but more of a clamshell design"; then, of my first pass, which only wrapped
# the leg at a set distance: "Not shin/calf shaped at all. Follow my drawings much more closely"). Left leg:
#   shin_stripe   the stripe down the front, from a point above the knee to the ankle: "i don't want that
#                 section to curve horizontally, but curving vertically is fine", so it is flat from side to
#                 side with a hard edge each side.
#   shin_plate    light tan, curved: the front shell, growing out of the stripe's edges below the knee, swelling
#                 to the calf's widest point and narrowing to little more than the stripe by the ankle.
#   calf_plate    light tan, curved: the back shell, flat-topped behind the knee, pointed at the bottom, its
#                 edges lapping over the front shell's.
# Traced from his blue lines in the front, back and side views of the whole-robot sheet (target (0, 0, 100),
# span 215: 0.1536 cm per px; the mark-up is the sheet at 0.7156 px per px). All tables are (height, cm).
# Where his front and back views gave different widths, or the two sides of one view differed ("things that
# should be symmetrical weren't"), the values here are the averages, the same both sides of the leg.
LEG_X = lambda z: 13.9 + (61.0 - z) * 0.027                            # the lower leg's centre line, from the stripe's middle
# The shin's front line in the side view (y by height). Will's second mark-up (reference/shell3_calf_markup_2.jpg,
# yellow): "one continuous line/curve. Ditch all the wobbly ness". One smooth curve from the stripe's point
# above the knee to the ankle, slightly steeper at the top as his line is; it passes within 0.15 cm of his
# first drawing's front line at mid-shin, and is 0.5 cm in front of it at the ankle: with a hard edge
# the shell turns back at once across the corners of the shin's strut, which is wider than the face lower down.
def SN_FRONT(z):
    t = (65.85 - z) / 43.85
    return -7.0 + 8.44 * t - 1.74 * t * t
SN_STRIPE = [(65.85, 0.0), (64.6, 0.85), (63.3, 1.33), (62, 1.8), (60.7, 2.15), (58.1, 2.15), (56.8, 2.3), (55.6, 2.2), (54.3, 2.1),
             (53, 1.9), (50.4, 1.83), (49, 1.65), (46.5, 1.5), (44, 1.3), (40, 1.25), (34, 1.2), (31, 1.28), (22, 1.28)]   # the stripe's half width
SN_RIM = [(59.4, -5.5), (58.1, -3.4), (56.8, -0.8), (56.5, 1.5), (56.2, 2.5), (54.2, 4.6), (53, 6.0), (51.7, 6.65), (50.4, 7.0), (49, 7.4),
          (47.8, 7.6), (46.5, 7.8), (42.7, 7.8), (41.3, 7.5), (40, 7.3), (37.5, 6.76), (36.2, 6.5), (35, 6.2), (33.7, 5.8), (32.4, 5.57),
          (31, 5.0), (29.8, 4.7), (28.5, 4.15), (27.2, 3.76), (25.9, 3.2), (24.6, 3.0), (23.4, 1.9), (22, 1.6)]   # the front shell's back edge in the side view (y): its top edge, then the seam
SN_HALF = [(59.4, 2.15), (58.1, 4.0), (56.8, 5.05), (55.6, 5.9), (54.3, 6.3), (53, 6.6), (51.7, 6.75), (50.4, 6.8), (49, 6.7), (47.8, 6.5),
           (46.5, 6.2), (45.2, 5.7), (44, 5.2), (42.7, 4.65), (41.4, 4.1), (40, 3.55), (38.8, 3.1), (37.5, 2.8), (36.2, 2.65), (35, 2.4),
           (33.7, 2.25), (32.4, 2.0), (31, 1.75), (22, 1.75)]          # the front shell's half width at that edge
CA_BACK = [(59.4, 10.7), (58.1, 12.3), (56.8, 13.5), (55.6, 14.4), (54, 15.2), (51.7, 15.5), (48.6, 15.65), (46.5, 15.4), (43.65, 14.75),
           (41.4, 13.8), (40, 13.2), (38.8, 12.7), (36.2, 12.4), (35, 12.2), (33.3, 12.4)]   # the calf's back line in the side view (y); its upper curve is Will's green line on the second mark-up ("needs to extend out more")
CA_HALF = [(59.4, 5.0), (58.1, 5.5), (56.8, 5.9), (55.6, 6.4), (54.3, 6.8), (53, 7.1), (51.7, 7.25), (50.4, 7.3), (49, 7.2), (47.8, 7.0),
           (46.5, 6.7), (45.2, 6.2), (44, 5.7), (42.7, 5.15), (41.4, 4.6), (40, 4.05), (38.8, 3.6), (37.5, 3.0), (36.2, 2.5), (35, 2.0), (33.7, 1.5), (33.3, 1.2)]   # the back shell's half width where it is widest
SN_EDGE = 35.0                                                         # the angle (degrees) at which the shell turns away from the flat face at each of its edges (Will: hard edges)
SN_FULL = 1.0                                                          # 1 = a plain quarter-round after that edge
CA_LAP = 0.6                                                           # the back shell laps this far forward over the front shell's edge
CA_TOP = lambda z: 8.1 - (59.4 - z) / 0.53                             # the back shell's top edge in the side view: it has only what is behind this (y)
CA_LOW = lambda z: 7.3 + (39.4 - z) * 0.836                            # and, below 39.4, its bottom edge, running down and back to the point
LEG_CLEAR = 0.55                                                       # nothing is nearer than this to the skeleton


def hugger(head, tail, lo, hi, reach):
    """For a limb whose bone runs from head to tail (cm): returns at(angle, z, off), the point `off` cm outside a
    smooth swell over the skeleton, and axis(z), the bone's point at that height."""
    from mathutils.bvhtree import BVHTree
    tree = BVHTree.FromObject(skel, bpy.context.evaluated_depsgraph_get())
    head, tail = np.array(head), np.array(tail)
    axis = lambda z: head + (tail - head) * (head[2] - z) / (head[2] - tail[2])
    way = lambda a: np.array((np.sin(np.radians(a)), -np.cos(np.radians(a)), 0.0))
    angs, hts = np.arange(0, 360, 5.0), np.arange(lo, hi + 0.5, 0.5)
    rad = np.full((len(hts), len(angs)), 3.0); raw = np.zeros(rad.shape)
    for i, z in enumerate(hts):
        for j, a in enumerate(angs):
            for da in (-1.7, 0.0, 1.7):                  # small parts slip between rays 5 degrees apart
                for dz in (-0.17, 0.17):
                    hit = tree.ray_cast(Vector(tuple(axis(z + dz) + way(a + da) * reach)), Vector(tuple(-way(a + da))), reach)
                    if hit[0] is not None: rad[i, j] = max(rad[i, j], reach - hit[3]); raw[i, j] = max(raw[i, j], reach - hit[3])
    need = rad.copy()
    def blur(f):
        g = np.pad(np.concatenate([f[:, -2:], f, f[:, :2]], axis=1), ((2, 2), (0, 0)), mode="edge")
        return sum(g[a:a + f.shape[0], b:b + f.shape[1]] for a in range(5) for b in range(5)) / 25
    for _ in range(60): rad = np.maximum(need, blur(rad))
    rad = blur(rad)
    def radius(a, z):
        fi = float(np.clip((z - hts[0]) / 0.5, 0, len(hts) - 1.001)); fj = (a % 360) / 5.0
        i, j = int(fi), int(fj); u, w = fi - i, fj - j; j1 = (j + 1) % len(angs)
        return (rad[i, j] * (1 - w) + rad[i, j1] * w) * (1 - u) + (rad[i + 1, j] * (1 - w) + rad[i + 1, j1] * w) * u
    tight = np.pad(np.concatenate([raw[:, -1:], raw, raw[:, :1]], axis=1), ((1, 1), (0, 0)), mode="edge")       # 0 where there is no skeleton
    tight = np.max([tight[a:a + need.shape[0], b:b + need.shape[1]] for a in range(3) for b in range(3)], axis=0)
    def actual(a, z):
        """The skeleton's own radius there (the largest of the nearest samples), not the smooth swell."""
        i = int(np.clip(round((z - hts[0]) / 0.5), 0, len(hts) - 1)); j = int(round((a % 360) / 5.0)) % len(angs)
        return tight[i, j]
    radius.actual = actual
    return (lambda a, z, off: tuple(axis(z) + way(a) * (radius(a, z) + off))), axis, radius


def by_height(pts, zs, soft=3.0):
    """A table of (height, value) read at the heights zs, evened out; its two ends are kept."""
    e = np.interp(-zs, [-p[0] for p in pts], [p[1] for p in pts])
    k = np.exp(-0.5 * (np.arange(-8, 9) / soft) ** 2); k /= k.sum(); ends = e[[0, -1]].copy()
    e = np.convolve(np.pad(e, 8, mode="edge"), k, mode="valid"); e[[0, -1]] = ends
    return e


def calves():
    at, axis, radius = hugger((13.7, 2.2, 62.9), (15.7, 4.6, 16.6), 18.0, 68.0, 11.0)
    C = Vector(tuple(axis(42.0)))
    from mathutils.bvhtree import BVHTree
    tree = BVHTree.FromObject(skel, bpy.context.evaluated_depsgraph_get())
    def short(p):
        """How much nearer the skeleton's surface than LEG_CLEAR the point is (0 if it is clear). The shells may
        pass between the shin's strut and the rods behind it, so this is distance, not a ring round the leg."""
        hit = tree.find_nearest(Vector(tuple(float(c) for c in p)), LEG_CLEAR)
        return 0.0 if hit[0] is None else LEG_CLEAR - hit[3]
    def spread(e):                                       # a smooth amount that is nowhere less than what is needed
        need = e.copy()
        for _ in range(10): e = np.maximum(need, np.convolve(np.pad(e, 3, mode="edge"), np.ones(7) / 7, mode="valid"))
        return e
    sm = lambda e, n=4: np.convolve(np.pad(e, n, mode="edge"), np.ones(2 * n + 1) / (2 * n + 1), mode="valid")
    # the stripe, and the front line with it, come forward where the skeleton would otherwise show through
    zs = np.linspace(65.85, 22.0, 111)
    yf, ws = np.array([SN_FRONT(z) for z in zs]), by_height(SN_STRIPE, zs, 1.5)
    fwd = spread(np.array([max(short((LEG_X(z) + u, y, z)) for u in (-w, 0.0, w)) for z, y, w in zip(zs, yf, ws)]))
    K.log("shin: front line moved forward by up to %.2f cm to clear the skeleton" % fwd.max()); yf = yf - fwd
    sheet_mesh("shin_stripe", [[[(LEG_X(z) + w * q, y, z) for q in (-1.0, 0.0, 1.0)] for z, y, w in zip(zs, yf, ws)]], "Primary", "calf_l", "legs", 0.4, C)
    # the front shell: each side a quarter-round from the stripe's edge out and back to its own edge.
    # Where it would be too near the skeleton it is made wider (whole cross-sections at a time, evened out
    # along the leg, so it stays smooth).
    keep = zs <= 59.4
    rim = np.interp(-zs, [-p[0] for p in SN_RIM], [p[1] for p in SN_RIM]); half = np.interp(-zs, [-p[0] for p in SN_HALF], [p[1] for p in SN_HALF])
    rim, half = np.maximum(sm(sm(rim)), yf), np.maximum(sm(sm(half)), ws)
    def front(sgn, half):
        t0 = np.radians(SN_EDGE)                      # the shell leaves the flat face at this angle: a hard edge, then the curve
        return [[(LEG_X(z) + sgn * (w + (h - w) * ((np.sin(th) - np.sin(t0)) / (1 - np.sin(t0))) ** SN_FULL), y + (r - y) * (np.cos(t0) - np.cos(th)) / np.cos(t0), z) for th in np.linspace(t0, np.pi / 2, 21)]
                for z, y, w, r, h in zip(zs[keep], yf[keep], ws[keep], rim[keep], half[keep])]
    wider = np.zeros(len(zs))
    for _ in range(4):
        lack = np.zeros(len(zs)); lack[keep] = [max(short(q) for q in ra[1:] + rb[1:]) for ra, rb in zip(front(1, half + wider), front(-1, half + wider))]
        wider = sm(wider + spread(lack) * 1.15)
    K.log("shin plate: made wider than drawn by up to %.2f cm (a side) to clear the skeleton" % wider.max())
    sheet_mesh("shin_plate", [front(1, half + wider), front(-1, half + wider)], "Primary", "calf_l", "legs", 0.4, C)
    # the back shell: a half-round from one side round the back to the other, cut by its top and bottom edges
    cz = np.linspace(59.4, 33.3, 67)
    yb, hb = by_height(CA_BACK, cz, 2.0), by_height(CA_HALF, cz, 2.0)
    y0 = np.interp(-cz, -zs, rim) - CA_LAP; y0 = np.where(cz > 56.2, np.interp(56.2, zs[::-1], rim[::-1]) - CA_LAP, y0)
    fw = np.interp(-cz, -zs, half + wider)
    def back(wide, deep):
        g = []
        for z, b, h, f, gw, gd in zip(cz, yb, hb, y0, wide, deep):
            cut = max(CA_TOP(z), CA_LOW(z) if z < 39.4 else -99.0, f)
            a0 = float(np.arcsin(np.clip((cut - f) / max(b + gd - f, 1e-6), 0, 1)))
            g.append([(LEG_X(z) + (h + gw) * np.cos(al), f + (b + gd - f) * np.sin(al), z) for al in np.linspace(a0, np.pi - a0, 41)])
        return g
    wide = sm(np.maximum(0.0, fw + 0.5 - hb) * (cz > 39.4))      # where it laps over the front shell (above 39.4) it is at least 0.5 cm wider
    deep = np.zeros(len(cz))
    for _ in range(4):
        lack = spread(np.array([max(short(q) for q in r) for r in back(wide, deep)])) * 1.15
        wide, deep = sm(wide + lack), sm(deep + lack)
    K.log("calf plate: made wider than drawn by up to %.2f cm a side (to lap over the front shell and clear the skeleton), deeper by up to %.2f" % (wide.max(), deep.max()))
    sheet_mesh("calf_plate", [back(wide, deep)], "Primary", "calf_l", "legs", 0.4, C)

# ---- the feet (Will, 2026-11-20: "make the feet, they don't gotta be anything special"). My design: one plain
# curved light-tan cap over the toes and instep, open underneath and at the back, where it stops just in front
# of the ankle block so the ankle can turn. Left foot; rigid to the foot bone.
FT_X, FT_HALF, FT_LOW = 15.8, 6.7, 0.6       # the cap's centre line and half width, and how far its edges stop above the ground
FT_TOP = [(-23.6, 2.0), (-22.0, 5.2), (-18, 6.9), (-12, 7.8), (-6, 8.4), (-3, 9.0), (-0.3, 10.0)]    # (y, height of its top), toe to ankle


def feet():
    ys = np.linspace(FT_TOP[0][0], FT_TOP[-1][0], 47)
    top = np.interp(ys, [p[0] for p in FT_TOP], [p[1] for p in FT_TOP])
    top = np.convolve(np.pad(top, 3, mode="edge"), np.ones(7) / 7, mode="valid"); top[0] = FT_TOP[0][1]
    half = FT_HALF * np.clip((ys - ys[0]) / 4.0, 0, 1) ** 0.5                         # rounded off at the toe
    grid = [[(FT_X + h * np.cos(a), y, FT_LOW + (t - FT_LOW) * np.sin(a) ** 0.6) for a in np.linspace(0, np.pi, 33)] for y, t, h in zip(ys, top, half)]
    sheet_mesh("foot_cap", [grid], "Primary", "foot_l", "legs", 0.4, Vector((FT_X, -10.0, 2.0)))


# the skeleton alone, to see the mask on and to check nothing of it pokes through
body = bpy.data.objects["SK_RobotFrame"]
part = classify(body); skeleton_edits.apply(body)
bm = bmesh.new(); bm.from_mesh(body.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if part[f.index] != "Skeleton"], context="FACES")
skel = bpy.data.objects.new("SkeletonOnly", bpy.data.meshes.new("SkeletonOnly"))
for m in body.data.materials: skel.data.materials.append(m)
bm.to_mesh(skel.data); bm.free()
bpy.context.scene.collection.objects.link(skel); skel.matrix_world = body.matrix_world

head()
chest()
chest_top()
forearm()
shoulder()
waist()
thighs()
calves()
feet()
parts = K.finish("Shell3", base_hands=False)


def poke_check(mask):
    """How far the skeleton's head sticks out through the mask, looking out from the middle of the head."""
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()
    tree = BVHTree.FromObject(mask, dg)
    names = {g.index: g.name for g in body.vertex_groups}
    worst = (0.0, None)
    used = {i for p in skel.data.polygons for i in p.vertices}
    for i in used:
        v = skel.data.vertices[i]
        if not v.groups or names[max(v.groups, key=lambda g: g.weight).group] != "head": continue
        d = v.co - HEAD_C; L = d.length
        hit = tree.ray_cast(HEAD_C, d.normalized())
        if hit[0] is not None and L - hit[3] > worst[0]: worst = (L - hit[3], tuple(round(c, 1) for c in v.co))
    K.log("skeleton through the mask: %.2f cm at %s" % worst if worst[1] else "skeleton through the mask: none")


poke_check(parts["head"])


def touch_check(o, what, xmin=15):
    """Where the skeleton cuts through a built slot's mesh."""
    from mathutils.bvhtree import BVHTree
    dg = bpy.context.evaluated_depsgraph_get()
    hits = BVHTree.FromObject(o, dg).overlap(BVHTree.FromObject(skel, dg))
    cs = [skel.data.polygons[j].center for _, j in hits if abs(skel.data.polygons[j].center.x) >= xmin]
    if not cs: K.log("skeleton through the %s: none" % what); return
    K.log("skeleton through the %s: %d faces, x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f" % ((what, len(cs)) + tuple(f(c[i] for c in cs) for i in range(3) for f in (min, max))))


touch_check(parts["arms"], "arm armour")
touch_check(parts["core"], "chest", 0)
touch_check(parts["legs"], "leg armour", 0)
if TRIAL is not None:
    cam, lights = K.studio((1000, 1000))
    for n, d in (("front", (0, -1, 0)), ("side", (1, 0, 0)), ("three_q", (0.7, -1, 0.5)), ("back", (0, 1, 0))):
        tv = [float(c) for c in os.environ.get("WWO_TRIAL_VIEW", "0,0,145,72").split(",")]      # what to look at: x, y, z, span
        K.shoot(OUT + "/renders/options/%s_%s.png" % (TRIAL, n), cam, lights, d, target=tuple(tv[:3]), span=tv[3])
    sys.exit(0)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "shell3_claude.blend"))

cam, lights = K.studio((1000, 1000))
R = OUT + "/renders/shell3_head_"
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (0.7, -1, 0.5), "low_q": (0.7, -1, -0.35), "back": (0, 1, 0), "back_q": (0.7, 1, 0.25)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, -1, 175), span=40)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell3_chest_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 138), span=70)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell3_forearm_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(39.5, 3.5, 110), span=50)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell3_shoulder_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(24.0, 3.5, 148.0), span=50)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell3_waist_"
bpy.data.objects["SK_Shell3_Arms"].hide_render = True          # the arms hang in front of the waist in the side view
bm = bmesh.new(); bm.from_mesh(skel.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if abs(f.calc_center_median().x) > 26 and f.calc_center_median().z < 150], context="FACES")
bm.to_mesh(skel.data); bm.free()
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0.0, 0.0, 108.0), span=50)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell3_thigh_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(13.0, 1.0, 84.0), span=56)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell3_calf_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(14.5, 3.5, 42.0), span=60)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
R = OUT + "/renders/shell3_foot_"
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(15.8, -4.0, 10.0), span=44)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
