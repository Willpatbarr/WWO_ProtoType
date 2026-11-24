# Shared toolkit for the Remnant enemies (BleakThorn swarm, CinderBloom soldier, IronBark tank): grown bodies of
# twisted wood, pierced bone plates, bark slabs, leaves and vines, skinned to one of the enemy rigs in base/.
# A model script opens its rig file, calls setup(), builds pieces with add() / add_side(), then finish() and deliver().
#
# Everything is in the rig's centimetres and space (faces -Y, its left is +X, up is Z). Pieces added with
# mirror=True are built on the LEFT and copied to the right; scattered dressing draws from RNG, which is seeded
# by setup(), so every build of a model is the same. Colour draws from its own dice (CRNG), so changing colour
# never moves a shape.
# Materials are roles (a name and a colour per model). Each material's colour comes from the vertex colours
# ("Col"): the role's colour, with every strand, leaf and thorn a slightly different shade that drifts along
# its surface. Roles listed in GLOW also emit that colour.
import bpy, math, os, sys
import numpy as np
from math import pi, cos, sin, radians
from mathutils import Matrix, Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K

SHELLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
RNG = np.random.default_rng()        # shapes; seeded by setup()
CRNG = np.random.default_rng()       # colour
COLORS, ROUGH, VARY, GLOW = {}, {}, {}, {}      # per role: linear colour, roughness, colour drift, emission strength
PARTS = []      # dict(v, f, mat, w {bone: weights}, mirror)
J = {}          # bone name -> head position
_S = {}
_WAVES = []


def setup(seed, colors, rough, vary, glow=None):
    """Start a model on the rig in the open file. vary: role -> (per piece value, per piece hue, along the surface)."""
    RNG.bit_generator.state = np.random.default_rng(seed).bit_generator.state
    CRNG.bit_generator.state = np.random.default_rng(5).bit_generator.state
    for d, src in ((COLORS, colors), (ROUGH, rough), (VARY, vary), (GLOW, glow or {})): d.clear(); d.update(src)
    PARTS.clear()
    _S["rig"] = rig = bpy.data.objects["Armature"]
    J.clear(); J.update({b.name: np.array(b.head_local) for b in rig.data.bones})
    _WAVES[:] = [(unit(CRNG.normal(0, 1, 3)) * f, CRNG.uniform(0, 6.3), a) for f, a in ((0.11, 0.5), (0.27, 0.3), (0.6, 0.2))]


def log(*a): print("WWO", *a, flush=True)
def A(*v): return np.array(v, float)
def unit(v):
    v = np.asarray(v, float); return v / (np.linalg.norm(v, axis=-1, keepdims=True) + 1e-12)
def lerp(a, b, t): return a + (b - a) * t
def rot(v, axis, ang):
    k = unit(axis); v = np.asarray(v, float)
    return v * cos(ang) + np.cross(k, v) * sin(ang) + k * np.dot(k, v) * (1 - cos(ang))


# ---------- skinning ----------
def chain_w(V, bones, end, blend=4.5):
    """Weights along a chain of bones: each vertex follows the nearest bone, sharing with the neighbour near a joint."""
    pts = [J[b] for b in bones] + [np.asarray(end, float)]
    n = len(V); best = np.full(n, 1e9); seg = np.zeros(n, int); s = np.zeros(n); Ls = np.zeros(n)
    for i in range(len(bones)):
        a, ab = pts[i], pts[i + 1] - pts[i]; L = np.linalg.norm(ab)
        t = np.clip((V - a) @ ab / (L * L), 0, 1); d = np.linalg.norm(V - (a + t[:, None] * ab), axis=1)
        m = d < best; best[m] = d[m]; seg[m] = i; s[m] = t[m] * L; Ls[m] = L
    W = np.zeros((n, len(bones)))
    prev = np.where((seg > 0) & (s < blend), 0.5 * (1 - s / blend), 0)
    nxt = np.where((seg < len(bones) - 1) & (Ls - s < blend), 0.5 * (1 - (Ls - s) / blend), 0)
    idx = np.arange(n)
    W[idx, seg] = 1 - prev - nxt
    W[idx, np.maximum(seg - 1, 0)] += prev; W[idx, np.minimum(seg + 1, len(bones) - 1)] += nxt
    return {b: W[:, i] for i, b in enumerate(bones)}


def other_side(bone):
    """The right-hand twin of a left-hand bone (the tank's rig mixes _l and _L)."""
    return bone[:-2] + {"_l": "_r", "_L": "_R"}.get(bone[-2:], bone[-2:])


def add(v, f, mat, w, mirror=False, grad=None):
    """grad: (colour, amount per vertex 0..1) shifts the role's colour toward another across the piece."""
    v = np.asarray(v, float)
    if isinstance(w, str): w = {w: np.ones(len(v))}
    PARTS.append(dict(v=v, f=list(f), mat=mat, w=w, mirror=mirror, grad=grad))


