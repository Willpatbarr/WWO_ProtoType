"""Imports VoltVine's leaves as small static meshes for the anchor grafts (Characters/WWOVineGraft.cpp copies
their geometry into the vines it lays over the worn shell parts): SM_VV_Leaf01-06, the fitted grafts' six
leaves, built by ~/Documents/WWO_Flora/tools/make_voltvine_pieces.py into ~/Documents/WWO_Flora/pieces_voltvine.

Run headless from the project folder (after Tools/import_robot_grafts.py has made VoltVine's materials):
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/import_vine_pieces.py -unattended -nullrhi

- Meshes: /Game/WWO/Characters/RobotFrame/Grafts/Pieces/SM_VV_Leaf<NN>, one LOD, no collision, their vertices kept
  on the CPU (allow_cpu_access: the game reads them to build its meshes).
- Materials by slot name, the fitted grafts' own: VV_Leaf (MI_VV_Leaf, the shared leaf sheet), VV_Stem (MI_VV_Stem).
Safe to rerun: everything is rebuilt in place. WWO_PIECES_SRC: another folder of exports.
"""
import os

import unreal

SRC = os.path.expanduser(os.environ.get("WWO_PIECES_SRC", "~/Documents/WWO_Flora/pieces_voltvine"))
MESHES = "/Game/WWO/Characters/RobotFrame/Grafts/Pieces"
MATERIALS = "/Game/WWO/Flora/VoltVine/Materials"
SLOTS = {"VV_Leaf": "MI_VV_Leaf", "VV_Stem": "MI_VV_Stem"}
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
REPORT = []


def log(msg):
    REPORT.append(msg)
    unreal.log_warning("WWO pieces: " + msg)


def options():
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("asset_type_sub_folders", False)
    p.set_editor_property("use_source_name_for_asset", True)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("import_static_meshes", True)
    meshes.set_editor_property("import_skeletal_meshes", False)
    for prop in ("collision", "generate_collision", "import_collision"):
        try:
            meshes.set_editor_property(prop, False)
        except Exception:  # noqa: BLE001
            pass
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


def import_piece(file):
    name = os.path.splitext(file)[0]
    scratch = MESHES + "/Import_" + name
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(SRC, file))
    task.set_editor_property("destination_path", scratch)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options())
    tools.import_asset_tasks([task])
    mesh = None
    for p in eal.list_assets(scratch, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            mesh = a
    if not mesh:
        log("FAILED: no static mesh came out of %s" % file)
        return None
    dest = MESHES + "/" + name
    if eal.does_asset_exist(dest):
        eal.delete_asset(dest)
    eal.duplicate_asset(mesh.get_path_name(), dest)
    eal.delete_directory(scratch)
    out = eal.load_asset(dest)
    out.set_editor_property("allow_cpu_access", True)
    try:
        nanite = out.get_editor_property("nanite_settings")
        nanite.set_editor_property("enabled", False)
        out.set_editor_property("nanite_settings", nanite)
    except Exception:  # noqa: BLE001
        pass
    missing = []
    mats = out.get_editor_property("static_materials")
    for slot in mats:
        key = str(slot.get_editor_property("material_slot_name"))
        mi = eal.load_asset(MATERIALS + "/" + SLOTS[key]) if key in SLOTS else None
        if mi:
            slot.set_editor_property("material_interface", mi)
        else:
            missing.append(key)
    out.set_editor_property("static_materials", mats)
    out.set_editor_property("body_setup", None)
    eal.save_loaded_asset(out)
    log("%s: %d slots %s, missing %s, LODs %d, %d triangles, bounds %s" % (
        name, len(mats), [str(m.get_editor_property("material_slot_name")) for m in mats], missing,
        out.get_num_lods(), out.get_num_triangles(0), out.get_bounds().box_extent))
    return out


def main():
    if not os.path.isdir(SRC):
        log("FAILED: no folder %s" % SRC)
        return
    count = 0
    for f in sorted(os.listdir(SRC)):
        if f.startswith("SM_VV_") and f.endswith(".fbx") and import_piece(f):
            count += 1
    log("%d pieces" % count)


try:
    main()
finally:
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "import_vine_pieces.log"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
