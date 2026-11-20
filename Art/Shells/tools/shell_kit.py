# Shared toolkit for building a robot shell body (a full set of swappable parts) on the game's
# robot skeleton. Run inside Blender on robot_frame_v2_rigged.blend (never saved over).
#
# Everything is built in metres in the robot's space (it faces -Y, its left is +X, up is Z) on the
# LEFT side, then mirrored. Every piece is rigid: 100 % one bone. Each piece belongs to a slot
# (head / core / arms / legs, the game's shell slots), and finish() joins one skinned mesh per slot.
#
# Materials are four roles, so the game can recolour them later:
#   Primary (armour), Secondary (trim / darker plates), Frame (joints, undersuit), Glow (light strips)
import bpy, bmesh, math, os
import numpy as np
from math import radians, cos, sin
from mathutils import Vector, Matrix

CM = 100.0
PIECES = []          # (object, bone, slot, mirror)
EDGE_POWER = 2.0     # 2 = plate faces roll off toward the border, 1 = a flat chamfer
SMOOTH = 40.0        # faces meeting at more than this many degrees get a hard edge


def log(*a): print("WWO", *a, flush=True)


def unit(v):
    v = np.asarray(v, float); return v / (np.linalg.norm(v) + 1e-12)


# ---------- materials ----------
ROLES = {
    "Primary": dict(color=(0.036, 0.036, 0.039), rough=0.48, metal=0.35),
    "Secondary": dict(color=(0.019, 0.019, 0.021), rough=0.4, metal=0.45),
    "Frame": dict(color=(0.012, 0.012, 0.013), rough=0.42, metal=0.7),
    "Glow": dict(color=(1, 1, 1), rough=0.3, metal=0.0, glow=(1.0, 1.0, 1.0), strength=14.0),
}


def material(role, prefix="M_Shell"):
    name = "%s_%s" % (prefix, role)
    m = bpy.data.materials.get(name)
    if m: return m
    r = ROLES[role]
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*r["color"], 1)
    b.inputs["Roughness"].default_value = r["rough"]
    b.inputs["Metallic"].default_value = r["metal"]
    if "glow" in r:
        b.inputs["Emission Color"].default_value = (*r["glow"], 1)
        b.inputs["Emission Strength"].default_value = r["strength"]
    m.diffuse_color = (*r["color"], 1)
    return m


# ---------- a lofted surface ----------
class Surf:
    """A tapering rounded-box envelope along the line a->b.

    secs: (t, out, in, front, back[, power[, shift_out[, shift_front]]]) with t in metres from a
    (it may run past either end). Angles: 0 = out (+X), 90 = front, 180 = in, 270 = back.
    power 1 = ellipse, lower = boxier."""

    def __init__(self, a, b, secs, front=(0, -1, 0), out=(1, 0, 0), pw=0.75):
        self.a = np.array(a, float); d = np.array(b, float) - self.a
        self.L = np.linalg.norm(d); self.ax = d / self.L
        f = np.array(front, float); self.f = unit(f - self.ax * f.dot(self.ax))
        o = np.cross(self.f, self.ax)
        self.o = o if o.dot(out) > 0 else -o
        rows = []
        for r in secs:
            r = list(r); r += [pw, 0.0, 0.0][len(r) - 5:]
            rows.append(r)
        self.k = np.array(rows, float)
        t = self.k[:, 0]; v = self.k[:, 1:]
        m = np.zeros_like(v)                      # tangents (finite differences), for a smooth profile
        for i in range(len(t)):
            i0, i1 = max(i - 1, 0), min(i + 1, len(t) - 1)
            m[i] = (v[i1] - v[i0]) / (t[i1] - t[i0])
        m[0] *= 0.0 if len(t) < 2 else 1.0
        self.t, self.v, self.m = t, v, m

    def dims(self, t):
        T, V, M = self.t, self.v, self.m
        if t <= T[0]: return V[0]
        if t >= T[-1]: return V[-1]
        i = int(np.searchsorted(T, t)) - 1
        h = T[i + 1] - T[i]; s = (t - T[i]) / h
        if getattr(self, "linear", False):        # straight runs between sections: slab-sided parts
            return V[i] * (1 - s) + V[i + 1] * s
        h00 = 2 * s ** 3 - 3 * s ** 2 + 1; h10 = s ** 3 - 2 * s ** 2 + s
        h01 = -2 * s ** 3 + 3 * s ** 2; h11 = s ** 3 - s ** 2
        return h00 * V[i] + h10 * h * M[i] + h01 * V[i + 1] + h11 * h * M[i + 1]

    def pt(self, th, t, lift=0.0):
        wo, wi, df, db, p, co, cf = self.dims(t)
        c, s = cos(radians(th)), sin(radians(th))
        a_, b_ = (wo if c >= 0 else wi), (df if s >= 0 else db)
        n = 2.0 / p                                # true polar angle on the superellipse: even spacing, no bunching
        r = (abs(c / a_) ** n + abs(s / b_) ** n) ** (-1.0 / n)
        r = max(r + lift, 0.0005)
        x, y = r * c, r * s
        return self.a + self.ax * t + self.o * (co + x) + self.f * (cf + y)