def merge(*vf):
    V, F, o = [], [], 0
    for v, f in vf:
        V.append(np.asarray(v, float)); F += [tuple(i + o for i in p) for p in f]; o += len(v)
    return np.concatenate(V), F


# ---------- shapes ----------
def spline(ctrl, n, extra=None):
    """Catmull-Rom through the control points; extra (one row per control) is interpolated alongside."""
    C = np.asarray(ctrl, float)
    if extra is not None: C = np.concatenate([C, np.asarray(extra, float).reshape(len(C), -1)], axis=1)
    P = np.concatenate([C[:1] * 2 - C[1:2], C, C[-1:] * 2 - C[-2:-1]])
    out = []
    for u in np.linspace(0, len(C) - 1, n):
        i = min(int(u), len(C) - 2); t = u - i
        p0, p1, p2, p3 = P[i], P[i + 1], P[i + 2], P[i + 3]
        out.append(0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t ** 3))
    out = np.array(out)
    return (out[:, :3], out[:, 3:]) if extra is not None else out


def frames(P, hint=(0, 0, 1)):
    T = unit(np.gradient(P, axis=0)); h = np.asarray(hint, float)
    n = h - (h @ T[0]) * T[0]
    if np.linalg.norm(n) < 0.05: n = np.cross(T[0], (1, 0, 0))
    N = [unit(n)]
    for i in range(1, len(P)):
        n = N[-1] - (N[-1] @ T[i]) * T[i]; N.append(unit(n))
    N = np.array(N); return T, N, np.cross(T, N)


def tube(P, r, k=6, rb=None, hint=(0, 0, 1), phase=0.0, caps=(True, True)):
    P = np.asarray(P, float); n = len(P)
    def fit(x):                                       # one radius, one per point, or a profile stretched along the path
        x = np.atleast_1d(np.asarray(x, float))
        return np.broadcast_to(x, (n,)) if len(x) in (1, n) else np.interp(np.linspace(0, 1, n), np.linspace(0, 1, len(x)), x)
    r = fit(r); rb = r if rb is None else fit(rb)
    T, N, B = frames(P, hint)
    ang = phase + 2 * pi * np.arange(k) / k
    V = (P[:, None, :] + r[:, None, None] * np.cos(ang)[None, :, None] * N[:, None, :]
         + rb[:, None, None] * np.sin(ang)[None, :, None] * B[:, None, :]).reshape(-1, 3)
    F = [(i * k + j, i * k + (j + 1) % k, (i + 1) * k + (j + 1) % k, (i + 1) * k + j) for i in range(n - 1) for j in range(k)]
    V = list(V)
    if caps[0]:
        c = len(V); V.append(P[0]); F += [(c, (j + 1) % k, j) for j in range(k)]
    if caps[1]:
        c = len(V); V.append(P[-1]); b = (n - 1) * k; F += [(c, b + j, b + (j + 1) % k) for j in range(k)]
    return np.array(V), F


def cone(base, d, length, r, k=4, hint=(0, 0, 1)):
    d = unit(d); base = np.asarray(base, float)
    return tube([base - d * r * 0.5, base + d * length * 0.5, base + d * length], [r, r * 0.45, 0.02], k=k, hint=hint, caps=(False, True))


