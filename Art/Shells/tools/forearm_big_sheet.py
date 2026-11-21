# Puts the forearm-size trials side by side: columns are FA_BIG 1.10, 1.25, 1.40, 1.55; rows front, side, 3/4.
#   for b in 1.10 1.25 1.40 1.55; do WWO_TRIAL=forearm_big_$b WWO_FA_BIG=$b WWO_TRIAL_VIEW="33,2,118,84" Blender -b ../base/robot_frame_v2_rigged.blend -P make_shell3.py; done
#   Blender -b -P forearm_big_sheet.py
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/options/forearm_big_"
K.sheet([R + "%s_%s.png" % (n, v) for v in ("front", "side", "three_q") for n in ("1.10", "1.25", "1.40", "1.55")], 4, R + "sheet.png")