class Form(Surf):
    """Like Surf, but the cross-section is a chamfered box with slightly bulged faces: flat planes that
    meet along softened crease lines, the hard-surface look. Sections are
    (t, out, in, front, back[, opts]); opts (also the Form's defaults) may hold
      c: corner chamfers, one value or four (out-front, in-front, in-back, out-back), each a fraction or (fx, fy)
      b: face bulges, one value or four (out, front, in, back): how far the middle of a face stands proud
      co, cf: the section's centre shifted out / to the front.
    soft: degrees the crease lines are rounded over."""

    def __init__(self, a, b_, secs, front=(0, -1, 0), out=(1, 0, 0), soft=7.0, **defaults):
        Surf.__init__(self, a, b_, [(0, 1, 1, 1, 1), (1, 1, 1, 1, 1)], front, out)
        self.soft = soft
        rows, ts = [], []
        for r in secs:
            o = dict(c=0.38, b=0.05, co=0.0, cf=0.0); o.update(defaults)
            if len(r) > 5: o.update(r[5])
            t, wo, wi, df, db = r[:5]
            c = o["c"]; c = [c] * 4 if not isinstance(c, (list, tuple)) or (len(c) == 2 and not isinstance(c[0], (list, tuple)) and False) else list(c)
            c = [(k, k) if not isinstance(k, (list, tuple)) else k for k in c]
            bb = o["b"]; bb = [bb] * 4 if not isinstance(bb, (list, tuple)) else list(bb)
            (x1, y1), (x2, y2), (x3, y3), (x4, y4) = c
            P = [(wo * (1 + bb[0]), 0), (wo, df * (1 - y1)), (wo * (1 - x1), df), (0, df * (1 + bb[1])),
                 (-wi * (1 - x2), df), (-wi, df * (1 - y2)), (-wi * (1 + bb[2]), 0), (-wi, -db * (1 - y3)),
                 (-wi * (1 - x3), -db), (0, -db * (1 + bb[3])), (wo * (1 - x4), -db), (wo, -db * (1 - y4))]
            rows.append([v for q in P for v in q] + [o["co"], o["cf"]]); ts.append(t)
        t = np.array(ts, float); v = np.array(rows, float)
        m = np.zeros_like(v)
        for i in range(len(t)):
            i0, i1 = max(i - 1, 0), min(i + 1, len(t) - 1)
            m[i] = (v[i1] - v[i0]) / (t[i1] - t[i0])
        self.t, self.v, self.m = t, v, m
        self._memo = {}

    def radius(self, P, th):
        c, s = cos(radians(th)), sin(radians(th))
        A = P; E = np.roll(P, -1, axis=0) - A
        den = c * E[:, 1] - s * E[:, 0]
        den = np.where(np.abs(den) < 1e-12, 1e-12, den)
        r = (A[:, 0] * E[:, 1] - A[:, 1] * E[:, 0]) / den
        u = (A[:, 0] * s - A[:, 1] * c) / den
        ok = (u >= -1e-7) & (u <= 1 + 1e-7) & (r > 0)
        return float(r[ok].max()) if ok.any() else 0.001

    def theta_at(self, t, y):
        """The angle on the +X side whose point at height t has this world Y (the surface must face along Y there)."""
        lo, hi = -90.0, 90.0
        ylo, yhi = self.pt(lo, t)[1], self.pt(hi, t)[1]
        for _ in range(40):
            mid = (lo + hi) / 2; ym = self.pt(mid, t)[1]
            if (ym - y) * (yhi - ylo) > 0: hi = mid
            else: lo = mid
        return (lo + hi) / 2

    def pt(self, th, t, lift=0.0):
        key = (round(th, 4), round(t, 6))
        hit = self._memo.get(key)
        if hit is None:
            d = self.dims(t); P = d[:24].reshape(12, 2)
            r = sum(self.radius(P, th + k * self.soft / 4.0) for k in (-2, -1, 0, 1, 2)) / 5.0
            hit = (r, d[24], d[25]); self._memo[key] = hit
        r, co, cf = hit
        r = max(r + lift, 0.0005)
        return self.a + self.ax * t + self.o * (co + r * cos(radians(th))) + self.f * (cf + r * sin(radians(th)))