def bundle(ctrl, ra, rb=None, strands=4, twist=1.0, n=22, k=6, strand=0.42, core=0.72, hint=(0, 0, 1), phase=0.0, thorn=0.0,
           rough=0.0, creep=0, stubs=0, creep_r=1.0):
    """A limb or trunk: a dark core with sinew strands twisting round it. ra / rb: section radii at each control.
    rough (0..1) makes it grown rather than turned: every strand gets its own thickness, twist and length, swells
    and thins along its run, carries knots, and thin ones ride over thick ones. creep: thin thorned vines wound
    loosely round the outside (creep_r scales their thickness). stubs: broken twig ends. Returns (core, strands, extras) as (verts, faces) each."""
    rb = ra if rb is None else rb
    P, R = spline(ctrl, n, np.stack([ra, rb], axis=1))
    T, N, B = frames(P, hint); u = np.linspace(0, 1, n); Rm = np.minimum(R[:, 0], R[:, 1])
    def wob(amp, lo=3.0, hi=8.0):                       # a slow wander around 1
        return 1 + amp * (0.6 * np.sin(RNG.uniform(lo, hi) * u + RNG.uniform(0, 6.3)) + 0.4 * np.sin(RNG.uniform(hi, 2.2 * hi) * u + RNG.uniform(0, 6.3)))
    def ring(a, rr): return P + (R[:, 0] * rr * np.cos(a))[:, None] * N + (R[:, 1] * rr * np.sin(a))[:, None] * B
    lump = wob(0.12 * rough)
    cv = tube(P, R[:, 0] * core * lump, k=max(k, 6), rb=R[:, 1] * core * lump, hint=hint)
    out, ex = [], []
    for s in range(strands):
        f, tw, t0, t1, ride, flat = 1.0, twist, 0.0, 1.0, 1 - strand * 0.55, 1.0
        if rough:
            f = lerp(1.0, RNG.uniform(0.5, 1.55), rough); tw = twist * lerp(1.0, RNG.uniform(0.4, 1.7), rough)
            if s >= 2:                                   # the first two run the whole length, the rest may start late or stop short
                if RNG.random() < 0.5 * rough: t0 = RNG.uniform(0.06, 0.32)
                if RNG.random() < 0.5 * rough: t1 = RNG.uniform(0.68, 0.94)
                if RNG.random() < 0.25 * rough: tw = -tw
            ride += 0.3 * rough * max(0.0, 1.0 - f); flat = RNG.uniform(0.68, 1.0)
        a = phase + 2 * pi * s / strands + 2 * pi * tw * u + 0.25 * (1 + rough) * np.sin(np.linspace(0, 7, n) + s)
        Q = ring(a, ride * wob(0.09 * rough))
        rs = Rm * strand * f * (wob(0.3 * rough) if rough else 0.9 + 0.25 * np.sin(np.linspace(0, 9, n) + 2 * s))
        for _ in range(int(RNG.integers(0, 3)) if rough else 0):
            rs = rs * (1 + 0.6 * rough * np.exp(-((u - RNG.uniform(0.1, 0.9)) / 0.045) ** 2))      # a knot
        rs = rs * np.clip((u - t0) / 0.12, 0.03, 1) * np.clip((t1 - u) / 0.12, 0.03, 1)        # sinks in at both ends
        i0, i1 = int(np.searchsorted(u, t0)), int(np.searchsorted(u, t1, side="right"))
        i0 = max(0, i0 - 1); i1 = min(n, max(i1 + 1, i0 + 4))
        out.append(tube(Q[i0:i1], rs[i0:i1], k=5, rb=rs[i0:i1] * flat, hint=hint, phase=s))
        if thorn > 0:
            for i in range(i0 + 2 + s % 3, i1 - 2, 7):
                o = unit(Q[i] - P[i]); ex.append(cone(Q[i] + o * rs[i] * 0.5, o + T[i] * 0.5, thorn * RNG.uniform(0.6, 1.4), max(rs[i] * 0.4, 0.3), k=4))
    for c in range(creep):
        a = RNG.uniform(0, 6.3) + 2 * pi * RNG.uniform(1.8, 3.2) * (1 if c % 2 else -1) * u + 0.4 * np.sin(11 * u + c)
        Q = ring(a, 1.1 * wob(0.07)); rc = creep_r * RNG.uniform(0.42, 0.62) * np.clip(np.minimum(u, 1 - u) / 0.08, 0.05, 1)
        m = int(1.6 * n); ui = np.linspace(0, 1, m); Q = np.stack([np.interp(ui, u, Q[:, d]) for d in range(3)], axis=1)
        ex.append(tube(Q, np.interp(ui, u, rc), k=3, hint=hint))
        Pc = np.stack([np.interp(ui, u, P[:, d]) for d in range(3)], axis=1)
        for i in range(3 + c, m - 3, 7):
            o = unit(Q[i] - Pc[i]); ex.append(cone(Q[i], o + RNG.normal(0, 0.3, 3), RNG.uniform(1.6, 2.6), 0.24, k=3))
    for _ in range(stubs):
        i = int(RNG.integers(2, n - 2)); a = RNG.uniform(0, 6.3); q = ring(np.full(n, a), 0.8)[i]
        o = unit(q - P[i]); d = unit(o + T[i] * RNG.uniform(-0.9, 0.9) + RNG.normal(0, 0.25, 3)); L = RNG.uniform(3.0, 6.5)
        ex.append(tube([q - d, q + d * L * 0.55, q + d * L], [Rm[i] * 0.3, Rm[i] * 0.2, 0.05], k=4, caps=(False, True)))
    return cv, merge(*out), (merge(*ex) if ex else None)


def burl(c, r):
    """A lumpy knot of wood."""
    ax = unit(RNG.normal(0, 1, 3)); x = np.linspace(-1, 1, 6)
    return tube(np.asarray(c, float) + ax * r * x[:, None], r * np.sqrt(np.clip(1 - x * x, 0.02, 1)) * RNG.uniform(0.8, 1.2, 6),
                k=7, rb=r * RNG.uniform(0.7, 1.0) * np.sqrt(np.clip(1 - x * x, 0.02, 1)), hint=np.cross(ax, (0.3, 0.5, 0.8)))


