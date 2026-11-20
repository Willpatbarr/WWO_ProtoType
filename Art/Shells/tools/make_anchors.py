# Graft anchors and paths for every shell part, written as JSON for the game (Config/ShellAnchors).
#   Blender -b base/robot_frame_v2_rigged.blend -P tools/make_anchors.py -- base   SK_Shell0_* + SK_RobotSkeleton (seams)
#   Blender -b shell1_claude.blend -P tools/make_anchors.py -- 1                   SK_Shell1_*
#   Blender -b shell2_claude.blend -P tools/make_anchors.py -- 2                   SK_Shell2_*
# Options after the shell: --norender (JSON only), --wrap (review renders show the wraps, not the parts).
# Review renders: renders/anchors/shell<N>_sheet.png. The vocabulary is in anchor_spec.py.
#
# Each part gets a temporary closed 'wrap' (the part plus the skeleton pieces on the same bones, voxelised,
# closed over its gaps and smoothed: a shrink-wrap). Anchors are ray hits on the wrap; paths are shortest
# lines over it. Space: the rig file's, in cm (x = the robot's left, -y = front, z = up), bind pose.
# Each part's JSON also carries 'surface': the wrap itself, thinned to one point per SURFACE_CELL cm cube (with
# its normal and bone), so the game can put things on the part between its anchors and paths (FWWOShellSurface),
# its keep-clear 'volumes' (the head's face; the same names on every part of a slot) and its optional
# 'obstacles' (thin bits standing out of the wrap, such as an antenna; only the parts that have them).
import bpy, bmesh, sys, os, json, math, heapq, importlib, re
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import shell_split as SS, anchor_spec as SPEC
importlib.reload(SS); importlib.reload(SPEC)

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else ["base"]
WHICH = ARGS[0]
SHELL = 0 if WHICH == "base" else int(WHICH)
SHELLS = os.path.dirname(TOOLS)
REPO = os.path.dirname(os.path.dirname(SHELLS))
OUT = os.path.join(REPO, "Config", "ShellAnchors")
RENDERS = os.path.join(SHELLS, "renders", "anchors")
os.makedirs(OUT, exist_ok=True); os.makedirs(RENDERS, exist_ok=True)
WARN = []
RELAX = 120          # string-pull iterations on the 1 cm path points
SURFACE_CELL = 2.0   # cm: the thinned wrap written as 'surface'


def log(*a): print("WWO", *a, flush=True)


def warn(*a):
    s = " ".join(str(x) for x in a); WARN.append(s); log("WARNING", s)


def unit(v):
    v = np.asarray(v, float); n = np.linalg.norm(v); return v / n if n > 1e-9 else v


rig = bpy.data.objects["Armature"]
BONES = {b.name: (np.array(b.head_local), np.array(b.tail_local)) for b in rig.data.bones}
WORDS = {"front": (0, -1, 0), "back": (0, 1, 0), "left": (1, 0, 0), "right": (-1, 0, 0), "up": (0, 0, 1),
         "down": (0, 0, -1), "outer": (1, 0, 0), "inner": (-1, 0, 0)}


def words(d):
    """A direction in robot terms (left-side meaning: outer = +X) as a vector."""
    if isinstance(d, str): d = {d: 1.0}
    return sum(np.array(WORDS[k], float) * w for k, w in d.items())


def swap(s):
    return re.sub(r"_(l|r)\b", lambda m: "_r" if m.group(1) == "l" else "_l", s)


def mirror_spec(x):
    """The right-side twin of a left-side spec entry (names and bones swapped, directions mirrored)."""
    if isinstance(x, str): return swap(x)
    if isinstance(x, list): return [mirror_spec(v) for v in x]
    if isinstance(x, dict):
        y = {k: mirror_spec(v) for k, v in x.items()}
        if "bone" in x: y["_mirror"] = not x.get("_mirror", False)
        return y
    return x


def expand(entries):
    """name -> spec, with every sided entry's _r twin added (and a _l counterpart for '_r' names)."""
    out = {}
    for name, e in entries.items():
        out[name] = dict(e, _mirror=False)
        if e.get("sided"):
            out[swap(name)] = mirror_spec(dict(e, _mirror=False))
    return out


# ---------- the wrap ----------
def mesh_tris(o):
    """World vertices, triangles and each triangle's bone, for an object at the identity in bind pose."""
    me = o.data; me.calc_loop_triangles()
    V = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", V); V = V.reshape(-1, 3)
    M = np.array(o.matrix_world); V = V @ M[:3, :3].T + M[:3, 3]
    T = np.empty(len(me.loop_triangles) * 3, int); me.loop_triangles.foreach_get("vertices", T); T = T.reshape(-1, 3)
    P = np.empty(len(me.loop_triangles), int); me.loop_triangles.foreach_get("polygon_index", P)
    fb = SS.face_bones(o)
    return V, T, np.array([fb[p] for p in P])


