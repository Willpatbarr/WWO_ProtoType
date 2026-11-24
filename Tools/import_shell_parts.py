"""Imports the robot's swappable shell parts: the always-on skeleton and, for each shell, a head,
core, arms and legs mesh, all skinned to SK_RobotFrame's skeleton.

Source: ~/Documents/WWO_Shells/export/*.fbx (WWO_SHELL_SRC to use another folder), written by
~/Documents/WWO_Shells/tools/export_shells.py:
  SK_RobotSkeleton          the base robot's joints, struts and hands (shown under every shell)
  SK_Shell0_<Slot>          the base robot's own armour
  SK_Shell1_<Slot>          shell 1 ("sleek")
  SK_Shell2_<Slot>          shell 2 ("scout")
  SK_Shell3_<Slot>          shell 3 (the tan one, from Will's sketches)
  SK_Shell4_<Slot>          shell 4 (bone-white plates over tan, from Will's paintings; Will calls it shell 5)
Slots: Head, Core, Arms, Legs.

Run headless from the project folder (after Tools/create_robot_materials.py has made M_WWO_Robot):
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/import_shell_parts.py -unattended -nullrhi

- Meshes: /Game/WWO/Characters/RobotFrame/Shells/<name>, three LODs each. UWWOBodyComponent shows the
  skeleton as the model and the four parts on its pose.
- Materials: the base robot's slots (M_Robot_<nn>_<hex>) take its MI_Robot_<role>; a shell's slots
  (M_Shell<N>_<Role>) take MI_Shell<N>_<Role> in /Game/WWO/Characters/RobotFrame/Shells/Materials,
  instances of M_WWO_Robot, one per shell and role so a part's colour is a parameter later.
Safe to rerun: the Shells folder is deleted and rebuilt.
"""
import os

import unreal

SRC = os.path.expanduser(os.environ.get("WWO_SHELL_SRC", "~/Documents/WWO_Shells/export"))
MESHES = "/Game/WWO/Characters/RobotFrame/Shells"
MATS = MESHES + "/Materials"
ROBOT = "/Game/WWO/Characters/RobotFrame/SK_RobotFrame"
ROBOT_MATS = "/Game/WWO/Characters/RobotFrame/Materials"
PARENT = "/Game/WWO/Materials/M_WWO_Robot"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
REPORT = []

# The base robot's slots (as Tools/create_robot_materials.py).
ROBOT_SLOTS = {
    "M_Robot_00_0c0c0c": "Joint", "M_Robot_01_9e9e9e": "Joint", "M_Robot_02_070707": "Joint", "M_Robot_03_212123": "Joint",
    "M_Robot_04_050505": "Joint", "M_Robot_05_e5720c": "Joint", "M_Robot_06_161616": "Armour", "M_Robot_07_ffffff": "Joint",
    "M_Robot_08_7f7f7f": "Trim", "M_Robot_09_262626": "Trim", "M_Robot_10_0f1114": "Joint", "M_Robot_11_1c2166": "Joint",
    "M_Robot_12_cccccc": "Visor", "M_Robot_13_1c1e1e": "Armour",
}
# The shells' roles (as the Blender builds' shell_kit.ROLES): colour, roughness, metallic, wear.
WEAR = dict(Speckle=0.18, Blotch=0.1, RoughnessVar=0.07, Bump=0.05)
SMOOTH = dict(Speckle=0.08, Blotch=0.08, RoughnessVar=0.08, Bump=0.02)
PLAIN = dict(Speckle=0.0, Blotch=0.0, RoughnessVar=0.0, Bump=0.0)
SHELL_ROLES = {
    "Primary": dict(Color=(0.036, 0.036, 0.039), Roughness=0.48, Metallic=0.35, **WEAR),
    "PrimaryLight": dict(Color=(0.052, 0.052, 0.056), Roughness=0.48, Metallic=0.35, **WEAR),
    "Secondary": dict(Color=(0.019, 0.019, 0.021), Roughness=0.4, Metallic=0.45, **SMOOTH),
    "Frame": dict(Color=(0.012, 0.012, 0.013), Roughness=0.42, Metallic=0.7, **SMOOTH),
}
SHELL_GLOW = {
    "1": dict(Color=(0.8, 0.82, 0.85), Roughness=0.3, Metallic=0.0, EmissiveColor=(0.9, 0.95, 1.0), Glow=14.0, **PLAIN),
    "2": dict(Color=(0.5, 1.0, 0.05), Roughness=0.3, Metallic=0.0, EmissiveColor=(0.22, 1.0, 0.0), Glow=8.0, **PLAIN),
    "3": dict(Color=(0.0, 0.0, 0.0), Roughness=0.3, Metallic=0.0, EmissiveColor=(1.0, 0.33, 0.0), Glow=12.0, **PLAIN),   # (its Blender strength, 1.3, read as barely lit in the game)
    "4": dict(Color=(0.0, 0.0, 0.0), Roughness=0.3, Metallic=0.0, EmissiveColor=(0.25, 1.0, 0.85), Glow=12.0, **PLAIN),   # teal visor bars; same Blender strength as 3
}
# A shell's own colours for a role, over SHELL_ROLES (as its make_shell<N>.py overrides shell_kit.ROLES).
SHELL_OWN = {
    "3": {
        "Primary": dict(Color=(0.74, 0.57, 0.36), Roughness=0.5, Metallic=0.1),
        "Secondary": dict(Color=(0.30, 0.19, 0.09), Roughness=0.45, Metallic=0.1),
        "Frame": dict(Color=(0.040, 0.036, 0.024), Roughness=0.55, Metallic=0.2),
    },
    "4": {
        "Primary": dict(Color=(0.66, 0.61, 0.53), Roughness=0.55, Metallic=0.05),
        "Secondary": dict(Color=(0.33, 0.27, 0.21), Roughness=0.5, Metallic=0.15),
        "Frame": dict(Color=(0.035, 0.035, 0.04), Roughness=0.5, Metallic=0.4),
    },
}