# ---------- pieces ----------
def add(name, verts, faces, role, bone, slot, bevel=0.0, smooth=None, mirror=None):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    vs = [bm.verts.new(tuple(v)) for v in verts]
    for f in faces:
        try: bm.faces.new([vs[i] for i in f])
        except ValueError: pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    return register(name, me, role, bone, slot, bevel, smooth, mirror)


def register(name, me, role, bone, slot, bevel=0.0, smooth=None, mirror=None):
    me.materials.append(material(role))
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    if bevel:
        m = o.modifiers.new("bevel", "BEVEL"); m.width = bevel; m.segments = 2
        m.limit_method = "ANGLE"; m.angle_limit = radians(42)
    o["smooth"] = SMOOTH if smooth is None else smooth
    if mirror is None: mirror = bone.endswith("_l")
    PIECES.append((o, bone, slot, mirror))
    return o


def tube(name, surf, t0, t1, role, bone, slot, nt=14, nth=44, lift=0.0, bevel=0.0, **kw):
    """A closed solid: the surface between t0 and t1, capped at both ends."""
    V, F = [], []
    ts = [t0 + (t1 - t0) * i / nt for i in range(nt + 1)]
    if getattr(surf, "linear", False):         # slab-sided: put a row on every section, so each run stays straight
        keys = [float(k) for k in surf.t if t0 + 1e-6 < k < t1 - 1e-6]
        ts = sorted(keys + [t for t in ts if all(abs(t - k) > (t1 - t0) / nt * 0.45 for k in keys)])
        nt = len(ts) - 1
    for t in ts:
        for j in range(nth):
            V.append(surf.pt(360.0 * j / nth, t, lift))
    for i in range(nt):
        for j in range(nth):
            a = i * nth + j; b = i * nth + (j + 1) % nth
            F.append((a, b, b + nth, a + nth))
    F.append(tuple(range(nth))); F.append(tuple(nt * nth + j for j in range(nth)))
    return add(name, V, F, role, bone, slot, bevel=bevel, **kw)


def _resample(line, n):
    line = np.array(line, float)
    d = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(line, axis=0) * np.array([1 / 360.0, 1 / 0.5]), axis=1))])
    s = np.linspace(0, d[-1], n)
    return np.stack([np.interp(s, d, line[:, 0]), np.interp(s, d, line[:, 1])], axis=1)