def sample(V, T, step):
    """Points covering the triangles, no further apart than step."""
    A, B, C = V[T[:, 0]], V[T[:, 1]], V[T[:, 2]]
    L = np.maximum(np.maximum(np.linalg.norm(B - A, axis=1), np.linalg.norm(C - B, axis=1)), np.linalg.norm(A - C, axis=1))
    K = np.clip(np.ceil(L / step), 1, 200).astype(int)
    pts = [V[np.unique(T)]]
    for k in np.unique(K):
        s = K == k
        ij = np.array([(i, j) for i in range(k + 1) for j in range(k + 1 - i)], float) / k
        a, b, c = A[s][:, None], B[s][:, None], C[s][:, None]
        pts.append((a + (b - a) * ij[None, :, :1] + (c - a) * ij[None, :, 1:]).reshape(-1, 3))
    return np.concatenate(pts)


def shift_or(dst, src, d):
    """dst |= src shifted by the integer offset d (no wrap-around)."""
    sl_d, sl_s = [], []
    for k in d:
        if k >= 0: sl_d.append(slice(k, None)); sl_s.append(slice(0, -k if k else None))
        else: sl_d.append(slice(0, k)); sl_s.append(slice(-k, None))
    dst[tuple(sl_d)] |= src[tuple(sl_s)]


def dilate(S, offs):
    D = S.copy()
    for d in offs: shift_or(D, S, d)
    return D


def closed_solid(pts, vox, close):
    """A voxel solid: the cells the points touch, closed over gaps up to ~2*close and with holes filled."""
    r = close / vox; R = int(math.ceil(r))
    pad = R + 3
    lo = (np.floor(pts.min(0) / vox) - pad) * vox       # on a lattice through x = 0: left and right voxelise alike
    idx = np.floor((pts - lo) / vox).astype(int)
    shape = tuple(idx.max(0) + pad + 1)
    S = np.zeros(shape, bool); S[idx[:, 0], idx[:, 1], idx[:, 2]] = True
    ball = [(i, j, k) for i in range(-R, R + 1) for j in range(-R, R + 1) for k in range(-R, R + 1)
            if 0 < i * i + j * j + k * k <= r * r + 1e-6]
    six = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    D = dilate(S, ball)
    E = np.zeros(shape, bool)                       # the outside: flood from the box's border
    for ax in range(3):
        for end in (0, -1):
            sl = [slice(None)] * 3; sl[ax] = end; E[tuple(sl)] = ~D[tuple(sl)]
    while True:
        n = dilate(E, six) & ~D
        if n.sum() == E.sum(): break
        E = n
    F = ~dilate(E, ball)                            # erode back by the same ball
    return F, lo


def blocks(F, lo, vox):
    """The voxel solid's outer faces as a quad mesh (vertices, faces), wound outward."""
    nx, ny, nz = F.shape
    P = np.zeros((nx + 2, ny + 2, nz + 2), bool); P[1:-1, 1:-1, 1:-1] = F
    quads = []
    corner = {0: [(0, 0, 0), (0, 1, 0), (0, 1, 1), (0, 0, 1)], 1: [(0, 0, 0), (0, 0, 1), (1, 0, 1), (1, 0, 0)],
              2: [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]}
    for ax in range(3):
        a = [slice(None)] * 3; b = [slice(None)] * 3
        a[ax] = slice(0, -1); b[ax] = slice(1, None)
        A, B = P[tuple(a)], P[tuple(b)]
        for sgn, m in ((1, A & ~B), (-1, B & ~A)):          # solid then empty along +ax: the face looks +ax
            c = np.argwhere(m)                               # face at the boundary plane index c[ax]+1 (padded) => c[ax] (grid)
            base = c - 1; base[:, ax] += 1
            cs = corner[ax] if sgn > 0 else corner[ax][::-1]
            quads.append(np.stack([base + np.array(k) for k in cs], axis=1))
    Q = np.concatenate(quads)                                # (n, 4, 3) grid corner coords
    flat = Q.reshape(-1, 3)
    keys, inv = np.unique(flat, axis=0, return_inverse=True)
    V = lo + keys * vox
    return V, inv.reshape(-1, 4)


