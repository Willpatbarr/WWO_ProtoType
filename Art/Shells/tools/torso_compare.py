import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/")
v = ("front", "three_q", "side", "back_q", "top")
K.sheet([R + "base_torso_%s.png" % n for n in v] + [R + "shell2_torso_%s.png" % n for n in v], 5, R + "shell2_vest_compare.png")