def log(msg):
    REPORT.append(msg)
    unreal.log_warning("WWO shells: " + msg)


def import_task(src, folder, options):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    tools.import_asset_tasks([task])


def mesh_options(skeleton):
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("asset_type_sub_folders", False)
    p.set_editor_property("use_source_name_for_asset", True)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_SKELETAL_MESH)
    # Keep Blender's normals. Recomputing them welds the corners of flat facets and shades hard plates round.
    common.set_editor_property("recompute_normals", False)
    skel = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
    skel.set_editor_property("skeleton", skeleton)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("import_skeletal_meshes", True)
    meshes.set_editor_property("import_static_meshes", False)
    meshes.set_editor_property("create_physics_asset", False)
    materials = p.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    try:
        materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    except Exception as e:  # noqa: BLE001
        log("texture pipeline: %s" % e)
    p.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    stack = unreal.InterchangePipelineStackOverride()
    stack.add_pipeline(p)
    return stack


def shell_instance(parent, name, values):
    path = "%s/%s" % (MATS, name)
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    for k, v in values.items():
        if isinstance(v, tuple):
            mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v, 1.0))
        else:
            mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    eal.save_loaded_asset(mi)
    return mi


def material_for(slot, parent, made):
    """The game material for a mesh's slot name, or None."""
    if slot in made:
        return made[slot]
    mi = None
    if slot in ROBOT_SLOTS:
        mi = eal.load_asset("%s/MI_Robot_%s" % (ROBOT_MATS, ROBOT_SLOTS[slot]))
    elif slot.startswith("M_Shell") and "_" in slot[7:]:
        shell, role = slot[7:].split("_", 1)
        values = SHELL_GLOW.get(shell) if role == "Glow" else SHELL_ROLES.get(role)
        if values and role != "Glow":
            values = dict(values, **SHELL_OWN.get(shell, {}).get(role, {}))
        if values:
            mi = shell_instance(parent, "MI_Shell%s_%s" % (shell, role), values)
    made[slot] = mi
    return mi


def keep_normals(sub, mesh, name):
    """Every LOD builds with the imported normals (a skeletal mesh's own default is to recompute them)."""
    for i in range(sub.get_lod_count(mesh)):
        bs = sub.get_lod_build_settings(mesh, i)
        if bs.get_editor_property("recompute_normals"):
            bs.set_editor_property("recompute_normals", False)
            sub.set_lod_build_settings(mesh, i, bs)
    left = [i for i in range(sub.get_lod_count(mesh)) if sub.get_lod_build_settings(mesh, i).get_editor_property("recompute_normals")]
    if left:
        log("%s: LODs %s still recompute normals" % (name, left))


def import_part(file, skeleton, parent, made):
    name = os.path.splitext(file)[0]
    scratch = MESHES + "/Import_" + name
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    import_task(os.path.join(SRC, file), scratch, mesh_options(skeleton))
    mesh = None
    for p in eal.list_assets(scratch, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, unreal.SkeletalMesh):
            mesh = a
    if not mesh:
        log("FAILED: no skeletal mesh came out of %s" % file)
        return None
    dest = MESHES + "/" + name
    if eal.does_asset_exist(dest):
        eal.delete_asset(dest)
    eal.duplicate_asset(mesh.get_path_name(), dest)
    eal.delete_directory(scratch)
    out = eal.load_asset(dest)
    sk = out.get_editor_property("skeleton")
    sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    keep_normals(sub, out, name)
    try:
        sub.regenerate_lod(out, 3, True, False)
    except Exception as e:  # noqa: BLE001
        log("%s: no LODs (%s)" % (name, e))
    slots, missing = [], []
    for slot in out.get_editor_property("materials"):
        key = str(slot.get_editor_property("material_slot_name"))
        mi = material_for(key, parent, made)
        if mi:
            slot.set_editor_property("material_interface", mi)
        else:
            missing.append(key)
        slots.append(slot)
    out.set_editor_property("materials", slots)
    keep_normals(sub, out, name)
    eal.save_loaded_asset(out)
    log("%s: skeleton %s, slots %s, missing %s, LODs %d, verts %s, bounds %s" % (
        name, sk.get_name() if sk else None, [str(s.get_editor_property("material_slot_name")) for s in slots], missing,
        sub.get_lod_count(out), [sub.get_num_verts(out, i) for i in range(sub.get_lod_count(out))], out.get_bounds().box_extent))
    return out


def main():
    robot = eal.load_asset(ROBOT)
    skeleton = robot.get_editor_property("skeleton") if robot else None
    parent = eal.load_asset(PARENT)
    if not skeleton or not parent:
        log("FAILED: no robot skeleton (%s) or no M_WWO_Robot (%s)" % (ROBOT, PARENT))
        return
    # Start clean: a mesh that is already loaded cannot be replaced by a duplicate (the old one stays).
    if eal.does_directory_exist(MESHES):
        eal.delete_directory(MESHES)
    made, count = {}, 0
    for f in sorted(os.listdir(SRC)):
        if f.endswith(".fbx") and import_part(f, skeleton, parent, made):
            count += 1
    eal.save_loaded_asset(skeleton)   # any bones it gained
    log("done: %d meshes" % count)


try:
    main()
finally:
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "import_shell_parts.log"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
