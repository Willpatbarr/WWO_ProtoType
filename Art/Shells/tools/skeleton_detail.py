# Kitbashed detail for the skeleton's plain blocks: the thighs, calves, feet and chest.
# (Will, 2026-11-23: "match the style of the arms more", same proportions, shoulder and knee joints untouched.)
#
# Each block keeps its outline exactly: its edges stay where they are as rails, and its faces are sunk
# between them as panels and bays. Pieces of the robot's own arms, hips and spine (KIT) are copied into the
# bays, with pistons, bolts and ribs made here. Nothing stands proud of the old block's surface except where
# a comment says so. Called from skeleton_edits.apply(), so the export and every review render get it.
#
# Space: the rig file's centimetres; the robot faces -Y, its left is +X. Built on the left, mirrored.
import bpy, bmesh, math
from math import radians, cos, sin, pi
from mathutils import Vector, Matrix
from skeleton_split import face_bones, islands

Z = Vector((0, 0, 1))

# name: (bone, vertex count, which of several matches). Run tools/kit_catalogue.py for pictures.
KIT = {
    "truss_a": ("lowerarm_l", 474, 0),     # forearm frame: ribs above, lattice below
    "truss_b": ("lowerarm_l", 466, 0),
    "cage": ("lowerarm_l", 433, 0),        # the forearm's open housing
    "link": ("lowerarm_l", 387, 0),        # long link with a lattice web
    "ribbed": ("lowerarm_l", 256, 0),      # ribbed strut
    "hub": ("lowerarm_l", 223, 0),
    "clamp": ("lowerarm_l", 193, 0),       # wrist ring
    "bracket": ("upperarm_l", 332, 0),     # long grey bracket with slots
    "lugs": ("upperarm_l", 369, 0),        # grey twin lugs
    "lattice": ("upperarm_l", 288, 0),     # black lattice bracket
    "arm_link": ("upperarm_l", 262, 0),
    "loops": ("pelvis", 400, 1),           # twin octagonal truss loops
    "rib": ("spine_01", 1224, 0),          # a spine segment
    "drum": ("spine_01", 714, 0),          # grey drum with a cross slot
}


def mat_index(body, code):
    for i, m in enumerate(body.data.materials):
        if m.name[8:10] == code: return i
    raise KeyError(code)


class Frame:
    """A flat region's frame: centre c, outward normal n, u (up along the face), v, half sizes hu, hv."""
    def __init__(self, pts, n, low=False):
        """low: put the frame's plane at the region's lowest point, not its highest (a floor that is not quite
        flat: whatever is stood on it then stays under the surface everywhere)."""
        self.n = n.normalized()
        up = Z - self.n * Z.dot(self.n)
        if up.length < 0.2: up = Vector((0, -1, 0)) - self.n * Vector((0, -1, 0)).dot(self.n)   # flat faces: u = forward
        self.u = up.normalized(); self.v = self.n.cross(self.u)
        us = [p.dot(self.u) for p in pts]; vs = [p.dot(self.v) for p in pts]; ns = [p.dot(self.n) for p in pts]
        self.hu = (max(us) - min(us)) / 2; self.hv = (max(vs) - min(vs)) / 2
        self.c = self.u * (max(us) + min(us)) / 2 + self.v * (max(vs) + min(vs)) / 2 + self.n * (min(ns) if low else max(ns))
        self.pts = pts

    def at(self, a=0.0, b=0.0, d=0.0):
        """A point a along u and b along v (each -1..1 of the half size), d cm above the surface."""
        return self.c + self.u * (a * self.hu) + self.v * (b * self.hv) + self.n * d

    def vrange(self, du):
        """The region's extent along v (relative to c) at du cm along u from c: (v0, v1). Convex regions."""
        if not hasattr(self, "_hull"):
            q = sorted({(round((p - self.c).dot(self.u), 4), round((p - self.c).dot(self.v), 4)) for p in self.pts})
            def half(seq):
                h = []
                for a in seq:
                    while len(h) > 1 and (h[-1][0] - h[-2][0]) * (a[1] - h[-2][1]) - (h[-1][1] - h[-2][1]) * (a[0] - h[-2][0]) <= 0: h.pop()
                    h.append(a)
                return h
            self._hull = half(q)[:-1] + half(q[::-1])[:-1]
        H = self._hull; du = max(-self.hu + 1e-3, min(self.hu - 1e-3, du)); hit = []
        for (u0, v0), (u1, v1) in zip(H, H[1:] + H[:1]):
            if (u0 - du) * (u1 - du) <= 0 and abs(u1 - u0) > 1e-9: hit.append(v0 + (v1 - v0) * (du - u0) / (u1 - u0))
        return (min(hit), max(hit)) if hit else (-self.hv, self.hv)

    def rect(self, a0, a1, b0, b1):
        """A rectangle of this region as its own frame (a along u, b along v, each -1..1 of the half sizes)."""
        return Frame([self.at(a, b) for a in (a0, a1) for b in (b0, b1)], self.n)

    def half_v_at(self, a):
        """The region's half width (along v) at height a (-1..1), for tapering faces."""
        t = self.c.dot(self.u) + a * self.hu; c0 = self.c.dot(self.v)
        w = [abs(p.dot(self.v) - c0) for p in self.pts if abs(p.dot(self.u) - t) < self.hu * 0.35]
        return min(w) if w else self.hv


