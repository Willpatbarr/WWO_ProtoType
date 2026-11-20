import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
bpy.data.objects["SK_RobotFrame"].hide_render = True
cam, lights = K.studio((800, 800))
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/%s_head_" % sys.argv[-1])
for n, d in {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (-0.7, -1, 0.08)}.items():
    K.shoot(R + n + ".png", cam, lights, d, target=(0, -1, 171), span=38)
