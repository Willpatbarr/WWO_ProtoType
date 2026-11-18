"""Imports the flora grafts for the robot frame: the skinned meshes the Blender builds in
~/Documents/WWO_Flora/tools (make_<flora>_grafts.py) make round SK_RobotFrame, onto the robot's own
skeleton, with their textures and materials.

Floras (WWO_GRAFT_FLORAS, default all, e.g. "veilspore"):
  voltvine   ~/Documents/WWO_Flora/grafts            SK_VV_*.fbx and textures/*.png
  veilspore  ~/Documents/WWO_Flora/grafts_veilspore  SK_VS_*.fbx and textures/*.png
  ironbark   ~/Documents/WWO_Flora/grafts_ironbark   SK_IB_*.fbx and textures/*.png

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/import_robot_grafts.py -unattended -nullrhi

- Meshes: /Game/WWO/Characters/RobotFrame/Grafts/SK_<prefix>_<Graft>, on SK_RobotFrame's skeleton,
  three LODs each (UWWOBodyComponent::AddModelGraft plays them on the robot's pose).
- Textures and materials: /Game/WWO/Flora/<Flora>/Textures and /Materials, named for the meshes' slots.
  - VoltVine: VV_Bark (M_WWO_Flora with the vine bark), VV_Leaf01-06 (M_WWO_FloraLeaf with each leaf),
    VV_Core, VV_Stem, VV_Tip, VV_Glow, VV_Fluid (M_WWO_GraftFlat), VV_Glass (M_WWO_GraftGlass).
  - VeilSpore: VS_Scale (M_WWO_Flora with the scale's painted textures: dark purple, glowing violet
    veins), VS_Under and VS_Mycelium (M_WWO_GraftFlat).
  - IronBark: IB_Bark (M_WWO_Flora with the bark: plates split by glowing orange cracks), IB_Pod and
    IB_Cord (M_WWO_GraftFlat).
  M_WWO_GraftFlat and M_WWO_GraftGlass are made here (in /Game/WWO/Materials); every parent is marked
  for skeletal meshes.
Safe to rerun: everything is rebuilt in place.
"""
import os

import unreal

MESHES = "/Game/WWO/Characters/RobotFrame/Grafts"
MATS = "/Game/WWO/Materials"
SKELETAL = "/Game/WWO/Characters/RobotFrame/SK_RobotFrame"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
REPORT = []

# The flat materials: base color, emissive color, glow (emissive strength), roughness.
FLAT_VV = {
    "VV_Core": ((0.03, 0.15, 0.35), (0.10, 0.45, 1.0), 6.0, 0.3),
    "VV_Stem": ((0.16, 0.30, 0.10), (0, 0, 0), 0.0, 0.55),
    "VV_Tip": ((0.55, 0.48, 0.32), (0, 0, 0), 0.0, 0.45),
    "VV_Glow": ((0.35, 0.7, 1.0), (0.35, 0.7, 1.0), 12.0, 0.3),
    "VV_Fluid": ((0.04, 0.15, 0.5), (0.06, 0.28, 1.0), 5.0, 0.2),
}
# VeilSpore's: the scales' glowing undersides and the mycelium threads (the scale tops are textured).
FLAT_VS = {
    "VS_Under": ((0.10, 0.06, 0.30), (0.20, 0.30, 1.0), 0.7, 0.5),
    "VS_Mycelium": ((0.55, 0.40, 0.85), (0.55, 0.28, 1.0), 1.5, 0.5),
}
# IronBark's: the seed pods and their cords (the bark is textured).
FLAT_IB = {
    "IB_Pod": ((0.85, 0.45, 0.08), (1.0, 0.5, 0.1), 0.8, 0.35),
    "IB_Cord": ((0.08, 0.06, 0.05), (0, 0, 0), 0.0, 0.7),
}
FLORAS = {
    "voltvine": dict(src="~/Documents/WWO_Flora/grafts", prefix="SK_VV_", root="/Game/WWO/Flora/VoltVine"),
    "veilspore": dict(src="~/Documents/WWO_Flora/grafts_veilspore", prefix="SK_VS_", root="/Game/WWO/Flora/VeilSpore"),
    "ironbark": dict(src="~/Documents/WWO_Flora/grafts_ironbark", prefix="SK_IB_", root="/Game/WWO/Flora/IronBark"),
}


def log(msg):
    REPORT.append(msg)
    unreal.log_warning("WWO grafts: " + msg)


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

