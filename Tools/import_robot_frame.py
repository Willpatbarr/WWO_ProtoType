# Imports the Blender robot frame (SK_RobotFrame.fbx, written by export_unreal.py in the robot's
# asset folder) as a skeletal mesh, then builds its IK Rig and the retargeter from the mannequin,
# the same way Tools/build_retargeters.py does for the models in Config/CharacterModels.json.
#
# Run headless:
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
# WWO_ROBOT_FBX overrides the FBX path. Writes /Game/WWO/Characters/RobotFrame/SK_RobotFrame (plus
# its skeleton, physics asset and materials), /Game/WWO/Retarget/IK_robotframe and
# RTG_Manny_robotframe, and a report next to this file (import_robot_frame.log).
# It does not add the model to CharacterModels.json.
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
FBX = os.environ.get("WWO_ROBOT_FBX", "/Volumes/UnrealSSD/Robot Frame Assets/robot_frame_skeleton/unreal/SK_RobotFrame.fbx")
MESH_DIR = "/Game/WWO/Characters/RobotFrame"
OUT_DIR = "/Game/WWO/Retarget"
MANNY = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
KEY = "robotframe"
REPORT = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def fresh(name, cls, factory):
    path = "%s/%s" % (OUT_DIR, name)
    if lib.does_asset_exist(path):
        return unreal.load_asset(path)
    return tools.create_asset(name, OUT_DIR, cls, factory)


def import_mesh():
    task = unreal.AssetImportTask()
    task.filename = FBX
    task.destination_path = MESH_DIR
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])
    mesh = None
    for path in lib.list_assets(MESH_DIR, recursive=True):
        a = unreal.load_asset(path.split(".")[0])
        REPORT.append("imported %s (%s)" % (path, type(a).__name__))
        if isinstance(a, unreal.SkeletalMesh):
            mesh = a
    return mesh


def fix_materials(mesh):
    for slot in mesh.get_editor_property("materials"):
        base = slot.material_interface
        while base and isinstance(base, unreal.MaterialInstance):
            base = base.get_editor_property("parent")
        if not isinstance(base, unreal.Material):
            continue
        changed = []
        for flag in ("used_with_skeletal_mesh",):
            if not base.get_editor_property(flag):
                base.set_editor_property(flag, True)
                changed.append(flag)
        if changed:
            unreal.MaterialEditingLibrary.recompile_material(base)
            lib.save_loaded_asset(base)
            REPORT.append("%s now %s" % (base.get_path_name(), changed))


def make_rig(key, mesh):
    existed = lib.does_asset_exist("%s/IK_%s" % (OUT_DIR, key))
    rig = fresh("IK_" + key, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    if not existed or key != "Manny":      # the mannequin's rig is shared with the other models: leave it as it is
        ok_mesh = c.set_skeletal_mesh(mesh)
        ok_chains = c.apply_auto_generated_retarget_definition()
        REPORT.append("IK_%s: mesh %s, auto chains %s" % (key, ok_mesh, ok_chains))
        lib.save_loaded_asset(rig)
    for ch in c.get_retarget_chains():
        REPORT.append("  IK_%s chain %s: %s -> %s" % (key, ch.chain_name, c.get_retarget_chain_start_bone(ch.chain_name), c.get_retarget_chain_end_bone(ch.chain_name)))
    REPORT.append("  IK_%s root %s" % (key, c.get_retarget_root()))
    return rig


def make_retargeter(key, source_rig, target_rig, target_mesh):
    rtg = fresh("RTG_Manny_" + key, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, unreal.load_asset(MANNY))
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, target_mesh)
    c.remove_all_ops()
    c.add_default_ops()
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    for i in range(c.get_num_retarget_ops()):
        c.run_op_initial_setup(i)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Retarget pose: only the arms and legs are lined up with the mannequin's. The robot's spine,
    # neck, head and clavicles keep their own rest directions (aligning those to the mannequin's
    # forward-leaning neck and back-swept clavicles pulls the rigid head and shoulders out of place).
    T = unreal.RetargetSourceOrTarget.TARGET
    REPORT.append("controller api: %s" % [n for n in dir(c) if "align" in n or "pose" in n or "map" in n or "chain" in n])
    c.reset_retarget_pose(c.get_current_retarget_pose_name(T), [], T)
    limbs = ["upperarm_l", "lowerarm_l", "hand_l", "upperarm_r", "lowerarm_r", "hand_r",
             "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r"]
    c.auto_align_bones(limbs, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN, T)
    for b in ("pelvis", "spine_01", "spine_02", "neck_01", "head", "clavicle_l", "upperarm_l", "lowerarm_l", "thigh_l", "calf_l", "foot_l"):
        REPORT.append("  pose offset %s: %s" % (b, c.get_rotation_offset_for_retarget_pose_bone(b, T).rotator()))
    REPORT.append("RTG_Manny_%s ops %s" % (key, [str(c.get_op_name(i)) for i in range(c.get_num_retarget_ops())]))
    lib.save_loaded_asset(rtg)


def main():
    mesh = import_mesh()
    if not mesh:
        REPORT.append("FAILED: no skeletal mesh came out of %s" % FBX)
        return
    sk = mesh.get_editor_property("skeleton")
    b = mesh.get_bounds()
    REPORT.append("mesh %s, skeleton %s, %d material slots, bounds extent %s" % (
        mesh.get_path_name(), sk.get_path_name() if sk else None, len(mesh.get_editor_property("materials")), b.box_extent))
    fix_materials(mesh)
    manny_rig = make_rig("Manny", unreal.load_asset(MANNY))
    rig = make_rig(KEY, mesh)
    make_retargeter(KEY, manny_rig, rig, mesh)


try:
    main()
except Exception as e:      # keep the report even when a step fails
    import traceback
    REPORT.append("EXCEPTION: " + traceback.format_exc())
open(os.path.join(HERE, "import_robot_frame.log"), "w").write("\n".join(str(r) for r in REPORT) + "\n")
