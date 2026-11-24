# Review sheet of the three enemy rigs: front and side of each, all at one scale (the frame is 3.2 m tall).
#   Blender -b -P enemy_rig_views.py
# Out: renders/enemy_rigs_sheet.png (soldier, swarm, tank; front then side). Left side orange, right side blue.
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shell_kit as K

SHELLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
R = SHELLS + "/renders/enemy_rig_"
shots = []
for enemy in ("soldier", "swarm", "tank"):
    bpy.ops.wm.open_mainfile(filepath=SHELLS + "/base/enemy_%s_rig.blend" % enemy)
    rig = bpy.data.objects["Armature"]; guide = bpy.data.objects["Guide_Bones"]
    guide.data.bevel_depth = 0.9
    mats = {}
    for key, rgb in (("mid", (0.8, 0.8, 0.8)), ("l", (1.0, 0.35, 0.05)), ("r", (0.1, 0.4, 1.0))):
        m = bpy.data.materials.new("guide_" + key); m.diffuse_color = (*rgb, 1); m.use_nodes = True
        m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*rgb, 1)
        guide.data.materials.append(m); mats[key] = len(mats)
    for s in guide.data.splines:                   # colour each tube by the side of the body its far end is on
        x = s.points[1].co.x
        s.material_index = mats["l" if x > 0.5 else "r" if x < -0.5 else "mid"]
    cam, lights = K.studio(res=(800, 1000))
    for view, d in (("front", (0, -1, 0)), ("side", (1, 0, 0))):
        p = R + enemy + "_" + view + ".png"
        K.shoot(p, cam, lights, d, target=(0, 0, 150), span=320)
        shots.append(p)
K.sheet(shots, 6, SHELLS + "/renders/enemy_rigs_sheet.png")
for p in shots: os.remove(p)
print("WWO wrote", SHELLS + "/renders/enemy_rigs_sheet.png")