def import_texture(src, root, file):
    name = os.path.splitext(file)[0]
    import_task(os.path.join(src, "textures", file), root + "/Textures", name)
    tex = eal.load_asset(root + "/Textures/" + name)
    if not tex:
        log("texture %s failed to import" % file)
        return None
    if name.endswith("_Normal"):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("flip_green_channel", True)  # Blender's normal maps are OpenGL style
    elif name.endswith("_ORM"):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    tex.set_editor_property("max_texture_size", 1024)  # a graft leaf is a few cm on screen
    eal.save_loaded_asset(tex)
    return tex


# --- Materials --------------------------------------------------------------------------------

def scalar_param(m, name, default, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    return e


def vec_param(m, name, default, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(*default))
    return e


def multiply(m, a, a_out, b, b_out, x, y):
    e = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, x, y)
    mel.connect_material_expressions(a, a_out, e, "A")
    mel.connect_material_expressions(b, b_out, e, "B")
    return e


def new_material(name):
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)
    return m


def make_flat():
    m = new_material("M_WWO_GraftFlat")
    color = vec_param(m, "Color", (0.5, 0.5, 0.5, 1), -600, -200)
    emissive = vec_param(m, "EmissiveColor", (0, 0, 0, 1), -600, 0)
    glow = scalar_param(m, "Glow", 0.0, -600, 150)
    mel.connect_material_property(color, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(scalar_param(m, "Roughness", 0.5, -600, 300), "", MP.MP_ROUGHNESS)
    mel.connect_material_property(multiply(m, emissive, "", glow, "", -300, 50), "", MP.MP_EMISSIVE_COLOR)
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def make_glass():
    # See-through glass: faint in the middle, opaque and glowing at the rim (a fresnel).
    m = new_material("M_WWO_GraftGlass")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", False)
    color = vec_param(m, "Color", (0.55, 0.75, 1.0, 1), -700, -200)
    rim_color = vec_param(m, "RimColor", (0.35, 0.65, 1.0, 1), -700, 0)
    fresnel = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -700, 200)
    fresnel.set_editor_property("exponent", 2.5)
    base_op = scalar_param(m, "Opacity", 0.18, -700, 350)
    rim_op = scalar_param(m, "RimOpacity", 0.85, -700, 450)
    opacity = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -400, 350)
    mel.connect_material_expressions(base_op, "", opacity, "A")
    mel.connect_material_expressions(rim_op, "", opacity, "B")
    mel.connect_material_expressions(fresnel, "", opacity, "Alpha")
    glow = scalar_param(m, "RimGlow", 3.0, -700, 100)
    rim = multiply(m, multiply(m, rim_color, "", fresnel, "", -500, 50), "", glow, "", -300, 50)
    mel.connect_material_property(color, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(rim, "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(opacity, "", MP.MP_OPACITY)
    mel.connect_material_property(scalar_param(m, "Roughness", 0.05, -700, 550), "", MP.MP_ROUGHNESS)
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def for_skeletal(path):
    m = eal.load_asset(path)
    if m and not m.get_editor_property("used_with_skeletal_mesh"):
        m.set_editor_property("used_with_skeletal_mesh", True)
        mel.recompile_material(m)
        eal.save_loaded_asset(m)
        log("%s now used with skeletal meshes" % path)
    return m


def instance(root, name, parent, textures=None, scalars=None, vectors=None):
    folder = root + "/Materials"
    path = folder + "/MI_" + name
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "MI_" + name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(mi, k, v)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v, 1.0))
    eal.save_loaded_asset(mi)
    return mi


def flat_instances(root, flat, specs):
    out = {}
    for name, spec in specs.items():
        base, emit, glow, rough = spec[:4]
        scalars = {"Glow": glow, "Roughness": rough}
        out[name] = instance(root, name, flat, None, scalars, {"Color": base, "EmissiveColor": emit})
    return out


def tex_params(textures, key):
    # The flora material's texture parameters from T_<key>_<map>.
    return {param: textures["T_%s_%s" % (key, suffix)]
            for param, suffix in (("BaseColorTexture", "BaseColor"), ("ORMTexture", "ORM"), ("NormalTexture", "Normal"), ("EmissiveTexture", "Emissive"))
            if "T_%s_%s" % (key, suffix) in textures}


def veilspore_materials(root, textures, flat):
    flora = for_skeletal(MATS + "/M_WWO_Flora")
    out = {"VS_Scale": instance(root, "VS_Scale", flora, tex_params(textures, "veilspore_scale"), {"EmissiveStrength": 5.0})}
    out.update(flat_instances(root, flat, FLAT_VS))
    return out


def ironbark_materials(root, textures, flat):
    flora = for_skeletal(MATS + "/M_WWO_Flora")
    out = {"IB_Bark": instance(root, "IB_Bark", flora, tex_params(textures, "ironbark_bark"), {"EmissiveStrength": 6.0})}
    out.update(flat_instances(root, flat, FLAT_IB))
    return out


