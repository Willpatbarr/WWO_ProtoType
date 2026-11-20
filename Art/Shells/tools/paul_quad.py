import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
bpy.data.objects["SK_RobotFrame"].hide_render = True
cam, lights = K.studio((1000, 1000))
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/shell2_paulquad_")
v = {"front": (0, -1, 0), "side": (1, 0, 0), "three_q": (0.7, -1, 0.5), "back_q": (0.7, 1, 0.25)}
for n, d in v.items(): K.shoot(R + n + ".png", cam, lights, d, target=(14, 3, 150), span=50)
K.sheet([R + n + ".png" for n in v], 2, (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/shell2_paul_quad.png"))