def add_side(v, f, mat, w, side):
    """Add a piece built in left-side space to one side only (the limbs: each side is grown separately)."""
    if isinstance(w, str): w = {w: np.ones(len(v))}
    if side < 0:
        v = np.asarray(v, float) * (-1, 1, 1); f = [p[::-1] for p in f]
        w = {other_side(b): x for b, x in w.items()}
    add(v, f, mat, w)


def leaf(base, d, nrm, L, W, droop=0.25, fold=0.35, teeth=4):
    """A serrated leaf lying along d, its top face toward nrm. Both faces are real geometry."""
    t = unit(d); n = unit(np.asarray(nrm, float) - np.dot(nrm, t) * t); s = np.cross(t, n)
    m = 2 * teeth + 1
    V = [np.asarray(base, float)]; rows = []
    for i in range(1, m):
        u = i / m; tip = i % 2 == 1
        ue = u + (0.95 / m if tip else -0.15 / m)
        h = W * sin(pi * u ** 0.7) ** 0.8 * (1.0 if tip else 0.38)
        mid = base + L * (u * t - droop * u * u * n); e = base + L * (ue * t - droop * ue * ue * n)
        rows.append((len(V), len(V) + 1, len(V) + 2))
        V += [e - s * h + n * h * fold, mid, e + s * h + n * h * fold]
    V.append(base + L * (t - droop * n)); tipi = len(V) - 1
    F = [(0, rows[0][1], rows[0][0]), (0, rows[0][2], rows[0][1])]
    for (a0, b0, c0), (a1, b1, c1) in zip(rows, rows[1:]): F += [(a0, b0, b1, a1), (b0, c0, c1, b1)]
    a, b, c = rows[-1]; F += [(a, b, tipi), (b, c, tipi)]
    V = np.array(V)
    return merge((V, F), (V, [p[::-1] for p in F]))


def arc_path(p0, d0, axis, L, bend, n=20, grow=1.5):
    P = [np.asarray(p0, float)]; d = unit(d0); w = np.linspace(0, 1, n - 1) ** grow; w = w / w.sum() * bend
    for i in range(n - 1):
        P.append(P[-1] + d * L / (n - 1)); d = rot(d, axis, w[i])
    return np.array(P)


def thorn_arc(p0, d0, axis, L, r0, bend, n=20, thorn=2.6):
    """A long bare vine curling round `axis`, with thorns on the outside of the curl."""
    P = arc_path(p0, d0, axis, L, bend, n)
    r = r0 * (1 - np.linspace(0, 1, n) ** 1.3) + 0.08
    parts = [tube(P, r, k=5, hint=axis)]
    T = unit(np.gradient(P, axis=0)); ax = unit(axis)
    for i in range(3, n - 2, 2):
        o = unit(np.cross(T[i], ax) + ax * (0.7 if i % 4 else -0.7) + T[i] * 0.4)
        parts.append(cone(P[i] + o * r[i] * 0.6, o, thorn * (0.5 + r[i] / r0), r[i] * 0.6, k=3))
    return merge(*parts)


def spray(p0, d0, L, leaves=7, size=11.0, width=3.0, bend=0.6, up=(0, 0, 1)):
    """A thin stem with serrated leaves alternating along it and one at the tip. Returns (stem, leaves)."""
    d0 = unit(d0); axis = unit(np.cross(d0, up) + RNG.normal(0, 0.3, 3))
    n = 10; P = arc_path(p0, d0, axis, L, bend * RNG.uniform(-1, 1), n, grow=1.0)
    T, N, B = frames(P, up)
    stem = tube(P, np.linspace(0.55, 0.15, n), k=3, hint=up)
    out = []
    for j in range(leaves):
        i = int(round(lerp(1.5, n - 1.01, j / max(leaves - 1, 1)))); side = 1 if j % 2 else -1
        last = j == leaves - 1
        d = T[i] if last else unit(T[i] * 0.8 + B[i] * side * RNG.uniform(0.5, 0.9) + N[i] * RNG.uniform(-0.2, 0.3))
        sc = RNG.uniform(0.75, 1.15) * (0.8 + 0.4 * j / leaves)
        out.append(leaf(P[i], d, N[i] + RNG.normal(0, 0.25, 3), size * sc, width * sc, droop=RNG.uniform(0.1, 0.35)))
    return stem, merge(*out)


