"""Imports the Quaternius Modular Sci-Fi MegaKit pieces the Godot arenas are built from
(Godot: res://maps/scifi/, CC0, used by tools/build_scifi_map.gd).

Run headless from the project folder (the Godot project path is read from WWO_GODOT, default
~/Documents/WhatWasOurs), before Tools/build_maps.py:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=Tools/import_kit.py -unattended -nullrhi

- Textures: /Game/WWO/Kit/Textures (the kit's trim sheets; glTF normal maps are OpenGL style, so
  their green channel is flipped for Unreal).
- M_WWO_Kit (/Game/WWO/Materials): the glTF metal/rough material (base color texture x factor,
  roughness = factor x ORM.g, metallic = factor x ORM.b, normal map, emissive factor), two-sided
  like the kit's. One instance per kit material, /Game/WWO/Kit/Materials/MI_Kit_<glTF name>,
  filled from the glTF files themselves.
- Meshes: /Game/WWO/Kit/Meshes/SM_<piece> (one static mesh per glTF file, all 158 of the free kit;
  the chest's and fan's animated parts baked in their rest pose). Collision is the mesh itself (complex as simple, double sided), like
  the Godot builder's trimesh shapes with backface_collision.
Imported meshes keep glTF axes as Interchange converts them (glTF x, y, z -> Unreal x, z, y);
build_maps.py turns each piece by 90 degrees of yaw to put it in the port's Godot convention.
Safe to rerun: everything is rebuilt in place.
"""
import json
import os
import subprocess

import unreal

GODOT = os.path.expanduser(os.environ.get("WWO_GODOT", "~/Documents/WhatWasOurs"))
KIT = os.path.join(GODOT, "maps/scifi")
ROOT = "/Game/WWO/Kit"
MATS = "/Game/WWO/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

# The pieces the arenas use (build_maps.py), then every other piece in the kit (the Arboretum map).
PIECES = [
    "Platforms/Platform_Metal", "Platforms/Platform_Metal2", "Platforms/Platform_DarkPlates", "Platforms/Platform_Squares",
    "Platforms/Platform_CenterPlate", "Platforms/Platform_Simple2", "Platforms/Platform_Stairs_4Wide", "Platforms/Door_Frame_Square",
    "Walls/WallWindow_Straight", "Walls/WallAstra_Straight", "Walls/WallBand_Straight", "Walls/TopSimple_Straight",
    "Walls/ShortWall_Metal2_Straight", "Columns/Column_Large_Straight",
    "Props/Prop_Computer", "Props/Prop_Chest", "Props/Prop_Light_Floor", "Props/Prop_Crate4", "Props/Prop_Crate3",
    "Props/Prop_Barrel_Large", "Props/Prop_Rail_4",
]
for _folder in ("Walls", "Platforms", "Columns", "Props"):
    for _file in sorted(os.listdir(os.path.join(KIT, _folder))):
        if _file.endswith(".gltf") and _folder + "/" + _file[:-5] not in PIECES:
            PIECES.append(_folder + "/" + _file[:-5])


def log(msg):
    unreal.log_warning("WWO kit: " + msg)


def import_task(src, folder, name=None, options=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", folder)
    if name:
        task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if options:
        task.set_editor_property("options", options)
    tools.import_asset_tasks([task])
    return [str(p) for p in task.get_editor_property("imported_object_paths")]


# --- Textures ---------------------------------------------------------------------------------

def texture_kind(name):
    if "Normal" in name:
        return "normal"
    if "ORM" in name:
        return "orm"
    return "color"


def import_texture(file):
    name = os.path.splitext(file)[0]
    src = os.path.join(KIT, "Textures", file)
    import_task(src, ROOT + "/Textures", name)
    path = ROOT + "/Textures/" + name
    if not eal.does_asset_exist(path):
        # Interchange won't take a non-power-of-two image (the 2000 x 2000 decal sheet): import a
        # 2048 copy made with sips into Saved/.
        tmp = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "KitImport")
        os.makedirs(tmp, exist_ok=True)
        scaled = os.path.join(tmp, file)
        subprocess.run(["sips", "-z", "2048", "2048", src, "--out", scaled], check=True, capture_output=True)
        import_task(scaled, ROOT + "/Textures", name)
    tex = eal.load_asset(path)
    if not tex:
        log("texture %s failed to import" % file)
        return None
    kind = texture_kind(name)
    if kind == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("flip_green_channel", True)  # glTF / Godot normal maps are OpenGL style
    elif kind == "orm":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    eal.save_loaded_asset(tex)
    return tex


