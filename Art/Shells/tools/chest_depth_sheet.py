# Puts the chest-depth trials side by side: columns are 0 (as built), 1, 2, 3 cm; rows side, 3/4, front.
#   for n in 0 1 2 3; do WWO_TRIAL=chest_depth_$n WWO_CHEST_DEPTH=$n Blender -b ../base/robot_frame_v2_rigged.blend -P make_shell3.py; done
#   Blender -b -P chest_depth_sheet.py
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/options/chest_depth_"
K.sheet([R + "%d_%s.png" % (n, v) for v in ("side", "three_q", "front") for n in (0, 1, 2, 3)], 4, R + "sheet.png")