def make_wrap(name, pts, vox, close, iters, lam):
    F, lo = closed_solid(pts, vox, close)
    V, Q = blocks(F, lo, vox)
    me = bpy.data.meshes.new(name); me.from_pydata(V.tolist(), [], Q.tolist())
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    m = o.modifiers.new("remesh", "REMESH"); m.mode = "VOXEL"; m.voxel_size = vox
    m = o.modifiers.new("smooth", "LAPLACIANSMOOTH"); m.iterations = iters; m.lambda_factor = lam
    m.use_volume_preserve = True; m.use_normalized = True
    dg = bpy.context.evaluated_depsgraph_get()
    me2 = bpy.data.meshes.new_from_object(o.evaluated_get(dg), depsgraph=dg)
    bpy.data.objects.remove(o, do_unlink=True); bpy.data.meshes.remove(me)
    # keep the biggest connected piece
    bm = bmesh.new(); bm.from_mesh(me2); bm.verts.ensure_lookup_table()
    comp, seen = [], set()
    for v0 in bm.verts:
        if v0.index in seen: continue
        st = [v0]; seen.add(v0.index); cs = []
        while st:
            v = st.pop(); cs.append(v)
            for e in v.link_edges:
                w = e.other_vert(v)
                if w.index not in seen: seen.add(w.index); st.append(w)
        comp.append(cs)
    comp.sort(key=len, reverse=True)
    if len(comp) > 1:
        log(name, "dropping", len(comp) - 1, "small wrap islands of", [len(c) for c in comp[1:6]], "verts")
        bmesh.ops.delete(bm, geom=[v for c in comp[1:] for v in c], context="VERTS")
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(me2); bm.free()
    return me2


class Wrap:
    def __init__(self, name, me, src_V, src_T, src_B, bones):
        self.name, self.me = name, me
        n = len(me.vertices)
        V = np.empty(n * 3); me.vertices.foreach_get("co", V); self.V = V.reshape(-1, 3)
        N = np.empty(n * 3); me.vertices.foreach_get("normal", N); self.N = N.reshape(-1, 3)
        T = np.empty(len(me.polygons) * 3, int); me.polygons.foreach_get("vertices", T); self.T = T.reshape(-1, 3)
        self.bvh = BVHTree.FromPolygons(self.V.tolist(), self.T.tolist())
        self.kd = KDTree(n)
        for i, v in enumerate(self.V): self.kd.insert(v, i)
        self.kd.balance()
        E = np.empty(len(me.edges) * 2, int); me.edges.foreach_get("vertices", E); E = E.reshape(-1, 2)
        n1 = [set() for _ in range(n)]
        for a, b in E: n1[a].add(b); n1[b].add(a)
        self.adj = []
        for i in range(n):                              # 2-ring neighbours: finer directions for Dijkstra
            s = set(n1[i])
            for j in n1[i]: s |= n1[j]
            s.discard(i); s = list(s)
            d = np.linalg.norm(self.V[s] - self.V[i], axis=1)
            self.adj.append(list(zip(s, d.tolist())))
        # each wrap vertex's bone: the nearest source face on one of this piece's bones
        keep = np.isin(src_B, list(bones))
        sb = BVHTree.FromPolygons(src_V.tolist(), src_T[keep].tolist())
        tb = src_B[keep]
        self.B = []
        for v in self.V:
            hit = sb.find_nearest(Vector(v))
            self.B.append(str(tb[hit[2]]))
        self.B = np.array(self.B)
        log(name, "wrap verts", n, "bones", {b: int((self.B == b).sum()) for b in sorted(set(self.B))})

    def normal(self, p):
        hits = self.kd.find_n(Vector(p), 6)
        w = np.array([1.0 / (h[2] + 0.3) for h in hits]); i = [h[1] for h in hits]
        return unit((self.N[i] * w[:, None]).sum(0))

    def bone(self, p): return str(self.B[self.kd.find(Vector(p))[1]])

    def nearest(self, p):
        return np.array(self.bvh.find_nearest(Vector(p))[0])

    def vert(self, p): return self.kd.find(Vector(p))[1]

    def dijkstra(self, s, t):
        if s == t: return [s]
        dist = {s: 0.0}; prev = {}; h = [(0.0, s)]; done = set()
        T = self.V[t]
        while h:
            d, u = heapq.heappop(h)
            if u in done: continue
            done.add(u)
            if u == t: break
            for v, w in self.adj[u]:
                nd = d + w
                if nd < dist.get(v, 1e18):
                    dist[v] = nd; prev[v] = u; heapq.heappush(h, (nd, v))
        if t not in prev: raise RuntimeError("%s: no path %d -> %d" % (self.name, s, t))
        out = [t]
        while out[-1] != s: out.append(prev[out[-1]])
        return out[::-1]


# ---------- anchors ----------
def target(spec):
    h, t_ = BONES[spec["bone"]]
    p = h + (t_ - h) * spec["t"]
    if spec.get("off"):
        o = words(spec["off"]); o[0] *= -1 if spec.get("_mirror", False) else 1; p = p + o
    return p


def ray_dir(spec):
    d = words(spec["dir"])
    if spec.get("_mirror", False): d[0] = -d[0]
    if spec.get("perp"):
        h, t_ = BONES[spec["bone"]]; a = unit(t_ - h)
        d = d - a * d.dot(a)
    return unit(d)


