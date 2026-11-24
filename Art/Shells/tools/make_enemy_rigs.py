# Builds one Blender rig per enemy from the Unreal skeleton assets in the WWO_EnemySkeletons repo.
#   Blender -b -P make_enemy_rigs.py                              skeletons repo cloned beside the WWO folder
#   Blender -b -P make_enemy_rigs.py -- /path/to/WWO_EnemySkeletons
# Out: base/enemy_<soldier|swarm|tank>_rig.blend, each holding
#   Armature      the skeleton: same bone names, parents and rest transforms as the Unreal asset
#   Guide_Bones   thin tubes from each joint to its children, to see the body (bones themselves are short
#                 stubs: an Unreal bone's own axes rarely point down the limb). Not exported.
# Same conventions as base/robot_frame_v2_rigged.blend: 1 unit = 1 cm, the character faces -Y, and
# export_shells.py's FBX settings (bone axes Y / X, no leaf bones) write the bones back unchanged.
# Each run ends with an FBX export and re-import that must reproduce every bone's rest transform.
import bpy, sys, os, tempfile
from mathutils import Matrix, Quaternion, Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_skeleton

SHELLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC = args[0] if args else os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(SHELLS))), "WWO_EnemySkeletons")

# enemy -> skeleton asset. The game draws the tank (also the FootRace boss) at 0.55 of this size.
RIGS = {
    "soldier": "Content/Robot_scout_R_21/Mesh/UE4_Mannequin_Skeleton.uasset",
    "swarm": "Content/ParagonMinions/Characters/Minions/Down_Minions/Meshes/Minion_Lane_Core_Skeleton.uasset",
    "tank": "Content/ParagonSteel/Characters/Heroes/Steel/Meshes/Steel_Skeleton.uasset",
}


def log(*a): print("WWO", *a, flush=True)


def rest_matrices(bones):
    out = []
    for b in bones:
        q, p = ue_skeleton.to_blender(b.wquat, b.wpos)
        m = Quaternion((q[3], q[0], q[1], q[2])).normalized().to_matrix().to_4x4(); m.translation = Vector(p)
        out.append(m)
    return out


def build(name, bones):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"; sc.unit_settings.scale_length = 0.01
    arm = bpy.data.armatures.new(name); rig = bpy.data.objects.new("Armature", arm)
    sc.collection.objects.link(rig); bpy.context.view_layer.objects.active = rig
    arm.display_type = "STICK"; rig.show_in_front = True
    rest = rest_matrices(bones)
    kids = {}
    for k, b in enumerate(bones): kids.setdefault(b.parent, []).append(k)
    bpy.ops.object.mode_set(mode="EDIT")
    ebs = []
    for k, b in enumerate(bones):
        e = arm.edit_bones.new(b.name)
        assert e.name == b.name, (e.name, b.name)
        reach = [(rest[c].translation - rest[k].translation).length for c in kids.get(k, [])]
        e.head = (0, 0, 0); e.tail = (0, min(5.0, max(1.0, 0.25 * max(reach, default=8.0))), 0)
        e.matrix = rest[k]
        if b.parent >= 0: e.parent = ebs[b.parent]; e.use_connect = False
        ebs.append(e)
    bpy.ops.object.mode_set(mode="OBJECT")

    cu = bpy.data.curves.new("Guide_Bones", "CURVE"); cu.dimensions = "3D"; cu.bevel_depth = 0.5; cu.bevel_resolution = 2
    body = set()                                         # the pelvis and everything under it: no IK or ragdoll helpers
    for k, b in enumerate(bones):
        if b.name.lower() == "pelvis" or b.parent in body: body.add(k)
    for k, b in enumerate(bones):
        if b.parent not in body: continue
        a, c = rest[b.parent].translation, rest[k].translation
        if not 0.05 < (c - a).length < 90: continue      # helpers sitting on their parent, far-flung attach points
        s = cu.splines.new("POLY"); s.points.add(1)
        s.points[0].co = (*a, 1); s.points[1].co = (*c, 1)
    guide = bpy.data.objects.new("Guide_Bones", cu); sc.collection.objects.link(guide)
    guide.hide_select = True
    return rig, rest


def round_trip(rig, bones, rest):
    """Export as export_shells.py does, read the FBX back, and return the worst bone error (cm, and matrix terms)."""
    me = bpy.data.meshes.new("probe"); me.from_pydata([(0, 0, 0), (1, 0, 0), (0, 0, 1)], [], [(0, 1, 2)])
    probe = bpy.data.objects.new("probe", me); bpy.context.scene.collection.objects.link(probe)
    probe.vertex_groups.new(name=bones[0].name).add([0, 1, 2], 1.0, "REPLACE")
    probe.modifiers.new("Armature", "ARMATURE").object = rig; probe.parent = rig
    for o in bpy.context.scene.objects: o.select_set(False)
    rig.select_set(True); probe.select_set(True); bpy.context.view_layer.objects.active = rig
    path = os.path.join(tempfile.mkdtemp(), "probe.fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"},
                             add_leaf_bones=False, mesh_smooth_type="FACE", bake_anim=False,
                             primary_bone_axis="Y", secondary_bone_axis="X", axis_forward="-Z", axis_up="Y")
    bpy.data.objects.remove(probe, do_unlink=True)
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path, primary_bone_axis="Y", secondary_bone_axis="X")
    new = [o for o in bpy.data.objects if o not in before]
    back = next(o for o in new if o.type == "ARMATURE")
    # (FBX lists bones depth first, so their order can differ; Unreal matches them by name)
    assert sorted(b.name for b in back.data.bones) == sorted(b.name for b in bones), "bone names changed"
    dpos = drot = 0.0; worst = ""
    for k, b in enumerate(bones):
        got = back.data.bones[b.name]
        assert (got.parent.name if got.parent else None) == (bones[b.parent].name if b.parent >= 0 else None), b.name
        m = back.matrix_world @ got.matrix_local
        dpos = max(dpos, (m.translation - rest[k].translation).length)
        r = max(abs(m.to_3x3().normalized()[i][j] - rest[k][i][j]) for i in range(3) for j in range(3))
        if r > drot: drot, worst = r, b.name
    for o in new: bpy.data.objects.remove(o, do_unlink=True)
    return dpos, drot, worst


for enemy, rel in RIGS.items():
    src = os.path.join(SRC, rel)
    bones = ue_skeleton.read(src)
    rig, rest = build(os.path.basename(rel).replace(".uasset", ""), bones)
    dpos, drot, worst = round_trip(rig, bones, rest)
    xs = [m.translation for m in rest]
    log("%-8s %3d bones  head %.1f cm  reach x %.0f  FBX round trip: position %.4f cm, rotation %.5f (%s)" % (
        enemy, len(bones), rest[[b.name.lower() for b in bones].index("head")].translation.z, max(v.x for v in xs), dpos, drot, worst))
    # 0.004 in a matrix term is under a quarter of a degree (FBX stores rest rotations as Euler angles)
    assert dpos < 0.01 and drot < 0.004, "FBX round trip moved the bones"
    out = os.path.join(SHELLS, "base", "enemy_%s_rig.blend" % enemy)
    bpy.ops.wm.save_as_mainfile(filepath=out, check_existing=False)
    log("wrote", out)
