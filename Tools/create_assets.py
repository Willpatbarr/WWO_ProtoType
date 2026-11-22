"""Creates the project's generated assets (the Godot builder tools' counterpart).

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=Tools/create_assets.py -unattended -nullrhi

- Materials in /Game/WWO/Materials (solid, unlit, soft glow, ground rings, particles, HUD overlays,
  the arenas' sky dome).
- Data assets in /Game/WWO/Data from Tools/wwo_data.json (made from the Godot .tres files plus the
  tuned values in saved_settings.json by Tools/tres_to_json.py).
Safe to rerun: existing assets are updated in place.
"""
import json
import os
import re

import unreal

ROOT = os.path.dirname(os.path.abspath(__file__))
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log_warning("WWO: " + msg)


def get_or_create(name, folder, cls, factory):
    path = folder + "/" + name
    if eal.does_asset_exist(path):
        return eal.load_asset(path)
    return tools.create_asset(name, folder, cls, factory)


# --- Materials --------------------------------------------------------------------------------

def new_material(name):
    path = "/Game/WWO/Materials/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return tools.create_asset(name, "/Game/WWO/Materials", unreal.Material, unreal.MaterialFactoryNew())


def vec_param(mat, name, default, x, y):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", unreal.LinearColor(*default))
    return e


def scalar_param(mat, name, default, x, y):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    return e


def custom(mat, code, inputs, output_type, x, y, desc="Custom"):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    e.set_editor_property("code", code)
    e.set_editor_property("output_type", output_type)
    e.set_editor_property("description", desc)
    custom_inputs = []
    for n, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        custom_inputs.append(ci)
    e.set_editor_property("inputs", custom_inputs)
    for n, src in inputs:
        src_expr, src_out = src if isinstance(src, tuple) else (src, "")
        mel.connect_material_expressions(src_expr, src_out, e, n)
    return e


def mask(mat, src, r, g, b, a, x, y):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
    e.set_editor_property("r", r)
    e.set_editor_property("g", g)
    e.set_editor_property("b", b)
    e.set_editor_property("a", a)
    mel.connect_material_expressions(src, "", e, "")
    return e


def finish(mat):
    mel.layout_material_expressions(mat)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)


def make_solid():
    # Opaque lit: BaseColor = Color, Emissive = Color * Glow.
    m = new_material("M_WWO_Solid")
    color = vec_param(m, "Color", (1, 1, 1, 1), -600, 0)
    glow = scalar_param(m, "Glow", 0.0, -600, 200)
    rough = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -300, 300)
    rough.set_editor_property("r", 0.6)
    # Colors are sRGB (like Godot's): convert to linear here.
    c = custom(m, "float3 lin = pow(max(C.rgb, 0.0), 2.2);\nreturn float4(lin, G);", [("C", (color, "RGBA")), ("G", glow)],
               unreal.CustomMaterialOutputType.CMOT_FLOAT4, -400, 0, "SolidColor")
    rgb = mask(m, c, True, True, True, False, -200, 0)
    g = mask(m, c, False, False, False, True, -200, 150)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -50, 150)
    mel.connect_material_expressions(rgb, "", mul, "A")
    mel.connect_material_expressions(g, "", mul, "B")
    mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    m.set_editor_property("used_with_skeletal_mesh", True)
    finish(m)


