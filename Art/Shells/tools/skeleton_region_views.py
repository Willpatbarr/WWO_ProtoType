# The full set of skeleton close-ups (no shell), one sheet per armour region, for sketching a new shell over.
#   Blender -b ../base/robot_frame_v2_rigged.blend -P skeleton_region_views.py
# Out: renders/skeleton_<region>_sheet.png for head, chest, shoulder, forearm, waist, thigh, calf, foot.
# Each view is 1000 px looking at the region's target with `span` cm of height, so the scale at the target is
# span / 1000 cm per pixel. Targets and spans match make_shell3.py's sheets, so a sketch maps the same way.
# From the waist down the arms are left out, as on the shell 3 sheets: they hang in front of the side view.
# Top row: front, side, 3/4.  Bottom row: low 3/4, back, back 3/4.
# The whole-robot sheet is skeleton_views.py.
import bpy, bmesh, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K, skeleton_edits
from skeleton_split import classify

REGIONS = [  # name, target (cm), span (cm), arms shown
    ("head", (0, -1, 175), 40, True),
    ("chest", (0, 0, 138), 70, True),
    ("shoulder", (24.0, 3.5, 148.0), 50, True),
    ("forearm", (39.5, 3.5, 110), 50, True),
    ("waist", (0.0, 0.0, 108.0), 50, False),
    ("thigh", (13.0, 1.0, 84.0), 56, False),
    ("calf", (14.5, 3.5, 42.0), 60, False),
    ("foot", (15.8, -4.0, 10.0), 44, False),
]

body = bpy.data.objects["SK_RobotFrame"]
part = classify(body); skeleton_edits.apply(body, part=part)
bm = bmesh.new(); bm.from_mesh(body.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if part[f.index] != "Skeleton"], context="FACES")
skel = bpy.data.objects.new("SkeletonOnly", bpy.data.meshes.new("SkeletonOnly"))
for m in body.data.materials: skel.data.materials.append(m)
bm.to_mesh(skel.data); bm.free()
bpy.context.scene.collection.objects.link(skel); skel.matrix_world = body.matrix_world; body.hide_render = True

cam, lights = K.studio((1000, 1000))
OUT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/skeleton_"
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (0.7, -1, 0.5), "low_q": (0.7, -1, -0.35), "back": (0, 1, 0), "back_q": (0.7, 1, 0.25)}
armless = False
for name, target, span, arms in REGIONS:
    if not arms and not armless:
        bm = bmesh.new(); bm.from_mesh(skel.data)
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if abs(f.calc_center_median().x) > 26 and f.calc_center_median().z < 150], context="FACES")
        bm.to_mesh(skel.data); bm.free(); armless = True
    R = OUT + name + "_"
    for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=target, span=span)
    K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