def blade(base, d, nrm, L, W, curl=0.5, wave=0.2, fold=0.25, rows=7, vein=0.06):
    """A broad, smooth-edged leaf drawn out to a point: widest a third of the way up, its tip curling toward nrm
    (negative curl droops away). Returns (leaf, vein, heat): the vein is a thin raised strip down the midrib, for a
    glowing role; heat is 0..1 per leaf vertex, rising toward the tip and the rim, for add()'s grad.
    Both faces are real geometry."""
    d = unit(d); n0 = unit(np.asarray(nrm, float) - np.dot(nrm, d) * d); s = np.cross(d, n0)
    P = arc_path(base, d, s if curl < 0 else -s, L, abs(curl), rows + 1, grow=1.7)
    T = unit(np.gradient(P, axis=0)); Nn = np.cross(s, T) * (1 if curl < 0 else 1)
    Nn = np.where((Nn @ n0)[:, None] < 0, -Nn, Nn)
    u = np.linspace(0, 1, rows + 1); w = u ** 0.55 * (1 - u) ** 1.35; w = W * w / w.max()
    ph = RNG.uniform(0, 6.3)
    V = [P[0]]; rws = []; heat = [0.0]
    for i in range(1, rows):
        lift = Nn[i] * (w[i] * fold + W * wave * np.sin(7 * u[i] + ph))
        rws.append((len(V), len(V) + 1, len(V) + 2)); V += [P[i] - s * w[i] + lift, P[i], P[i] + s * w[i] + lift]
        h = 0.7 * u[i] ** 2.3; heat += [h + 0.16, h, h + 0.16]
    V.append(P[-1]); tip = len(V) - 1; heat.append(1.0)
    F = [(0, rws[0][1], rws[0][0]), (0, rws[0][2], rws[0][1])]
    for (a0, b0, c0), (a1, b1, c1) in zip(rws, rws[1:]): F += [(a0, b0, b1, a1), (b0, c0, c1, b1)]
    a, b, c = rws[-1]; F += [(a, b, tip), (b, c, tip)]
    V = np.array(V); lf = merge((V, F), (V, [q[::-1] for q in F]))
    vn = None
    if vein:
        VV, FF = [], []
        for side in (1, -1):                            # a sliver on each face of the leaf
            o = len(VV)
            for i in range(rows + 1):
                h = W * vein * (1 - u[i]) ** 0.7 + 0.03; c = P[i] + Nn[i] * side * 0.18
                VV += [c - s * h, c + s * h]
            for i in range(rows):
                q = (o + 2 * i, o + 2 * i + 1, o + 2 * i + 3, o + 2 * i + 2); FF.append(q if side > 0 else q[::-1])
        vn = (np.array(VV), FF)
    return lf, vn, np.array(heat + heat)


def ball(c, r, rz=None, k=7, rings=5, axis=(0, 0, 1)):
    """An ellipsoid: radius r across, rz along axis."""
    rz = r if rz is None else rz; ax = unit(axis); x = np.linspace(-1, 1, rings + 1)
    return tube(np.asarray(c, float) + ax * rz * x[:, None], r * np.sqrt(np.clip(1 - x * x, 0.004, 1)), k=k, hint=np.cross(ax, (0.31, 0.52, 0.8)))


def bud_stem(p0, d0, L, bend=0.6, toward=(0, 0, 1), bud=0.7, long=2.2, r=0.3):
    """A thin stem curving toward `toward`, ending in a bud. Returns (stem, bud)."""
    d0 = unit(d0); axis = np.cross(d0, toward)
    if np.linalg.norm(axis) < 0.05: axis = np.cross(d0, (1, 0.2, 0))
    P = arc_path(p0, d0, axis, L, bend, 8, grow=1.2); t = unit(P[-1] - P[-2])
    return tube(P, np.linspace(r, r * 0.6, 8), k=3, hint=axis), ball(P[-1] + t * long * 0.6, bud, long, k=5, rings=4, axis=t)


def branch(p, d, L, r, depth=3, lean=(0, 0, 1), drip=0.0):
    """A forking branch: each limb runs L, then splits in two shorter, thinner ones that lean toward `lean`.
    drip: chance that a tip hangs a bead of sap. Returns (wood, drips); drips is None when there are none."""
    wood, drips = [], []
    def grow(p, d, L, r, depth):
        ax = unit(np.cross(d, lean) + RNG.normal(0, 0.35, 3))
        P = arc_path(p, d, ax, L, RNG.uniform(-0.5, 0.5), 6, grow=1.0)
        wood.append(tube(P, np.linspace(r, r * 0.62, 6), k=4, hint=ax, caps=(False, True)))
        e = unit(P[-1] - P[-2])
        if depth <= 0:
            if RNG.random() < drip:
                c = P[-1] + A(0, 0, -RNG.uniform(2.5, 5.0))
                drips.append(tube([P[-1], c], [0.12, 0.12], k=3)); drips.append(ball(c + A(0, 0, -1.2), 0.75, 1.5, k=5, rings=4))
            return
        side = unit(np.cross(e, lean) + RNG.normal(0, 0.3, 3))
        for sgn in (1, -1):
            grow(P[-1] - e * r * 0.3, unit(e + side * sgn * RNG.uniform(0.45, 0.85) + np.asarray(lean, float) * RNG.uniform(0.1, 0.5)),
                 L * RNG.uniform(0.55, 0.75), r * 0.62, depth - 1)
    grow(np.asarray(p, float), unit(d), L, r, depth)
    return merge(*wood), (merge(*drips) if drips else None)