def place(w, spec, label):
    """Ray-cast a spec onto the wrap: the hit point."""
    c = target(spec); d = ray_dir(spec)
    hit = w.bvh.ray_cast(Vector(c + d * 150), Vector(-d), 300)
    if hit[0] is None:
        p = w.nearest(c); warn("%s %s: ray missed, using the nearest wrap point" % (w.name, label))
    else:
        p = np.array(hit[0])
        if np.linalg.norm(p - c) > 35: warn("%s %s: hit %.1f cm from its target" % (w.name, label, np.linalg.norm(p - c)))
    return p


def size_at(w, p, n):
    """Roughly the radius of open, fairly flat wrap round p (clamped 2..20 cm)."""
    near = sorted(w.kd.find_range(Vector(p), 22.0), key=lambda h: h[2])
    r = 20.0
    for co, i, d in near:
        if d < 1.0: continue
        dev = abs(np.dot(np.array(co) - p, n))
        if w.N[i].dot(n) < math.cos(math.radians(32)) or dev > 0.6 + 0.12 * d:
            r = d; break
    return float(min(max(r, 2.0), 20.0))


def up_at(p, n, spec, limb):
    mode = spec.get("up", "limb" if limb else "z")
    if mode == "limb":
        h, t_ = BONES[spec["bone"]]; v = unit(h - t_)
    elif mode == "z": v = np.array([0.0, 0, 1])
    else:
        v = words(mode)
        if spec.get("_mirror", False): v[0] = -v[0]
    u = v - n * v.dot(n)
    if np.linalg.norm(u) < 0.25:                         # the surface faces along 'up': fall back to the front
        v = np.array([0.0, -1, 0]); u = v - n * v.dot(n)
    return unit(u)


def anchor(w, spec, name, limb):
    p = place(w, spec, name)
    n = w.normal(p)
    return dict(p=p, n=n, up=up_at(p, n, spec, limb), size=size_at(w, p, n), bone=w.bone(p))


def seams():
    out = {}
    for name, e in expand(SPEC.SEAMS).items():
        h, _ = BONES[e["bone"]]
        if e.get("off"):
            o = words(e["off"]); o[0] *= -1 if e["_mirror"] else 1; h = h + o
        n = words(e["n"])
        if e["_mirror"]: n[0] = -n[0]
        n = unit(n)
        up = unit(np.array([0, -1.0, 0]) if abs(n[2]) > 0.9 else np.array([0, 0, 1.0]) - n * n[2])
        out[name] = dict(p=h, n=n, up=up, size=4.0, bone=e["bone"])
    return out


# ---------- paths ----------
def resample(P, step, closed=False):
    P = np.asarray(P, float)
    seg = np.linalg.norm(np.diff(P, axis=0), axis=1); L = seg.sum()
    if L < 1e-6: return P[[0, -1]].copy(), 0.0
    s = np.concatenate([[0], np.cumsum(seg)])
    n = max(1, int(round(L / step)))
    q = np.linspace(0, L, n + 1)
    return np.stack([np.interp(q, s, P[:, k]) for k in range(3)], axis=1), L


