"""Imports the environment plants for the Arboretum map: the spring tree and the tropical plants.

Source: the folder made by ~/Documents/WWO_Flora/tools/prep_arboretum_packs.py (Blender), read from
WWO_FLORA_SRC, default ~/Documents/WWO_Flora/arboretum (meshes/*.glb, textures/*.png).

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=Tools/import_flora.py -unattended -nullrhi

- Textures: /Game/WWO/Flora/Textures (T_<set>_BaseColor, _Normal, _ORM, and the crown's _Emissive).
  The normal maps are OpenGL style, so their green channel is flipped for Unreal.
- Materials (/Game/WWO/Materials):
  - M_WWO_Flora: opaque, for bark and stone. BaseColorTexture x Tint, ORM (occlusion, roughness,
    metallic), NormalTexture, EmissiveTexture x EmissiveStrength.
  - M_WWO_FloraLeaf: the same, masked by the base color's alpha (OpacityClip), two-sided, with the
    two-sided foliage shading model (light through the leaf = base color x Subsurface).
  M_WWO_FloraLeaf also has Recolor (0 to 1): the texture's brightness mapped onto a ramp from
  RecolorDark to RecolorLight (RecolorGain scales the brightness first). MI_Flora_Tropical sets it
  to 1 for the Arboretum's red leaves; the spring tree leaves it at 0 and stays pink.
- Instances: /Game/WWO/Flora/Materials/MI_Flora_<set>.
- Meshes: /Game/WWO/Flora/Meshes/SM_<name>, one per glb, Nanite off (masked cards). The spring tree
  is four meshes sharing one pivot: SM_SpringTree_Trunk and _Planter collide (complex as simple);
  _Crown, _Petals and every tropical plant have no collision, so shots and players pass through.
Imported meshes keep glTF axes as Interchange converts them (glTF y is up -> Unreal z is up).
Safe to rerun: everything is rebuilt in place.
"""
import os

import unreal

SRC = os.path.expanduser(os.environ.get("WWO_FLORA_SRC", "~/Documents/WWO_Flora/arboretum"))
ROOT = "/Game/WWO/Flora"
MATS = "/Game/WWO/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

# Texture set -> parent material
SETS = {
    "SpringTree_Trunk": "M_WWO_Flora",
    "SpringTree_Planter": "M_WWO_Flora",
    "SpringTree_Crown": "M_WWO_FloraLeaf",
    "Tropical": "M_WWO_FloraLeaf",
}
# Scalar parameters set on a set's instance: the tropical plants are recolored red (the Arboretum's
# leaves); the spring tree keeps its own pink.
SCALARS = {
    "Tropical": {"Recolor": 1.0},
}
# Each mesh has one material slot (the glb files carry no material names): mesh name prefix -> set.
MESH_SET = {
    "SpringTree_Trunk": "SpringTree_Trunk",
    "SpringTree_Planter": "SpringTree_Planter",
    "SpringTree_Crown": "SpringTree_Crown",
    "SpringTree_Petals": "SpringTree_Crown",
    "Tropical_": "Tropical",
}
COLLIDES = ("SpringTree_Trunk", "SpringTree_Planter")


def log(msg):
    unreal.log_warning("WWO flora: " + msg)


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

def import_texture(file):
    name = os.path.splitext(file)[0]
    import_task(os.path.join(SRC, "textures", file), ROOT + "/Textures", name)
    tex = eal.load_asset(ROOT + "/Textures/" + name)
    if not tex:
        log("texture %s failed to import" % file)
        return None
    if name.endswith("_Normal"):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("flip_green_channel", True)  # Blender / Unity normal maps are OpenGL style
    elif name.endswith("_ORM"):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    eal.save_loaded_asset(tex)
    return tex


# --- Materials --------------------------------------------------------------------------------

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


def multiply(m, a, a_out, b, b_out, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, x, y)
    mel.connect_material_expressions(a, a_out, e, "A")
    mel.connect_material_expressions(b, b_out, e, "B")
    return e


