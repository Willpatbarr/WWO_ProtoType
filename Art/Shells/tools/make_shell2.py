# Shell 2, "scout": angular slab armour, twin green goggles, an antenna, boxy pauldrons, green light strips.
# Reference: reference/shell2_scout.png
#
#   /Applications/Blender.app/Contents/MacOS/Blender -b \
#     base/robot_frame_v2_rigged.blend \
#     -P tools/make_shell2.py
# Writes shell2_claude.blend and renders/shell2_*.png.
import bpy, sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
from shell_kit import Surf, Form, tube, patch, panel, strip, cyl, joint, ball, hull

OUT = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "")
P, S, F = "Primary", "Secondary", "Frame"
K.ROLES["Glow"] = dict(color=(0.5, 1.0, 0.05), rough=0.3, metal=0.0, glow=(0.22, 1.0, 0.0), strength=5.0)
HEAD_IN = float(os.environ.get("HEAD_IN", "0.01"))
TAG = os.environ.get("SHELL_TAG", "shell2")
CHIN_IN = float(os.environ.get("CHIN_IN", "0.015"))        # the chin made this much narrower in total (metres)
JAW_SHAVE = float(os.environ.get("JAW_SHAVE", "0.015"))   # planed off the underside of the jaw (metres)
JAW_BACK = float(os.environ.get("JAW_BACK", "0.015"))     # the front of both jaw structures moved back by this much (metres)
GOGGLE_UP = float(os.environ.get("GOGGLE_UP", "0.02"))   # the whole goggle assembly raised by this much (metres)
EYE_OUT = float(os.environ.get("EYE_OUT", "0.025"))     # how much further apart the two lenses sit (metres, in total)
SKETCH = os.environ.get("SKETCH_COLORS") == "1"      # review only: the jaw structures in the colours of Will's sketch
K.ROLES["SketchGrey"] = dict(color=(0.22, 0.22, 0.21), rough=0.7, metal=0.0)
K.ROLES["SketchSalmon"] = dict(color=(0.55, 0.3, 0.22), rough=0.7, metal=0.0)
K.ROLES["SketchYellow"] = dict(color=(0.6, 0.62, 0.2), rough=0.7, metal=0.0)
K.ROLES["SketchOlive"] = dict(color=(0.42, 0.45, 0.12), rough=0.7, metal=0.0)
K.ROLES["SketchPink"] = dict(color=(0.42, 0.2, 0.2), rough=0.7, metal=0.0)
K.ROLES["PrimaryLight"] = dict(color=(0.052, 0.052, 0.056), rough=0.48, metal=0.35)   # the head shell and jaw: a shade lighter than the armour
PL = "PrimaryLight"
K.SMOOTH = 13.0
K.EDGE_POWER = 1.0
Form.linear = True
E = (0.045, 0.045, 0.004)        # plate edges: a narrow, hard chamfer
BOX = dict(c=0.22, b=0.012, soft=1.5)

PAUL_TILT = float(os.environ.get("PAUL_TILT", 12))
SHO, ELB, WRI = (0.232, 0.04, 1.528), (0.34, 0.04, 1.26), (0.449, 0.04, 0.992)
HIP, KNEE, ANK = (0.12, 0.0, 1.032), (0.137, 0.022, 0.629), (0.157, 0.046, 0.166)


