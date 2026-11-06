# Prepares the character models: turns on the skeletal mesh usage flag their materials need (Grave
# Mercer's ships without it and renders as the default grey), then builds the IK Rigs and IK
# Retargeters that let the models copy the mannequin's pose
# at runtime (UWWOBodyComponent: the mannequin stays as an invisible animation driver, the model
# follows it through FAnimNode_RetargetPoseFromMesh).
#
# Run headless:
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
# Writes /Game/WWO/Retarget/IK_<key> and RTG_Manny_<key>, and a report next to this file
# (build_retargeters.log). Models are listed in Config/CharacterModels.json.
import json
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
OUT_DIR = "/Game/WWO/Retarget"
MANNY = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
REPORT = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def fresh(name, cls, factory):
    # Reuse an existing asset (the retargeters reference the rigs, so they can't be deleted); its
    # setup is redone below.
    path = "%s/%s" % (OUT_DIR, name)
    if lib.does_asset_exist(path):
        return unreal.load_asset(path)
    return tools.create_asset(name, OUT_DIR, cls, factory)


def make_rig(key, mesh_path):
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        REPORT.append("%s: mesh %s did not load" % (key, mesh_path))
        return None
    rig = fresh("IK_" + key, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    ok_mesh = c.set_skeletal_mesh(mesh)
    ok_chains = c.apply_auto_generated_retarget_definition()
    chains = [str(ch.chain_name) for ch in c.get_retarget_chains()]
    REPORT.append("%s: mesh %s, auto chains %s, %d chains %s, root %s" % (key, ok_mesh, ok_chains, len(chains), chains, c.get_retarget_root()))
    lib.save_loaded_asset(rig)
    return rig


def make_retargeter(key, source_rig, target_rig, target_mesh_path):
    rtg = fresh("RTG_Manny_" + key, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, unreal.load_asset(MANNY))
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, unreal.load_asset(target_mesh_path))
    c.remove_all_ops()
    c.add_default_ops()
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    for i in range(c.get_num_retarget_ops()):
        c.run_op_initial_setup(i)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # A-pose vs T-pose: line the target's chains up with the mannequin's in the retarget pose.
    c.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    ops = [str(c.get_op_name(i)) for i in range(c.get_num_retarget_ops())]
    REPORT.append("  RTG_Manny_%s ops %s" % (key, ops))
    lib.save_loaded_asset(rtg)


def fix_materials(key, mesh_path):
    # Every base material the mesh uses must allow skeletal meshes (and cloth), or it falls back to grey.
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        return
    for slot in mesh.get_editor_property("materials"):
        mi = slot.material_interface
        base = mi
        while base and isinstance(base, unreal.MaterialInstance):
            base = base.get_editor_property("parent")
        if not isinstance(base, unreal.Material):
            continue
        changed = []
        for flag in ("used_with_skeletal_mesh", "used_with_clothing", "used_with_morph_targets"):
            if not base.get_editor_property(flag):
                base.set_editor_property(flag, True)
                changed.append(flag)
        if changed:
            unreal.MaterialEditingLibrary.recompile_material(base)
            lib.save_loaded_asset(base)
            REPORT.append("%s: %s now %s" % (key, base.get_path_name(), changed))


def main():
    models = json.load(open(os.path.join(PROJECT, "Config", "CharacterModels.json")))["models"]
    for key, m in models.items():
        fix_materials(key, m["mesh"].split(".")[0])
    manny_rig = make_rig("Manny", MANNY)
    for key, m in models.items():
        rig = make_rig(key, m["mesh"].split(".")[0])
        if rig and manny_rig:
            make_retargeter(key, manny_rig, rig, m["mesh"].split(".")[0])
    open(os.path.join(HERE, "build_retargeters.log"), "w").write("\n".join(REPORT) + "\n")


main()
