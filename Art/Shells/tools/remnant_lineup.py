# The Remnant enemies side by side at true scale, from their built files.
#   Blender -b -P remnant_lineup.py
# Out: renders/remnant_lineup.png (front, then three-quarter). Skips a model whose <name>_claude.blend isn't built.
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K

SHELLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS = [("bleakswarm", "SK_BleakSwarm"), ("cindersoldier", "SK_CinderSoldier"), ("ironbark", "SK_IronBark")]
GAP = {1: 0.0, 2: 150.0, 3: 215.0}      # cm between centre lines, by how many models are built

bpy.ops.wm.read_factory_settings(use_empty=True)
found = [(f, n) for f, n in MODELS if os.path.exists("%s/%s_claude.blend" % (SHELLS, f))]
for i, (f, n) in enumerate(found):
    with bpy.data.libraries.load("%s/%s_claude.blend" % (SHELLS, f)) as (src, dst): dst.objects = [n]
    ob = dst.objects[0]; bpy.context.scene.collection.objects.link(ob)
    rig = ob.parent; bpy.context.scene.collection.objects.link(rig)
    rig.location.x = (i - (len(found) - 1) / 2) * GAP[len(found)]
big = len(found) > 2
cam, lights = K.studio(res=(int((780 if big else 600) * len(found) + 300), 1200))
bpy.context.scene.world.node_tree.nodes["Background.001"].inputs["Color"].default_value = (0.42, 0.41, 0.43, 1)
R = SHELLS + "/renders/remnant_lineup_"
for n, d in (("front", (0, -1, 0)), ("three_q", (-0.5, -1, 0.1))): K.shoot(R + n + ".png", cam, lights, d, target=(0, 4, 140 if big else 106), span=310 if big else 240, lens=110)
K.sheet([R + "front.png", R + "three_q.png"], 2, SHELLS + "/renders/remnant_lineup.png")
for n in ("front", "three_q"): os.remove(R + n + ".png")
print("WWO wrote", SHELLS + "/renders/remnant_lineup.png")
