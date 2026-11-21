# Review: a shell as the game shows it (the always-on skeleton, in red, under the shell's four parts).
# What pokes through, and what is left bare?
#   Blender -b ../shell1_claude.blend -P game_fit_views.py -- 1
#   Blender -b ../base/robot_frame_v2_rigged.blend -P game_fit_views.py -- base
# Out: renders/fit/shell<N>_*.png and a sheet. WWO_PLAIN=1 keeps the skeleton's own colours.
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K, shell_split
which = sys.argv[-1]
for o in bpy.data.objects:
    if o.type == "MESH": o.hide_render = True
parts = shell_split.game_parts(which)
if not os.environ.get("WWO_PLAIN"):
    red = bpy.data.materials.new("FrameRed"); red.use_nodes = True
    red.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.8, 0.03, 0.02, 1)
    sk = parts["Skeleton"]
    for i in range(len(sk.data.materials)): sk.data.materials[i] = red
cam, lights = K.studio()
D = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/renders/fit"
os.makedirs(D, exist_ok=True)
R = D + "/shell%s_" % which
full = {"three_q": (-0.62, -1, 0.12), "front": (0, -1, 0), "back": (0, 1, 0), "side": (1, 0, 0), "rear_q": (0.62, 1, 0.12)}
for n, d in full.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 100), span=215)
K.sheet([R + n + ".png" for n in full], 5, R + "sheet.png")
up = {"up_front": (0, -1, 0.1), "up_three_q": (-0.62, -1, 0.25), "up_side": (1, 0, 0.1), "up_back": (0.3, 1, 0.2)}
for n, d in up.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 148), span=95)
K.sheet([R + n + ".png" for n in up], 4, R + "upper_sheet.png")
hd = {"head_front": (0, -1, 0.25), "head_three_q": (-0.62, -1, 0.35), "head_side": (1, 0, 0.15), "head_top": (0.2, -0.5, 1)}
for n, d in hd.items(): K.shoot(R + n + ".png", cam, lights, d, target=(0, 0, 178), span=42)
K.sheet([R + n + ".png" for n in hd], 4, R + "head_sheet.png")