class Detail:
    def __init__(self, body):
        self.body = body
        self.rig = bpy.data.objects["Armature"]
        self.fb = face_bones(body)
        self.src = bmesh.new(); self.src.from_mesh(body.data); self.src.faces.ensure_lookup_table()
        self.isl = list(islands(self.src))
        self.out = {}            # bone -> bmesh of new geometry
        self.drop = set()        # face indices of the old blocks
        self.kits = {}
        self.M = {c: mat_index(body, c) for c in ("00", "01", "03", "04", "08", "09", "07")}

    # ----- finding pieces of the robot -----
    def find(self, bone, nverts=None, mat=None, pick=None):
        got = []
        for fs in self.isl:
            if self.fb[fs[0].index] != bone: continue
            if mat and self.body.data.materials[fs[0].material_index].name[8:10] != mat: continue
            n = len({v.index for f in fs for v in f.verts})
            if nverts and n != nverts: continue
            if pick and not pick(fs): continue
            got.append(fs)
        return got

    def remove(self, fs):
        """Mark a piece of the robot (and its twin on the right side) for removal."""
        self.drop |= {f.index for f in fs}
        bone = self.fb[fs[0].index]
        if not bone.endswith("_l"): return
        c = sum((v.co for f in fs for v in f.verts), Vector()) / sum(len(f.verts) for f in fs)
        n = len({v.index for f in fs for v in f.verts}); twins = 0
        for gs in self.find(bone[:-2] + "_r", n):
            g = sum((v.co for f in gs for v in f.verts), Vector()) / sum(len(f.verts) for f in gs)
            if (Vector((-g.x, g.y, g.z)) - c).length < 0.05: self.drop |= {f.index for f in gs}; twins += 1
        assert twins == 1, (bone, n, twins)

    def kit(self, name):
        if name in self.kits: return self.kits[name]
        bone, nv, k = KIT[name]
        fs = self.find(bone, nv)[k]
        b = self.rig.data.bones[bone]
        L = (b.tail_local - b.head_local).normalized()          # along the limb
        D = (Vector((0, 1, 0)) - L * L.y)
        if D.length < 0.3: D = Vector((1, 0, 0)) - L * L.x
        D.normalize(); W = L.cross(D)                           # D: front-back, W: across
        vs = {}
        for f in fs:
            for v in f.verts: vs.setdefault(v.index, Vector((v.co.dot(W), v.co.dot(L), v.co.dot(D))))
        mn = Vector([min(p[i] for p in vs.values()) for i in range(3)]); mx = Vector([max(p[i] for p in vs.values()) for i in range(3)])
        c = (mn + mx) / 2
        idx = {vi: k2 for k2, vi in enumerate(vs)}
        k = dict(verts=[p - c for p in vs.values()], half=(mx - mn) / 2,
                 faces=[([idx[v.index] for v in f.verts], f.smooth, f.material_index) for f in fs],
                 sharp={tuple(sorted((idx[e.verts[0].index], idx[e.verts[1].index]))) for f in fs for e in f.edges if not e.smooth})
        self.kits[name] = k
        return k

    # ----- output -----
    def bone(self, name):
        self.cur = self.out.setdefault(name, bmesh.new())
        return self.cur

    def poly(self, pts, mat="04", smooth=False):
        vs = [self.cur.verts.new(p) for p in pts]
        f = self.cur.faces.new(vs); f.material_index = self.M[mat]; f.smooth = smooth
        return f

    def place(self, name, c, long, out, length=None, scale=None, face=None, mat=None, width=None, depth=None, top=None,
              turn=False, fit=None, fill=0.94):
        """Copy a kit piece: its length along `long`, its flat side toward `out` (face "d"/"w" picks which; turn flips it).
        length: its full length in cm (uniform scale), or scale. width / depth: squeeze to at most this (full size).
        top: put its outermost point this far along `out` from c (else centred on c).
        fit: a Frame (a bay's floor, with u along `long`): the piece is squeezed sideways, height by height, to fill
        that outline, so a tapering bay gets a tapering piece and nothing leaves the bay."""
        k = self.kit(name); h = k["half"]
        s = scale if scale else length / (2 * h.y)
        L = long.normalized(); N = (out - L * out.dot(L)).normalized(); S = L.cross(N)
        if face is None: face = "d" if h.z <= h.x else "w"          # its flat side up
        if turn: face = {"d": "-d", "w": "-w"}[face]
        # the piece's (across, along, up) for each vertex
        if face == "d": loc, hs, hn = (lambda p: (p.x, p.y, p.z)), h.x, h.z
        elif face == "-d": loc, hs, hn = (lambda p: (-p.x, p.y, -p.z)), h.x, h.z
        elif face == "w": loc, hs, hn = (lambda p: (-p.z, p.y, p.x)), h.z, h.x
        else: loc, hs, hn = (lambda p: (p.z, p.y, -p.x)), h.z, h.x
        sw = s if not width or 2 * hs * s <= width else width / (2 * hs)
        sd = s if not depth or 2 * hn * s <= depth else depth / (2 * hn)
        o = Vector(c)
        if top is not None: o = o + N * (top - hn * sd)
        bm = self.cur; vs = []
        for p in k["verts"]:
            a, l, n = loc(p)
            if fit:
                du = (o + L * (l * s) - fit.c).dot(fit.u)
                v0, v1 = fit.vrange(du); sgn = 1 if S.dot(fit.v) > 0 else -1
                q = fit.c + fit.u * du + fit.v * ((v0 + v1) / 2 + sgn * (a / hs) * (v1 - v0) / 2 * fill)
                q += N * ((o - fit.c).dot(N) + n * sd)
            else:
                q = o + S * (a * sw) + L * (l * s) + N * (n * sd)
            vs.append(bm.verts.new(q))
        flip = face in ("-d", "-w") and False
        mi = self.M[mat] if mat else None
        for ix, sm, m in k["faces"]:
            try: f = bm.faces.new([vs[i] for i in ix])
            except ValueError: continue
            f.smooth = sm; f.material_index = m if mi is None else mi
        for a, b in k["sharp"]:
            e = bm.edges.get((vs[a], vs[b]))
            if e: e.smooth = False
        self._placed = getattr(self, "_placed", []) + [vs]
        return vs

    def cyl(self, a, b, r, n=12, mat="04", r2=None, caps=True, smooth=True):
        a = Vector(a); b = Vector(b); ax = (b - a).normalized()
        u = ax.cross(Z if abs(ax.z) < 0.9 else Vector((1, 0, 0))).normalized(); v = ax.cross(u)
        bm = self.cur; r2 = r if r2 is None else r2
        ra = [bm.verts.new(a + (u * cos(2 * pi * j / n) + v * sin(2 * pi * j / n)) * r) for j in range(n)]
        rb = [bm.verts.new(b + (u * cos(2 * pi * j / n) + v * sin(2 * pi * j / n)) * r2) for j in range(n)]
        for j in range(n):
            f = bm.faces.new((ra[j], ra[(j + 1) % n], rb[(j + 1) % n], rb[j])); f.smooth = smooth; f.material_index = self.M[mat]
        if caps:
            f = bm.faces.new(ra[::-1]); f.material_index = self.M[mat]
            f = bm.faces.new(rb); f.material_index = self.M[mat]
            for ring in (ra, rb):
                for j in range(n): bm.edges.get((ring[j], ring[(j + 1) % n])).smooth = False

    def turned(self, a, b, prof, n=14, mat="04"):
        """A lathe shape from a to b: prof = [(t 0..1, radius), ...]. Each step is a hard edge."""
        a = Vector(a); b = Vector(b)
        for (t0, r0), (t1, r1) in zip(prof, prof[1:]):
            if t1 - t0 < 1e-6: continue
            self.cyl(a + (b - a) * t0, a + (b - a) * t1, r0, n=n, mat=mat, r2=r1)

    def piston(self, a, b, r=0.7, split=0.55, mat="04", rod="01"):
        """A cylinder on a rod, eyes at both ends, like the ones already on the legs."""
        a = Vector(a); b = Vector(b)
        self.turned(a, b, [(0.0, r * 0.9), (0.05, r * 0.9), (0.05, r), (split, r), (split, r * 1.12), (split + 0.04, r * 1.12)], mat=mat)
        self.cyl(a + (b - a) * (split + 0.04), a + (b - a) * 0.95, r * 0.42, n=8, mat=rod)
        self.turned(a, b, [(0.95, r * 0.8), (1.0, r * 0.8)], mat=mat)

    def spring(self, a, b, r=1.0, turns=7, mat="03", core="04"):
        """A coil-over: a core rod with stacked rings."""
        a = Vector(a); b = Vector(b)
        self.cyl(a, b, r * 0.5, n=8, mat=core)
        for i in range(turns):
            t0 = (i + 0.15) / turns; t1 = (i + 0.85) / turns; tm = (t0 + t1) / 2
            self.turned(a, b, [(t0, r * 0.72), (tm, r), (t1, r * 0.72)], n=12, mat=mat)
        self.turned(a, b, [(0, r * 1.1), (0.04, r * 1.1)], mat=core); self.turned(a, b, [(0.96, r * 1.1), (1, r * 1.1)], mat=core)

    def bolt(self, p, n, r=0.35, h=0.2, mat="03"):
        p = Vector(p); n = n.normalized()
        self.cyl(p - n * 0.05, p + n * h, r, n=6, mat=mat, smooth=False)

    def box(self, c, u, v, n, hu, hv, hn, mat="04", taper=1.0):
        """A block centred on c with half sizes along u, v, n; its top (toward +n) scaled by taper."""
        c = Vector(c); bm = self.cur
        lo = [bm.verts.new(c + u * (a * hu) + v * (b * hv) - n * hn) for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        hi = [bm.verts.new(c + u * (a * hu * taper) + v * (b * hv * taper) + n * hn) for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        fs = [lo[::-1], hi] + [(lo[j], lo[(j + 1) % 4], hi[(j + 1) % 4], hi[j]) for j in range(4)]
        for q in fs:
            f = bm.faces.new(q); f.material_index = self.M[mat]
        return hi

    # ----- a block's shell -----
    def shell(self, fs, dissolve=1.5):
        """Take a block out of the robot into the current bone's mesh, as flat facets. Returns its faces."""
        bm = self.cur
        self.remove(fs)
        vm = {}; new = []
        for f in fs:
            try: g = bm.faces.new([vm.setdefault(v.index, bm.verts.new(v.co)) for v in f.verts])
            except ValueError: continue
            g.material_index = f.material_index; new.append(g)
        vs = list(vm.values())
        bmesh.ops.remove_doubles(bm, verts=vs, dist=1e-4)
        vs = [v for v in vs if v.is_valid]
        es = list({e for v in vs for e in v.link_edges})
        bmesh.ops.dissolve_limit(bm, angle_limit=radians(dissolve), verts=vs, edges=es)
        new = list({f for v in vs if v.is_valid for f in v.link_faces})
        bmesh.ops.recalc_face_normals(bm, faces=new)
        return new

    def frame(self, faces):
        n = sum((f.normal * f.calc_area() for f in faces), Vector())
        return Frame([v.co.copy() for f in faces for v in f.verts], n)

    def sink(self, faces, rail=0.9, depth=0.4, floor=None, flat=False):
        """Sink a face (or joined faces) between rails left at the old surface. Returns its frame before sinking,
        with .depth and the sunk half sizes .iu, .iv."""
        fr = self.frame(faces)
        bmesh.ops.inset_region(self.cur, faces=faces, thickness=rail, depth=0.0, use_even_offset=True, use_boundary=True)
        inner = {v for f in faces for v in f.verts}
        for v in inner: v.co -= fr.n * depth
        if flat:                                             # a face that is not quite flat gets a flat floor, at its lowest
            low = min(v.co.dot(fr.n) for v in inner)
            for v in inner: v.co -= fr.n * (v.co.dot(fr.n) - low)
        if floor:
            for f in faces: f.material_index = self.M[floor]
        g = Frame([v.co.copy() for v in inner], fr.n, low=True)
        fr.depth = depth; fr.iu = g.hu; fr.iv = g.hv; fr.floor = g
        fr.inner = [v.co.copy() for v in inner]
        return fr

    @staticmethod
    def floor_near(fr, p, radius):
        """The bay's floor frame lowered only as far as the floor's corners within radius of p need (an uneven floor)."""
        near = [q for q in fr.inner if (q - p).length < radius] or fr.inner
        g = Frame(fr.inner, fr.n, low=True)
        g.c = g.c + fr.n * (min(q.dot(fr.n) for q in near) - g.c.dot(fr.n))
        return g

    # ----- joining it all into the robot -----
    def finish(self, part):
        body = self.body
        self.src.free()
        # the old blocks go, both sides
        names = {g.index: g.name for g in body.vertex_groups}
        cats = ["Skeleton", "Head", "Core", "Arms", "Legs"]
        bm = bmesh.new(); bm.from_mesh(body.data); bm.faces.ensure_lookup_table()
        lay = bm.faces.layers.int.new("wwo_part")
        for f in bm.faces: f[lay] = cats.index(part[f.index])
        bmesh.ops.delete(bm, geom=[bm.faces[i] for i in self.drop], context="FACES")
        loose = [v for v in bm.verts if not v.link_faces]
        if loose: bmesh.ops.delete(bm, geom=loose, context="VERTS")
        bm.to_mesh(body.data); bm.free()
        objs = []; tris = 0
        for bone, nb in self.out.items():
            for side in ((1, -1) if bone.endswith("_l") else (1,)):
                me = bpy.data.meshes.new("detail_" + bone)
                nb.to_mesh(me)
                b = bone
                if side < 0:
                    me.transform(Matrix.Scale(-1, 4, (1, 0, 0))); me.flip_normals(); b = bone[:-2] + "_r"
                for m in body.data.materials: me.materials.append(m)
                o = bpy.data.objects.new(me.name, me); bpy.context.scene.collection.objects.link(o)
                o.vertex_groups.new(name=b).add(range(len(me.vertices)), 1.0, "REPLACE")
                tris += sum(len(p.vertices) - 2 for p in me.polygons)
                objs.append(o)
            nb.free()
        hidden = body.hide_get(); body.hide_set(False)
        bpy.ops.object.select_all(action="DESELECT")
        for o in objs: o.select_set(True)
        body.select_set(True); bpy.context.view_layer.objects.active = body
        bpy.ops.object.join()
        body.hide_set(hidden)
        a = body.data.attributes["wwo_part"]
        part.clear(); part.update({i: cats[d.value] for i, d in enumerate(a.data)})
        body.data.attributes.remove(a)
        print("WWO skeleton detail: +%d tris" % tris, flush=True)


# =====================================================================================================
def near(f, n, lim=0.85): return f.normal.dot(Vector(n).normalized()) > lim
def zmid(f): return f.calc_center_median().z
def big(f, a=20.0): return f.calc_area() > a


def bolts4(d, fr, inset=0.7, lift=None, r=0.3):
    """A bolt in each corner of a sunk panel."""
    for a in (-1, 1):
        for b in (-1, 1):
            hv = fr.floor.half_v_at(a * 0.8)
            p = fr.floor.c + fr.u * a * (fr.iu - inset) + fr.v * b * (hv - inset)
            d.bolt(p, fr.n, r=r, h=min(0.2, fr.depth * 0.6))


SIDE = {"out": (1, 0, 0), "in": (-1, 0, 0), "front": (0, -1, 0), "back": (0, 1, 0)}


def insert(d, fr, name, top, depth, mat=None, fill=0.92, span=0.97, face=None, turn=False):
    """A kit piece filling a bay, its top `top` cm above the bay's floor (keep it under the bay's depth)."""
    d.place(name, fr.floor.c, fr.u, fr.n, length=2 * fr.iu * span, top=top, depth=depth, fit=fr.floor, fill=fill, mat=mat, face=face, turn=turn)


def thigh(d):
    d.bone("thigh_l")
    faces = d.shell(d.find("thigh_l", 153, "04")[0])

    DONE = set()

    def band(s, z0, z1, a=14.0):
        fs = [f for f in faces if f.is_valid and near(f, SIDE[s], 0.9) and z0 < zmid(f) < z1 and big(f, a)]
        DONE.update(fs)
        return fs

    for s in SIDE:
        # --- top band: a sunk plate with bolts
        for f in band(s, 92, 100, 30):
            bolts4(d, d.sink([f], rail=0.8, depth=0.35))
        # --- the two middle bands: one deep bay with a piece of the arms in it
        # (the back's two bands meet at an angle, so there the lower one is the bay and the upper one a plate)
        mid = band(s, 77, 92.5, 30)
        if s == "back":
            mid.sort(key=zmid)
            bolts4(d, d.sink(mid[1:], rail=0.8, depth=0.35))
            mid = mid[:1]
        if mid:
            fr = d.sink(mid, rail=1.0, depth=1.5, floor="04")
            if s == "out": insert(d, fr, "truss_a", 1.35, 2.6, face="w", turn=True)
            elif s == "in": insert(d, fr, "truss_b", 1.35, 2.6, face="w", turn=True)
            elif s == "front": insert(d, fr, "bracket", 1.35, 2.4, fill=0.7)
            else: d.spring(fr.floor.at(-0.84, 0, 0.72), fr.floor.at(0.84, 0, 0.72), r=0.62, turns=6)
        # --- bottom band: a slot down to the knee
        for f in band(s, 66, 77, 25):
            fr = d.sink([f], rail=0.8, depth=0.9, floor="04")
            if s in ("out", "in"): insert(d, fr, "link", 0.8, 1.6, fill=0.8, face="w")
            else: d.piston(fr.floor.at(0.8, 0, 0.42), fr.floor.at(-0.7, 0, 0.42), r=0.4)
    panel_lines(d, faces, DONE)


def calf(d):
    d.bone("calf_l")
    faces = d.shell(d.find("calf_l", 182, "04")[0])

    DONE = set()

    def band(s, z0, z1, a=14.0):
        fs = [f for f in faces if f.is_valid and near(f, SIDE[s], 0.9) and z0 < zmid(f) < z1 and big(f, a)]
        DONE.update(fs)
        return fs

    for s in SIDE:
        for f in band(s, 48, 60, 20):
            fr = d.sink([f], rail=0.7, depth=0.3)
            if fr.iu > 1.3 and fr.iv > 1.3: bolts4(d, fr, inset=0.55, r=0.25)
        for f in band(s, 33, 48, 30):
            fr = d.sink([f], rail=0.9, depth=1.2 if s in ("out", "in") else 0.9, floor="04")
            if s == "out": insert(d, fr, "truss_b", 1.05, 2.2, face="w", turn=True)
            elif s == "in": insert(d, fr, "truss_a", 1.05, 2.2, face="w", turn=True)
            elif s == "front": insert(d, fr, "bracket", 0.8, 1.6, fill=0.8)
            else: d.piston(fr.floor.at(0.85, 0, 0.42), fr.floor.at(-0.85, 0, 0.42), r=0.4)
        for f in band(s, 20, 33, 20):
            d.sink([f], rail=0.6, depth=0.3, floor="04")
    panel_lines(d, faces, DONE)


def width(f):
    fr = Frame([v.co.copy() for v in f.verts], f.normal)
    return 2 * min(fr.hu, fr.hv)


def panel_lines(d, faces, done, area=7.0, wide=2.0, rail=0.4, depth=0.2):
    """Every face not yet worked that is big enough gets a shallow sunk panel, so no facet stays plain."""
    for f in faces:
        if f.is_valid and f not in done and f.calc_area() > area and width(f) > wide:
            d.sink([f], rail=rail, depth=depth)


def slats(d, fr, n, a0=-0.8, a1=0.8, b=0.8, h=None, t=0.28, mat="04"):
    """Louvre bars across a sunk panel (along v), from a0 to a1 of its height."""
    h = fr.depth * 0.8 if h is None else h
    for i in range(n):
        a = a0 + (a1 - a0) * (i + 0.5) / n
        v0, v1 = fr.floor.vrange(a * fr.iu)
        c = fr.floor.c + fr.u * (a * fr.iu) + fr.v * ((v0 + v1) / 2) + fr.n * (h / 2)
        d.box(c, fr.u, fr.v, fr.n, t, (v1 - v0) / 2 * b, h / 2, mat=mat, taper=0.7)


def foot(d):
    d.bone("foot_l")
    X, Y = Vector((1, 0, 0)), Vector((0, 1, 0))
    block = d.find("foot_l", 24, "04", lambda fs: min(v.co.z for f in fs for v in f.verts) > 8)[0]
    heel = d.find("foot_l", 24, "04", lambda fs: max(v.co.y for f in fs for v in f.verts) > 13)[0]
    toe = d.find("foot_l", 24, "04", lambda fs: max(v.co.y for f in fs for v in f.verts) < -8 and max(v.co.z for f in fs for v in f.verts) < 2)[0]
    roller = d.find("foot_l", 64, "04")[0]

    # --- the ankle block: a bay each side with a hub in it, louvres front and back
    faces = d.shell(block); done = set()
    for f in list(faces):
        if not f.is_valid or f.calc_area() < 30: continue
        if abs(f.normal.x) > 0.9:
            fr = d.sink([f], rail=0.8, depth=0.9, floor="04"); done.add(f)
            c = fr.floor.at(0.05, 0, 0)
            d.turned(c, c + fr.n * 0.8, [(0, 2.3), (0.45, 2.3), (0.45, 1.7), (0.8, 1.7), (0.8, 0.8), (1.0, 0.8)], n=16)
            for k in range(6):
                a = pi / 6 + k * pi / 3
                d.bolt(c + (fr.u * cos(a) + fr.v * sin(a)) * 2.0 + fr.n * 0.36, fr.n, r=0.22, h=0.15)
        elif abs(f.normal.y) > 0.9:
            fr = d.sink([f], rail=0.8, depth=0.6, floor="04"); done.add(f)
            slats(d, fr, 4)
    panel_lines(d, faces, done)

    # --- the sole plates: a sunk deck with cross ribs, the plate's edge left as a rim
    for isl, ribs in ((heel, 0), (toe, 3)):
        faces = d.shell(isl)
        top = [f for f in faces if f.normal.z > 0.9 and f.calc_area() > 50][0]
        fr = d.sink([top], rail=0.9, depth=0.7, floor="04")
        if ribs: slats(d, fr, ribs, b=0.94, h=0.5, t=0.45)
        else:
            deck = fr
            for b0, b1 in ((-0.95, -0.55), (0.55, 0.95)):            # a lattice link along each edge of the heel deck
                insert_in(d, fr.floor.rect(-0.95, 0.6, b0, b1), "link", 0.65, 1.2, face="w", fill=1.0)

    # --- the toe hinge: the same roller, turned with grooves and a pin through it
    xs = [v.co.x for f in roller for v in f.verts]; ys = [v.co.y for f in roller for v in f.verts]; zs = [v.co.z for f in roller for v in f.verts]
    d.remove(roller)
    x0, x1 = min(xs), max(xs); c = Vector((0, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2)); r = (max(zs) - min(zs)) / 2
    prof = [(0, r * 0.55), (0.03, r * 0.55), (0.03, r)]
    for i in range(5):
        t0 = 0.03 + 0.94 * i / 5; t1 = 0.03 + 0.94 * (i + 1) / 5
        prof += [(t1 - 0.035, r), (t1 - 0.035, r * 0.8), (t1, r * 0.8), (t1, r)] if i < 4 else [(t1, r), (t1, r * 0.55), (1.0, r * 0.55)]
    d.turned(c + X * x0, c + X * x1, prof, n=16)

    # --- what joins the ankle block to the sole (there was only air): a sprung post, two pistons forward to the
    # toe hinge and two short struts back to the heel. All inside the block's and the sole's width.
    bx = [v.co.x for f in block for v in f.verts]; by = [v.co.y for f in block for v in f.verts]; bz = min(v.co.z for f in block for v in f.verts)
    cx = (min(bx) + max(bx)) / 2; cy = (min(by) + max(by)) / 2
    floor_z = deck.floor.c.z
    d.turned((cx, cy, floor_z), (cx, cy, floor_z + 0.9), [(0, 2.6), (0.5, 2.6), (0.5, 1.9), (1, 1.9)], n=16)        # the post's foot
    for k in range(6):
        a = k * pi / 3
        d.bolt((cx + 2.25 * cos(a), cy + 2.25 * sin(a), floor_z + 0.45), Z, r=0.2, h=0.15)
    d.spring((cx, cy, floor_z + 0.9), (cx, cy, bz), r=1.5, turns=5)
    for sx in (-1, 1):
        x = cx + sx * 2.3
        d.piston((x, min(by) + 0.9, bz + 0.3), (x, c.y + 0.6, c.z + r * 0.75), r=0.5, split=0.5)
        d.cyl((x - 0.45, min(by) + 0.9, bz + 0.3), (x + 0.45, min(by) + 0.9, bz + 0.3), 0.62, n=10)                 # its eye
        d.cyl((x, max(by) - 0.9, bz + 0.2), (x, max(by) + 2.2, floor_z), 0.42, n=8)
        d.box((x, max(by) + 2.2, floor_z + 0.25), X, Y, Z, 0.7, 0.9, 0.25)


def insert_in(d, fl, name, top, depth, **kw):
    """A kit piece filling a frame made with Frame.rect (part of a bay's floor)."""
    span = kw.pop("span", 0.97); fill = kw.pop("fill", 0.92)
    d.place(name, fl.c, fl.u, fl.n, length=2 * fl.hu * span, top=top, depth=depth, fit=fl, fill=fill, **kw)


def chest(d):
    d.bone("spine_02")
    isl = d.find("spine_02", 40, "09", lambda fs: max(v.co.x for f in fs for v in f.verts) > 8)[0]
    faces = d.shell(isl)
    wall = [f for f in faces if abs(f.normal.x) > 0.99 and abs(f.calc_center_median().x) < 0.1]    # a wall down the middle, never seen
    bmesh.ops.delete(d.cur, geom=wall, context="FACES_ONLY")
    faces = [f for f in faces if f.is_valid]
    done = set()

    def pick(n, lim=0.9, a=10.0):
        fs = [f for f in faces if f.is_valid and f not in done and big(f, a) and near(f, n, lim)]
        done.update(fs); return fs

    for sx in (1, -1):
        # --- the front wedge: one bay each side of the centre ridge, with ribs across it
        fs = pick((0.6 * sx, -0.8, -0.1), 0.97)
        fr = d.sink(fs, rail=1.0, depth=1.2, floor="04", flat=True)
        slats(d, fr, 8, a0=-0.72, a1=0.94, b=0.86, h=1.0, t=0.5, mat="03")
        # --- the side: a deep bay; a bolted collar where the shoulder housing comes out, a truss below it
        fs = pick((0.96 * sx, -0.18, -0.2), 0.97)
        fr = d.sink(fs, rail=1.2, depth=1.6, floor="04")
        fl = fr.floor
        # where the housing's axis (y 4.0, z 153.6) meets the bay's floor. The floor is uneven (the block's corners
        # were trimmed for shell 3), so the collar and the truss each sit at the lowest corner near them.
        hc = Vector((fl.c.x + (-(fl.n.y * (4.0 - fl.c.y) + fl.n.z * (153.6 - fl.c.z)) / fl.n.x), 4.0, 153.6))
        fl = d.floor_near(fr, hc, 11.0)
        hc = hc + fl.n * (fl.c - hc).dot(fl.n)
        d.turned(hc, hc + fl.n * 1.3, [(0, 6.1), (0.4, 6.1), (0.4, 5.5), (1.0, 5.5)], n=24, mat="09")
        for k in range(8):
            a = pi / 8 + k * pi / 4
            d.bolt(hc + (fl.u * cos(a) + fl.v * sin(a)) * 5.8 + fl.n * 0.52, fl.n, r=0.3, h=0.2, mat="03")
        lo = fr.floor.rect(-0.72, -0.12, -0.4, 0.55)
        lo = d.floor_near(fr, lo.c, 12.0).rect(-0.72, -0.12, -0.4, 0.55)
        insert_in(d, lo, "loops", 0.8, 2.4, fill=0.95)
        # --- the back: a lattice link down each side of the spine, plates above
        fs = pick((0.13 * sx, 0.97, -0.22), 0.97)
        fr = d.sink(fs, rail=0.9, depth=1.0, floor="04", flat=True)
        insert(d, fr, "link", 0.9, 1.8, face="w", fill=0.6)
        for n in ((0.1 * sx, 1.0, 0.02), (0.08 * sx, 0.82, 0.57), (0.08 * sx, -0.29, 0.95), (0.24 * sx, -0.72, 0.65)):
            for f in pick(n, 0.97, 30):
                fr = d.sink([f], rail=0.8, depth=0.4)
                bolts4(d, fr, inset=0.8, r=0.32)
    panel_lines(d, faces, done, area=8.0, wide=2.2, rail=0.5, depth=0.25)


def build(body, part):
    d = Detail(body)
    thigh(d)
    calf(d)
    foot(d)
    chest(d)
    d.finish(part)