def patch(name, surf, top, bottom, role, bone, slot, nu=20, nv=10, lift=0.0, thick=0.012, bevel=0.0025, edge=None, **kw):
    """An armour plate lying on the surface. top and bottom are its two edges as (angle, t) lines
    running the same way round; the plate is the surface between them, `thick` deep.
    edge=(fraction across, fraction along, drop): chamfer the face down toward its border."""
    A = _resample(top, nu + 1); B = _resample(bottom, nu + 1)
    V, F = [], []
    for li, layer in enumerate((lift, lift - thick)):
        for i in range(nv + 1):
            k = i / nv
            for j in range(nu + 1):
                th, t = A[j] * (1 - k) + B[j] * k
                l = layer
                if edge and li == 0:        # the plate's face falls away toward its border: a wide soft chamfer
                    mu, mv, drop = edge; u = j / nu
                    e = min(min(u, 1 - u) / mu, min(k, 1 - k) / mv, 1.0)
                    l -= drop * (1 - e) ** EDGE_POWER
                V.append(surf.pt(th, t, l))
    n = (nu + 1) * (nv + 1)
    idx = lambda l, i, j: l * n + i * (nu + 1) + j
    for i in range(nv):
        for j in range(nu):
            F.append((idx(0, i, j), idx(0, i, j + 1), idx(0, i + 1, j + 1), idx(0, i + 1, j)))
            F.append((idx(1, i, j), idx(1, i + 1, j), idx(1, i + 1, j + 1), idx(1, i, j + 1)))
    for j in range(nu):
        F.append((idx(0, 0, j), idx(1, 0, j), idx(1, 0, j + 1), idx(0, 0, j + 1)))
        F.append((idx(0, nv, j), idx(0, nv, j + 1), idx(1, nv, j + 1), idx(1, nv, j)))
    for i in range(nv):
        F.append((idx(0, i, 0), idx(0, i + 1, 0), idx(1, i + 1, 0), idx(1, i, 0)))
        F.append((idx(0, i, nu), idx(1, i, nu), idx(1, i + 1, nu), idx(0, i + 1, nu)))
    return add(name, V, F, role, bone, slot, bevel=bevel, **kw)


def panel(name, surf, th0, th1, t0, t1, role, bone, slot, **kw):
    return patch(name, surf, [(th0, t0), (th1, t0)], [(th0, t1), (th1, t1)], role, bone, slot, **kw)


def strip(name, surf, th0, th1, t0, t1, bone, slot, lift=0.013, **kw):
    """A light strip set into a plate."""
    return panel(name, surf, th0, th1, t0, t1, "Glow", bone, slot, nu=4, nv=4, lift=lift, thick=0.006, bevel=0.0, **kw)


def cyl(name, c, axis, r, hh, role, bone, slot, n=36, r2=None, bevel=0.003, **kw):
    """A cylinder (or cone) centred on c, half-height hh along axis."""
    ax = unit(axis); c = np.array(c, float)
    u = unit(np.cross(ax, (0, 0, 1) if abs(ax[2]) < 0.9 else (1, 0, 0))); v = np.cross(ax, u)
    V, F = [], []
    for side, rr in ((-1, r), (1, r if r2 is None else r2)):
        for j in range(n):
            a = 2 * math.pi * j / n
            V.append(c + ax * hh * side + (u * cos(a) + v * sin(a)) * rr)
    for j in range(n):
        F.append((j, (j + 1) % n, n + (j + 1) % n, n + j))
    F.append(tuple(range(n))); F.append(tuple(range(n, 2 * n)))
    return add(name, V, F, role, bone, slot, bevel=bevel, **kw)


def joint(name, c, axis, r, hh, bone, slot, cap=0.72, **kw):
    """A round joint: a dark drum with a stepped cap on each face."""
    ax = unit(axis); c = np.array(c, float)
    cyl(name, c, ax, r, hh, "Frame", bone, slot, **kw)
    cyl(name + "_cap", c, ax, r * cap, hh + 0.006, "Secondary", bone, slot, **kw)
    cyl(name + "_hub", c, ax, r * cap * 0.45, hh + 0.011, "Frame", bone, slot, **kw)


def ball(name, c, radii, role, bone, slot, rot=None, **kw):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=28, v_segments=14, radius=1.0)
    M = Matrix.Translation(Vector(c)) @ (rot or Matrix.Identity(4)) @ Matrix.Diagonal((*np.broadcast_to(radii, 3), 1.0))
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
    bm.to_mesh(me); bm.free()
    return register(name, me, role, bone, slot, **kw)


