import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/")
K.sheet([R + "shell2_eye%d_head_%s.png" % (n, v) for v in ("front", "three_q") for n in (1, 2, 3)], 3, R + "shell2_eye_options.png")