def slab(base, along, nrm, L, W, bend=0.5, flare=0.08, thick=3.4, holes=0, cracks=1):
    """A slab of bark hung like a shingle: fixed at `base`, running L along `along`, its face toward nrm, wrapped
    round the body (bend), its ragged point lifting away (flare). cracks: glowing splits that run its length and
    show on both faces. Returns (slab, cracks); cracks is None when there are none."""
    a = unit(along); n = unit(np.asarray(nrm, float) - np.dot(nrm, a) * a); s = np.cross(a, n); base = np.asarray(base, float)
    p1, p2 = RNG.uniform(0, 6.3, 2)
    def surf(x, v): return base + a * L * v + s * x + n * (flare * L * v * v - bend * x * x / W)
    def hw(v):
        v = min(max(v, 0), 1)
        return W * min(1, v / 0.1 + 0.25) ** 0.6 * (1 - v) ** 0.55 * (1 + 0.2 * sin(9 * v + p1) + 0.12 * sin(23 * v + p2))
    hl = [(RNG.uniform(-0.35, 0.35) * hw(v), v, L * RNG.uniform(0.07, 0.11), RNG.uniform(1.4, 2.2), RNG.uniform(-15, 15))
          for v in RNG.uniform(0.25, 0.62, holes)]
    plate = web_plate(surf, hw, hl, thick=thick, nu=6, nv=10)
    cr = []
    for c in range(cracks):
        m = 9; vs = np.linspace(RNG.uniform(0.05, 0.14), RNG.uniform(0.7, 0.88), m)
        xs = np.cumsum(RNG.normal(0, 0.16, m)); xs = np.clip(xs - xs.mean() + RNG.uniform(-0.25, 0.25), -0.6, 0.6)
        P = np.array([surf(x * hw(v), v) for x, v in zip(xs, vs)])
        tp = np.clip(np.minimum(np.arange(m), np.arange(m)[::-1]) / 2.0, 0.15, 1)
        cr.append(tube(P, 1.25 * tp, k=4, rb=(thick / 2 + 0.35) * np.clip(tp + 0.4, 0, 1), hint=s, phase=pi / 4))
        j = int(RNG.integers(2, m - 3)); x1 = xs[j] + RNG.choice([-1, 1]) * RNG.uniform(0.3, 0.5)      # one fork
        Q = np.array([P[j], surf(lerp(xs[j], x1, 0.5) * hw(vs[j + 1]), vs[j + 1]), surf(np.clip(x1, -0.8, 0.8) * hw(vs[j + 2]), vs[j + 2])])
        cr.append(tube(Q, [1.0, 0.7, 0.15], k=4, rb=[thick / 2 + 0.3, thick / 2 + 0.25, thick / 2], hint=s, phase=pi / 4))
    return plate, (merge(*cr) if cr else None)


# ---------- perforated bone plates ----------
def vesica(W, p=0.7, skew=1.0):
    return lambda v: W * max(sin(pi * min(max(v, 0), 1) ** skew), 0.0) ** p


def auto_holes(hw, length, rows=5, v0=0.2, v1=0.84, gap=3.9):
    out = []
    for r, v in enumerate(np.linspace(v0, v1, rows)):
        w = hw(v); cols = max(1, int((2 * w - 1.2) / gap))
        if 2 * w < 2.6: continue
        for c in range(cols):
            x = 0.0 if cols == 1 else lerp(-(w - 2.2), w - 2.2, c / (cols - 1))
            if r % 2 and cols > 1: x *= 0.8
            out.append((x, v + RNG.uniform(-0.02, 0.02), length * (v1 - v0) / rows * RNG.uniform(0.44, 0.6), RNG.uniform(0.95, 1.4), -x * 7))
    return out


