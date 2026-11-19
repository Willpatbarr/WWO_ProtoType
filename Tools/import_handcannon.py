"""Imports the hand cannon model built in Blender (~/Documents/WWO_Handcannon/tools: make_handcannons.py,
then export_handcannon.py) as /Game/WWO/Weapons/HandCannon/SM_HandCannon, with a material per slot
(MI_HC_<slot> of M_WWO_Robot: colour, metallic and roughness read from the export's
SM_HandCannon_materials.json, wear from WEAR below).

  UnrealEditor-Cmd <uproject> -run=pythonscript -script=<abs path>/Tools/import_handcannon.py -unattended -nullrhi
Safe to rerun: everything is rebuilt in place.
"""
import json
import os

import unreal

SRC = os.environ.get("WWO_HANDCANNON_SRC", os.path.expanduser("~/Documents/WWO_Handcannon/export"))
DEST = "/Game/WWO/Weapons/HandCannon"
MATS = "/Game/WWO/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty


def log(msg):
    unreal.log_warning("WWO hand cannon: " + msg)


# Wear on each slot (M_WWO_Robot's detail: speckle, blotches, roughness swing, dents), by slot name.
WEAR = {
    "HC_Olive": dict(Speckle=0.16, Blotch=0.16, RoughnessVar=0.16, Bump=0.05),
    "HC_Parkerized": dict(Speckle=0.14, Blotch=0.15, RoughnessVar=0.18, Bump=0.05),
    "HC_Brass": dict(Speckle=0.10, Blotch=0.20, RoughnessVar=0.22, Bump=0.03),
    "HC_Steel": dict(Speckle=0.08, Blotch=0.16, RoughnessVar=0.22, Bump=0.03),
    "HC_Rubber": dict(Speckle=0.08, Blotch=0.10, RoughnessVar=0.10, Bump=0.12),
}
DETAIL_SIZE = 30.0   # cm of the mesh per repeat of the fine speckle


def make_parent():
    # The robot's material: it projects its wear detail on the mesh's own (local) position, so it
    # needs no UVs and suits these flat-coloured parts too.
    old = MATS + "/M_WWO_GunFlat"
    if eal.does_asset_exist(old):
        eal.delete_asset(old)
    return eal.load_asset(MATS + "/M_WWO_Robot")


def make_instance(parent, name, d):
    path = DEST + "/Materials/MI_" + name
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "MI_" + name, DEST + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    c = d["color"]
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(c[0], c[1], c[2], 1))
    mel.set_material_instance_vector_parameter_value(mi, "SpeckleColor", unreal.LinearColor(min(1, c[0] * 1.8 + 0.06), min(1, c[1] * 1.8 + 0.06), min(1, c[2] * 1.8 + 0.06), 1))
    values = dict(Metallic=d["metallic"], Roughness=d["roughness"], Glow=d["glow"], Specular=0.5, DetailSize=DETAIL_SIZE)
    values.update(WEAR.get(name, {}))
    for pname, v in values.items():
        mel.set_material_instance_scalar_parameter_value(mi, pname, float(v))
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    return mi


def make_lods(sm):
    # Nanite (on for the project's meshes) scales the detail by itself; without it, two reduced
    # versions for the gun seen in other players' hands and on the ground.
    nanite = sm.get_editor_property("nanite_settings").get_editor_property("enabled")
    log("Nanite %s, LOD 0 (the fallback when Nanite is on) has %d tris" % ("on" if nanite else "off", sm.get_num_triangles(0)))
    if nanite:
        return
    opts = unreal.EditorScriptingMeshReductionOptions()
    settings = []
    for tris, screen in ((1.0, 1.0), (0.35, 0.35), (0.12, 0.12)):
        r = unreal.EditorScriptingMeshReductionSettings()
        r.set_editor_property("percent_triangles", tris)
        r.set_editor_property("screen_size", screen)
        settings.append(r)
    opts.set_editor_property("reduction_settings", settings)
    opts.set_editor_property("auto_compute_lod_screen_size", False)
    log("LODs: %d" % unreal.EditorStaticMeshLibrary.set_lods(sm, opts))


def import_mesh():
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    mesh.set_editor_property("import_skeletal_meshes", False)
    for owner, name, value in ((pipeline.get_editor_property("material_pipeline"), "import_materials", False),
                               (pipeline.get_editor_property("material_pipeline").get_editor_property("texture_pipeline"), "import_textures", False),
                               (pipeline.get_editor_property("common_meshes_properties"), "bake_meshes", True)):
        try:
            owner.set_editor_property(name, value)
        except Exception as e:
            log("pipeline has no %s (%s)" % (name, e))
    source = unreal.InterchangeManager.create_source_data(os.path.join(SRC, "SM_HandCannon.fbx"))
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", True)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipeline.get_path_name())])
    ok = unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(DEST, source, params)
    log("import returned %s" % ok)
    for path in eal.list_assets(DEST, recursive=False, include_folder=False):
        asset = eal.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            return asset
    return None


def main():
    mats = json.load(open(os.path.join(SRC, "SM_HandCannon_materials.json")))
    parent = make_parent()
    made = {name: make_instance(parent, name, d) for name, d in mats.items()}
    sm = import_mesh()
    if not sm:
        raise RuntimeError("no static mesh imported")
    slots = sm.get_editor_property("static_materials")
    for i, slot in enumerate(slots):
        name = str(slot.get_editor_property("material_slot_name"))
        mi = made.get(name)
        log("slot %d %s -> %s" % (i, name, mi.get_name() if mi else "NO MATERIAL"))
        if mi:
            sm.set_material(i, mi)
    for path in eal.list_assets(DEST + "/Materials", recursive=False, include_folder=False):
        if str(path).split("/")[-1].split(".")[0][3:] not in made:
            eal.delete_asset(str(path).split(".")[0])
    make_lods(sm)
    b = sm.get_bounds()
    log("%s origin %s extent %s, %d slots, %d tris" % (sm.get_path_name(), b.origin, b.box_extent, len(slots), sm.get_num_triangles(0)))
    eal.save_loaded_asset(sm)


main()
