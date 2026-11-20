import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
bpy.data.objects["SK_RobotFrame"].hide_render = True
cam, lights = K.studio((1000, 1000))
TAG = sys.argv[-1] if sys.argv[-1].startswith("shell2") else "shell2"
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/%s_headquad_" % TAG)
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (-0.7, -1, 0.08), "low": (-0.55, -1, -0.5)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 1, 174), span=46)
K.sheet([R + n + ".png" for n in v], 2, (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/%s_head_quad.png" % TAG))
