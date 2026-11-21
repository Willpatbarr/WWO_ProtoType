# Close-ups of the skeleton's chest (no shell), for sketching a new chest over.
#   Blender -b ../base/robot_frame_v2_rigged.blend -P skeleton_chest_views.py
# Out: renders/skeleton_chest_sheet.png. Each view is 1000 px looking at TARGET with SPAN cm of height,
# so the scale at the target is SPAN / 1000 cm per pixel.
# Top row: front, side, 3/4.  Bottom row: low 3/4, back, back 3/4.
import bpy, bmesh, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K, skeleton_edits
from skeleton_split import classify

TARGET, SPAN = (0, 0, 138), 70
body = bpy.data.objects["SK_RobotFrame"]
part = classify(body); skeleton_edits.apply(body)
bm = bmesh.new(); bm.from_mesh(body.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if part[f.index] != "Skeleton"], context="FACES")
skel = bpy.data.objects.new("SkeletonOnly", bpy.data.meshes.new("SkeletonOnly"))
for m in body.data.materials: skel.data.materials.append(m)
bm.to_mesh(skel.data); bm.free()
bpy.context.scene.collection.objects.link(skel); skel.matrix_world = body.matrix_world; body.hide_render = True
cam, lights = K.studio((1000, 1000))
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/skeleton_chest_"
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (0.7, -1, 0.5), "low_q": (0.7, -1, -0.35), "back": (0, 1, 0), "back_q": (0.7, 1, 0.25)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=TARGET, span=SPAN)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
