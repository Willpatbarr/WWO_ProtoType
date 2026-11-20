import os
import bpy, bmesh, sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
body = bpy.data.objects["SK_RobotFrame"]; me = body.data
names = {g.index: g.name for g in body.vertex_groups}
bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table()
dl = bm.verts.layers.deform.verify()
seen = set(); out = []
for v in bm.verts:
    if v.index in seen: continue
    g = names[max(v[dl].items(), key=lambda kv: kv[1])[0]] if v[dl] else "-"
    if not g.startswith(("spine_02", "clavicle")): continue
    stack = [v]; isl = []; seen.add(v.index)
    while stack:
        a = stack.pop(); isl.append(a)
        for e in a.link_edges:
            o = e.other_vert(a)
            if o.index not in seen: seen.add(o.index); stack.append(o)
    co = np.array([q.co[:] for q in isl]); mats = {f.material_index for q in isl[:30] for f in q.link_faces}
    out.append((len(isl), g, [me.materials[m].name[8:] for m in mats], np.round(co.min(0), 1), np.round(co.max(0), 1)))
for o in sorted(out, key=lambda o: -(o[4] - o[3]).prod())[:28]: print("WWO", o)
cam, lights = K.studio((800, 800))
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/base_torso_")
v = {"front": (0, -1, 0.05), "three_q": (-0.7, -1, 0.25), "side": (1, 0, 0), "back_q": (0.6, 1, 0.25), "top": (0, -0.35, 1)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 3, 148), span=70)
K.sheet([R + n + ".png" for n in v], 5, R + "sheet.png")
