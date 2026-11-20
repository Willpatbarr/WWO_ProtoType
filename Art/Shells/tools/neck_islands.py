import bpy, bmesh, numpy as np
body = bpy.data.objects["SK_RobotFrame"]; me = body.data
names = {g.index: g.name for g in body.vertex_groups}
bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table()
dl = bm.verts.layers.deform.verify()
seen = set(); out = []
for v in bm.verts:
    if v.index in seen or not (150 < v.co.z < 176 and abs(v.co.x) < 12 and -14 < v.co.y < 12): continue
    stack = [v]; isl = []
    seen.add(v.index)
    while stack:
        a = stack.pop(); isl.append(a)
        for e in a.link_edges:
            o = e.other_vert(a)
            if o.index not in seen: seen.add(o.index); stack.append(o)
    co = np.array([q.co[:] for q in isl]); mn, mx = co.min(0), co.max(0)
    g = names[max(isl[0][dl].items(), key=lambda kv: kv[1])[0]]
    mats = {f.material_index for q in isl[:20] for f in q.link_faces}
    out.append((len(isl), g, [me.materials[m].name[-9:] for m in mats], np.round(mn, 1), np.round(mx, 1)))
for o in sorted(out, key=lambda o: -o[0]):
    if o[0] >= 8: print("WWO", o)