def make_unlit():
    # Translucent unlit: Emissive = Color * (1 + Glow), Opacity = Alpha.
    m = new_material("M_WWO_Unlit")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    color = vec_param(m, "Color", (1, 1, 1, 1), -700, 0)
    alpha = scalar_param(m, "Alpha", 1.0, -700, 200)
    glow = scalar_param(m, "Glow", 0.0, -700, 300)
    c = custom(m, "return float4(pow(max(C.rgb, 0.0), 2.2) * (1.0 + G), saturate(A));", [("C", (color, "RGBA")), ("A", alpha), ("G", glow)],
               unreal.CustomMaterialOutputType.CMOT_FLOAT4, -400, 0)
    mel.connect_material_property(mask(m, c, True, True, True, False, -150, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask(m, c, False, False, False, True, -150, 150), "", unreal.MaterialProperty.MP_OPACITY)
    finish(m)


SOFT_CODE = """
float facing = abs(dot(normalize(N), normalize(V)));
float f = Rim > 0.5 ? pow(1.0 - facing, Edge) : pow(facing, Edge);
float3 col = lerp(pow(max(C.rgb, 0.0), 2.2), float3(1.0, 1.0, 1.0), saturate(Core * pow(facing, 3.0)));
return float4(col, saturate(C.a * Alpha * f));
"""


def make_soft(name, additive):
    # A soft glowing ball: brightest in the middle (or at the outline with Rim), fading where
    # it meets the floor or a wall (depth fade). Godot: FX.soft_material.
    m = new_material(name)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    normal = mel.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -900, -200)
    view = mel.create_material_expression(m, unreal.MaterialExpressionCameraVectorWS, -900, -100)
    color = vec_param(m, "Color", (1, 1, 1, 1), -900, 0)
    alpha = scalar_param(m, "Alpha", 1.0, -900, 150)
    edge = scalar_param(m, "Edge", 1.5, -900, 250)
    rim = scalar_param(m, "Rim", 0.0, -900, 350)
    core = scalar_param(m, "Core", 0.0, -900, 450)
    c = custom(m, SOFT_CODE, [("N", normal), ("V", view), ("C", (color, "RGBA")), ("Alpha", alpha), ("Edge", edge), ("Rim", rim), ("Core", core)],
               unreal.CustomMaterialOutputType.CMOT_FLOAT4, -550, 0, "Soft")
    rgb = mask(m, c, True, True, True, False, -250, -50)
    a = mask(m, c, False, False, False, True, -250, 100)
    fade = mel.create_material_expression(m, unreal.MaterialExpressionDepthFade, -50, 150)
    fade.set_editor_property("fade_distance_default", 40.0)
    mel.connect_material_expressions(a, "", fade, "Opacity")
    if additive:
        mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, 100, 0)
        mel.connect_material_expressions(rgb, "", mul, "A")
        mel.connect_material_expressions(fade, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        mel.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        mel.connect_material_property(fade, "", unreal.MaterialProperty.MP_OPACITY)
    finish(m)


GROUND_CODE = """
float2 p = UV * 2.0 - 1.0;
float r = length(p);
if (r > 1.0) return float4(0, 0, 0, 0);
float ring = smoothstep(1.0 - RingWidth, 1.0 - RingWidth * 0.35, r) * (1.0 - smoothstep(0.95, 1.0, r));
float inner = Fill * (0.45 + 0.55 * r * r) * (1.0 - smoothstep(0.9, 1.0, r));
float wave = 0.5 + 0.5 * sin(r * 16.0 - T * 2.5 + sin(atan2(p.y, p.x) * 3.0 + T * 0.7) * 1.4);
float a = max(ring, inner * (1.0 + Ripple * (wave - 0.5)));
return float4(lerp(pow(max(C.rgb, 0.0), 2.2), float3(1.0, 1.0, 1.0), ring * 0.25), saturate(C.a * Alpha * a));
"""


def make_ground():
    # A flat disc on the ground with a bright outer ring (Godot: FX.ground_material).
    m = new_material("M_WWO_Ground")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, -200)
    time = mel.create_material_expression(m, unreal.MaterialExpressionTime, -900, -100)
    color = vec_param(m, "Color", (1, 1, 1, 1), -900, 0)
    alpha = scalar_param(m, "Alpha", 1.0, -900, 150)
    ring = scalar_param(m, "RingWidth", 0.12, -900, 250)
    fill = scalar_param(m, "Fill", 0.0, -900, 350)
    ripple = scalar_param(m, "Ripple", 0.0, -900, 450)
    c = custom(m, GROUND_CODE, [("UV", uv), ("T", time), ("C", (color, "RGBA")), ("Alpha", alpha), ("RingWidth", ring), ("Fill", fill), ("Ripple", ripple)],
               unreal.CustomMaterialOutputType.CMOT_FLOAT4, -550, 0, "Ground")
    mel.connect_material_property(mask(m, c, True, True, True, False, -250, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask(m, c, False, False, False, True, -250, 150), "", unreal.MaterialProperty.MP_OPACITY)
    finish(m)


def make_particle():
    # Instanced particles: color and alpha per instance (custom data 0-3), soft edges.
    m = new_material("M_WWO_Particle")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    normal = mel.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -900, -200)
    view = mel.create_material_expression(m, unreal.MaterialExpressionCameraVectorWS, -900, -100)
    data = []
    for i in range(4):
        e = mel.create_material_expression(m, unreal.MaterialExpressionPerInstanceCustomData, -900, i * 100)
        e.set_editor_property("data_index", i)
        data.append(e)
    c = custom(m, "float facing = abs(dot(normalize(N), normalize(V)));\nreturn float4(pow(max(float3(R, G, B), 0.0), 2.2) * 1.5, saturate(A * (0.35 + 0.65 * facing)));",
               [("N", normal), ("V", view), ("R", data[0]), ("G", data[1]), ("B", data[2]), ("A", data[3])],
               unreal.CustomMaterialOutputType.CMOT_FLOAT4, -550, 0, "Particle")
    mel.connect_material_property(mask(m, c, True, True, True, False, -250, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask(m, c, False, False, False, True, -250, 150), "", unreal.MaterialProperty.MP_OPACITY)
    finish(m)


def make_chunk():
    m = new_material("M_WWO_Chunk")
    m.set_editor_property("used_with_instanced_static_meshes", True)
    parts = []
    for i in range(3):
        e = mel.create_material_expression(m, unreal.MaterialExpressionPerInstanceCustomData, -700, i * 100)
        e.set_editor_property("data_index", i)
        parts.append(e)
    c = custom(m, "return pow(max(float3(R, G, B), 0.0), 2.2);", [("R", parts[0]), ("G", parts[1]), ("B", parts[2])],
               unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, 0, "Chunk")
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    finish(m)


VIGNETTE_CODE = """
float2 p = UV - 0.5;
p.x *= Aspect;
return float4(0, 0, 0, smoothstep(0.3, 0.9, length(p)) * Strength);
"""

SCOPE_CODE = """
float2 p = UV - 0.5;
p.x *= Aspect;
float d = length(p);
float outside = smoothstep(0.44, 0.446, d);
float lines = max(step(abs(p.x), 0.0011), step(abs(p.y), 0.0011)) * step(0.025, d) * (1.0 - outside);
float dotMask = 1.0 - smoothstep(0.0028, 0.0038, d);
float3 col = lerp(float3(0, 0, 0), float3(1.0, 0.15, 0.1), dotMask);
return float4(col, max(max(outside, lines * 0.9), dotMask) * Strength);
"""


def make_ui(name, code):
    # HUD overlays (Godot: vignette.gdshader and scope.gdshader).
    m = new_material(name)
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -700, -100)
    strength = scalar_param(m, "Strength", 0.0, -700, 0)
    aspect = scalar_param(m, "Aspect", 1.777, -700, 100)
    c = custom(m, code, [("UV", uv), ("Strength", strength), ("Aspect", aspect)], unreal.CustomMaterialOutputType.CMOT_FLOAT4, -400, 0, name)
    mel.connect_material_property(mask(m, c, True, True, True, False, -150, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask(m, c, False, False, False, True, -150, 150), "", unreal.MaterialProperty.MP_OPACITY)
    finish(m)


SKY_CODE = """
// Godot 4's default ProceduralSkyMaterial (sky_top / sky_horizon / ground colors, curves, the
// sun's halo), seen along the view ray; Unreal's z is Godot's y. Colors are sRGB like Godot's.
float3 d = normalize(-V);
float PI_2 = 1.5707963;
float3 top = pow(float3(0.385, 0.454, 0.55), 2.2);
float3 horizon = pow(float3(0.6463, 0.6558, 0.6708), 2.2);
float3 bottom = pow(float3(0.2, 0.169, 0.133), 2.2);
float v = acos(clamp(d.z, -1.0, 1.0));
float c = 1.0 - v / PI_2;
float3 sky = lerp(horizon, top, saturate(1.0 - pow(saturate(1.0 - c), 1.0 / 0.15)));
float sun = acos(clamp(dot(normalize(SunDir), d), -1.0, 1.0));
float sun_max = 0.5235988; // sun_angle_max 30 degrees, light_angular_distance 0
if (sun < sun_max) {
    sky = lerp(float3(1, 1, 1), sky, saturate(1.0 - pow(saturate(1.0 - sun / sun_max), 1.0 / 0.15)));
}
c = (v - PI_2) / PI_2;
float3 ground = lerp(horizon, bottom, saturate(1.0 - pow(saturate(1.0 - c), 1.0 / 0.02)));
return (d.z >= 0.0 ? sky : ground) * Energy;
"""


def make_sky():
    # The arenas' sky dome (Godot: the WorldEnvironment's default ProceduralSkyMaterial). Is Sky, so
    # the sky light's real-time capture lights the level from it as Godot's ambient light does.
    # Energy scales it to the sun (build_maps.py sets it to the sun's lux / pi, so the sky and the
    # sun keep Godot's ratio); SunDir points at the sun.
    m = new_material("M_WWO_Sky")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("is_sky", True)
    view = mel.create_material_expression(m, unreal.MaterialExpressionCameraVectorWS, -700, -100)
    sun = vec_param(m, "SunDir", (-0.5, 0.0, 0.866, 0.0), -700, 0)
    energy = scalar_param(m, "Energy", 1.0, -700, 150)
    c = custom(m, SKY_CODE, [("V", view), ("SunDir", (sun, "RGB")), ("Energy", energy)], unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, 0, "GodotSky")
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(m)


def make_materials():
    make_solid()
    make_sky()
    make_unlit()
    make_soft("M_WWO_SoftAdd", True)
    make_soft("M_WWO_SoftAlpha", False)
    make_ground()
    make_particle()
    make_chunk()
    make_ui("M_WWO_Vignette", VIGNETTE_CODE)
    make_ui("M_WWO_Scope", SCOPE_CODE)
    log("materials done")


# --- Data assets ------------------------------------------------------------------------------

def pascal_to_snake(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


ENUMS = {
    "kind_grenade": {"explosive": "EXPLOSIVE", "pool": "POOL", "spike": "SPIKE", "tripmine": "TRIPMINE", "wall": "WALL", "blind": "BLIND", "heal_pool": "HEAL_POOL", "reveille": "REVEILLE"},
    "kind_ability": {"passive": "PASSIVE", "active": "ACTIVE", "triggered": "TRIGGERED", "charged": "CHARGED"},
    "ult": {"blast": "BLAST", "veil": "VEIL", "entrench": "ENTRENCH", "beam": "BEAM"},
}
OPS = [unreal.WWOStatOp.FLAT, unreal.WWOStatOp.PERCENT_ADD, unreal.WWOStatOp.MULTIPLY]


def convert(asset, key, value):
    if key == "kind":
        if isinstance(asset, unreal.WWOGrenadeData):
            return getattr(unreal.WWOGrenadeKind, ENUMS["kind_grenade"][value])
        return getattr(unreal.WWOAbilityKind, ENUMS["kind_ability"][value])
    if key == "ult":
        return getattr(unreal.WWOUltKind, ENUMS["ult"][value])
    if key == "mods":
        mods = []
        for m in value:
            mod = unreal.WWOStatModifier()
            mod.set_editor_property("stat", m["stat"])
            mod.set_editor_property("op", OPS[int(m.get("op", 1))])
            mod.set_editor_property("value", float(m.get("value", 0.0)))
            mods.append(mod)
        return mods
    if isinstance(value, list) and len(value) == 4:
        return unreal.LinearColor(*value)
    if isinstance(value, list) and len(value) == 3:
        return unreal.Vector(*value)
    if isinstance(value, bool):
        return value
    if isinstance(value, float) and key not in ("order",):
        return value
    return value


SKIP = {"model", "script"}


def fill(asset, item):
    for key, value in item.items():
        if key in SKIP:
            continue
        try:
            asset.set_editor_property(key, convert(asset, key, value))
        except Exception as e:  # a Godot field the Unreal asset doesn't have
            log("skip %s.%s (%s)" % (asset.get_name(), key, e))


def data_asset(name, folder, cls):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    return get_or_create(name, folder, cls, factory)


def make_data():
    data = json.load(open(os.path.join(ROOT, "wwo_data.json")))
    groups = {
        "weapons": ("Weapons", "DA_Weapon_", unreal.WWOWeaponData),
        "grenades": ("Grenades", "DA_Grenade_", unreal.WWOGrenadeData),
        "abilities": ("Abilities", "DA_Ability_", unreal.WWOAbilityData),
        "frame_parts": ("FrameParts", "DA_FramePart_", unreal.WWOFramePartData),
        "flora": ("Flora", "DA_Flora_", unreal.WWOFloraData),
        "traits": ("Traits", "DA_Trait_", unreal.WWOTraitData),
    }
    for key, (folder, prefix, cls) in groups.items():
        for item in data[key]:
            asset = data_asset(prefix + item["id"], "/Game/WWO/Data/" + folder, cls)
            fill(asset, item)
            eal.save_loaded_asset(asset)
        log("%d %s" % (len(data[key]), key))
    for key, cls in (("soldier", unreal.WWOSoldierData), ("swarm", unreal.WWOSwarmData), ("tank", unreal.WWOTankData)):
        asset = data_asset("DA_Enemy_" + key.capitalize(), "/Game/WWO/Data/Enemies", cls)
        fill(asset, data[key])
        eal.save_loaded_asset(asset)
    asset = data_asset("DA_Ally", "/Game/WWO/Data/Allies", unreal.WWOAllyData)
    fill(asset, data["ally"])
    eal.save_loaded_asset(asset)
    log("data done")


make_materials()
make_data()
