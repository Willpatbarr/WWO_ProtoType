# Close-ups of shell 3's left shoulder as built (skeleton + chest armour), for sketching the shoulder armour over.
#   Blender -b ../shell3_claude.blend -P shell3_shoulder_views.py
# Out: renders/shell3_shoulder_sheet.png. Each view is 1000 px looking at TARGET with SPAN cm of height,
# so the scale at the target is SPAN / 1000 cm per pixel.
# Top row: front, side, 3/4.  Bottom row: low 3/4, back, back 3/4.
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
TARGET, SPAN = (24.0, 3.5, 148.0), 50
bpy.data.objects["SK_RobotFrame"].hide_render = True
cam, lights = K.studio((1000, 1000))
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/shell3_shoulder_"
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (0.7, -1, 0.5), "low_q": (0.7, -1, -0.35), "back": (0, 1, 0), "back_q": (0.7, 1, 0.25)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=TARGET, span=SPAN)
K.sheet([R + n + ".png" for n in v], 3, R + "sheet.png")
