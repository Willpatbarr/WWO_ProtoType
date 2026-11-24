# Pictures of the kit pieces skeleton_detail.py copies, each seen from its four sides.
#   Blender -b ../base/robot_frame_v2_rigged.blend -P kit_catalogue.py
# Out: renders/kit_catalogue.png. One row per piece, in KIT's order; columns: face "d", "d" turned, "w", "w" turned
# (the side that faces out of a bay when place() is given that face).
import bpy, bmesh, sys, os
from mathutils import Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K, skeleton_detail as SD

body = bpy.data.objects["SK_RobotFrame"]
d = SD.Detail(body)
body.hide_render = True
cam, lights = K.studio((300, 300))
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/kit_"
ps = []
X, Y, Zv = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
for i, name in enumerate(SD.KIT):
    k = d.kit(name)
    print("WWO kit %-9s half w %.1f l %.1f d %.1f  verts %d" % (name, k["half"].x, k["half"].y, k["half"].z, len(k["verts"])))
    for j, (face, turn) in enumerate((("d", False), ("d", True), ("w", False), ("w", True))):
        d.out.clear(); d.bone("x")
        d.place(name, (0, 0, 0), Zv, -Y, scale=1.0, face=face, turn=turn)     # facing the camera at -Y
        me = bpy.data.meshes.new("k"); d.cur.to_mesh(me)
        for m in body.data.materials: me.materials.append(m)
        o = bpy.data.objects.new("k", me); bpy.context.scene.collection.objects.link(o)
        p = R + "%02d_%d.png" % (i, j); ps.append(p)
        K.shoot(p, cam, lights, (0.35, -1, 0.25), target=(0, 0, 0), span=k["half"].y * 2.6)
        bpy.data.objects.remove(o, do_unlink=True)
K.sheet(ps, 8, R + "catalogue.png")
for p in ps: os.remove(p)
