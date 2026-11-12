"""Concrete versions of the Sci-Fi kit's structure materials, for the Arboretum map: the kit's shapes
and panel lines, surfaced as weathered concrete instead of white and dark panelling.

Before running, copy the concrete surface (Megascans "Dirty Painted Concrete Wall", shipped with
Lyra's example content) into the project:
  cp <LyraStarterGame>/Plugins/LyraExampleContent/Content/Megascans/Surfaces/Dirty_Painted_Concrete_Wall_vdqfcg3/*.uasset \
     Content/WWO/Kit/Concrete/
Then, headless from the project folder (after Tools/import_kit.py):
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<full path>/Tools/create_kit_concrete.py -unattended -nullrhi

- M_WWO_KitConcrete (/Game/WWO/Materials): the concrete's color, normal and occlusion / roughness
  projected in world space (ConcreteSize cm per repeat, so every piece lines up with its neighbors
  whatever its UVs), x ConcreteTint. The kit material's own textures still shape it: its normal map
  adds the panel seams and bevels, and its base color darkens the concrete where the kit is dark
  (KitContrast, 0 = even concrete). EmissiveFactor as in M_WWO_Kit.
  Weathering on top (see weather()): blotchy dirt, leak streaks hanging from the top of each piece
  and a dark band of grime at its foot, all scaled by Grime (0 = clean). The mask they read,
  T_Grime_Mask, is made by Tools/make_grime_mask.py into Tools/art and imported here.
- One instance per structural kit material: /Game/WWO/Kit/Materials/Concrete/MI_KitConcrete_<name>,
  carrying that kit instance's textures. Rails, lights, decals and black keep the kit's own.
- M_WWO_KitGlass and MI_KitConcrete_M_Glass: see-through, dusty glass for the kit's windows (the
  kit's own M_Glass is opaque).
A map builder swaps them in per placed piece: for each material slot of a wall, floor, column or
door frame whose material is MI_Kit_<name>, use MI_KitConcrete_<name> if it exists (props keep the
kit look).
Safe to rerun.
"""
import os

import unreal

MATS = "/Game/WWO/Materials"
KIT_MATS = "/Game/WWO/Kit/Materials"
OUT = KIT_MATS + "/Concrete"
SURFACE = "/Game/WWO/Kit/Concrete"
COLOR = SURFACE + "/T_Dirty_Painted_Concrete_Wall_vdqfcg3_4K_D"
NORMAL = SURFACE + "/T_Dirty_Painted_Concrete_Wall_vdqfcg3_4K_N"
ORD = SURFACE + "/T_DirtyPaintedConcreteWall_vdqfcg3_4K_ORDp"  # occlusion, roughness, displacement
GRIME = SURFACE + "/T_Grime_Mask"  # imported from Tools/art (Tools/make_grime_mask.py)
# Kit materials that become concrete (the rest stay as the kit has them).
STRUCTURAL = ["MI_Trim_01", "MI_Trim_01_Floor", "MI_Trim_01_Stairs", "MI_Trim_02", "MI_Trim_02_Floor", "MI_Trim_02_Variation",
              "MI_Trim_03", "MI_Trim_03_Dark", "MI_PaddedWall"]

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
ST = unreal.MaterialSamplerType


def log(msg):
    unreal.log_warning("WWO concrete: " + msg)


