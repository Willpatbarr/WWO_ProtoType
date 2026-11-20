import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
bpy.data.objects["SK_RobotFrame"].hide_render = True
bpy.data.objects["SK_Shell2_Arms"].hide_render = True
cam, lights = K.studio((800, 800))
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/shell2_torso_profile")
K.shoot(R + "_side.png", cam, lights, (1, 0, 0), target=(0, 3, 146), span=62)
K.shoot(R + "_q.png", cam, lights, (0.8, -0.6, 0.2), target=(0, 3, 146), span=62)
K.sheet([R + "_side.png", R + "_q.png"], 2, R + ".png")