def make_path(w, pts, closed, step):
    """A shortest line over the wrap through the waypoints pts (closed: back to the first)."""
    wp = list(pts) + ([pts[0]] if closed else [])
    vs = [w.vert(p) for p in wp]
    legs = []
    for k, (a, b) in enumerate(zip(vs, vs[1:])):
        L = w.V[w.dijkstra(a, b)].copy()
        L[0], L[-1] = wp[k], wp[k + 1]                    # exactly through the waypoints
        legs.append(L)
    # relax (string-pull, putting the points back on the wrap) coarse then fine, the waypoints pinned
    for spacing, iters in ((3.0, RELAX // 2), (1.0, RELAX)):
        line, pins = [], [0]
        for k, L in enumerate(legs):
            r, _ = resample(L, spacing)
            line.extend(r[1:] if k else r)
            pins.append(len(line) - 1)                      # the leg's end: the next waypoint
        line = np.array(line); pinned = np.zeros(len(line), bool); pinned[pins] = True
        for _ in range(iters):
            q = line.copy()
            q[1:-1] = 0.5 * line[1:-1] + 0.25 * (line[:-2] + line[2:])
            q[pinned] = line[pinned]
            line = np.array([w.nearest(p) for p in q])
        legs = [line[pins[k]:pins[k + 1] + 1] for k in range(len(pins) - 1)]
    # resample each leg on its own so every waypoint is a point of the line (a shared resampling would cut
    # past them on whichever side its phase falls: lopsided over ridges); legs under 1 cm join the next
    out = [legs[0][0]]; acc = [legs[0][0]]
    for k, L in enumerate(legs):
        acc.extend(L[1:])
        seg = np.linalg.norm(np.diff(np.array(acc), axis=0), axis=1).sum()
        if seg < 1.0 and k < len(legs) - 1: continue
        r, _ = resample(np.array(acc), step); out.extend(r[1:]); acc = [r[-1]]
    out = np.array([w.nearest(p) for p in out])
    if closed: out[-1] = out[0]
    L = float(np.linalg.norm(np.diff(out, axis=0), axis=1).sum())
    return dict(points=[dict(p=p, n=w.normal(p), bone=w.bone(p)) for p in out], length=L, closed=closed)


def ring_points(w, spec):
    h, t_ = BONES[spec["bone"]]; a = unit(t_ - h); c = h + (t_ - h) * spec["t"]
    f = np.array([0, -1.0, 0]); f = unit(f - a * f.dot(a)); o = np.cross(f, a)
    if o[0] < 0: o = -o
    if spec.get("_mirror", False):
        pass                                              # bones are mirrored already; keep the same front->outer order
    out = []
    for k in range(spec["n"]):
        th = 2 * math.pi * k / spec["n"]
        side = o * (-1 if spec.get("_mirror", False) else 1)
        d = unit(f * math.cos(th) + side * math.sin(th))
        hit = w.bvh.ray_cast(Vector(c + d * 150), Vector(-d), 300)
        if hit[0] is None:
            warn("%s ring %s: ray %d missed" % (w.name, spec["bone"], k)); out.append(w.nearest(c + d * 10))
        else: out.append(np.array(hit[0]))
    return out


# ---------- keep-clear volumes and obstacles ----------
def face_volume(w, A, spec):
    """The front of the head between the jaw and the brow (the wrap facing the front), as a half-ellipsoid over
    it: its flat side as deep as the face's deepest part, its round side reaching 'pad' cm out past the most
    forward bit (goggles, visor) and as wide and tall as the face plus 'pad' (rig cm: p the centre of its flat
    side, n the way out of the face, up, radii [across, up, out])."""
    lo = A["jaw_front"]["p"][2] - spec["lo"]; hi = A["head_front"]["p"][2] + spec["hi"]
    front = np.array([0, -1.0, 0])
    m = (w.N @ front > spec["facing"]) & (w.V[:, 2] >= lo) & (w.V[:, 2] <= hi)
    F = w.V[m]
    if len(F) < 10: warn("%s face: only %d wrap points face the front" % (w.name, len(F))); F = w.V[(w.V[:, 2] >= lo) & (w.V[:, 2] <= hi)]
    back = float(np.percentile(F[:, 1], 90))                 # the face's deepest part (y grows toward the back)
    cx = 0.5 * (F[:, 0].min() + F[:, 0].max()); cz = 0.5 * (F[:, 2].min() + F[:, 2].max())
    pad = spec["pad"]
    radii = [0.5 * (F[:, 0].max() - F[:, 0].min()) + pad, 0.5 * (F[:, 2].max() - F[:, 2].min()) + pad, max(back - F[:, 1].min(), 1.0) + pad]
    log(w.name, "face volume at (%.1f, %.1f, %.1f) radii %s from %d wrap points" % (cx, back, cz, [round(r, 1) for r in radii], len(F)))
    return dict(kind="half_ellipsoid", p=np.array([cx, back, cz]), n=front, up=np.array([0, 0, 1.0]), radii=radii, bone=w.bone(np.array([cx, F[:, 1].min(), cz])))


def find_obstacles(w, PV, PT, spec):
    """Thin bits of the part (its own triangles PV/PT) that the wrap only covers with a thin tube: the part's
    voxel solid less its opening by a ball of radius spec['thick'] / 2 (what a ball that big cannot reach into),
    in clumps at least spec['min_len'] cm long standing spec['out'] cm or more off what is left, each as a capsule
    {a, b, radius, bone} (rig cm)."""
    vox = SPEC.WRAP["voxel"]
    pts = sample(PV, PT, vox * 0.45)
    F, lo = closed_solid(pts, vox, SPEC.WRAP["close"])
    r = spec["thick"] * 0.5 / vox; R = int(math.ceil(r))
    ball = [(i, j, k) for i in range(-R, R + 1) for j in range(-R, R + 1) for k in range(-R, R + 1) if 0 < i * i + j * j + k * k <= r * r + 1e-6]
    O = dilate(~dilate(~F, ball), ball)                      # the opening: the solid without its thin bits
    # how far each voxel is from the opened solid (in voxels, up to 'out' + a bit)
    steps = int(math.ceil(spec["out"] / vox)) + 1
    six = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    near = O.copy()
    for _ in range(steps): near = dilate(near, six)
    idx = np.floor((pts - lo) / vox).astype(int)
    thin = F[idx[:, 0], idx[:, 1], idx[:, 2]] & ~near[idx[:, 0], idx[:, 1], idx[:, 2]]
    if not thin.any(): log(w.name, "obstacles: none"); return {}
    P = pts[thin]
    key = np.floor(P / 1.0).astype(int)
    cells = {}
    for i, k in enumerate(map(tuple, key)): cells.setdefault(k, []).append(i)
    seen, clumps = set(), []
    for k0 in cells:
        if k0 in seen: continue
        st = [k0]; seen.add(k0); ids = []
        while st:
            k = st.pop(); ids.extend(cells[k])
            for d in ((i, j, l) for i in (-1, 0, 1) for j in (-1, 0, 1) for l in (-1, 0, 1)):
                n = (k[0] + d[0], k[1] + d[1], k[2] + d[2])
                if n in cells and n not in seen: seen.add(n); st.append(n)
        clumps.append(P[ids])
    out = []
    for C in clumps:
        c = C.mean(0); _, _, vt = np.linalg.svd(C - c, full_matrices=False); ax = vt[0]
        s = (C - c) @ ax; a, b = c + ax * s.min(), c + ax * s.max()
        if s.max() - s.min() < spec["min_len"]:
            continue
        perp = np.linalg.norm((C - c) - np.outer(s, ax), axis=1)
        rad = float(perp.max()) + spec["pad"]
        # the end nearer the rest of the part runs on toward it by 'out', so the capsule reaches what it stands on
        on = lambda q: O[tuple(np.clip(np.floor((q - lo) / vox).astype(int), 0, np.array(O.shape) - 1))]
        ka = min(range(0, 40), key=lambda t: 0 if on(a - ax * t * 0.5) else 1)
        kb = min(range(0, 40), key=lambda t: 0 if on(b + ax * t * 0.5) else 1)
        if ka <= kb: a = a - ax * (spec["out"] + vox)
        else: b = b + ax * (spec["out"] + vox)
        out.append(dict(a=a, b=b, radius=rad, bone=w.bone(0.5 * (a + b)), length=float(np.linalg.norm(b - a)), points=len(C)))
    out.sort(key=lambda o: -o["length"])
    res = {}
    for k, o in enumerate(out):
        res["protrusion_%d" % (k + 1)] = o
        log(w.name, "obstacle protrusion_%d: %.1f cm long, radius %.1f, from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f), %d points" %
            ((k + 1), o["length"], o["radius"], *o["a"], *o["b"], o["points"]))
    log(w.name, "obstacles: %d thin clumps, %d kept (sizes %s)" % (len(clumps), len(out), sorted([len(c) for c in clumps], reverse=True)[:12]))
    return res


# ---------- the run ----------
PIECES = {"Head": [("Head", 0)], "Core": [("Core", 0)], "Arms": [("Arms_l", 1), ("Arms_r", -1)],
          "Legs": [("Legs_l", 1), ("Legs_r", -1)]}


def piece_bones(slot, side):
    bs = SS.slot_bones(slot)
    if side: bs = [b for b in bs if b.endswith("_l" if side > 0 else "_r")]
    return set(bs)


def run():
    parts = SS.game_parts(WHICH)
    SV, ST, SB = mesh_tris(parts["Skeleton"])
    seam = seams()
    results, wraps = {}, {}
    for slot in SS.SLOTS:
        PV, PT, PB = mesh_tris(parts[slot])
        anchors_spec = expand(SPEC.ANCHORS[slot]); paths_spec = expand(SPEC.PATHS[slot])
        A, Pa, Vo, Ob = {}, {}, {}, {}
        for pname, side in PIECES[slot]:
            bones = piece_bones(slot, side)
            pm = np.ones(len(PT), bool) if not side else (PV[PT].mean(1)[:, 0] * side) > 0
            sm = np.isin(SB, list(bones))
            # the part's triangles (this side) and the skeleton's on this piece's bones, in one vertex list
            V = np.concatenate([PV, SV]); T = np.concatenate([PT[pm], ST[sm] + len(PV)])
            tri_b = np.concatenate([PB[pm], SB[sm]])
            pts = sample(V, T, SPEC.WRAP["voxel"] * 0.45)
            me = make_wrap("wrap_" + pname, pts, SPEC.WRAP["voxel"], SPEC.WRAP["close"], SPEC.WRAP["smooth_iters"],
                           SPEC.WRAP["smooth_lambda"])
            w = Wrap(pname, me, V, T, tri_b, bones)
            wraps[pname] = w
            limb = slot in ("Arms", "Legs")
            mine = lambda name: (not side) or name.endswith("_l" if side > 0 else "_r")
            for name, e in sorted(anchors_spec.items()):
                if mine(name): A[name] = anchor(w, e, name, limb)
            for name, e in sorted(paths_spec.items()):
                if not mine(name): continue
                if e.get("ring"):
                    r = dict(e["ring"]); r.setdefault("_mirror", False)
                    wp = ring_points(w, r)
                else:
                    wp = []
                    for x in e["via"]:
                        if isinstance(x, dict): wp.append(place(w, x, name + " waypoint"))
                        elif x.startswith("@"): wp.append(w.nearest(seam[x[1:]]["p"]))
                        else: wp.append(A[x]["p"])
                path = make_path(w, wp, e["closed"], SPEC.STEP)
                path.update(from_seam=e["from_seam"], to_seam=e["to_seam"], kind=e["kind"])
                Pa[name] = path
                log(slot, name, "length %.1f points %d" % (path["length"], len(path["points"])))
            for name, e in SPEC.VOLUMES.get(slot, {}).items():
                if e["kind"] == "face": Vo[name] = face_volume(w, A, e)
            if slot in SPEC.OBSTACLES["slots"]:
                Ob.update(find_obstacles(w, PV, PT[pm], SPEC.OBSTACLES))
        part = "SK_Shell%d_%s" % (SHELL, slot)
        for name, o in SPEC.OBSTACLES_EXTRA.get(part, {}).items():
            Ob[name] = dict(a=np.array(o["a"], float), b=np.array(o["b"], float), radius=o["radius"], bone=o["bone"])
        results[slot] = (A, Pa, Vo, Ob)
    return parts, seam, results, wraps


def surface(ws):
    """The wraps thinned to one vertex per SURFACE_CELL cube (the one nearest its centre): (bones, rows)."""
    bones, rows = [], []
    for w in ws:
        key = np.floor(w.V / SURFACE_CELL).astype(int)
        off = np.linalg.norm(w.V / SURFACE_CELL - (key + 0.5), axis=1)
        order = np.lexsort((off, key[:, 2], key[:, 1], key[:, 0]))
        k = key[order]
        first = np.ones(len(order), bool); first[1:] = np.any(k[1:] != k[:-1], axis=1)
        for i in order[first]:
            b = str(w.B[i])
            if b not in bones: bones.append(b)
            rows.append([round(float(x), 1) for x in w.V[i]] + [round(float(x), 3) for x in unit(w.N[i])] + [bones.index(b)])
    return bones, rows


# ---------- JSON ----------
def r3(v): return [round(float(x), 3) for x in v]
def r4(v): return [round(float(x), 4) for x in v]


def dump(obj, ind=0):
    def flat(o):
        if isinstance(o, dict): return all(not isinstance(v, (dict, list)) or (isinstance(v, list) and all(not isinstance(x, (dict, list)) for x in v)) for v in o.values())
        if isinstance(o, list): return all(not isinstance(x, (dict, list)) for x in o)
        return True
    if flat(obj): return json.dumps(obj, sort_keys=True, separators=(",", ":"))
    pad = " " * (ind + 1)
    if isinstance(obj, dict):
        items = [pad + json.dumps(k) + ": " + dump(obj[k], ind + 1) for k in sorted(obj)]
        return "{\n" + ",\n".join(items) + "\n" + " " * ind + "}"
    return "[\n" + ",\n".join(pad + dump(x, ind + 1) for x in obj) + "\n" + " " * ind + "]"


def write(name, slot, anchors, paths, surf=None, volumes=None, obstacles=None):
    doc = {"part": name, "shell": SHELL if slot != "Skeleton" else None, "slot": slot, "space": "blender_rig_cm",
           "anchors": {k: dict(p=r3(a["p"]), n=r4(a["n"]), up=r4(a["up"]), size=round(float(a["size"]), 2), bone=a["bone"])
                       for k, a in anchors.items()},
           "paths": {k: dict(closed=bool(p["closed"]), length=round(p["length"], 2), from_seam=p["from_seam"], to_seam=p["to_seam"],
                             points=[dict(p=r3(q["p"]), n=r4(q["n"]), bone=q["bone"]) for q in p["points"]])
                     for k, p in paths.items()}}
    if volumes:
        doc["volumes"] = {k: dict(kind=v["kind"], p=r3(v["p"]), n=r4(v["n"]), up=r4(v["up"]), radii=[round(float(x), 2) for x in v["radii"]],
                                  bone=v["bone"]) for k, v in volumes.items()}
    if obstacles:
        doc["obstacles"] = {k: dict(kind="capsule", a=r3(o["a"]), b=r3(o["b"]), radius=round(float(o["radius"]), 2), bone=o["bone"])
                            for k, o in obstacles.items()}
    if surf:
        doc["surface"] = {"cell": SURFACE_CELL, "bones": surf[0], "points": surf[1]}
    path = os.path.join(OUT, name + ".json")
    with open(path, "w") as f: f.write(dump(doc) + "\n")
    log("wrote", path, os.path.getsize(path) // 1024, "KB")


# ---------- review renders ----------
def emit_mat(name, col, strength=1.5):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*col, 1)
    b.inputs["Emission Color"].default_value = (*col, 1); b.inputs["Emission Strength"].default_value = strength
    return m


def markers(points, mat, rad, stick=0.0, name="mk"):
    bm = bmesh.new()
    for p, n in points:
        g = bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=rad)
        bmesh.ops.translate(bm, vec=Vector(p), verts=g["verts"])
        if stick:
            g = bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=rad * 0.28, radius2=rad * 0.28, depth=stick)
            q = Vector((0, 0, 1)).rotation_difference(Vector(n)).to_matrix().to_4x4()
            bmesh.ops.transform(bm, matrix=q, verts=g["verts"])
            bmesh.ops.translate(bm, vec=Vector(p) + Vector(n) * (stick / 2), verts=g["verts"])
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); me.materials.append(mat)
    return o


