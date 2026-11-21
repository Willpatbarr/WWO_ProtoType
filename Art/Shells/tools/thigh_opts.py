# Options sheet for thinning the skeleton's thighs: baseline, then each amount in OPTS (cm off the front
# and off the outer side), left to right. Rows: front view, side view, 3/4 view of the robot's left thigh.
# The orange rim is the baseline thigh's outline: its width is what was taken off. The arms are left out of the way.
#   Blender -b ../base/robot_frame_v2_rigged.blend -P thigh_opts.py
import bpy, bmesh, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K, skeleton_edits
from skeleton_split import classify, face_bones

OPTS = (0.0, 0.5, 1.0, 1.5)
body = bpy.data.objects["SK_RobotFrame"]; body.hide_render = True
part = classify(body); fb = face_bones(body)
LEG = ("thigh", "calf", "foot", "pelvis")
mats = [m.name[8:10] for m in body.data.materials]
zs = {p.index: [body.data.vertices[i].co.z for i in p.vertices] for p in body.data.polygons}
# the baseline thigh blocks alone, in orange and inside out, so only the part outside the new block shows
ghost = body.copy(); ghost.data = body.data.copy(); bpy.context.scene.collection.objects.link(ghost)
skeleton_edits.apply(ghost, 0.0, 0.0)
import bmesh as _b
from skeleton_split import islands
bm = _b.new(); bm.from_mesh(ghost.data); bm.faces.ensure_lookup_table(); keep = set()
for fs in islands(bm):
    vs = [v.co for f in fs for v in f.verts]
    if fb[fs[0].index].startswith("thigh") and mats[fs[0].material_index] == "04" and max(c.x for c in vs) - min(c.x for c in vs) > 10 and max(c.z for c in vs) - min(c.z for c in vs) > 25:
        keep.update(f.index for f in fs)
_b.ops.delete(bm, geom=[f for f in bm.faces if f.index not in keep], context="FACES")
_b.ops.reverse_faces(bm, faces=bm.faces[:])
bm.to_mesh(ghost.data); bm.free()
om = bpy.data.materials.new("Removed"); om.use_nodes = True
b = om.node_tree.nodes["Principled BSDF"]; b.inputs["Base Color"].default_value = (1, 0.25, 0, 1)
b.inputs["Emission Color"].default_value = (1, 0.25, 0, 1); b.inputs["Emission Strength"].default_value = 1.5
om.use_backface_culling = True; ghost.visible_shadow = False
ghost.data.materials.clear(); ghost.data.materials.append(om)
cam, lights = K.studio((700, 700))
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/thigh_opts_"
views = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (0.7, -1, 0.15)}
objs = []
for d in OPTS:
    o = body.copy(); o.data = body.data.copy(); bpy.context.scene.collection.objects.link(o)
    skeleton_edits.apply(o, d, d)
    bm = bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if part[f.index] != "Skeleton" or not fb[f.index].startswith(LEG)], context="FACES")
    bm.to_mesh(o.data); bm.free(); o.hide_render = True; objs.append(o)
paths = []
for n, v in views.items():
    for d, o in zip(OPTS, objs):
        o.hide_render = False; ghost.hide_render = (d == 0)
        p = "%s%s_%s.png" % (R, n, d); paths.append(p)
        K.shoot(p, cam, lights, v, target=(13, 0, 84), span=52)
        o.hide_render = True
K.sheet(paths, len(OPTS), R + "sheet.png")
