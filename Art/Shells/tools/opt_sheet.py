import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
R = (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/")
tag, out = sys.argv[-2], sys.argv[-1]
K.sheet([R + "%s%d_head_%s.png" % (tag, n, v) for v in ("front", "side", "three_q") for n in (1, 2, 3)], 3, R + out)