def tube(pts, closed, mat, rad=0.38, name="path"):
    cu = bpy.data.curves.new(name, "CURVE"); cu.dimensions = "3D"; cu.bevel_depth = rad; cu.bevel_resolution = 2
    sp = cu.splines.new("POLY"); sp.points.add(len(pts) - 1)
    for k, p in enumerate(pts): sp.points[k].co = (*p, 1)
    sp.use_cyclic_u = False
    o = bpy.data.objects.new(name, cu); bpy.context.scene.collection.objects.link(o); cu.materials.append(mat)
    return o


def render(parts, seam, results, wraps, show_wraps):
    import shell_kit as K
    importlib.reload(K)
    if show_wraps:
        for o in parts.values(): o.hide_render = True
        grey = bpy.data.materials.new("WrapGrey"); grey.use_nodes = True
        grey.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.25, 0.25, 0.27, 1)
        for name, w in wraps.items():
            o = bpy.data.objects.new("W_" + name, w.me); bpy.context.scene.collection.objects.link(o)
            w.me.materials.append(grey)
        parts["Skeleton"].hide_render = False
    cols = {"anchor": (1.0, 0.8, 0.05), "line": (0.05, 0.75, 1.0), "ring": (1.0, 0.15, 0.75), "spiral": (0.2, 1.0, 0.25),
            "seam": (1.0, 0.08, 0.05)}
    mats = {k: emit_mat("AN_" + k, c) for k, c in cols.items()}
    allA = [(a["p"], a["n"]) for slot in results for a in results[slot][0].values()]
    markers(allA, mats["anchor"], 1.1, stick=4.0, name="anchors")
    markers([(s["p"], s["n"]) for s in seam.values()], mats["seam"], 1.6, stick=6.0, name="seams")
    for slot in results:
        for name, p in results[slot][1].items():
            tube([q["p"] for q in p["points"]], p["closed"], mats[p["kind"]], name=name)
    cam, lights = K.studio((640, 900))
    bpy.context.scene.view_settings.view_transform = "Standard"     # keep the marker colours saturated
    tag = "shell%d%s" % (SHELL, "_wrap" if show_wraps else "")
    R = os.path.join(RENDERS, tag + "_")
    views = {"front": (0, -1, 0), "back": (0, 1, 0), "side": (1, 0, 0), "three_q": (-0.62, -1, 0.12)}
    shots = []
    for zone, tgt, span in (("full", (0, 0, 96), 200), ("upper", (0, 0, 140), 100), ("lower", (0, 0, 55), 110)):
        for n, d in views.items():
            p = R + zone + "_" + n + ".png"; K.shoot(p, cam, lights, d, target=tgt, span=span); shots.append(p)
    K.sheet(shots, 4, R + "sheet.png")
    log("sheet", R + "sheet.png")


