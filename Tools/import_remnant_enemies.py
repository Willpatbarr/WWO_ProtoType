"""Imports the three Remnant enemy models onto the skeletons their enemies already animate with.

Source: the private WWO_EnemySkeletons repo (branch remnant-models), cloned beside this project
(WWO_REMNANT_SRC to use another folder of FBX), Built/fbx/:
  SK_BleakSwarm.fbx      swarm    on /Game/ParagonMinions/.../Minion_Lane_Core_Skeleton
  SK_CinderSoldier.fbx   soldier  on /Game/Robot_scout_R_21/Mesh/UE4_Mannequin_Skeleton
  SK_IronBark.fbx        tank     on /Game/ParagonSteel/.../Steel_Skeleton (the game draws it at 0.55)
Built by Art/Shells/tools/make_bleakswarm.py, make_cindersoldier.py, make_ironbark.py (-- export).

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/import_remnant_enemies.py -unattended -nullrhi

- Everything goes to /Game/Remnants (Content/Remnants, git-ignored: the meshes carry Fab skeletons and this repo
  is public; the built assets are kept in WWO_EnemySkeletons, Content/Remnants). Config/CharacterModels.json's
  soldier / swarm / tank entries point at the meshes.
- Materials: M_Remnant reads the vertex colour (each role's colour, every strand its own shade) and glows it
  by Glow; one instance per material slot (MI_<slot minus M_>) with that role's roughness and glow.
Safe to rerun: the meshes and instances are replaced.
"""
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.expanduser(os.environ.get(
    "WWO_REMNANT_SRC", os.path.join(os.path.dirname(os.path.dirname(HERE)), "WWO_EnemySkeletons", "Built", "fbx")))
DEST = "/Game/Remnants"
MATS = DEST + "/Materials"
MODELS = {
    "SK_BleakSwarm": "/Game/ParagonMinions/Characters/Minions/Down_Minions/Meshes/Minion_Lane_Core_Skeleton",
    "SK_CinderSoldier": "/Game/Robot_scout_R_21/Mesh/UE4_Mannequin_Skeleton",
    "SK_IronBark": "/Game/ParagonSteel/Characters/Heroes/Steel/Meshes/Steel_Skeleton",
}
# Per material slot: roughness (from the make_*.py scripts) and glow. Blender's emission strengths (1.3, 1.6)
# read as barely lit in the game (as shell 3's did), so the glowing roles get the shells' level.
ROLES = {
    "M_Bleak_Wood": (0.85, 0), "M_Bleak_Sinew": (0.7, 0), "M_Bleak_Bone": (0.55, 0), "M_Bleak_Leaf": (0.6, 0),
    "M_Bleak_Vine": (0.6, 0),
    "M_Cinder_Wood": (0.85, 0), "M_Cinder_Sinew": (0.7, 0), "M_Cinder_Bone": (0.55, 0), "M_Cinder_Leaf": (0.55, 0),
    "M_Cinder_Vine": (0.6, 0), "M_Cinder_Ember": (0.4, 10.0),
    "M_Iron_Wood": (0.9, 0), "M_Iron_Root": (0.8, 0), "M_Iron_Bark": (0.85, 0), "M_Iron_Leaf": (0.6, 0),
    "M_Iron_Cap": (0.5, 0), "M_Iron_Amber": (0.3, 12.0),
}
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
REPORT = []


def log(msg):
    REPORT.append(msg)
    unreal.log_warning("WWO remnants: " + msg)


def make_material():
    path = MATS + "/M_Remnant"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset("M_Remnant", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -600, 0)
    rough = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 200)
    rough.set_editor_property("parameter_name", "Roughness"); rough.set_editor_property("default_value", 0.7)
    glow = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 320)
    glow.set_editor_property("parameter_name", "Glow"); glow.set_editor_property("default_value", 0.0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 300)
    mel.connect_material_property(vc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_expressions(vc, "", mul, "A")
    mel.connect_material_expressions(glow, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def instance(parent, slot):
    name = "MI_" + slot[2:] if slot.startswith("M_") else "MI_" + slot
    path = "%s/%s" % (MATS, name)
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    rough, glow = ROLES.get(slot, (0.7, 0))
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(rough))
    mel.set_material_instance_scalar_parameter_value(mi, "Glow", float(glow))
    eal.save_loaded_asset(mi)
    return mi


def options(skeleton):
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("asset_type_sub_folders", False)
    p.set_editor_property("use_source_name_for_asset", True)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_SKELETAL_MESH)
    try:
        common.set_editor_property("vertex_color_import_option", unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    except Exception as e:  # noqa: BLE001
        log("vertex colour option: %s" % e)
    skel = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
    skel.set_editor_property("skeleton", skeleton)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("import_skeletal_meshes", True)
    meshes.set_editor_property("import_static_meshes", False)
    meshes.set_editor_property("create_physics_asset", False)
    mats = p.get_editor_property("material_pipeline")
    mats.set_editor_property("import_materials", False)
    try:
        mats.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    except Exception as e:  # noqa: BLE001
        log("texture pipeline: %s" % e)
    p.get_editor_property("animation_pipeline").set_editor_property("import_animations", False)
    stack = unreal.InterchangePipelineStackOverride()
    stack.add_pipeline(p)
    return stack


def import_model(name, skeleton_path, parent):
    skeleton = eal.load_asset(skeleton_path)
    src = os.path.join(SRC, name + ".fbx")
    if not skeleton or not os.path.exists(src):
        log("FAILED %s: skeleton %s, fbx %s" % (name, bool(skeleton), os.path.exists(src)))
        return
    scratch = DEST + "/Import_" + name
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    task = unreal.AssetImportTask()
    for k, v in (("filename", src), ("destination_path", scratch), ("automated", True), ("replace_existing", True),
                 ("save", True), ("options", options(skeleton))):
        task.set_editor_property(k, v)
    tools.import_asset_tasks([task])
    mesh = None
    for p in eal.list_assets(scratch, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, unreal.SkeletalMesh):
            mesh = a
    if not mesh:
        log("FAILED: no skeletal mesh came out of %s" % src)
        return
    dest = DEST + "/" + name
    if eal.does_asset_exist(dest):
        eal.delete_asset(dest)
    eal.duplicate_asset(mesh.get_path_name(), dest)
    eal.delete_directory(scratch)
    out = eal.load_asset(dest)
    sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    try:
        sub.regenerate_lod(out, 3, True, False)
    except Exception as e:  # noqa: BLE001
        log("%s: no LODs (%s)" % (name, e))
    slots = []
    for slot in out.get_editor_property("materials"):
        key = str(slot.get_editor_property("material_slot_name"))
        slot.set_editor_property("material_interface", instance(parent, key))
        slots.append(slot)
    out.set_editor_property("materials", slots)
    eal.save_loaded_asset(out)
    sk = out.get_editor_property("skeleton")
    log("%s: skeleton %s, slots %s, LODs %d, bounds %s" % (
        name, sk.get_path_name() if sk else None, [str(s.get_editor_property("material_slot_name")) for s in slots],
        sub.get_lod_count(out), out.get_bounds().box_extent))


def main():
    parent = make_material()
    for name, skel in MODELS.items():
        import_model(name, skel, parent)
    log("done")
    with open(os.path.join(HERE, "import_remnant_enemies.log"), "w") as f:
        f.write("\n".join(REPORT) + "\n")


main()
