import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
bpy.data.objects["SK_RobotFrame"].hide_render = True
cam, lights = K.studio((800, 800))
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/shell2_torso_")
v = {"front": (0, -1, 0.05), "three_q": (-0.7, -1, 0.25), "side": (1, 0, 0), "back_q": (0.6, 1, 0.25), "top": (0, -0.35, 1)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 3, 146), span=62)
K.sheet([R + n + ".png" for n in v], 5, R + "sheet.png")