def web_plate(surf, hw, holes, thick=1.0, nu=6, nv=10):
    """A bone plate with almond holes. surf(x, v) -> point (x: cm across, v: 0..1 along); hw(v): half width.
    holes: (x, v, half length, half width, lean in degrees)."""
    def nrm(x, v):
        e = 0.01
        return unit(np.cross(surf(x + e, v) - surf(x - e, v), surf(x, min(v + e, 1)) - surf(x, max(v - e, 0))))
    front, back = [], []
    for j in range(nv + 1):
        v = j / nv; w = max(hw(v), 0.12)
        for i in range(nu + 1):
            x = w * (2 * i / nu - 1); p = surf(x, v); n = nrm(x, v)
            edge = 1 - 0.55 * abs(2 * i / nu - 1) ** 3            # thinner toward the rim
            front.append(p + n * thick * 0.5 * edge); back.append(p - n * thick * 0.5 * edge)
    V = np.array(front + back); o = (nu + 1) * (nv + 1); F = []
    g = lambda i, j: j * (nu + 1) + i
    for j in range(nv):
        for i in range(nu):
            F.append((g(i, j), g(i + 1, j), g(i + 1, j + 1), g(i, j + 1)))
            F.append((o + g(i, j), o + g(i, j + 1), o + g(i + 1, j + 1), o + g(i + 1, j)))
    for j in range(nv):
        F.append((g(0, j), g(0, j + 1), o + g(0, j + 1), o + g(0, j)))
        F.append((g(nu, j), o + g(nu, j), o + g(nu, j + 1), g(nu, j + 1)))
    for i in range(nu):
        F.append((g(i, 0), o + g(i, 0), o + g(i + 1, 0), g(i + 1, 0)))
        F.append((g(i, nv), g(i + 1, nv), o + g(i + 1, nv), o + g(i, nv)))
    if not holes: return V, F
    cut = []
    for x, v, hl, hwid, lean in holes:
        c = surf(x, v); n = nrm(x, v)
        tv = unit(surf(x, min(v + 0.01, 1)) - surf(x, max(v - 0.01, 0))); tv = unit(tv - (tv @ n) * n); tu = np.cross(tv, n)
        a = radians(lean); L = cos(a) * tv + sin(a) * tu; S = np.cross(n, L)
        ring = [L * hl, L * hl * 0.35 + S * hwid, -L * hl * 0.45 + S * hwid * 0.8, -L * hl, -L * hl * 0.45 - S * hwid * 0.8, L * hl * 0.35 - S * hwid]
        b = len(cut) // 1
        vs = [c + r + n * thick * 2.5 for r in ring] + [c + r - n * thick * 2.5 for r in ring]
        fs = [(0, 1, 2, 3, 4, 5), (11, 10, 9, 8, 7, 6)] + [(i, i + 6, (i + 1) % 6 + 6, (i + 1) % 6) for i in range(6)]
        cut.append((np.array(vs), fs))
    return boolean(V, F, *merge(*cut))


def _obj(V, F):
    me = bpy.data.meshes.new("t"); me.from_pydata([tuple(p) for p in V], [], [tuple(p) for p in F]); me.update()
    o = bpy.data.objects.new("t", me); bpy.context.scene.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o
    for x in bpy.context.scene.objects: x.select_set(x is o)
    bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False); bpy.ops.object.mode_set(mode="OBJECT")
    return o


def boolean(V, F, CV, CF):
    a, b = _obj(V, F), _obj(CV, CF)
    m = a.modifiers.new("b", "BOOLEAN"); m.operation = "DIFFERENCE"; m.object = b; m.solver = "EXACT"; m.use_self = True
    dg = bpy.context.evaluated_depsgraph_get(); me = bpy.data.meshes.new_from_object(a.evaluated_get(dg))
    V2 = np.array([v.co[:] for v in me.vertices]); F2 = [tuple(p.vertices) for p in me.polygons]
    for o in (a, b): bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.meshes.remove(me)
    assert len(F2) > len(F) * 0.5, "boolean ate the plate"
    return V2, F2


# ---------- colour ----------
def pieces(part):
    """Label each connected piece of a part (one strand, one leaf, one thorn)."""
    n = len(part["v"]); parent = np.arange(n)
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]; i = parent[i]
        return i
    for f in part["f"]:
        a = find(f[0])
        for j in f[1:]:
            b = find(j)
            if b != a: parent[b] = a
    roots = np.array([find(i) for i in range(n)]); _, lab = np.unique(roots, return_inverse=True)
    return lab


def shade(part, v):
    """Vertex colours: the role's colour, each strand or leaf a shade of its own, drifting a little along its surface."""
    base = np.array(COLORS[part["mat"]]); val, hue, surf = VARY[part["mat"]]
    if "lab" not in part: part["lab"] = pieces(part)
    lab = part["lab"]; k = lab.max() + 1
    tint = (1 + CRNG.uniform(-val, val, (k, 1))) * (1 + CRNG.normal(0, hue, (k, 3)))
    drift = sum(a * np.sin(v @ w + ph) for w, ph, a in _WAVES)
    col = base * tint[lab]
    if part.get("grad") is not None:
        to, t = part["grad"]; t = np.clip(np.asarray(t, float), 0, 1)[:, None]
        col = col * (1 - t) + np.array(to) * (0.85 + 0.15 * tint[lab]) * t
    return np.clip(col * (1 + surf * drift)[:, None], 0, 1)


