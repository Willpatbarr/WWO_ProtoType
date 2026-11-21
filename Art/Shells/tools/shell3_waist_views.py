# Close-ups of shell 3's waist as built, for sketching the waist armour over.
#   Blender -b ../shell3_claude.blend -P shell3_waist_views.py
# Out: renders/shell3_waist_sheet.png. Each view is 1000 px looking at TARGET with SPAN cm of height,
# so the scale at the target is SPAN / 1000 cm per pixel.
# The arms are hidden so the side view is clear.
# Top row: front, side, 3/4.  Bottom row: low 3/4, back, back 3/4.
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
TARGET, SPAN = (0.0, 0.0, 108.0), 50
bpy.data.objects["SK_RobotFrame"].hide_render = True
# the arms hang in front of the waist in the side view: leave them out of these renders (nothing is saved)
import bmesh
bpy.data.objects["SK_Shell3_Arms"].hide_render = True
sk = bpy.data.objects["SkeletonOnly"]
bm = bmesh.new(); bm.from_mesh(sk.data)
bmesh.ops.delete(bm, geom=[f for f in bm.faces if abs(f.calc_center_median().x) > 26 and f.calc_center_median().z < 150], context="FACES")
bm.to_mesh(sk.data); bm.free()
cam, lights = K.studio((1000, 1000))
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/shell3_waist_"
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (0.7, -1, 0.5), "low_q": (0.7, -1, -0.35), "back": (0, 1, 0), "back_q": (0.7, 1, 0.25)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=TARGET, span=SPAN)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
