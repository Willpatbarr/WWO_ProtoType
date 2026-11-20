import bpy, numpy as np
rig = bpy.data.objects["Armature"]; body = bpy.data.objects["SK_RobotFrame"]
print("WWO scale", bpy.context.scene.unit_settings.scale_length, rig.matrix_world.to_scale()[:], body.matrix_world.to_scale()[:])
for b in rig.data.bones:
    print("WWO bone %-14s parent %-12s head %s tail %s" % (b.name, b.parent.name if b.parent else "-", [round(v,1) for v in b.head_local], [round(v,1) for v in b.tail_local]))
me = body.data
print("WWO verts", len(me.vertices), "polys", len(me.polygons), "mats", [m.name for m in me.materials])
names = {g.index: g.name for g in body.vertex_groups}
co = np.array([v.co[:] for v in me.vertices])
vb = np.array([names[max(v.groups, key=lambda g: g.weight).group] for v in me.vertices])
for n in sorted(set(vb)):
    if "_0" in n: continue
    c = co[vb == n]
    print("WWO group %-12s n %6d min %s max %s" % (n, len(c), np.round(c.min(0),1), np.round(c.max(0),1)))