# ---------- finishing ----------
def finish(name, prefix):
    """Join every piece into one skinned mesh `name` on the rig, with one material `prefix`<role> per role."""
    rig = _S["rig"]
    V, F, MI, COL, groups, o = [], [], [], [], {}, 0
    mats = list(COLORS)
    for part in PARTS:
        for side in ((1, -1) if part["mirror"] else (1,)):
            v = part["v"] * (side, 1, 1)
            f = part["f"] if side > 0 else [p[::-1] for p in part["f"]]
            V.append(v); F += [tuple(i + o for i in p) for p in f]; MI += [mats.index(part["mat"])] * len(f)
            COL.append(shade(part, v))
            for b, w in part["w"].items():
                if side < 0: b = other_side(b)
                assert b in J, b
                groups.setdefault(b, []).append((o, w))
            o += len(v)
    V = np.concatenate(V); COL = np.concatenate(COL)
    me = bpy.data.meshes.new(name); me.from_pydata([tuple(p) for p in V], [], F); me.update()
    for role in mats:
        m = bpy.data.materials.new(prefix + role); m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*COLORS[role], 1); b.inputs["Roughness"].default_value = ROUGH[role]
        vc = m.node_tree.nodes.new("ShaderNodeVertexColor"); vc.layer_name = "Col"          # the colour comes from the vertex colours
        m.node_tree.links.new(vc.outputs["Color"], b.inputs["Base Color"])
        if role in GLOW:
            m.node_tree.links.new(vc.outputs["Color"], b.inputs["Emission Color"])
            b.inputs["Emission Strength"].default_value = GLOW[role]
        m.diffuse_color = (*COLORS[role], 1); me.materials.append(m)
    me.polygons.foreach_set("material_index", MI)
    for p in me.polygons: p.use_smooth = True
    me.set_sharp_from_angle(angle=radians(80))
    ca = me.color_attributes.new("Col", "BYTE_COLOR", "CORNER")
    li = np.zeros(len(me.loops), int); me.loops.foreach_get("vertex_index", li)
    ca.data.foreach_set("color", np.concatenate([COL[li], np.ones((len(li), 1))], axis=1).ravel())
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    for b, lst in groups.items():
        g = ob.vertex_groups.new(name=b)
        for off, w in lst:
            for i in np.nonzero(w > 0.001)[0]: g.add([int(off + i)], float(w[i]), "REPLACE")
    ob.parent = rig; ob.modifiers.new("Armature", "ARMATURE").object = rig
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    by = {n: sum(len(p.vertices) - 2 for p in me.polygons if p.material_index == i) for i, n in enumerate(mats)}
    log(name, "verts", len(V), "tris", tris, by, "bones", len(groups))
    log("extent", np.round(V.min(0), 1), np.round(V.max(0), 1))
    return ob


def renders(tag, file, target, span, details=()):
    """renders/<file>_<tag>sheet.png: front, three-quarter, side and back. details: (label, direction, target, span)
    close-ups, joined into <file>_detail.png (rest pose only)."""
    cam, lights = K.studio(res=(900, 1200))
    bpy.context.scene.world.node_tree.nodes["Background.001"].inputs["Color"].default_value = (0.42, 0.41, 0.43, 1)
    R = SHELLS + "/renders/" + file + "_" + tag
    views = {"front": (0, -1, 0), "three_q": (-0.62, -1, 0.12), "side": (-1, 0, 0), "back": (0.45, 1, 0.15)}
    for n, d in views.items(): K.shoot(R + n + ".png", cam, lights, d, target=target, span=span)
    K.sheet([R + n + ".png" for n in views], 4, R + "sheet.png")
    if not tag and details:
        for n, d, t, sp in details: K.shoot(R + n + ".png", cam, lights, d, target=t, span=sp)
        K.sheet([R + n + ".png" for n, *_ in details], len(details), R + "detail.png")


def deliver(ob, file, target, span, details=(), pose=()):
    """Save <file>_claude.blend and render the sheets; `-- pose` adds a posed skinning sheet, `-- export` writes
    export/<mesh name>.fbx with export_shells.py's settings."""
    rig = _S["rig"]
    g = bpy.data.objects.get("Guide_Bones")
    if g: g.hide_render = True; g.hide_set(True)
    bpy.ops.wm.save_as_mainfile(filepath=SHELLS + "/" + file + "_claude.blend", check_existing=False)
    renders("", file, target, span, details)
    if "pose" in ARGS:
        K.pose(list(pose)); renders("pose_", file, target, span); K.unpose()
    if "export" in ARGS:
        os.makedirs(SHELLS + "/export", exist_ok=True)
        for x in bpy.context.scene.objects: x.select_set(False)
        rig.select_set(True); ob.select_set(True); bpy.context.view_layer.objects.active = rig
        path = SHELLS + "/export/" + ob.name + ".fbx"
        bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"},
                                 add_leaf_bones=False, mesh_smooth_type="FACE", bake_anim=False, colors_type="SRGB",
                                 primary_bone_axis="Y", secondary_bone_axis="X", axis_forward="-Z", axis_up="Y")
        log("wrote", path)
