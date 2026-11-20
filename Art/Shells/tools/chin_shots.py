import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
bpy.data.objects["SK_RobotFrame"].hide_render = True
cam, lights = K.studio((700, 700))
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/%s_face_" % sys.argv[-1])
for n, d in {"front": (0, -1, 0), "low": (-0.45, -1, -0.25)}.items():
    K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 168), span=34)
