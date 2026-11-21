# Numbers for fitting a shell over the game's skeleton: which skeleton pieces can be seen from outside.
#   Blender -b ../shell1_claude.blend -P fit_measure.py -- 1 [bone prefixes...]
# For each separate skeleton piece on the named bones: its box (cm, world), and how much of it is exposed
# (a vertex is exposed when most of 26 directions from it reach open air without hitting a shell part).
import bpy, bmesh, sys, os
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_split
from skeleton_split import islands
a = sys.argv[sys.argv.index("--") + 1:]
which, want = a[0], tuple(a[1:]) or ("head", "neck", "clavicle", "spine_02")
parts = shell_split.game_parts(which)
dg = bpy.context.evaluated_depsgraph_get()
V, T = [], []
for s in shell_split.SLOTS:
    o = parts[s]; me = o.data; me.calc_loop_triangles(); n0 = len(V)
    V += [o.matrix_world @ v.co for v in me.vertices]
    T += [tuple(n0 + i for i in t.vertices) for t in me.loop_triangles]
bvh = BVHTree.FromPolygons(V, T)
D = [Vector((x, y, z)).normalized() for x in (-1, 0, 1) for y in (-1, 0, 1) for z in (-1, 0, 1) if (x, y, z) != (0, 0, 0)]
sk = parts["Skeleton"]; M = sk.matrix_world
unit = 100.0 if max(abs(c) for v in V[:2000] for c in v) < 5 else 1.0        # report in cm
names = {g.index: g.name for g in sk.vertex_groups}
bm = bmesh.new(); bm.from_mesh(sk.data); bm.faces.ensure_lookup_table()
dl = bm.verts.layers.deform.verify()
rows = []
for fs in islands(bm):
    v0 = fs[0].verts[0]; d = v0[dl]
    bone = names[max(d.items(), key=lambda kv: kv[1])[0]] if d else "-"
    if not bone.startswith(want): continue
    vs = list({v for f in fs for v in f.verts})
    P = [M @ v.co for v in vs]
    ex = []
    for p in P:
        free = sum(1 for d_ in D if bvh.ray_cast(p + d_ * 0.0005 * (1 if unit == 100 else 100), d_)[0] is None)
        if free >= 9: ex.append(p)
    if not ex: continue
    lo = [min(p[i] for p in P) * unit for i in range(3)]; hi = [max(p[i] for p in P) * unit for i in range(3)]
    el = [min(p[i] for p in ex) * unit for i in range(3)]; eh = [max(p[i] for p in ex) * unit for i in range(3)]
    m = sk.data.materials[fs[0].material_index].name
    rows.append((len(ex) / len(P), bone, m, len(P), lo, hi, el, eh))
f3 = lambda a: "(%6.1f %6.1f %6.1f)" % tuple(a)
for r in sorted(rows, key=lambda r: (r[1], -r[0])):
    print("WWO %-11s %-12s verts %5d exposed %3d%%  box %s..%s  exposed box %s..%s" % (r[1], r[2], r[3], r[0] * 100, f3(r[4]), f3(r[5]), f3(r[6]), f3(r[7])))