if __name__ == "__main__":
    parts, seam, results, wraps = run()
    for slot, (A, Pa, Vo, Ob) in results.items():
        write("SK_Shell%d_%s" % (SHELL, slot), slot, A, Pa, surface([wraps[p] for p, _ in PIECES[slot]]), Vo, Ob)
    if WHICH == "base":
        write("SK_RobotSkeleton", "Skeleton", seam, {})
    # seam joins in this file (the cross-shell check is check_anchors.py)
    for slot, (A, Pa, _, _) in results.items():
        for name, p in Pa.items():
            for key, end in (("from_seam", 0), ("to_seam", -1)):
                if p[key]:
                    d = np.linalg.norm(p["points"][end]["p"] - seam[p[key]]["p"])
                    log("seam join", slot, name, key, p[key], "%.2f cm" % d)
    if "--dump" in ARGS:                                  # the wraps' vertices, for fitting the seams (fit_seams.py)
        d = ARGS[ARGS.index("--dump") + 1]; os.makedirs(d, exist_ok=True)
        for k, w in wraps.items(): np.save(os.path.join(d, "shell%d_%s.npy" % (SHELL, k)), w.V)
    if "--norender" not in ARGS:
        render(parts, seam, results, wraps, "--wrap" in ARGS)
    log("warnings", len(WARN))
    for w in WARN: log("  ", w)
