# Whole-robot views of shell 3 as built:  Blender -b ../shell3_claude.blend -P shell3_full_views.py
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
bpy.data.objects["SK_RobotFrame"].hide_render = True
cam, lights = K.studio()
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/shell3_full_")
views = {"three_q": (-0.62, -1, 0.12), "front": (0, -1, 0), "back": (0, 1, 0), "side": (1, 0, 0)}
for n, d in views.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 100), span=215)
K.sheet([R + n + ".png" for n in views], 4, R + "sheet.png")