def hull(name, pts, role, bone, slot, bevel=0.003, sym=False, **kw):
    """A faceted block: the convex hull of the points (sym: also their mirror across X)."""
    pts = [tuple(p) for p in pts]
    if sym: pts += [(-p[0], p[1], p[2]) for p in pts if abs(p[0]) > 1e-6]
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in pts]
    bmesh.ops.convex_hull(bm, input=vs)
    loose = [v for v in bm.verts if not v.link_faces]
    if loose: bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bmesh.ops.dissolve_limit(bm, angle_limit=radians(2), verts=bm.verts, edges=bm.edges)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    return register(name, me, role, bone, slot, bevel=bevel, **kw)


# ---------- finishing ----------
def _baked(o, bone):
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(o.evaluated_get(dg), depsgraph=dg)
    me.transform(o.matrix_world)
    for p in me.polygons: p.use_smooth = True
    me.set_sharp_from_angle(angle=radians(o["smooth"]))
    return me


def finish(prefix, base_hands=True):
    """Mirror, skin and join the pieces into one mesh per slot, in the rig file's centimetres."""
    rig = bpy.data.objects["Armature"]; body = bpy.data.objects["SK_RobotFrame"]
    bpy.context.view_layer.update()
    made = {}
    for o, bone, slot, mirror in PIECES:
        for side in ((1, -1) if mirror else (1,)):
            me = _baked(o, bone)
            b = bone
            if side < 0:
                me.transform(Matrix.Scale(-1, 4, (1, 0, 0))); me.flip_normals()
                if b.endswith("_l"): b = b[:-2] + "_r"
            me.transform(Matrix.Scale(CM, 4))
            n = bpy.data.objects.new("p", me); bpy.context.scene.collection.objects.link(n)
            n.vertex_groups.new(name=b).add(range(len(me.vertices)), 1.0, "REPLACE")
            made.setdefault(slot, []).append(n)
    if base_hands:      # the hands (and their finger bones' skinning) are the base robot's
        h = body.copy(); h.data = body.data.copy(); bpy.context.scene.collection.objects.link(h)
        keep = {g.index for g in h.vertex_groups if g.name.startswith(("hand_", "thumb_", "index_", "middle_", "ring_", "pinky_"))}
        bm = bmesh.new(); bm.from_mesh(h.data)
        dl = bm.verts.layers.deform.verify()
        drop = [v for v in bm.verts if not (v[dl] and max(v[dl].items(), key=lambda kv: kv[1])[0] in keep)]
        bmesh.ops.delete(bm, geom=drop, context="VERTS")
        bm.to_mesh(h.data); bm.free()
        h.data.materials.clear(); h.data.materials.append(material("Frame"))
        for p in h.data.polygons: p.material_index = 0
        for m in list(h.modifiers): h.modifiers.remove(m)
        h.parent = None; h.matrix_world = body.matrix_world
        made.setdefault("arms", []).append(h)
    for o, *_ in PIECES: bpy.data.objects.remove(o, do_unlink=True)
    PIECES.clear()
    out = {}
    for slot, objs in made.items():
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs: o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        bpy.ops.object.join()
        o = bpy.context.view_layer.objects.active
        o.name = o.data.name = "SK_%s_%s" % (prefix, slot.capitalize())
        used = sorted({g.group for v in o.data.vertices for g in v.groups})
        o.parent = rig; o.matrix_parent_inverse = Matrix.Identity(4)
        md = o.modifiers.new("Armature", "ARMATURE"); md.object = rig
        tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
        log(o.name, "verts", len(o.data.vertices), "tris", tris, "materials", [m.name for m in o.data.materials])
        out[slot] = o
    body.hide_render = True; body.hide_set(True)
    return out