def box(name, lo, hi, role, bone, slot, **kw):
    pts = [(x, y, z) for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    return hull(name, pts, role, bone, slot, **kw)


def rod(name, a, b_, r, role, bone, slot, **kw):
    a, b_ = np.array(a, float), np.array(b_, float)
    return cyl(name, (a + b_) / 2, b_ - a, r, np.linalg.norm(b_ - a) / 2, role, bone, slot, n=12, bevel=0.0, **kw)


def ring(pts, z, k=1.0, dy=0.0):
    """A helmet outline (half of it, x >= 0) at height z, scaled about the head's centre."""
    return [(x * k, (y - 0.0) * k + dy, z) for x, y in pts]


def ring(name, c, ri, ro, y0, y1, role, bone, slot, n=10, **kw):
    """A hollow collar round the Y axis through c: inner radius ri, outer ro, from y0 to y1."""
    V, F = [], []
    for y in (y0, y1):
        for r in (ri, ro):
            for j in range(n):
                a = 2 * np.pi * j / n
                V.append((c[0] + r * np.cos(a), y, c[1] + r * np.sin(a)))
    q = lambda yi, ri_, j: yi * 2 * n + ri_ * n + j % n
    for j in range(n):
        F += [(q(0, 0, j), q(0, 0, j + 1), q(1, 0, j + 1), q(1, 0, j)), (q(0, 1, j), q(1, 1, j), q(1, 1, j + 1), q(0, 1, j + 1)),
              (q(0, 0, j), q(0, 1, j), q(0, 1, j + 1), q(0, 0, j + 1)), (q(1, 0, j), q(1, 0, j + 1), q(1, 1, j + 1), q(1, 1, j))]
    return K.add(name, V, F, role, bone, slot, **kw)


def solid(name, faces, role, bone, slot, inset=0.008, centre=(0.0, -0.04, 1.68)):
    """A structure of flat facets, both sides given: each face is one plane with hard edges and no bevel.
    Each facet is a slab, `inset` deep along its own normal toward the middle of the head."""
    V, F = [], []
    C = np.array(centre)
    for f in faces:
        P = [np.array(p, float) for p in f]; n0 = len(V); k = len(P)
        nrm = np.zeros(3)
        for a, c in zip(P, P[1:] + P[:1]):          # Newell's method: the plane's normal, for any outline
            nrm += np.cross(a, c)
        nrm /= np.linalg.norm(nrm) + 1e-12
        if nrm.dot(C - np.mean(P, axis=0)) < 0: nrm = -nrm
        V += P + [q + nrm * inset for q in P]
        F.append(tuple(range(n0, n0 + k))); F.append(tuple(range(n0 + 2 * k - 1, n0 + k - 1, -1)))
        for i in range(k):
            a, c = n0 + i, n0 + (i + 1) % k
            F.append((a, c, c + k, a + k))
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in V], [], F); me.update()
    import bmesh
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.recalc_face_normals(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    return K.register(name, me, role, bone, slot, bevel=0.0, smooth=1.0, mirror=False)


def top_plate(name, stations, role, bone, slot, nu=10, nseg=8, thick=0.012, centre=(0.0, 0.0, 1.775)):
    """A plate lofted through stations ((yc, zc), half width, (ye, ze), bow): hard edges at the stations,
    a slight bow between them, a fall toward both side edges; `thick` deep toward the middle of the head."""
    rows = []                                    # each row: the right half, centre to edge
    for i, (c, w, e, bow) in enumerate(stations):
        def row(c, w, e):
            return [np.array((w * u, c[0] + (e[0] - c[0]) * u * u, c[1] + (e[1] - c[1]) * u * u)) for u in np.linspace(0, 1, nu + 1)]
        if i == 0: rows.append((row(c, w, e), True)); continue
        c0, w0, e0, _ = stations[i - 1]
        d = np.array((c[0] - c0[0], c[1] - c0[1])); n = np.array((-d[1], d[0])) / (np.linalg.norm(d) + 1e-9)
        if n.dot(np.array(((c[0] + c0[0]) / 2 - centre[1], (c[1] + c0[1]) / 2 - centre[2]))) < 0: n = -n
        for k in range(1, nseg + 1):
            q = k / nseg; bulge = stations[i - 1][3] * 4 * q * (1 - q)
            lerp = lambda a, b_: (a[0] + (b_[0] - a[0]) * q + n[0] * bulge, a[1] + (b_[1] - a[1]) * q + n[1] * bulge)
            rows.append((row(lerp(c0, c), w0 + (w - w0) * q, lerp(e0, e)), k == nseg))
    C = np.array(centre)
    grid = [[np.array((-p[0], p[1], p[2])) for p in r[::-1][:-1]] + r for r, _ in rows]        # full width
    nr, nc = len(grid), len(grid[0])
    V = [p for r in grid for p in r]
    V += [p + (C - p) / np.linalg.norm(C - p) * thick for p in V]
    n0 = nr * nc
    idx = lambda l, i, j: l * n0 + i * nc + j
    F = []
    for i in range(nr - 1):
        for j in range(nc - 1):
            F.append((idx(0, i, j), idx(0, i, j + 1), idx(0, i + 1, j + 1), idx(0, i + 1, j)))
            F.append((idx(1, i, j), idx(1, i + 1, j), idx(1, i + 1, j + 1), idx(1, i, j + 1)))
        F.append((idx(0, i, 0), idx(0, i + 1, 0), idx(1, i + 1, 0), idx(1, i, 0)))
        F.append((idx(0, i, nc - 1), idx(1, i, nc - 1), idx(1, i + 1, nc - 1), idx(0, i + 1, nc - 1)))
    for j in range(nc - 1):
        F.append((idx(0, 0, j), idx(1, 0, j), idx(1, 0, j + 1), idx(0, 0, j + 1)))
        F.append((idx(0, nr - 1, j), idx(0, nr - 1, j + 1), idx(1, nr - 1, j + 1), idx(1, nr - 1, j)))
    return K.add(name, V, F, role, bone, slot, bevel=0.0, smooth=6.0, mirror=False)


CUTTERS = []


def armhole(o, r=0.088, rz=0.120):
    """Cut a ten-sided opening round each shoulder joint out of an armour block."""
    for side in (1, -1):
        me = bpy.data.meshes.new("armhole")
        n = 10
        V = [(side * x, SHO[1] - 0.006 + r * np.cos(2 * np.pi * (j + 0.5) / n), SHO[2] - 0.048 + rz * np.sin(2 * np.pi * (j + 0.5) / n)) for x in (0.150, 0.300) for j in range(n)]
        F = [(j, (j + 1) % n, n + (j + 1) % n, n + j) for j in range(n)] + [tuple(range(n)), tuple(range(n, 2 * n))]
        me.from_pydata(V, [], F); me.update(); me.materials.append(K.material(P))
        c = bpy.data.objects.new("armhole", me); bpy.context.scene.collection.objects.link(c)
        c.hide_render = True; c.display_type = "WIRE"; CUTTERS.append(c)
        m = o.modifiers.new("armhole", "BOOLEAN"); m.operation = "DIFFERENCE"; m.object = c
        try: m.solver = "EXACT"
        except Exception: pass
        o.modifiers.move(len(o.modifiers) - 1, 0)        # cut first, then bevel


def cut_solid(name, planes, L, role, bone, slot, size=0.5):
    """A block of flat facets: a cube cut down by planes ((normal), offset) given in a local frame; L maps
    local (out, forward, up) to the world. Every facet is exactly one plane; hard edges, no bevel."""
    import bmesh
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=size)
    for n, d in planes:
        n = np.array(n, float); ln = np.linalg.norm(n); n /= ln
        r = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=tuple(n * d / ln), plane_no=tuple(n), clear_outer=True)
        ed = [e for e in r["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
        if ed: bmesh.ops.contextual_create(bm, geom=ed)
    bmesh.ops.dissolve_limit(bm, angle_limit=np.radians(0.5), verts=bm.verts, edges=bm.edges)
    for v in bm.verts: v.co = L(*v.co)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    return K.register(name, me, role, bone, slot, bevel=0.0, smooth=1.0)


def recess(o, pts):
    """Cut the convex hull of pts out of a piece."""
    import bmesh
    me = bpy.data.meshes.new("recess"); bm = bmesh.new()
    bmesh.ops.convex_hull(bm, input=[bm.verts.new(p) for p in pts]); bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free(); me.materials.append(K.material(P))
    c = bpy.data.objects.new("recess", me); bpy.context.scene.collection.objects.link(c)
    c.hide_render = True; c.display_type = "WIRE"; CUTTERS.append(c)
    m = o.modifiers.new("recess", "BOOLEAN"); m.operation = "DIFFERENCE"; m.object = c
    try: m.solver = "EXACT"
    except Exception: pass


def head():
    b, s = "head", "head"
    # ---- one shell, to Will's side profile: a low rounded-flat top, the front dropping straight to a chin at
    # the front, a short flat jaw, then a straight diagonal up to the back of the head at goggle height
    # The skeleton's skull is always shown (skeleton_split.py) and the top of the head has to hold it: its top is
    # at 188.3 cm, 4.8 cm either side, and its forehead reaches y -10.3 at 184 cm. (The top was 187.8 before.)
    Z0 = 1.898
    def sec(z, w, front, back, **o):            # front / back: how far the outline reaches (metres, +front, +back)
        cf = (front - back) / 2.0; d = (front + back) / 2.0
        return (Z0 - z, w, w, d, d, dict(cf=cf, **o))
    def skull(inn):                             # inn: how far each side comes in at goggle height (metres)
        return Form((0, 0, Z0), (0, 0, 1.60), [
            # the top is a shallow curve from a slanted forehead back to a rounded back corner; each side
            # above the band is flat, in two planes
            sec(1.898, 0.060 - inn, -0.040, 0.060),
            sec(1.894, 0.068 - inn, 0.030, 0.075),
            sec(1.888, 0.078 - inn, 0.068, 0.085),
            sec(1.870, 0.081 - inn, 0.090, 0.092),
            sec(1.846, 0.082 - inn, 0.107, 0.100),
            sec(1.831, 0.083 - inn, 0.114, 0.108),
            sec(1.789, 0.092 - inn, 0.125, 0.130),          # widest from here down to the bottom of the goggle band
            sec(1.716, 0.092 - inn, 0.124, 0.130),         # full depth to the bottom of the goggle band
            # below the band the head is only a core: the two jaw structures sit over it
            sec(1.706, 0.046, 0.108 - JAW_BACK, 0.100),
            sec(1.672, 0.040, 0.104 - JAW_BACK, 0.000),
            sec(1.648, 0.034, 0.100 - JAW_BACK, -0.040 + JAW_BACK * 0.5),
            sec(1.626, 0.024, 0.096 - JAW_BACK, -0.078 + JAW_BACK)], c=[(0.45, 0.3), (0.45, 0.3), 0.3, 0.3], b=0.0, soft=1.5)
    JAW_Z = 1.626
    h = skull(HEAD_IN)
    band = skull(0.0)                           # the goggle band keeps its own width
    T = lambda z: Z0 - z
    # ---- the top of the head, from Will's sketch (reference/top_sketch.png): the old shell is now a dark core,
    # taken in under three pieces: a curved top plate, and a plate each side of two curved facets
    tube("skull", h, T(1.845), T(1.750), S, b, s, nt=16, nth=64, lift=-0.004, bevel=0.0012)      # (was 12 mm in; the skeleton's ear discs reach 7 cm out)
    tube("skull_top", h, T(1.884), T(1.845), S, b, s, nt=8, nth=64, lift=-0.006, bevel=0.0)      # the core carried up under the top plate, over the skeleton's skull
    # top plate: stations along the centre line (y, z) with the plate's half width and its side edge (y, z) there.
    # Hard edges at the stations; between them the plate bows out a little; across, it falls away to the edges.
    ST_SKETCH = [((-0.126, 1.787), 0.059, (-0.1028, 1.787), 0.002),          # as traced from the sketch
                 ((-0.104, 1.833), 0.054, (-0.086, 1.826), 0.0),
                 ((-0.0881, 1.8535), 0.051, (-0.0749, 1.8455), 0.004),
                 ((0.0498, 1.8857), 0.049, (0.0451, 1.8787), 0.002),
                 ((0.1004, 1.8519), 0.051, (0.0905, 1.844), 0.003),
                 ((0.1265, 1.7876), 0.056, (0.1154, 1.7876), 0.0)]
    ST = [((-0.126, 1.787), 0.062, (-0.1028, 1.787), 0.002),                 # raised and widened over the skull
          ((-0.116, 1.846), 0.064, (-0.098, 1.838), 0.0),
          ((-0.080, 1.889), 0.064, (-0.068, 1.880), 0.004),
          ((0.0498, 1.898), 0.062, (0.0451, 1.890), 0.002),
          ((0.1004, 1.856), 0.058, (0.0905, 1.848), 0.003),
          ((0.1265, 1.7876), 0.056, (0.1154, 1.7876), 0.0)]
    def edge_z(st, y):                           # the height of a top plate's side edge at y
        e = [s_[2] for s_ in st]
        return float(np.interp(y, [q[0] for q in e], [q[1] for q in e]))
    def up(pts):                                 # a side plate's outline, stretched to stay under the raised top plate
        return [(y, 1.787 + (z - 1.787) * (edge_z(ST, y) - 1.787) / max(edge_z(ST_SKETCH, y) - 1.787, 1e-4)) for y, z in pts]
    top_plate("top_plate", ST, "SketchSalmon" if SKETCH else PL, b, s)
    # side plates, on the head's own side surface. Outlines are (y, z) read off the side view.
    Q = lambda pts: [(h.theta_at(Z0 - z, y), Z0 - z) for y, z in up(pts)]
    patch("side_a", h, Q([(-0.0976, 1.7870), (-0.0903, 1.8113), (-0.0700, 1.8400), (-0.0351, 1.8560), (0.0128, 1.8660)]),
          Q([(-0.0728, 1.7870), (-0.0728, 1.8083), (-0.0613, 1.8272), (-0.0438, 1.8337), (0.0267, 1.8374)]),
          "SketchYellow" if SKETCH else PL, b, s, nu=36, nv=6, lift=0.001, thick=0.008, bevel=0.0, smooth=35.0, mirror=True)
    patch("side_b", h, Q([(-0.0728, 1.8083), (0.0434, 1.8091), (0.0884, 1.8381)]),
          Q([(-0.0728, 1.7870), (0.0434, 1.7870), (0.1131, 1.7870)]),
          "SketchOlive" if SKETCH else PL, b, s, nu=36, nv=6, lift=0.001, thick=0.008, bevel=0.0, smooth=35.0, mirror=True)
    tube("underhead", h, T(1.750), T(JAW_Z), S, b, s, nt=14, nth=64, bevel=0.0012)        # the core under the band: the goggles' colour
    # ---- the jaw, traced from Will's sketch (reference/jaw_sketch.png): two structures, four flat facets each
    # (a top, a front and one plane per side), no bevels. Each side plane's outline is read off the side view;
    # its X comes from the plane through the front edge and the back top corner.
    def back(y):                                # the front section moved back; the move fades out behind it
        return y + JAW_BACK * min(max((-0.076 - y) / 0.066, 0.0), 1.0)
    def side(plane, outline):
        x0, y0, z0, k, m = plane
        return [(x0 + k * (back(y) - y0) + m * (z - z0), back(y), z) for y, z in outline]
    mir = lambda p: (-p[0], p[1], p[2])
    # upper: a top sloping forward from under the band, a front plate, and a cheek each side running back under the band
    UP = (0.029, back(-0.142), 1.700, (0.074 - 0.029 - 0.0401 * 0.0178) / (0.004 - back(-0.142)), 0.0401)       # the front plate is nearer the camera than the sketch plane: widths corrected for that
    u = side(UP, [(-0.142, 1.6986), (-0.105, 1.7178), (-0.002, 1.7178), (-0.001, 1.713), (-0.010, 1.697), (-0.076, 1.6795),
                  (-0.079, 1.6756), (-0.088, 1.6535), (-0.113, 1.6468), (-0.133, 1.6662)])   # held 5-7 mm clear of the lower jaw where they met
    ft, tb, fb = u[0], u[1], u[-1]
    solid("jaw_upper", [[ft, tb, mir(tb), mir(ft)], [fb, ft, mir(ft), mir(fb)], u, [mir(p) for p in u]], "SketchGrey" if SKETCH else PL, b, s)
    # lower: a chin block (a top and a front) and one plane each side: the jaw's body, with an arm rising to the band at the back
    lm = 0.0561 / (1.4225 * (0.042 - back(-0.1157)) + 0.1031)
    LO = (0.0305, back(-0.1157), 1.6185, 1.4225 * lm, lm)
    l = side(LO, [(-0.1334, 1.6582), (-0.137, 1.6488), (-0.1157, 1.6185), (0.0038, 1.645), (0.013, 1.688), (0.042, 1.7216),
                  (0.0074, 1.713), (-0.0003, 1.697), (-0.0268, 1.690), (-0.0376, 1.658), (-0.1127, 1.6404)])
    p1, p2, p3 = l[0], l[1], l[2]
    solid("jaw_lower", [[p1, p2, mir(p2), mir(p1)], [p2, p3, mir(p3), mir(p2)], l, [mir(p) for p in l]], "SketchPink" if SKETCH else PL, b, s)
    # ---- the goggle band: all the way round the head
    ez = 1.737 + GOGGLE_UP
    tube("band", band, T(1.764 + GOGGLE_UP), T(1.718), S, b, s, nt=4, nth=64, lift=0.007, bevel=0.002)
    eyes = ((0.036 + EYE_OUT / 2, 0.035, 0.040), (-0.042 - EYE_OUT / 2, 0.024, 0.028))
    box("goggle_bar", (min(-0.078, eyes[1][0] - 0.038), -0.150, ez - 0.022), (max(0.078, eyes[0][0] + 0.049), -0.118, ez + 0.022), S, b, s, bevel=0.004)
    HEAD_Y = -0.104                          # just inside the front of the head, so the barrels run into it
    for x, r, ln in eyes:       # the robot's left lens is the big one
        y0 = -0.150
        yb = y0 - ln + 0.008                     # where the glass sits, set back inside the rim
        cyl("lens_core", (x, (HEAD_Y + yb) / 2, ez), (0, 1, 0), r * 0.76 + 0.001, (HEAD_Y - yb) / 2, S, b, s, n=10, bevel=0.0)
        cyl("lens", (x, yb - 0.001, ez), (0, 1, 0), r * 0.76, 0.002, "Glow", b, s, n=20, bevel=0.0)
        # the eye piece: one barrel of constant outside diameter from inside the head's face to the lip
        ring("eye_piece", (x, ez), r * 0.76, r + 0.010, HEAD_Y, yb - 0.017, P, b, s, bevel=0.002)
    for r, hh, role in ((0.051, 0.010, P), (0.034, 0.016, F), (0.016, 0.021, S)):      # 5.1 cm: the skeleton's ear disc is 4.85
        cyl("temple", (0.096 - (0.006 if role == P else 0), 0.012, 1.7795), (1, 0, 0), r, hh + (0.006 if role == P else 0), role, b, s, n=12, mirror=True, bevel=0.002)      # over the skeleton's ear discs
    # ---- antenna and its cable: low on the back of the head, on the robot's right
    ax_, ay, az = -0.108, 0.078, 1.737 + GOGGLE_UP
    box("antenna_base", (ax_ - 0.016, ay - 0.024, az - 0.036), (ax_ + 0.012, ay + 0.024, az + 0.036), S, b, s)
    rod("antenna_stub", (ax_ - 0.002, ay, az + 0.036), (ax_ - 0.002, ay, az + 0.085), 0.009, F, b, s)
    rod("antenna", (ax_ - 0.002, ay, az + 0.085), (ax_ - 0.002, ay, az + 0.215), 0.0035, F, b, s)
    rod("antenna_tip", (ax_ - 0.002, ay, az + 0.215), (ax_ - 0.002, ay, az + 0.24), 0.0045, "Glow", b, s)
    pts = [(ax_, ay + 0.02, az - 0.03), (-0.10, 0.10, az - 0.075), (-0.075, 0.105, az - 0.13), (-0.045, 0.085, az - 0.165)]
    for i in range(3):
        rod("cable%d" % i, pts[i], pts[i + 1], 0.006, F, b, s)
        ball("cable_knuckle%d" % i, pts[i + 1], 0.0065, F, b, s)
    n = Surf((0, 0.022, 1.675), (0, 0.036, 1.56), [(0, 0.04, 0.04, 0.04, 0.04), (0.115, 0.048, 0.048, 0.046, 0.048)], pw=1.0)
    tube("neck", n, 0.0, 0.115, F, "neck_01", s, nt=4, nth=20)
    for t in (0.03, 0.06):
        tube("neck_ring", n, t, t + 0.018, S, "neck_01", s, nt=1, nth=12, lift=0.006, bevel=0.002)


def core():
    s = "core"
    b = "spine_02"
    c = Form((0, 0.02, 1.60), (0, 0.02, 1.20), [
        # depth: the chest is deepest at the pecs; the back is deepest higher up, across the shoulder blades
        (0.00, 0.105, 0.105, 0.096, 0.094),
        (0.05, 0.184, 0.184, 0.142, 0.156),
        (0.085, 0.194, 0.194, 0.158, 0.176),
        (0.14, 0.200, 0.200, 0.176, 0.168),
        (0.19, 0.190, 0.190, 0.170, 0.150),
        (0.24, 0.150, 0.150, 0.130, 0.124),
        (0.32, 0.104, 0.104, 0.094, 0.092),          # below the chest armour the torso is a narrow column
        (0.40, 0.094, 0.094, 0.084, 0.082)], c=[(0.42, 0.36), (0.42, 0.36), 0.28, 0.28], b=0.012, soft=1.5)
    tube("chest_under", c, 0.0, 0.40, F, b, s, nt=18, nth=56, lift=-0.018)
    # ---- chest armour, faceted like the reference (reference/chest_ref_*.jpg): one block of flat planes standing
    # well proud of the abdomen. An upper front plane and a lower one that tucks back under it meet on the line
    # of the green slit; a plane each side runs back to the armpit.
    cu = hull("cuirass", [(0.085, -0.150, 1.578), (0.115, -0.200, 1.470), (0.070, -0.146, 1.298),
                     (0.175, -0.095, 1.565), (0.208, -0.060, 1.500), (0.142, -0.085, 1.280),
                     (0.208, 0.0, 1.500), (0.175, 0.0, 1.565), (0.142, 0.0, 1.280), (0.085, -0.02, 1.578)], P, b, s, sym=True)
    # under the arm the armour wraps round from the chest to the back
    ua = hull("underarm", [(0.171, -0.074, 1.378), (0.171, 0.100, 1.378),
                      (0.150, -0.080, 1.282), (0.140, 0.085, 1.288), (0.150, -0.05, 1.378), (0.150, 0.06, 1.378), (0.110, 0.0, 1.282)],
         P, b, s, mirror=True)
    box("chest_light", (-0.030, -0.2035, 1.468), (0.030, -0.190, 1.476), "Glow", b, s, bevel=0.0)
    # trap armour: a strap over each trap, from the top of the chest to the top of the back, standing proud of the torso
    strap = [(-0.150, 1.560), (-0.096, 1.602), (-0.01, 1.624), (0.098, 1.622), (0.160, 1.580)]        # (y, z) along its top
    for i in range(4):
        (y0, z0), (y1, z1) = strap[i], strap[i + 1]
        hull("strap%d" % i, [(x, y, z - d) for x in (0.094, 0.138) for (y, z) in ((y0, z0), (y1, z1)) for d in (0.0, 0.03)], P, b, s, mirror=True)
    box("strap_buckle", (0.088, -0.150, 1.562), (0.144, -0.128, 1.588), S, b, s, mirror=True)
    for i, t in enumerate((0.262, 0.312)):
        patch("rib%d" % i, c, [(48 + 4 * i, t - 0.004), (88.4, t + 0.006)], [(51 + 4 * i, t + 0.036), (88.4, t + 0.046)], S, b, s,
              nu=14, nv=4, lift=0.003, thick=0.011, edge=E, mirror=True)
    # back: two big planes over the shoulder blades meeting a raised spine box with the light, and a pack below
    ba = hull("back_armour", [(0.09, 0.150, 1.605), (0.190, 0.125, 1.565), (0.13, 0.218, 1.525), (0.208, 0.115, 1.50),
                         (0.16, 0.175, 1.42), (0.105, 0.150, 1.290), (0.168, 0.09, 1.366), (0.142, 0.085, 1.290),
                         (0.208, 0.0, 1.50), (0.185, 0.0, 1.565), (0.105, 0.02, 1.290), (0.09, 0.03, 1.605)], P, b, s, sym=True)
    # an opening round each shoulder joint, cut through the chest, back and underarm armour
    for o in (cu, ba): armhole(o)
    hull("spine_box", [(0.036, 0.20, 1.60), (0.036, 0.236, 1.585), (0.036, 0.238, 1.44), (0.036, 0.20, 1.42)], P, b, s, sym=True)
    box("spine_light", (-0.004, 0.236, 1.47), (0.004, 0.241, 1.555), "Glow", b, s, bevel=0.0)
    box("back_pack", (-0.052, 0.14, 1.295), (0.052, 0.205, 1.385), P, b, s)
    for i, t in enumerate((0.275, 0.325)):
        patch("backrib%d" % i, c, [(228 + 5 * i, t), (312 - 5 * i, t)], [(232 + 5 * i, t + 0.04), (308 - 5 * i, t + 0.04)], S, b, s,
              nu=20, nv=4, lift=0.003, thick=0.011, edge=E)
    # the gorget: the skeleton's chest block rises to a ridge under the neck (164.5 cm) and carries a plate over
    # each shoulder housing (159.3 cm, out to x 16.7); this sits 8 mm over both
    hull("gorget", [(0.0, 0.035, 1.656), (0.0, 0.070, 1.652), (0.092, 0.036, 1.641), (0.092, 0.085, 1.637),
                    (0.119, 0.040, 1.636), (0.118, 0.088, 1.629), (0.0, 0.100, 1.642), (0.0, -0.047, 1.621),
                    (0.093, -0.046, 1.620), (0.115, -0.034, 1.612), (0.176, -0.010, 1.603), (0.176, 0.088, 1.603),
                    (0.176, -0.010, 1.57), (0.176, 0.088, 1.57), (0.0, -0.10, 1.57), (0.10, -0.085, 1.565),
                    (0.0, 0.135, 1.575), (0.10, 0.125, 1.57)], S, b, s, sym=True, bevel=0.0)
    ball("shoulder_ball", SHO, 0.05, F, "clavicle_l", s)
    for r, hh, role in ((0.044, 0.026, S), (0.032, 0.032, F), (0.018, 0.036, S)):
        cyl("shoulder_ring", (SHO[0] - 0.012, SHO[1] - 0.012, SHO[2] - 0.004), (0, 1, 0), r, hh, role, "clavicle_l", s, n=12)
    b = "spine_01"
    a = Form((0, 0.015, 1.245), (0, 0.006, 1.08), [
        (0.00, 0.104, 0.104, 0.082, 0.078), (0.08, 0.090, 0.090, 0.074, 0.072), (0.165, 0.104, 0.104, 0.080, 0.078)], c=0.4, b=0.012, soft=1.5)
    tube("abdomen", a, 0.0, 0.165, F, b, s, nt=8, nth=40, lift=-0.003)
    for i, t in enumerate((0.0, 0.046, 0.092)):
        patch("ab%d" % i, a, [(52, t + 0.004), (88.4, t + 0.01)], [(56, t + 0.04), (88.4, t + 0.046)], S, b, s,
              nu=10, nv=4, lift=0.005, thick=0.011, edge=E, mirror=True)
        patch("flank%d" % i, a, [(-34, t + 0.008), (40, t)], [(-34, t + 0.04), (44, t + 0.034)], S, b, s,
              nu=12, nv=4, lift=0.003, thick=0.009, edge=E, mirror=True)
    patch("lumbar", a, [(244, 0.0), (296, 0.0)], [(250, 0.15), (290, 0.15)], S, b, s, nu=10, nv=10, lift=0.005, thick=0.012, edge=E)
    b = "pelvis"
    p = Form((0, 0.008, 1.135), (0, 0.0, 0.93), [
        (0.000, 0.114, 0.114, 0.086, 0.084),
        (0.040, 0.130, 0.130, 0.096, 0.096),
        (0.095, 0.126, 0.126, 0.096, 0.098),
        (0.140, 0.080, 0.080, 0.084, 0.088),
        (0.205, 0.044, 0.044, 0.058, 0.060)], c=0.4, b=0.012, soft=1.5)
    tube("pelvis_under", p, 0.0, 0.205, F, b, s, nt=14, nth=44, lift=-0.006)
    patch("belt_front", p, [(46, 0.0), (134, 0.0)], [(54, 0.07), (74, 0.10), (106, 0.10), (126, 0.07)], P, b, s,
          nu=24, nv=8, lift=0.01, thick=0.018, edge=E)
    patch("belt_back", p, [(226, 0.0), (314, 0.0)], [(234, 0.08), (306, 0.08)], P, b, s, nu=24, nv=8, lift=0.01, thick=0.018, edge=E)
    patch("hip_guard", p, [(-46, -0.004), (40, -0.004)], [(-40, 0.15), (-18, 0.18), (14, 0.18), (34, 0.13)], P, b, s,
          nu=20, nv=14, lift=0.022, thick=0.02, edge=E, mirror=True)
    patch("cod", p, [(66, 0.105), (114, 0.105)], [(78, 0.2), (102, 0.2)], S, b, s, nu=12, nv=10, lift=0.006, thick=0.012, edge=E)
    patch("seat", p, [(240, 0.085), (300, 0.085)], [(256, 0.198), (284, 0.198)], S, b, s, nu=12, nv=10, lift=0.006, thick=0.012, edge=E)
    ball("hip_ball", HIP, 0.058, F, b, s, mirror=True)


def arms():
    s = "arms"
    b = "upperarm_l"
    # the pauldron, to Will's sketch (reference/pauldron_sketch.jpg): a raised band over the shoulder -- a top
    # facet sloping down outward, an outer facet leaning out toward the bottom (with the square cutout) and a
    # short bottom facet -- and two facets each side falling away from the band to a point at the front and back.
    # Hard edges, no bevels. Points are (x, forward of the joint, z).
    T1, T2, T5 = (0.1815, 0.040, 1.608), (0.2926, 0.040, 1.584), (0.327, 0.040, 1.4795)
    TIP, IN, BOT = (0.2566, 0.088, 1.542), (0.191, 0.088, 1.556), (0.306, 0.065, 1.464)
    # built as plates (only the facets Will drew): open underneath and toward the neck, so no closing faces
    d = np.array(T5) - np.array(T2); d[1] = 0; d /= np.linalg.norm(d); nrm = np.array((-d[2], 0, d[0]))       # down the outer facet, and out of it
    for sx, bone in ((1, "upperarm_l"), (-1, "upperarm_r")):
        W = lambda q, sg=1: (sx * q[0], SHO[1] - sg * q[1], q[2])
        X = lambda q: (sx * q[0], q[1], q[2])
        C0 = (sx * (SHO[0] - 0.01), SHO[1], SHO[2] - 0.03)
        sides = [[W(T1, g), W(T2, g), W(TIP, g), W(IN, g)] for g in (1, -1)] + [[W(T2, g), W(T5, g), W(BOT, g), W(TIP, g)] for g in (1, -1)]
        solid("pauldron", sides + [[W(T1), W(T2), W(T2, -1), W(T1, -1)], [W(T5), W(BOT), W(BOT, -1), W(T5, -1)]], P, bone, s, inset=0.016, centre=C0)
        face = solid("pauldron_face", [[W(T2), W(T5), W(T5, -1), W(T2, -1)]], P, bone, s, inset=0.016, centre=C0)
        at = lambda z_, v_, o_: X(tuple(np.array((T2[0], SHO[1] - v_, T2[2])) + d * ((T2[2] - z_) / -d[2]) + nrm * o_))
        rc = lambda z0, z1, hv, o0, o1: [at(z_, v_, o_) for z_ in (z0, z1) for v_ in (-hv, hv) for o_ in (o0, o1)]
        recess(face, rc(1.5728, 1.5213, 0.031, -0.010, 0.05))
        hull("pauldron_recess", rc(1.5768, 1.5173, 0.035, -0.022, -0.0095), S, bone, s, bevel=0.0, mirror=False)
    u = Form(SHO, ELB, [
        (0.030, 0.046, 0.044, 0.050, 0.050),
        (0.140, 0.056, 0.048, 0.060, 0.058),
        (0.240, 0.048, 0.042, 0.052, 0.052),
        (0.285, 0.040, 0.038, 0.042, 0.042)], **BOX)
    tube("upperarm_under", u, 0.03, 0.285, F, b, s, nt=8, nth=32, lift=-0.007)
    patch("bicep", u, [(-116, 0.13), (116, 0.13)], [(-116, 0.24), (-50, 0.258), (50, 0.258), (116, 0.24)], P, b, s, nu=32, nv=10, lift=0.004, thick=0.014, edge=E)
    patch("upperarm_in", u, [(136, 0.15), (224, 0.15)], [(136, 0.24), (224, 0.24)], S, b, s, nu=12, nv=6, edge=E)
    b = "lowerarm_l"
    joint("elbow", ELB, u.o, 0.04, 0.034, b, s, n=16)
    f = Form(ELB, WRI, [
        (0.020, 0.046, 0.040, 0.048, 0.050),
        (0.070, 0.068, 0.048, 0.064, 0.068),
        (0.190, 0.056, 0.042, 0.054, 0.054),
        (0.250, 0.042, 0.036, 0.042, 0.042),
        (0.285, 0.035, 0.033, 0.036, 0.036)], c=[0.3, 0.22, 0.22, 0.3], b=0.012, soft=1.5)
    tube("forearm_under", f, 0.02, 0.285, F, b, s, nt=10, nth=32, lift=-0.007)
    patch("gauntlet", f, [(-124, 0.05), (-60, 0.03), (60, 0.03), (124, 0.05)], [(-124, 0.225), (-50, 0.25), (50, 0.25), (124, 0.225)], P, b, s,
          nu=40, nv=14, lift=0.004, thick=0.015, edge=E)
    patch("gauntlet_top", f, [(-34, 0.05), (34, 0.05)], [(-26, 0.20), (26, 0.20)], P, b, s, nu=10, nv=12, lift=0.013, thick=0.012, edge=E)
    strip("gauntlet_light", f, -1.5, 1.5, 0.10, 0.17, b, s, lift=0.0145)
    patch("forearm_in", f, [(140, 0.06), (220, 0.06)], [(146, 0.225), (214, 0.225)], S, b, s, nu=10, nv=8, edge=E)
    tube("cuff", f, 0.258, 0.288, S, b, s, nt=1, nth=24, lift=0.005, bevel=0.002)


def legs():
    s = "legs"
    b = "thigh_l"
    t = Form(HIP, KNEE, [
        (-0.030, 0.064, 0.050, 0.074, 0.074),
        (0.000, 0.076, 0.058, 0.086, 0.084),
        (0.100, 0.094, 0.064, 0.104, 0.094),
        (0.250, 0.080, 0.058, 0.088, 0.078),
        (0.350, 0.058, 0.047, 0.066, 0.058),
        (0.400, 0.048, 0.044, 0.054, 0.052)], c=[(0.34, 0.3), 0.24, 0.24, 0.3], b=0.012, soft=1.5)
    tube("thigh_under", t, 0.0, 0.40, F, b, s, nt=14, nth=40, lift=-0.008)
    patch("thigh_front", t, [(34, 0.03), (150, 0.07)], [(40, 0.32), (70, 0.36), (110, 0.36), (150, 0.31)], P, b, s,
          nu=30, nv=18, lift=0.004, thick=0.016, edge=E)
    patch("thigh_side", t, [(-70, 0.06), (26, 0.0)], [(-70, 0.27), (-40, 0.31), (0, 0.31), (30, 0.28)], P, b, s,
          nu=26, nv=16, lift=0.01, thick=0.018, edge=E)
    patch("thigh_back", t, [(176, 0.085), (282, 0.07)], [(180, 0.30), (278, 0.30)], P, b, s, nu=24, nv=14, edge=E)
    b = "calf_l"
    joint("knee", KNEE, (1, 0, 0), 0.044, 0.043, b, s, n=16)
    kx, ky, kz = KNEE
    hull("kneecap", [(kx + q * 0.034, ky - 0.05, kz + z) for q in (-1, 1) for z in (0.046, -0.02)] +
         [(kx + q * 0.024, ky - 0.06, kz + z) for q in (-1, 1) for z in (0.03, -0.012)] +
         [(kx + q * 0.02, ky - 0.046, kz - 0.056) for q in (-1, 1)] +
         [(kx + q * 0.034, ky - 0.026, kz + z) for q in (-1, 1) for z in (0.046, -0.03)], P, b, s, bevel=0.003)
    c = Form(KNEE, ANK, [
        (0.030, 0.052, 0.046, 0.056, 0.056),
        (0.110, 0.068, 0.056, 0.070, 0.090),
        (0.290, 0.058, 0.048, 0.062, 0.064),
        (0.400, 0.044, 0.038, 0.050, 0.044),
        (0.458, 0.036, 0.034, 0.040, 0.038)], c=[0.3, 0.3, 0.24, 0.24], b=0.012, soft=1.5)
    tube("calf_under", c, 0.03, 0.458, F, b, s, nt=14, nth=36, lift=-0.007)
    patch("shin", c, [(34, 0.07), (146, 0.07)], [(44, 0.37), (70, 0.41), (110, 0.41), (136, 0.37)], P, b, s, nu=28, nv=18, lift=0.006, thick=0.016, edge=E)
    patch("shin_side", c, [(-52, 0.08), (26, 0.06)], [(-44, 0.33), (26, 0.35)], P, b, s, nu=16, nv=14, lift=0.003, thick=0.013, edge=E)
    patch("shin_in", c, [(154, 0.08), (214, 0.09)], [(154, 0.34), (206, 0.32)], S, b, s, nu=12, nv=14, edge=E)
    strip("shin_light", c, -8, -5.4, 0.20, 0.275, b, s, lift=0.0045)
    patch("calf_back", c, [(232, 0.06), (304, 0.06)], [(244, 0.29), (292, 0.29)], P, b, s, nu=16, nv=14, lift=0.004, thick=0.014, edge=E)
    b = "foot_l"
    joint("ankle", ANK, (1, 0, 0), 0.034, 0.037, b, s, n=16)
    f = Form((0.157, 0.125, 0.003), (0.157, -0.228, 0.003), [
        (0.000, 0.044, 0.044, 0.085, 0.002),
        (0.040, 0.050, 0.050, 0.135, 0.002),
        (0.090, 0.052, 0.052, 0.150, 0.002),
        (0.150, 0.054, 0.054, 0.105, 0.002),
        (0.220, 0.058, 0.058, 0.076, 0.002),
        (0.310, 0.058, 0.058, 0.060, 0.002),
        (0.345, 0.052, 0.052, 0.046, 0.002),
        (0.353, 0.046, 0.046, 0.036, 0.002)], front=(0, 0, 1), c=[(0.24, 0.3), (0.24, 0.3), 0.02, 0.02], b=0.01, soft=1.5)
    tube("foot", f, 0.0, 0.353, S, b, s, nt=18, nth=40, lift=-0.002, bevel=0.003)
    patch("toe", f, [(4, 0.27), (176, 0.27)], [(4, 0.35), (176, 0.35)], P, b, s, nu=24, nv=6, lift=0.006, thick=0.011, edge=E)
    patch("instep", f, [(4, 0.16), (176, 0.16)], [(4, 0.262), (176, 0.262)], P, b, s, nu=24, nv=8, lift=0.009, thick=0.012, edge=E)
    patch("heel", f, [(4, 0.0), (176, 0.0)], [(4, 0.075), (176, 0.075)], P, b, s, nu=24, nv=6, lift=0.006, thick=0.011, edge=E)
    patch("ankle_guard", f, [(2, 0.082), (54, 0.082)], [(2, 0.15), (44, 0.15)], P, b, s, nu=10, nv=6, lift=0.005, thick=0.01, edge=E, mirror=True)
    patch("ankle_guard_in", f, [(126, 0.082), (178, 0.082)], [(136, 0.15), (178, 0.15)], P, b, s, nu=10, nv=6, lift=0.005, thick=0.01, edge=E, mirror=True)


head(); core(); arms(); legs()
parts = K.finish("Shell2")
for c_ in CUTTERS: bpy.data.objects.remove(c_, do_unlink=True)


def borrow_neck_pistons(head):
    """The neck pistons are the base robot's own: its five piston rods (skinned to the head) copied in."""
    import bmesh
    body = bpy.data.objects["SK_RobotFrame"]
    o = body.copy(); o.data = body.data.copy(); bpy.context.scene.collection.objects.link(o)
    mi = [i for i, m in enumerate(o.data.materials) if m and m.name.startswith("M_Robot_02")]
    bm = bmesh.new(); bm.from_mesh(o.data)
    keep = set()
    for f in bm.faces:
        c = f.calc_center_median()
        if f.material_index in mi and 150 < c.z < 180 and abs(c.x) < 9 and -8 < c.y < 10:
            keep.update(f.verts)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v not in keep], context="VERTS")
    bm.to_mesh(o.data); bm.free()
    o.data.materials.clear(); o.data.materials.append(K.material("Frame"))
    for p_ in o.data.polygons: p_.material_index = 0
    for m in list(o.modifiers): o.modifiers.remove(m)
    o.hide_set(False); o.hide_render = False
    K.log("neck pistons", len(o.data.vertices), "vertices")
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True); head.select_set(True); bpy.context.view_layer.objects.active = head
    bpy.ops.object.join()


borrow_neck_pistons(parts["head"])
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, TAG + "_claude.blend"))

if "--norender" not in sys.argv:
    cam, lights = K.studio()
    R = os.path.join(OUT, "renders", "shell2_")
    views = {"three_q": (-0.62, -1, 0.12), "rear_q": (0.7, 1, 0.15), "front": (0, -1, 0), "back": (0, 1, 0), "side": (1, 0, 0)}
    for n, d in views.items():
        K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 100), span=215)
    K.pose([("spine_02", (1, 0, 0), 10), ("head", (0, 0, 1), -20),
            ("thigh_l", (1, 0, 0), -40), ("calf_l", (1, 0, 0), 55), ("thigh_r", (1, 0, 0), 18), ("calf_r", (1, 0, 0), 25),
            ("upperarm_r", (1, 0, 0), -65), ("lowerarm_r", (1, 0, 0), -45), ("upperarm_l", (1, 0, 0), -35), ("lowerarm_l", (1, 0, 0), -80)])
    K.shoot(R + "pose.png", cam, lights, (-0.8, -1, 0.15), target=(0, 0, 100), span=215)
    K.unpose()
    K.sheet([R + n + ".png" for n in list(views) + ["pose"]], 6, R + "sheet.png")
