# Review sheet for the skeleton: the base robot cut by skeleton_split.py, the same way the export cuts it.
#   Blender -b ../base/robot_frame_v2_rigged.blend -P skeleton_views.py
# Out: renders/skeleton_sheet.png  (top row: the skeleton alone; bottom row: the whole base robot)
import bpy, bmesh, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
from skeleton_split import classify
import skeleton_edits

body = bpy.data.objects["SK_RobotFrame"]
part = classify(body)
skeleton_edits.apply(body)
skel = body.copy(); skel.data = body.data.copy(); skel.name = "SkeletonOnly"
bpy.context.scene.collection.objects.link(skel)
bm = bmesh.new(); bm.from_mesh(skel.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if part[f.index] != "Skeleton"], context="FACES")
bm.to_mesh(skel.data); bm.free()

cam, lights = K.studio()
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/skeleton_"
views = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (-0.62, -1, 0.12), "back": (0, 1, 0), "rear_q": (0.62, 1, 0.12)}
paths = []
for row, (show, hide) in (("only", (skel, body)), ("whole", (body, skel))):
    show.hide_render = False; hide.hide_render = True
    for n, d in views.items():
        p = R + row + "_" + n + ".png"; paths.append(p)
        K.shoot(p, cam, lights, d, target=(0, 0, 100), span=215)
K.sheet(paths, len(views), R + "sheet.png")