# ---------- review renders ----------
def studio(res=(900, 1400)):
    sc = bpy.context.scene
    try: sc.render.engine = "BLENDER_EEVEE"
    except Exception: sc.render.engine = "BLENDER_EEVEE_NEXT"
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.film_transparent = False
    for o in list(sc.objects):
        if o.type in ("LIGHT", "CAMERA"): bpy.data.objects.remove(o, do_unlink=True)
    w = bpy.data.worlds.new("W"); sc.world = w; w.use_nodes = True
    nt = w.node_tree; nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputWorld"); mix = nt.nodes.new("ShaderNodeMixShader"); lp = nt.nodes.new("ShaderNodeLightPath")
    env = nt.nodes.new("ShaderNodeBackground"); cam = nt.nodes.new("ShaderNodeBackground")
    env.inputs["Color"].default_value = (0.75, 0.76, 0.8, 1); env.inputs["Strength"].default_value = 0.55
    cam.inputs["Color"].default_value = (0.105, 0.105, 0.105, 1)
    nt.links.new(lp.outputs["Is Camera Ray"], mix.inputs["Fac"]); nt.links.new(env.outputs[0], mix.inputs[1])
    nt.links.new(cam.outputs[0], mix.inputs[2]); nt.links.new(mix.outputs[0], out.inputs["Surface"])
    try:
        sc.view_settings.view_transform = "AgX"; sc.view_settings.look = "AgX - Medium High Contrast"
    except Exception: pass
    c = bpy.data.cameras.new("Cam"); co = bpy.data.objects.new("Cam", c); sc.collection.objects.link(co); sc.camera = co
    c.clip_start = 1; c.clip_end = 100000
    rigs = []
    for name, en in (("Key", 2.6), ("Fill", 1.3), ("Rim", 2.2)):
        l = bpy.data.lights.new(name, "SUN"); l.energy = en; l.angle = radians(20)
        o = bpy.data.objects.new(name, l); sc.collection.objects.link(o); rigs.append(o)
    return co, rigs


def shoot(path, cam, lights, direction, target=(0, 0, 95), span=205.0, lens=70):
    """Render looking at the robot from `direction` (robot space; front is -Y). Lights follow the camera."""
    sc = bpy.context.scene
    d = Vector(direction).normalized(); t = Vector(target)
    cam.data.type = "PERSP"; cam.data.lens = lens; cam.data.sensor_fit = "VERTICAL"; cam.data.sensor_height = 24
    dist = span / 2 / math.tan(math.atan(12.0 / lens))
    cam.location = t + d * dist
    cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    side = d.cross(Vector((0, 0, 1))).normalized()
    for o, v in zip(lights, (d * 0.8 + side * 0.7 + Vector((0, 0, 0.9)), d * 0.8 - side * 0.9 + Vector((0, 0, 0.2)), -d * 0.9 - side * 0.3 + Vector((0, 0, 0.7)))):
        o.rotation_euler = v.normalized().to_track_quat("Z", "Y").to_euler()
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def sheet(paths, cols, out, scale=1):
    def load(p):
        im = bpy.data.images.load(p); w, h = im.size
        a = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4); bpy.data.images.remove(im); return a
    t = [load(p) for p in paths]
    rows = [np.concatenate(t[i:i + cols], axis=1) for i in range(0, len(t), cols)]
    a = np.concatenate(rows[::-1], axis=0)
    h, w, _ = a.shape
    im = bpy.data.images.new("sheet", w, h, alpha=False); im.pixels = a.ravel().tolist()
    im.filepath_raw = out; im.file_format = "PNG"; im.save(); bpy.data.images.remove(im)


def pose(rots):
    """Pose the rig for a skinning check. rots: (bone, axis in robot space, degrees), parents first."""
    rig = bpy.data.objects["Armature"]
    for bone, axis, deg in rots:
        pb = rig.pose.bones[bone]
        h = pb.matrix.to_translation()
        pb.matrix = Matrix.Translation(h) @ Matrix.Rotation(radians(deg), 4, Vector(axis)) @ Matrix.Translation(-h) @ pb.matrix
        bpy.context.view_layer.update()


def unpose():
    rig = bpy.data.objects["Armature"]
    for pb in rig.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