def voltvine_materials(root, textures, flat, glass):
    flora = for_skeletal(MATS + "/M_WWO_Flora")
    leaf = for_skeletal(MATS + "/M_WWO_FloraLeaf")

    def tex_set(key):
        return {param: textures["T_voltvine_%s_%s" % (key, suffix)]
                for param, suffix in (("BaseColorTexture", "BaseColor"), ("ORMTexture", "ORM"), ("NormalTexture", "Normal"), ("EmissiveTexture", "Emissive"))
                if "T_voltvine_%s_%s" % (key, suffix) in textures}

    out = {"VV_Bark": instance(root, "VV_Bark", flora, tex_set("vine_twin"), {"EmissiveStrength": 4.0})}
    for n in ("01", "02", "03", "04", "05", "06"):
        out["VV_Leaf" + n] = instance(root, "VV_Leaf" + n, leaf, tex_set("leaf_" + n), {"EmissiveStrength": 2.5, "Recolor": 0.0, "Subsurface": 0.4})
    out.update(flat_instances(root, flat, FLAT_VV))
    out["VV_Glass"] = instance(root, "VV_Glass", glass)
    return out


# --- Meshes -----------------------------------------------------------------------------------

def mesh_options(skeleton):
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("asset_type_sub_folders", False)
    p.set_editor_property("use_source_name_for_asset", True)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_SKELETAL_MESH)
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
    anims = p.get_editor_property("animation_pipeline")
    anims.set_editor_property("import_animations", False)
    stack = unreal.InterchangePipelineStackOverride()
    stack.add_pipeline(p)
    return stack


def import_graft(src, file, skeleton, materials):
    name = os.path.splitext(file)[0]
    scratch = MESHES + "/Import_" + name
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    import_task(os.path.join(src, file), scratch, None, mesh_options(skeleton))
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
    try:
        unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).regenerate_lod(out, 3, True, False)
    except Exception as e:  # noqa: BLE001
        log("%s: no LODs (%s)" % (name, e))
    # The game materials by slot name (after the LODs are made, which reset them).
    slots, missing = [], []
    for slot in out.get_editor_property("materials"):
        key = str(slot.get_editor_property("material_slot_name"))
        mi = materials.get(key)
        if not mi:
            missing.append(key)
        if mi:
            slot.set_editor_property("material_interface", mi)
        slots.append(slot)
    out.set_editor_property("materials", slots)
    eal.save_loaded_asset(out)
    got = [str(m.get_editor_property("material_interface").get_name()) if m.get_editor_property("material_interface") else None
           for m in eal.load_asset(dest).get_editor_property("materials")]
    log("%s materials: %s" % (name, got))
    log("%s: skeleton %s, %d slots, missing %s, LODs %d, bounds %s" % (
        name, sk.get_name() if sk else None, len(slots), missing, unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_lod_count(out), out.get_bounds().box_extent))
    return out


def main():
    robot = eal.load_asset(SKELETAL)
    skeleton = robot.get_editor_property("skeleton") if robot else None
    if not skeleton:
        log("FAILED: no robot skeleton at %s" % SKELETAL)
        return
    flat, glass = make_flat(), make_glass()
    wanted = [f.strip() for f in os.environ.get("WWO_GRAFT_FLORAS", ",".join(FLORAS)).split(",") if f.strip()]
    for flora in wanted:
        cfg = FLORAS[flora]
        src = os.path.expanduser(cfg["src"])
        textures = {}
        if os.path.isdir(os.path.join(src, "textures")):
            for f in sorted(os.listdir(os.path.join(src, "textures"))):
                if f.endswith(".png") and not f.endswith("_Roughness.png"):
                    t = import_texture(src, cfg["root"], f)
                    if t:
                        textures[os.path.splitext(f)[0]] = t
        if flora == "voltvine":
            materials = voltvine_materials(cfg["root"], textures, flat, glass)
        elif flora == "veilspore":
            materials = veilspore_materials(cfg["root"], textures, flat)
        else:
            materials = ironbark_materials(cfg["root"], textures, flat)
        log("%s: %d textures, %d materials" % (flora, len(textures), len(materials)))
        count = 0
        for f in sorted(os.listdir(src)):
            if f.startswith(cfg["prefix"]) and f.endswith(".fbx") and import_graft(src, f, skeleton, materials):
                count += 1
        log("%s: %d grafts" % (flora, count))
    log("done")


try:
    main()
finally:
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "import_robot_grafts.log"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
