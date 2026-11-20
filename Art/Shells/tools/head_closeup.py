import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
bpy.data.objects["SK_RobotFrame"].hide_render = True
cam, lights = K.studio((800, 800))
N = sys.argv[-1] if sys.argv[-1].startswith("shell") else "shell1"
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/%s_head_" % N)
v = {"three_q": (-0.7, -1, 0.05), "side": (1, 0, 0), "front": (0, -1, 0), "top_q": (-0.25, -1, 0.5)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 2, 170), span=60)
K.sheet([R + n + ".png" for n in v], 4, R + "sheet.png")