def link(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        log("could not connect %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def new(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def call(m, path, x, y):
    return new(m, unreal.MaterialExpressionMaterialFunctionCall, x, y, material_function=unreal.load_asset(path))


def multiply(m, a, a_out, b, b_out, x, y):
    e = new(m, unreal.MaterialExpressionMultiply, x, y)
    link(a, a_out, e, "A")
    link(b, b_out, e, "B")
    return e


def scalar(m, name, value, x, y):
    return new(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def color_param(m, name, rgb, x, y):
    return new(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))


def pick(m, src, src_out, ch, x, y):
    e = new(m, unreal.MaterialExpressionComponentMask, x, y, r=ch == "r", g=ch == "g", b=ch == "b", a=False)
    link(src, src_out, e, "")
    return e


def unary(m, cls, src, x, y, **props):
    e = new(m, cls, x, y, **props)
    link(src, "", e, "")
    return e


def binary(m, cls, a, b, x, y, **props):
    """a (op) b; b can be a number (the node's constant)."""
    e = new(m, cls, x, y, **props)
    link(a, "", e, "A")
    if isinstance(b, (int, float)):
        e.set_editor_property("const_b", float(b))
    else:
        link(b, "", e, "B")
    return e


def lerp(m, a, b, alpha, x, y):
    e = new(m, unreal.MaterialExpressionLinearInterpolate, x, y)
    link(a, "", e, "A")
    link(b, "", e, "B")
    link(alpha, "", e, "Alpha")
    return e


def normal_rgb(m, src):
    e = new(m, unreal.MaterialExpressionComponentMask, -1000, -120, r=True, g=True, b=True, a=False)
    link(src, "XYZ Texture", e, "")
    return e


def weather(m, base):
    """Stains and grime over the concrete's color, all placed in world space from GrimeMask
    (Tools/make_grime_mask.py: R blotches, G leak streaks, B speckle). Grime scales all three.
    - Dirt: blotchy patches (DirtSize cm per repeat) multiplying the color by DirtColor.
    - Streaks: leaks hanging from the top of each piece on its vertical faces: the mask's width
      repeats every StreakWidth cm along the wall and its height spans StreakHeight cm down from
      the piece's top (the kit's walls are 3 m). Multiplies by StreakColor.
    - Base grime: a dark band (the color x BaseGrimeColor) up to about BaseHeight cm above the piece's foot,
      ragged with the blotches, on vertical faces.
    """
    Mul, Add, Sub, Div = (unreal.MaterialExpressionMultiply, unreal.MaterialExpressionAdd,
                          unreal.MaterialExpressionSubtract, unreal.MaterialExpressionDivide)
    Sat, Inv = unreal.MaterialExpressionSaturate, unreal.MaterialExpressionOneMinus
    x0, y0 = -1500, -2400
    mask_tex = new(m, unreal.MaterialExpressionTextureObjectParameter, x0, y0, parameter_name="GrimeMask",
                   texture=unreal.load_asset(GRIME), sampler_type=ST.SAMPLERTYPE_MASKS)
    amount = scalar(m, "Grime", 1.0, x0, y0 + 150)

    blot = call(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture", x0 + 350, y0)
    link(mask_tex, "", blot, "TextureObject")
    link(scalar(m, "DirtSize", 500.0, x0, y0 + 250), "", blot, "TextureSize")
    dirt = pick(m, blot, "XYZ Texture", "r", x0 + 650, y0)
    patches = unary(m, Sat, binary(m, Mul, binary(m, Sub, dirt, 0.5, x0 + 800, y0), 3.0, x0 + 930, y0), x0 + 1060, y0)
    dirt_amt = binary(m, Mul, binary(m, Mul, patches, scalar(m, "DirtStrength", 0.65, x0 + 930, y0 + 120), x0 + 1200, y0), amount, x0 + 1330, y0)
    base = lerp(m, base, binary(m, Mul, base, color_param(m, "DirtColor", (0.5, 0.47, 0.43), x0 + 1200, y0 + 200), x0 + 1460, y0 + 100), dirt_amt, x0 + 1600, y0)

    # Vertical faces only: 1 at a wall, 0 on floors and ceilings.
    nz = pick(m, new(m, unreal.MaterialExpressionVertexNormalWS, x0, y0 + 500), "", "b", x0 + 200, y0 + 500)
    wall = unary(m, Sat, unary(m, Inv, binary(m, Mul, unary(m, unreal.MaterialExpressionAbs, nz, x0 + 350, y0 + 500), 3.0, x0 + 480, y0 + 500), x0 + 610, y0 + 500), x0 + 740, y0 + 500)
    # Heights within the piece, in cm, from its bounds (ObjectPositionWS is the bounds' center).
    world = new(m, unreal.MaterialExpressionWorldPosition, x0, y0 + 700)
    wz = pick(m, world, "", "b", x0 + 200, y0 + 700)
    mid = pick(m, new(m, unreal.MaterialExpressionObjectPositionWS, x0, y0 + 800), "", "b", x0 + 200, y0 + 800)
    half = pick(m, new(m, unreal.MaterialExpressionObjectBounds, x0, y0 + 900), "", "b", x0 + 200, y0 + 900)  # half the piece's height
    up = binary(m, Sub, wz, binary(m, Sub, mid, half, x0 + 350, y0 + 800), x0 + 480, y0 + 750)         # above its foot
    below_top = binary(m, Sub, binary(m, Add, mid, half, x0 + 350, y0 + 900), wz, x0 + 480, y0 + 880)  # below its top

    along = binary(m, Div, binary(m, Add, pick(m, world, "", "r", x0 + 200, y0 + 1000), pick(m, world, "", "g", x0 + 200, y0 + 1080), x0 + 400, y0 + 1000),
                   scalar(m, "StreakWidth", 450.0, x0 + 200, y0 + 1180), x0 + 560, y0 + 1000)
    # Clamped, so a piece taller than StreakHeight doesn't start a second row of streaks partway down.
    down = new(m, unreal.MaterialExpressionClamp, x0 + 720, y0 + 880, min_default=0.0, max_default=0.98)  # short of 1, which wraps to the top
    link(binary(m, Div, below_top, scalar(m, "StreakHeight", 300.0, x0 + 480, y0 + 960), x0 + 600, y0 + 880), "", down, "")
    uv = new(m, unreal.MaterialExpressionAppendVector, x0 + 850, y0 + 950)
    link(along, "", uv, "A")
    link(down, "", uv, "B")
    leaks = new(m, unreal.MaterialExpressionTextureSample, x0 + 1000, y0 + 950, texture=unreal.load_asset(GRIME), sampler_type=ST.SAMPLERTYPE_MASKS)
    link(mask_tex, "", leaks, "Tex")
    link(uv, "", leaks, "UVs")
    streak = new(m, Mul, x0 + 1250, y0 + 950)
    link(leaks, "G", streak, "A")
    link(wall, "", streak, "B")
    streak_amt = unary(m, Sat, binary(m, Mul, binary(m, Mul, streak, scalar(m, "StreakStrength", 1.3, x0 + 1250, y0 + 1100), x0 + 1400, y0 + 950), amount, x0 + 1530, y0 + 950), x0 + 1660, y0 + 950)
    base = lerp(m, base, binary(m, Mul, base, color_param(m, "StreakColor", (0.34, 0.27, 0.2), x0 + 1530, y0 + 1150), x0 + 1800, y0 + 1050), streak_amt, x0 + 1950, y0 + 950)

    low = unary(m, Sat, unary(m, Inv, binary(m, Div, up, scalar(m, "BaseHeight", 70.0, x0 + 400, y0 + 1400), x0 + 560, y0 + 1400), x0 + 700, y0 + 1400), x0 + 830, y0 + 1400)
    ragged = unary(m, Sat, binary(m, Mul, low, binary(m, Add, dirt, 0.7, x0 + 830, y0 + 1500), x0 + 980, y0 + 1400), x0 + 1110, y0 + 1400)
    band = binary(m, Mul, ragged, wall, x0 + 1250, y0 + 1400)
    base_amt = binary(m, Mul, binary(m, Mul, band, scalar(m, "BaseStrength", 0.85, x0 + 1250, y0 + 1520), x0 + 1400, y0 + 1400), amount, x0 + 1530, y0 + 1400)
    grimy = binary(m, Mul, base, color_param(m, "BaseGrimeColor", (0.12, 0.1, 0.075), x0 + 1530, y0 + 1600), x0 + 1800, y0 + 1500)
    return lerp(m, base, grimy, base_amt, x0 + 2100, y0 + 1200)


def make_material():
    path = MATS + "/M_WWO_KitConcrete"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset("M_WWO_KitConcrete", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("two_sided", True)       # like M_WWO_Kit: the kit's walls are single sheets
    m.set_editor_property("used_with_nanite", True)
    size = new(m, unreal.MaterialExpressionScalarParameter, -1500, 0, parameter_name="ConcreteSize", default_value=300.0)

    def projected(name, texture, sampler, function, y):
        obj = new(m, unreal.MaterialExpressionTextureObjectParameter, -1500, y, parameter_name=name,
                  texture=unreal.load_asset(texture), sampler_type=sampler)
        f = call(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/" + function, -1150, y)
        link(obj, "", f, "TextureObject")
        link(size, "", f, "TextureSize")
        return f

    color = projected("ConcreteColor", COLOR, ST.SAMPLERTYPE_COLOR, "WorldAlignedTexture", -500)
    normal = projected("ConcreteNormal", NORMAL, ST.SAMPLERTYPE_NORMAL, "WorldAlignedNormal", -250)
    ord_ = projected("ConcreteORD", ORD, ST.SAMPLERTYPE_MASKS, "WorldAlignedTexture", 250)

    # The kit material's own textures (same parameter names as M_WWO_Kit, so instances copy across).
    kit_base = new(m, unreal.MaterialExpressionTextureSampleParameter2D, -1500, -1000, parameter_name="BaseColorTexture",
                   texture=unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"), sampler_type=ST.SAMPLERTYPE_COLOR)
    kit_factor = new(m, unreal.MaterialExpressionVectorParameter, -1500, -800, parameter_name="BaseColorFactor",
                     default_value=unreal.LinearColor(1, 1, 1, 1))
    kit_normal = new(m, unreal.MaterialExpressionTextureSampleParameter2D, -1500, 550, parameter_name="NormalTexture",
                     texture=unreal.load_asset("/Engine/EngineMaterials/DefaultNormal"), sampler_type=ST.SAMPLERTYPE_NORMAL)

    # Darken by the kit's own value: 1 - KitContrast x (1 - brightness).
    gray = new(m, unreal.MaterialExpressionDesaturation, -1000, -950)
    link(multiply(m, kit_base, "RGB", kit_factor, "", -1200, -950), "", gray, "")
    link(new(m, unreal.MaterialExpressionConstant, -1150, -820, r=1.0), "", gray, "Fraction")
    lum = new(m, unreal.MaterialExpressionComponentMask, -850, -950, r=True, g=False, b=False, a=False)
    link(gray, "", lum, "")
    bright = new(m, unreal.MaterialExpressionSaturate, -720, -950)
    link(lum, "", bright, "")
    dark = new(m, unreal.MaterialExpressionOneMinus, -600, -950)
    link(bright, "", dark, "")
    contrast = new(m, unreal.MaterialExpressionScalarParameter, -720, -830, parameter_name="KitContrast", default_value=0.5)
    keep = new(m, unreal.MaterialExpressionOneMinus, -350, -950)
    link(multiply(m, dark, "", contrast, "", -480, -950), "", keep, "")

    tint = new(m, unreal.MaterialExpressionVectorParameter, -900, -650, parameter_name="ConcreteTint",
               default_value=unreal.LinearColor(0.44, 0.42, 0.385, 1))  # the scan is near-white painted concrete; Will: a touch toward tan
    base = multiply(m, multiply(m, color, "XYZ Texture", tint, "", -650, -500), "", keep, "", -200, -600)
    base = weather(m, base)
    mel.connect_material_property(base, "", MP.MP_BASE_COLOR)

    blend = call(m, "/Engine/Functions/Engine_MaterialFunctions02/Utility/BlendAngleCorrectedNormals", -500, 100)
    # The scan's relief also catches the lights like a wet surface: NormalFlatten eases it toward flat.
    flat = lerp(m, normal_rgb(m, normal), new(m, unreal.MaterialExpressionConstant3Vector, -900, 60, constant=unreal.LinearColor(0, 0, 1, 0)),
                scalar(m, "NormalFlatten", 0.45, -900, 160), -700, 60)
    link(flat, "", blend, "BaseNormal")
    link(kit_normal, "RGB", blend, "AdditionalNormal")
    mel.connect_material_property(blend, "", MP.MP_NORMAL)

    def channel(r, g, y):
        e = new(m, unreal.MaterialExpressionComponentMask, -700, y, r=r, g=g, b=False, a=False)
        link(ord_, "XYZ Texture", e, "")
        return e

    mel.connect_material_property(channel(True, False, 250), "", MP.MP_AMBIENT_OCCLUSION)
    # The scan's roughness dips low enough to look wet under indoor lights (Will: it should look rough
    # and dry): keep it above RoughnessMin and turn the specular right down.
    rough = lerp(m, scalar(m, "RoughnessMin", 0.93, -700, 430), new(m, unreal.MaterialExpressionConstant, -700, 520, r=1.0), channel(False, True, 350), -450, 400)
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, "Specular", 0.1, -450, 560), "", MP.MP_SPECULAR)
    emissive = new(m, unreal.MaterialExpressionVectorParameter, -500, 700, parameter_name="EmissiveFactor",
                   default_value=unreal.LinearColor(0, 0, 0, 1))
    mel.connect_material_property(emissive, "", MP.MP_EMISSIVE_COLOR)
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def make_glass():
    """M_WWO_KitGlass + MI_KitConcrete_M_Glass: the kit's glass is an opaque light grey (the free kit's
    files carry no transparency), so windows get real, dusty glass: see-through where clean
    (Opacity), clouded where the grime mask's blotches fall (DirtyOpacity)."""
    path = MATS + "/M_WWO_KitGlass"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset("M_WWO_KitGlass", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("two_sided", True)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    mask_tex = new(m, unreal.MaterialExpressionTextureObjectParameter, -1200, 0, parameter_name="GrimeMask",
                   texture=unreal.load_asset(GRIME), sampler_type=ST.SAMPLERTYPE_MASKS)
    blot = call(m, "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture", -900, 0)
    link(mask_tex, "", blot, "TextureObject")
    link(scalar(m, "DirtSize", 350.0, -1200, 150), "", blot, "TextureSize")
    dirt = pick(m, blot, "XYZ Texture", "r", -650, 0)
    dusty = unary(m, unreal.MaterialExpressionSaturate, binary(m, unreal.MaterialExpressionMultiply,
                  binary(m, unreal.MaterialExpressionSubtract, dirt, 0.35, -520, 0), 2.2, -400, 0), -280, 0)
    color = lerp(m, color_param(m, "GlassColor", (0.55, 0.68, 0.66), -520, -300), color_param(m, "DustColor", (0.45, 0.42, 0.36), -520, -150), dusty, -100, -250)
    opacity = lerp(m, scalar(m, "Opacity", 0.18, -520, 200), scalar(m, "DirtyOpacity", 0.7, -520, 300), dusty, -100, 200)
    rough = lerp(m, scalar(m, "Roughness", 0.08, -520, 420), new(m, unreal.MaterialExpressionConstant, -520, 520, r=0.7), dusty, -100, 420)
    mel.connect_material_property(color, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(opacity, "", MP.MP_OPACITY)
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    inst = OUT + "/MI_KitConcrete_M_Glass"
    mi = eal.load_asset(inst) if eal.does_asset_exist(inst) else tools.create_asset(
        "MI_KitConcrete_M_Glass", OUT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", m)
    eal.save_loaded_asset(mi)
    return mi


def make_instance(parent, name):
    kit = eal.load_asset(KIT_MATS + "/MI_Kit_" + name)
    if not kit:
        log("no kit material MI_Kit_%s" % name)
        return None
    out_name = "MI_KitConcrete_" + name
    path = OUT + "/" + out_name
    if eal.does_asset_exist(path):
        mi = eal.load_asset(path)
    else:
        mi = tools.create_asset(out_name, OUT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    for param in ("BaseColorTexture", "NormalTexture"):
        t = mel.get_material_instance_texture_parameter_value(kit, param)
        if t:
            mel.set_material_instance_texture_parameter_value(mi, param, t)
    for param in ("BaseColorFactor", "EmissiveFactor"):
        mel.set_material_instance_vector_parameter_value(mi, param, mel.get_material_instance_vector_parameter_value(kit, param))
    eal.save_loaded_asset(mi)
    return mi


def import_grime_mask():
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(os.path.dirname(os.path.abspath(__file__)), "art", "T_Grime_Mask.png"))
    task.set_editor_property("destination_path", SURFACE)
    task.set_editor_property("destination_name", "T_Grime_Mask")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    tex = eal.load_asset(GRIME)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property("srgb", False)
    eal.save_loaded_asset(tex)


def main():
    import_grime_mask()
    for t in (COLOR, NORMAL, ORD):
        if not eal.does_asset_exist(t):
            log("missing %s (copy the concrete surface in first; see this script's header)" % t)
            return
        tex = eal.load_asset(t)
        log("%s %dx%d" % (tex.get_name(), tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
    parent = make_material()
    made = [n for n in STRUCTURAL if make_instance(parent, n)]
    log("%d instances: %s" % (len(made), ", ".join(made)))
    make_glass()
    log("glass: MI_KitConcrete_M_Glass")
    log("done")


main()
