import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/")
K.sheet([R + "shell2_chinp%d_face_%s.png" % (n, v) for v in ("front", "low") for n in (0, 1, 2, 3)], 4, R + "shell2_chin_width_options.png")