# --- Material ---------------------------------------------------------------------------------

def tex_param(m, name, default, sampler, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", unreal.load_asset(default) if isinstance(default, str) else default)
    e.set_editor_property("sampler_type", sampler)
    return e


def vec_param(m, name, default, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(*default))
    return e


def scalar_param(m, name, default, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    return e


def one(m, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", 1.0)
    return e


def multiply(m, a, a_out, b, b_out, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, x, y)
    mel.connect_material_expressions(a, a_out, e, "A")
    mel.connect_material_expressions(b, b_out, e, "B")
    return e


def make_kit_material(orm_default):
    # glTF metal/rough, as Godot's importer reads it (no occlusion texture in the kit, and Godot
    # ignores KHR_materials_specular, so neither is used here).
    path = MATS + "/M_WWO_Kit"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset("M_WWO_Kit", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_nanite", True)  # Interchange makes the kit meshes Nanite
    white = "/Engine/EngineResources/WhiteSquareTexture"
    base_tex = tex_param(m, "BaseColorTexture", white, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -900, -300)
    base_fac = vec_param(m, "BaseColorFactor", (1, 1, 1, 1), -900, -100)
    # A linear texture sampler needs a linear default; UseORM 0 ignores it (factors alone).
    orm_tex = tex_param(m, "ORMTexture", orm_default, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -900, 50)
    use_orm = scalar_param(m, "UseORM", 0.0, -900, 200)
    rough_fac = scalar_param(m, "RoughnessFactor", 1.0, -900, 250)
    metal_fac = scalar_param(m, "MetallicFactor", 1.0, -900, 330)
    rough_tex = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -650, 150)
    mel.connect_material_expressions(one(m, -800, 120), "", rough_tex, "A")
    mel.connect_material_expressions(orm_tex, "G", rough_tex, "B")
    mel.connect_material_expressions(use_orm, "", rough_tex, "Alpha")
    metal_tex = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -650, 300)
    mel.connect_material_expressions(one(m, -800, 280), "", metal_tex, "A")
    mel.connect_material_expressions(orm_tex, "B", metal_tex, "B")
    mel.connect_material_expressions(use_orm, "", metal_tex, "Alpha")
    normal_tex = tex_param(m, "NormalTexture", "/Engine/EngineMaterials/DefaultNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -900, 450)
    emissive = vec_param(m, "EmissiveFactor", (0, 0, 0, 1), -900, 650)
    mel.connect_material_property(multiply(m, base_tex, "RGB", base_fac, "", -500, -200), "", MP.MP_BASE_COLOR)
    mel.connect_material_property(multiply(m, rough_tex, "", rough_fac, "", -500, 150), "", MP.MP_ROUGHNESS)
    mel.connect_material_property(multiply(m, metal_tex, "", metal_fac, "", -500, 300), "", MP.MP_METALLIC)
    mel.connect_material_property(normal_tex, "RGB", MP.MP_NORMAL)
    mel.connect_material_property(emissive, "", MP.MP_EMISSIVE_COLOR)
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def make_instance(parent, name, gltf, mat, textures):
    """MI_Kit_<name> from one glTF material entry."""
    folder = ROOT + "/Materials"
    path = folder + "/MI_Kit_" + name
    if eal.does_asset_exist(path):
        mi = eal.load_asset(path)
    else:
        mi = tools.create_asset("MI_Kit_" + name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)

    def tex(ref):
        if not ref:
            return None
        uri = gltf["images"][gltf["textures"][ref["index"]]["source"]]["uri"]
        return textures.get(os.path.splitext(os.path.basename(uri))[0])

    pbr = mat.get("pbrMetallicRoughness", {})
    for param, ref in (("BaseColorTexture", pbr.get("baseColorTexture")), ("ORMTexture", pbr.get("metallicRoughnessTexture")),
                       ("NormalTexture", mat.get("normalTexture"))):
        t = tex(ref)
        if t:
            mel.set_material_instance_texture_parameter_value(mi, param, t)
            if param == "ORMTexture":
                mel.set_material_instance_scalar_parameter_value(mi, "UseORM", 1.0)
    base = pbr.get("baseColorFactor", [1, 1, 1, 1])  # glTF factors are linear already
    mel.set_material_instance_vector_parameter_value(mi, "BaseColorFactor", unreal.LinearColor(*base))
    mel.set_material_instance_scalar_parameter_value(mi, "RoughnessFactor", float(pbr.get("roughnessFactor", 1.0)))
    mel.set_material_instance_scalar_parameter_value(mi, "MetallicFactor", float(pbr.get("metallicFactor", 1.0)))
    e = mat.get("emissiveFactor", [0, 0, 0])
    mel.set_material_instance_vector_parameter_value(mi, "EmissiveFactor", unreal.LinearColor(e[0], e[1], e[2], 1.0))
    eal.save_loaded_asset(mi)
    return mi


# --- Meshes -----------------------------------------------------------------------------------

def mesh_options():
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("asset_type_sub_folders", False)
    p.set_editor_property("use_source_name_for_asset", True)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    common.set_editor_property("bake_meshes", True)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    meshes.set_editor_property("collision", False)
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


def import_piece(piece, instances):
    name = os.path.basename(piece)
    scratch = ROOT + "/Import_" + name
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    import_task(os.path.join(KIT, piece + ".gltf"), scratch, None, mesh_options())
    mesh = None
    for p in eal.list_assets(scratch, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            mesh = a
    if not mesh:
        log("no static mesh imported from %s" % piece)
        return None
    dest = ROOT + "/Meshes/SM_" + name
    if eal.does_asset_exist(dest):
        eal.delete_asset(dest)
    eal.duplicate_asset(mesh.get_path_name(), dest)
    eal.delete_directory(scratch)
    out = eal.load_asset(dest)
    mats = out.get_editor_property("static_materials")
    for i, sm in enumerate(mats):
        slot = str(sm.get_editor_property("material_slot_name"))
        mi = instances.get(slot)
        if not mi:
            # Slot names can carry a suffix; match the longest kit material name they start with.
            for k in sorted(instances, key=len, reverse=True):
                if slot.startswith(k):
                    mi = instances[k]
                    break
        if mi:
            out.set_material(i, mi)
        else:
            log("%s: no kit material for slot %s" % (name, slot))
    body = out.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    body.set_editor_property("double_sided_geometry", True)
    eal.save_loaded_asset(out)
    b = out.get_bounding_box()
    log("SM_%s bounds (%.0f %.0f %.0f) .. (%.0f %.0f %.0f), %d slots" % (name, b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z, len(mats)))
    return out


def main():
    gltfs = {p: json.load(open(os.path.join(KIT, p + ".gltf"))) for p in PIECES}
    needed = set()
    for g in gltfs.values():
        for img in g.get("images", []):
            needed.add(os.path.basename(img["uri"]))
    textures = {}
    for f in sorted(needed):
        t = import_texture(f)
        if t:
            textures[os.path.splitext(f)[0]] = t
    log("%d textures" % len(textures))
    parent = make_kit_material(textures["T_Trim_02_ORM"])
    instances = {}
    for g in gltfs.values():
        for mat in g.get("materials", []):
            if mat["name"] not in instances:
                instances[mat["name"]] = make_instance(parent, mat["name"], g, mat, textures)
    log("%d material instances" % len(instances))
    for p in PIECES:
        import_piece(p, instances)
    log("done")


main()
