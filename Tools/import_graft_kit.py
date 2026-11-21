"""Imports a flora's graft KIT: the small static pieces the anchor-based grafts place on the shell
parts' anchors and paths (Config/GraftRecipes.json, piece "mesh"), built in Blender by
~/Documents/WWO_Flora/tools/make_<flora>_kit.py into one folder of SM_<prefix>_<Piece>.fbx (+ kit.json).

The game does not draw these meshes as they are: it copies their geometry into the meshes it builds per
bone (Characters/WWOVineGraft.cpp, KitPiece), so they keep their vertices on the CPU (allow_cpu_access), as
Tools/import_vine_pieces.py's leaves do.

Settings (environment variables, VeilSpore defaults):
  WWO_KIT_FLORA      the flora's folder name under /Game/WWO/Flora (default VeilSpore)
  WWO_KIT_SRC        the folder of FBX files (default ~/Documents/WWO_Flora/kit_veilspore)
  WWO_KIT_PREFIX     only files starting with this are imported (default SM_VS_)
  WWO_KIT_MATERIALS  slot name -> material asset, as "Slot=/Game/Path/MI_X,Slot2=..." (default
                     VS_Fungus=/Game/WWO/Flora/VeilSpore/Materials/MI_VS_Fungus, the instance
                     Tools/import_robot_grafts.py makes for VeilSpore's atlas; run that first)

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/import_graft_kit.py -unattended -nullrhi

- Meshes: /Game/WWO/Flora/<Flora>/Kit/SM_<prefix>_<Piece>, static, one LOD, no Nanite, no collision
  (cosmetic), vertices kept on the CPU, their one slot given the flora's material by slot name. A piece's
  local frame (from Blender, default FBX axes): origin on the surface, X = the anchor's up, Z = out of the
  surface, so the game places it with FRotationMatrix::MakeFromXZ(Up, N). Blender's Y comes in mirrored
  (Unreal's Y = -Blender's); the log's bounds show it, to compare with kit.json's bounds_min/max_cm.
- Writes Tools/import_graft_kit.log. Safe to rerun: each mesh is rebuilt in place.
- Run Tools/make_graft_piece_lods.py after this: it gives the meshes the LODs the anchor grafts' farther detail
  levels copy (this script rebuilds them with one LOD).
"""
import os

import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
REPORT = []

FLORA = os.environ.get("WWO_KIT_FLORA", "VeilSpore")
SRC = os.path.expanduser(os.environ.get("WWO_KIT_SRC", "~/Documents/WWO_Flora/kit_veilspore"))
PREFIX = os.environ.get("WWO_KIT_PREFIX", "SM_VS_")
DEST = "/Game/WWO/Flora/%s/Kit" % FLORA
MATERIALS = dict(
    kv.split("=", 1) for kv in os.environ.get(
        "WWO_KIT_MATERIALS", "VS_Fungus=/Game/WWO/Flora/VeilSpore/Materials/MI_VS_Fungus").split(",") if "=" in kv)


def log(msg):
    REPORT.append(msg)
    unreal.log_warning("WWO kit: " + msg)


def import_task(src, folder, options):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    tools.import_asset_tasks([task])
    return [str(p) for p in task.get_editor_property("imported_object_paths")]


def mesh_options():
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("asset_type_sub_folders", False)
    p.set_editor_property("use_source_name_for_asset", True)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("import_static_meshes", True)
    meshes.set_editor_property("import_skeletal_meshes", False)
    for prop, value in (("combine_static_meshes", True), ("collision", False), ("generate_collision", False),
                        ("import_collision", False)):
        try:  # names differ between engine versions; none matters for the result
            meshes.set_editor_property(prop, value)
        except Exception:  # noqa: BLE001
            pass
    materials = p.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", False)
    try:
        materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    except Exception as e:  # noqa: BLE001
        log("texture pipeline: %s" % e)
    anims = p.get_editor_property("animation_pipeline")
    anims.set_editor_property("import_animations", False)
    stack = unreal.InterchangePipelineStackOverride()
    stack.add_pipeline(p)
    return stack


def import_piece(file, mats):
    name = os.path.splitext(file)[0]
    scratch = DEST + "/Import_" + name
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    import_task(os.path.join(SRC, file), scratch, mesh_options())
    mesh = None
    for p in eal.list_assets(scratch, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            mesh = a
    if not mesh:
        log("FAILED: no static mesh came out of %s" % file)
        eal.delete_directory(scratch)
        return None
    dest = DEST + "/" + name
    if eal.does_asset_exist(dest):
        eal.delete_asset(dest)
    eal.duplicate_asset(mesh.get_path_name(), dest)
    eal.delete_directory(scratch)
    out = eal.load_asset(dest)
    out.set_editor_property("allow_cpu_access", True)  # the game reads the vertices to build its meshes
    try:
        nanite = out.get_editor_property("nanite_settings")
        nanite.set_editor_property("enabled", False)
        out.set_editor_property("nanite_settings", nanite)
    except Exception as e:  # noqa: BLE001
        log("%s nanite: %s" % (name, e))
    out.set_editor_property("body_setup", None)
    # The flora's material by slot name.
    slots, missing = [], []
    for slot in out.get_editor_property("static_materials"):
        key = str(slot.get_editor_property("material_slot_name"))
        mi = mats.get(key)
        if mi:
            slot.set_editor_property("material_interface", mi)
        else:
            missing.append(key)
        slots.append(slot)
    out.set_editor_property("static_materials", slots)
    eal.save_loaded_asset(out)
    got = [str(s.get_editor_property("material_interface").get_name()) if s.get_editor_property("material_interface") else None
           for s in eal.load_asset(dest).get_editor_property("static_materials")]
    box = out.get_bounding_box()
    lo, hi = box.min, box.max
    log("%s: %d slots %s, missing %s, LODs %d, %d triangles, bounds (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f) cm" % (
        name, len(slots), got, missing, out.get_num_lods(), out.get_num_triangles(0), lo.x, lo.y, lo.z, hi.x, hi.y, hi.z))
    return out


def main():
    if not os.path.isdir(SRC):
        log("FAILED: no folder %s" % SRC)
        return
    mats = {}
    for slot, path in MATERIALS.items():
        m = eal.load_asset(path) if eal.does_asset_exist(path) else None
        if m:
            mats[slot] = m
        else:
            log("material %s for slot %s not found (run Tools/import_robot_grafts.py first)" % (path, slot))
    count = 0
    for f in sorted(os.listdir(SRC)):
        if f.startswith(PREFIX) and f.endswith(".fbx") and import_piece(f, mats):
            count += 1
    log("%s: %d kit pieces from %s into %s" % (FLORA, count, SRC, DEST))
    log("done")


try:
    main()
finally:
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "import_graft_kit.log"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