def make_material(name, orm_default, leaf):
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_nanite", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)  # foliage painting, scattered plants
    if leaf:
        m.set_editor_property("two_sided", True)
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    white = "/Engine/EngineResources/WhiteSquareTexture"
    base_tex = tex_param(m, "BaseColorTexture", white, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -900, -300)
    tint = vec_param(m, "Tint", (1, 1, 1, 1), -900, -100)
    # A linear texture sampler needs a linear default: one of the imported ORM maps.
    orm_tex = tex_param(m, "ORMTexture", orm_default, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -900, 50)
    normal_tex = tex_param(m, "NormalTexture", "/Engine/EngineMaterials/DefaultNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -900, 300)
    emissive_tex = tex_param(m, "EmissiveTexture", "/Engine/EngineResources/Black", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -900, 550)
    emissive_strength = scalar_param(m, "EmissiveStrength", 0.0, -900, 750)
    base = multiply(m, base_tex, "RGB", tint, "", -500, -200)
    if leaf:
        # Recolor: the texture's brightness mapped onto a two-color ramp (RecolorDark in the shadows
        # to RecolorLight in the highlights), blended in by Recolor. 0 keeps the texture's own colors.
        gray = mel.create_material_expression(m, unreal.MaterialExpressionDesaturation, -700, -700)
        mel.connect_material_expressions(base_tex, "RGB", gray, "")
        full = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -800, -620)
        full.set_editor_property("r", 1.0)
        mel.connect_material_expressions(full, "", gray, "Fraction")
        lum = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, -600, -700)
        lum.set_editor_property("r", True)
        lum.set_editor_property("g", False)
        lum.set_editor_property("b", False)
        lum.set_editor_property("a", False)
        mel.connect_material_expressions(gray, "", lum, "")
        gain = multiply(m, lum, "", scalar_param(m, "RecolorGain", 1.2, -700, -600), "", -500, -700)
        ramp_at = mel.create_material_expression(m, unreal.MaterialExpressionSaturate, -400, -700)
        mel.connect_material_expressions(gain, "", ramp_at, "")
        ramp = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -300, -750)
        mel.connect_material_expressions(vec_param(m, "RecolorDark", (0.12, 0.0, 0.01, 1), -600, -900), "", ramp, "A")
        mel.connect_material_expressions(vec_param(m, "RecolorLight", (0.7, 0.015, 0.035, 1), -600, -800), "", ramp, "B")
        mel.connect_material_expressions(ramp_at, "", ramp, "Alpha")
        mixed = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -150, -400)
        mel.connect_material_expressions(base, "", mixed, "A")
        mel.connect_material_expressions(ramp, "", mixed, "B")
        mel.connect_material_expressions(scalar_param(m, "Recolor", 0.0, -400, -550), "", mixed, "Alpha")
        base = mixed
    mel.connect_material_property(base, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(orm_tex, "R", MP.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(orm_tex, "G", MP.MP_ROUGHNESS)
    mel.connect_material_property(orm_tex, "B", MP.MP_METALLIC)
    mel.connect_material_property(normal_tex, "RGB", MP.MP_NORMAL)
    mel.connect_material_property(multiply(m, emissive_tex, "RGB", emissive_strength, "", -500, 600), "", MP.MP_EMISSIVE_COLOR)
    if leaf:
        m.set_editor_property("opacity_mask_clip_value", 0.33)
        mel.connect_material_property(base_tex, "A", MP.MP_OPACITY_MASK)
        subsurface = scalar_param(m, "Subsurface", 0.4, -900, -450)
        mel.connect_material_property(multiply(m, base, "", subsurface, "", -300, -400), "", MP.MP_SUBSURFACE_COLOR)
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def make_instance(parent, key, textures):
    folder = ROOT + "/Materials"
    name = "MI_Flora_" + key
    path = folder + "/" + name
    if eal.does_asset_exist(path):
        mi = eal.load_asset(path)
    else:
        mi = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    for param, suffix in (("BaseColorTexture", "BaseColor"), ("ORMTexture", "ORM"), ("NormalTexture", "Normal"), ("EmissiveTexture", "Emissive")):
        t = textures.get("T_%s_%s" % (key, suffix))
        if t:
            mel.set_material_instance_texture_parameter_value(mi, param, t)
    for param, value in SCALARS.get(key, {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, param, value)
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


def import_mesh(name, instances):
    scratch = ROOT + "/Import_" + name
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    import_task(os.path.join(SRC, "meshes", name + ".glb"), scratch, None, mesh_options())
    mesh = None
    for p in eal.list_assets(scratch, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            mesh = a
    if not mesh:
        log("no static mesh imported from %s" % name)
        return None
    dest = ROOT + "/Meshes/SM_" + name
    if eal.does_asset_exist(dest):
        eal.delete_asset(dest)
    eal.duplicate_asset(mesh.get_path_name(), dest)
    eal.delete_directory(scratch)
    out = eal.load_asset(dest)
    slots = len(out.get_editor_property("static_materials"))
    key = next((v for k, v in MESH_SET.items() if name.startswith(k)), None)
    if key and slots == 1:
        out.set_material(0, instances[key])
    else:
        log("%s: no material (set %s, %d slots)" % (name, key, slots))
    nanite = out.get_editor_property("nanite_settings")
    nanite.set_editor_property("enabled", False)
    out.set_editor_property("nanite_settings", nanite)
    body = out.get_editor_property("body_setup")
    if name in COLLIDES:
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    else:
        inst = body.get_editor_property("default_instance")
        inst.set_editor_property("collision_profile_name", "NoCollision")
        body.set_editor_property("default_instance", inst)
    eal.save_loaded_asset(out)
    b = out.get_bounding_box()
    log("SM_%s size (%.0f %.0f %.0f) cm, min z %.0f, %d tris, material %s, nanite %s, collision %s" % (
        name, b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z, b.min.z, out.get_num_triangles(0), out.get_material(0).get_name() if out.get_material(0) else None,
        out.get_editor_property("nanite_settings").get_editor_property("enabled"),
        "complex" if name in COLLIDES else str(out.get_editor_property("body_setup").get_editor_property("default_instance").get_editor_property("collision_profile_name"))))
    return out


def main():
    textures = {}
    for f in sorted(os.listdir(os.path.join(SRC, "textures"))):
        if f.endswith(".png"):
            t = import_texture(f)
            if t:
                textures[os.path.splitext(f)[0]] = t
    log("%d textures" % len(textures))
    orm = textures["T_Tropical_ORM"]
    parents = {"M_WWO_Flora": make_material("M_WWO_Flora", orm, False),
               "M_WWO_FloraLeaf": make_material("M_WWO_FloraLeaf", orm, True)}
    instances = {key: make_instance(parents[parent], key, textures) for key, parent in SETS.items()}
    log("%d material instances" % len(instances))
    count = 0
    for f in sorted(os.listdir(os.path.join(SRC, "meshes"))):
        if f.endswith(".glb") and import_mesh(os.path.splitext(f)[0], instances):
            count += 1
    log("%d meshes" % count)
    log("done")


main()
