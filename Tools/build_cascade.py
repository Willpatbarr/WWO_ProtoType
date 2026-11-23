"""Builds L_Cascade (the 6v6 PvP map, also run by the PvE waves) from Tools/cascade_layout.py's boxes, dressed
with Cascade's art kit (Tools/cascade_dress.py places it; Tools/import_cascade_kit.py imports it and makes its
materials, and must have run).

Not run on its own: Tools/build_maps.py calls build(ns) when WWO_MAPS has "cascade", passing its own globals
(Builder, mu, place, new_level, lighting's constants, solid_material, kit_mesh, merge_kind, lamp_instance,
fit_mesh, actors, levels, eal, tools, mel, log...), so this follows build_arboretum() call for call.

- Structure: every box kind merged into one mesh in /Game/WWO/Maps/Cascade (collision merged too), in the kit's
  hewn stone projected in world space (MI_Cas_Stone<Floor|Wall|Roof|Mossy|Dark>). The layout's gameplay is
  untouched: cover boxes (low walls, blocks, pillars, monoliths, the hall column) stay separate actors tagged
  Cover but hidden, each under a chipped kit block; crates are the sci-fi kit's crates (fit_mesh); ramps are the
  stairs' and bridges' collision, hidden in game; the visual steps, arches, brackets and trim have no collision.
- Replaced by the kit (CAS_REPLACED: not built as boxes): the piers (rock pillars), the giant arches, the mesas
  (karst spires), the ivy cards. The islets' cylinders stay as hidden collision under the rock stacks, and the old
  stepped cone above the hall's drum (body_top) as hidden collision under the citadel's tower cluster.
- Orange trim: M_WWO_Lamp ("CasTrim"). Lights: the layout's 42 warm point lights, no shadows.
- Chasm: a glossy teal water plane at z -10 with moving ripples and foam (no collision). Falling kills at z -9: a
  TargetPoint tagged KillZ, which UWWOHealthComponent reads (the default is -30 m).
- Lighting: lighting()'s setup with the sun at SUN_PITCH / SUN_YAW, warm white, a movable sky light (a stationary
  one gives nothing without a bake) tinted warm against the blue it captures, a bluish height fog, and its own
  sky (M_WWO_Sky_Cascade, made here: M_WWO_Sky's shader with the concept's bright blue and drifting clouds, so
  the other maps' sky is untouched). The post process adds bloom, a vignette and a little saturation and contrast.
- Gameplay: a PlayerStart per team spawn (6 + 6), the PvE start first (ChoosePlayerStart takes the
  first), EnemySpawn markers tagged PvEOnly (so PvP respawns don't use them) with their kinds, one
  NavMeshBoundsVolume, invisible boundary walls (r 100 m, to z 40) and a ceiling.
"""
import math
import os
import random
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cascade_layout  # noqa: E402
import cascade_dress  # noqa: E402

CAS_MERGED = "/Game/WWO/Maps/Cascade"
CAS_COVER = ("low_cover", "block", "pillar", "monolith", "column", "crate")         # own actors, tagged Cover
CAS_SEPARATE = CAS_COVER + ("ramp",)                                       # not merged (ramps stay hidden collision)
CAS_NO_COLLISION = cascade_layout.NO_COLLISION
CAS_NO_SHADOW = ("trim", "ramp", "body_top")
CAS_REPLACED = ("pier", "giant_arch", "mesa", "ivy")                       # the art kit draws these (Tools/cascade_dress.py)
CAS_HIDDEN = ("ramp", "islet", "body_top") + CAS_COVER                     # collision only: a kit piece is what you see
KIT_TEX = "/Game/WWO/Maps/CascadeKit/Textures/"
KIT_MATS = "/Game/WWO/Maps/CascadeKit/Materials"
SUN_PITCH = -46.0
SUN_YAW = 35.0            # off the map's axes, so walls and arches throw slanted shadows
SKY_LIGHT = 1.4           # the sky's fill: with no bounce light, this is all that reaches the shade
SUN_COLOR = (1.0, 0.89, 0.74)
FOG_COLOR = (0.62, 0.74, 0.86)
LIGHT_CANDELAS = 200.0
WATER_COLOR = (0.010, 0.060, 0.075)   # linear: deep teal (the sky's sheen on it brightens it a lot)
WATER_SHALLOW = (0.030, 0.150, 0.150)
KILL_Z = -9.0
MOSS_TINT = (0.62, 0.72, 0.5)    # over the kit's moss color on the map's own stone: darker and less yellow than the texture

# M_WWO_Sky's shader (Tools/create_assets.py SKY_CODE) with the concept's clear blue sky (sRGB) and drifting
# clouds from the kit's noise texture (two samples on a flat sheet overhead, thinning to nothing at the horizon).
SKY_TOP, SKY_HORIZON = (0.20, 0.44, 0.82), (0.72, 0.84, 0.95)
CLOUD_UV = """
float3 d = normalize(-V);
float2 p = d.xy / max(d.z + 0.10, 0.04);
return float4(p * 0.11 + T * float2(0.0020, 0.0011), p * 0.31 + T * float2(0.0031, -0.0013) + 0.37);
"""
SKY_CODE = """
float3 d = normalize(-V);
float PI_2 = 1.5707963;
float3 top = pow(float3(%.3f, %.3f, %.3f), 2.2);
float3 horizon = pow(float3(%.3f, %.3f, %.3f), 2.2);
float3 bottom = pow(float3(0.2, 0.169, 0.133), 2.2);
float v = acos(clamp(d.z, -1.0, 1.0));
float c = 1.0 - v / PI_2;
float3 sky = lerp(horizon, top, saturate(1.0 - pow(saturate(1.0 - c), 1.0 / 0.15)));
float sun = acos(clamp(dot(normalize(SunDir), d), -1.0, 1.0));
float sun_max = 0.5235988;
float glare = 0.0;
if (sun < sun_max) {
    glare = 1.0 - saturate(1.0 - pow(saturate(1.0 - sun / sun_max), 1.0 / 0.15));
    sky = lerp(sky, float3(1, 1, 1), glare);
}
float cloud = saturate((CA.a * 0.62 + CB.a * 0.30 + CA.r * 0.28 - Cover) * 3.2) * saturate(d.z * 5.0);
float3 lit = lerp(float3(0.56, 0.62, 0.74), float3(1.0, 0.96, 0.90), saturate(CB.r * 0.9 + 0.25 + glare));
sky = lerp(sky, lit, cloud * 0.92);
c = (v - PI_2) / PI_2;
float3 ground = lerp(horizon, bottom, saturate(1.0 - pow(saturate(1.0 - c), 1.0 / 0.02)));
return (d.z >= 0.0 ? sky : ground) * Energy;
""" % (SKY_TOP + SKY_HORIZON)


def custom_node(ns, m, desc, code, out, inputs, x, y):
    c = ns.mel.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out)
    c.set_editor_property("description", desc)
    ins = []
    for n, _, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    for n, src, src_out in inputs:
        ns.mel.connect_material_expressions(src, src_out, c, n)
    return c


def noise_sample(ns, m, uv, channels, x, y):
    """T_Cas_Noise sampled at two of a float4's components (channels "rg" or "ba")."""
    mel = ns.mel
    k = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, x - 200, y)
    for ch in "rgba":
        k.set_editor_property(ch, ch in channels)
    mel.connect_material_expressions(uv, "", k, "")
    t = mel.create_material_expression(m, unreal.MaterialExpressionTextureSample, x, y)
    t.set_editor_property("texture", unreal.load_asset(KIT_TEX + "T_Cas_Noise"))
    t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    mel.connect_material_expressions(k, "", t, "UVs")
    return t


def sky_material(ns):
    """M_WWO_Sky_Cascade: M_WWO_Sky with Cascade's colors and clouds (remade every build, so changes apply)."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    path = "/Game/WWO/Materials/M_WWO_Sky_Cascade"
    m = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "M_WWO_Sky_Cascade", "/Game/WWO/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(m)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("is_sky", True)
    view = mel.create_material_expression(m, unreal.MaterialExpressionCameraVectorWS, -1300, -100)
    time = mel.create_material_expression(m, unreal.MaterialExpressionTime, -1300, 300)
    sun = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -700, 0)
    sun.set_editor_property("parameter_name", "SunDir")
    sun.set_editor_property("default_value", unreal.LinearColor(-0.5, 0.0, 0.866, 0.0))
    energy = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -700, 150)
    energy.set_editor_property("parameter_name", "Energy")
    energy.set_editor_property("default_value", 1.0)
    cover = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -700, 250)
    cover.set_editor_property("parameter_name", "Cover")
    cover.set_editor_property("default_value", 0.62)
    uv = custom_node(ns, m, "CloudUV", CLOUD_UV, unreal.CustomMaterialOutputType.CMOT_FLOAT4, [("V", view, ""), ("T", time, "")], -1100, 300)
    ca, cb = noise_sample(ns, m, uv, "rg", -700, 400), noise_sample(ns, m, uv, "ba", -700, 650)
    c = custom_node(ns, m, "CascadeSky", SKY_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                    [("V", view, ""), ("SunDir", sun, "RGB"), ("Energy", energy, ""), ("CA", ca, "RGBA"), ("CB", cb, "RGBA"), ("Cover", cover, "")], -400, 0)
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def sky_instance(ns):
    """lighting()'s sky, with the sun where Cascade's is (its own instance: the other maps keep theirs)."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    path = "/Game/WWO/Materials/Map/MI_WWO_Sky_Cascade"
    if eal.does_asset_exist(path):
        mi = eal.load_asset(path)
    else:
        mi = tools.create_asset("MI_WWO_Sky_Cascade", "/Game/WWO/Materials/Map", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", sky_material(ns))
    p = math.radians(-SUN_PITCH)
    mel.set_material_instance_vector_parameter_value(mi, "SunDir", unreal.LinearColor(-math.cos(p), 0.0, math.sin(p), 0.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Energy", ns.SKY_ENERGY)
    eal.save_loaded_asset(mi)
    return mi


WATER_UV = """
// Two sets of ripples sliding across each other, and slow foam drifting on top.
float2 p = P.xy / Size;
return float4(p + T * float2(0.011, 0.006), p * 2.3 + T * float2(-0.008, 0.013));
"""
WATER_COLOR_CODE = """
float foam = saturate((NA.a * 0.75 + NB.g * 0.45 - 0.78) * 5.0) * Foam;
float3 c = lerp(Deep, Shallow, saturate(NA.r * 1.4 - 0.2));
return lerp(c, float3(0.55, 0.68, 0.70), foam);
"""


def water_material(ns):
    """M_WWO_CascadeWater: lit, teal and glossy with moving ripples (the kit's moss normal map, flattened, sliding two
    ways) and drifting foam from the kit's noise, so the sky and the sun's glare break up on it. Remade each build."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    path = "/Game/WWO/Materials/M_WWO_CascadeWater"
    m = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "M_WWO_CascadeWater", "/Game/WWO/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(m)
    F4, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT4, unreal.CustomMaterialOutputType.CMOT_FLOAT3
    pos = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -1500, 0)
    time = mel.create_material_expression(m, unreal.MaterialExpressionTime, -1500, 100)
    size = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -1500, 200)
    size.set_editor_property("r", 1800.0)
    uv = custom_node(ns, m, "WaterUV", WATER_UV, F4, [("P", pos, ""), ("T", time, ""), ("Size", size, "")], -1250, 0)
    na, nb = noise_sample(ns, m, uv, "rg", -800, -300), noise_sample(ns, m, uv, "ba", -800, -50)
    normals = []
    for i, ch in enumerate(("rg", "ba")):
        k = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, -1000, 300 + 250 * i)
        for c in "rgba":
            k.set_editor_property(c, c in ch)
        mel.connect_material_expressions(uv, "", k, "")
        t = mel.create_material_expression(m, unreal.MaterialExpressionTextureSample, -800, 300 + 250 * i)
        t.set_editor_property("texture", unreal.load_asset(KIT_TEX + "T_Cas_Moss_N"))
        t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        mel.connect_material_expressions(k, "", t, "UVs")
        normals.append(t)
    consts = {}
    for i, (name, rgb) in enumerate((("Deep", WATER_COLOR), ("Shallow", WATER_SHALLOW))):
        e = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -800, 800 + 150 * i)
        e.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        consts[name] = e
    foam = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -800, 1100)
    foam.set_editor_property("r", 0.8)
    col = custom_node(ns, m, "WaterColor", WATER_COLOR_CODE, F3, [("NA", na, "RGBA"), ("NB", nb, "RGBA"), ("Deep", consts["Deep"], ""),
                                                                 ("Shallow", consts["Shallow"], ""), ("Foam", foam, "")], -400, -200)
    mel.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    nrm = custom_node(ns, m, "WaterNormal", "return normalize(float3((A.xy + B.xy) * 0.22, 1.0));", F3, [("A", normals[0], "RGB"), ("B", normals[1], "RGB")], -400, 350)
    mel.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
    for value, prop, y in ((0.16, unreal.MaterialProperty.MP_ROUGHNESS, 600), (0.55, unreal.MaterialProperty.MP_SPECULAR, 700)):
        k = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -400, y)
        k.set_editor_property("r", value)
        mel.connect_material_property(k, "", prop)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def stone_instance(ns, name, tint, **scalars):
    """An instance of the kit's world-projected stone (M_WWO_CasStone) for the map's own boxes."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    path = KIT_MATS + "/MI_Cas_Stone" + name
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "MI_Cas_Stone" + name, KIT_MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    parent = unreal.load_asset("/Game/WWO/Materials/M_WWO_CasStone")
    if not parent:
        raise RuntimeError("missing M_WWO_CasStone: run Tools/import_cascade_kit.py first")
    mi.set_editor_property("parent", parent)
    mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    mel.set_material_instance_vector_parameter_value(mi, "MossTint", unreal.LinearColor(*MOSS_TINT, 1.0))
    for k, v in scalars.items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    eal.save_loaded_asset(mi)
    return mi


def lighting(ns):
    """build_maps.lighting() with Cascade's sun, sky and haze."""
    actors = ns.actors
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000.0), unreal.Rotator(0.0, SUN_PITCH, SUN_YAW))
    sun.light_component.set_editor_property("intensity", ns.SUN_LUX)
    for k, v in (("enable_light_shaft_bloom", True), ("bloom_scale", 0.12), ("bloom_threshold", 0.2)):   # a soft glare around the sun
        try:
            sun.light_component.set_editor_property(k, v)
        except Exception as e:
            ns.log("cascade: sun %s not set (%s)" % (k, e))
    sun.light_component.set_light_color(unreal.LinearColor(SUN_COLOR[0], SUN_COLOR[1], SUN_COLOR[2], 1.0))
    sun.set_actor_label("Sun")
    dome = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    comp = dome.static_mesh_component
    comp.set_static_mesh(ns.SKY_SPHERE)
    comp.set_material(0, sky_instance(ns))
    comp.set_collision_profile_name("NoCollision")
    comp.set_editor_property("cast_shadow", False)
    comp.set_editor_property("can_ever_affect_navigation", False)
    radius = max(ns.SKY_SPHERE.get_bounding_box().max.x, 1.0)
    dome.set_actor_scale3d(unreal.Vector(1, 1, 1) * (500000.0 / radius))
    dome.set_actor_label("SkyDome")
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500.0))
    sc = sky.light_component
    sc.set_editor_property("real_time_capture", True)
    sc.set_editor_property("lower_hemisphere_is_black", False)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)   # a stationary sky light gives nothing until lighting is built, and it never is
    sc.set_editor_property("intensity", SKY_LIGHT)
    sc.set_light_color(unreal.LinearColor(1.0, 0.8, 0.6, 1.0))   # against the clear blue it captures: the shade comes out cool gray, not blue
    sky.set_actor_label("SkyLight")
    # Bluish haze that hides the far arches and mesas, thicker below the decks (the fog's base is at z 0).
    fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0.0))
    fc = fog.component
    fc.set_editor_property("fog_density", 0.011)
    fc.set_editor_property("fog_height_falloff", 0.04)
    fc.set_editor_property("start_distance", 4500.0)
    fc.set_editor_property("fog_max_opacity", 0.8)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(*(c * ns.SKY_ENERGY for c in FOG_COLOR), 1.0))
    fog.set_actor_label("HeightFog")
    ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    for k, v in (("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM), ("auto_exposure_min_brightness", ns.EXPOSURE_MIN_EV),
                 ("auto_exposure_max_brightness", ns.EXPOSURE_MAX_EV), ("auto_exposure_bias", ns.EXPOSURE_BIAS)):
        s.set_editor_property("override_" + k, True)
        s.set_editor_property(k, v)
    # The grade: a little glow off the trims and the water, corners drawn in, color and contrast up a touch,
    # contact shade under the decks and in the corners.
    for k, v in (("bloom_intensity", 0.55), ("vignette_intensity", 0.28), ("ambient_occlusion_intensity", 0.6), ("ambient_occlusion_radius", 120.0),
                 ("color_saturation", unreal.Vector4(1.08, 1.08, 1.08, 1.0)), ("color_contrast", unreal.Vector4(1.05, 1.05, 1.05, 1.0))):
        try:
            s.set_editor_property("override_" + k, True)
            s.set_editor_property(k, v)
        except Exception as e:
            ns.log("cascade: post process %s not set (%s)" % (k, e))
    ppv.set_editor_property("settings", s)
    ppv.set_actor_label("PostProcess")


def build(ns):
    """ns: build_maps.py's globals (a SimpleNamespace)."""
    eal, actors, levels, mu = ns.eal, ns.actors, ns.levels, ns.mu
    ns.new_level("/Game/WWO/Maps/L_Cascade")
    if eal.does_directory_exist(CAS_MERGED):
        eal.delete_directory(CAS_MERGED)
    layout = cascade_layout.build()
    b = ns.Builder(1, 1, 13)
    rng = random.Random(13)
    lighting(ns)
    # The kit's hewn stone, projected in world space (Tools/import_cascade_kit.py): warm pale paving, cooler walls,
    # mossier roofs and balcony, and the cover a little lighter and warmer so it reads against the walls.
    si = lambda name, tint, **kw: stone_instance(ns, name, tint, **kw)   # noqa: E731
    floor = si("Floor", (1.12, 1.06, 0.98), StoneSize=720.0, MossAmount=0.55, Streaks=0.0)
    moss = si("Mossy", (1.0, 1.02, 0.95), StoneSize=720.0, MossAmount=1.3, MossSlope=0.35)
    wall = si("Wall", (1.45, 1.43, 1.42), StoneSize=820.0, MossAmount=0.7)
    roof = si("Roof", (1.2, 1.25, 1.2), StoneSize=820.0, MossAmount=1.6, MossSlope=0.3)
    stone = si("Dark", (1.15, 1.15, 1.18), StoneSize=820.0, MossAmount=1.0)
    si("Cover", (1.6, 1.5, 1.36), MossAmount=0.8, StoneSize=560.0)       # the kit blocks over the cover boxes (cascade_dress.FIELDS)
    dark = si("Rail", (1.0, 0.98, 0.95), StoneSize=260.0, MossAmount=1.2)      # the rails: low walls of small worn stone
    mats = {"floor": floor, "floor_z3": floor, "floor_z5": floor, "dais": floor, "landing": floor, "bridge": floor, "plinth": floor,
            "steps": floor, "balcony": moss, "hall_wall": wall, "body": wall, "body_top": None, "wall": wall, "tunnel_wall": wall, "gallery_wall": wall,
            "roof": roof, "tunnel_roof": roof, "gallery_roof": roof, "islet": None, "arch": stone, "bracket": stone,
            "low_cover": None, "block": None, "crate": None, "pillar": None, "monolith": None, "column": None,
            "rail": dark, "trim": ns.lamp_instance("CasTrim", (1.0, 0.2, 0.02), 4.5), "ramp": None}
    dresser = cascade_dress.Dresser(ns, layout)
    groups = {}
    covers = 0
    crates = sum(1 for box in layout["boxes"] if box.kind == "crate")
    for box in layout["boxes"]:
        kind = box.kind
        if kind in CAS_REPLACED:
            continue
        if kind == "body" and box.z - box.height * 0.5 > 13.9:
            kind = "body_top"        # the old stepped cone: kept as collision under the kit's tower cluster
        if kind == "trim" and box.z > 14.2 and math.hypot(box.x, box.y) < 24.0:
            continue                 # the cone's glowing tier rings
        a = b.box(mu(box.x, box.y, box.z), (box.length, box.thickness, box.height), mats[kind], "Cas_" + kind,
                  ("Cover",) if kind in CAS_COVER else (), yaw=90.0 - box.yaw, pitch=box.pitch,
                  hidden=kind in CAS_HIDDEN, mesh=ns.CYLINDER if box.shape == "cylinder" else None)
        if kind in CAS_COVER:
            dresser.cover(box, covers)
            covers += 1
        if kind in CAS_NO_SHADOW:
            a.static_mesh_component.set_editor_property("cast_shadow", False)
        if kind in CAS_NO_COLLISION:
            a.static_mesh_component.set_collision_profile_name("NoCollision")
            a.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)
        if kind not in CAS_SEPARATE and kind != "islet":
            groups.setdefault(kind, []).append(a)
    merged = 0
    for kind, group in groups.items():
        if len(group) < 2:
            continue
        try:
            m = ns.merge_kind(kind, group, CAS_MERGED, "Cas")
            if m:
                merged += 1
                if kind in CAS_NO_SHADOW:
                    m.static_mesh_component.set_editor_property("cast_shadow", False)
                if kind in CAS_HIDDEN:
                    m.set_actor_hidden_in_game(True)
                if kind in CAS_NO_COLLISION:
                    # The merged mesh carries the pieces' simple collision; these kinds are looks only.
                    m.static_mesh_component.set_collision_profile_name("NoCollision")
                    m.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)
        except Exception as e:  # keep the separate pieces if merging isn't available
            ns.log("cascade: couldn't merge %s (%s); keeping %d pieces" % (kind, e, len(group)))
    if merged:
        eal.save_directory(CAS_MERGED, only_if_is_dirty=False, recursive=True)

    # The art kit over all of it: rock, the citadel's towers, wall dressing, plants, waterfalls.
    dresser.rocks()
    dresser.citadel()
    slits, buttresses = dresser.walls()
    ivy = dresser.edges()
    maples = dresser.plants()
    dresser.water()
    instances = dresser.flush()

    # The chasm's water (no collision: falling through it reaches the kill height).
    water = b.box(unreal.Vector(0, 0, cascade_layout.WATER_Z * 100.0 - 5.0), (6000.0, 6000.0, 0.1), water_material(ns), "Cas_water")   # out past the haze: no edge to see
    water.static_mesh_component.set_collision_profile_name("NoCollision")
    water.static_mesh_component.set_editor_property("cast_shadow", False)
    water.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)

    # Lights: warm, no shadows (the machine is weak; the Arboretum's precedent).
    for i, (x, y, z, color, radius, name) in enumerate(layout["lights"]):
        a = actors.spawn_actor_from_class(unreal.PointLight, mu(x, y, z), unreal.Rotator(0.0, 0.0, 0.0))
        c = a.get_component_by_class(unreal.PointLightComponent)
        c.set_mobility(unreal.ComponentMobility.MOVABLE)
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        c.set_intensity(LIGHT_CANDELAS)
        # The layout's warm colors pulled a third of the way to white, so the rooms aren't all one orange-brown.
        c.set_light_color(unreal.LinearColor(*(k + (1.0 - k) * 0.35 for k in color), 1.0))
        c.set_editor_property("attenuation_radius", radius * 100.0)
        c.set_editor_property("cast_shadows", False)
        a.set_actor_label("Cas_light_%s_%d" % (name, i))

    # PvE start first (ChoosePlayerStart takes the first PlayerStart), then a Player Start on each team
    # spawn, facing the middle, on a team-colored pad.
    px, py, pz, _ = layout["pve"]["player"]
    ns.place(unreal.PlayerStart, mu(px, py, pz + 1.05), mu(0, 0, pz + 1.05), 0.0, "PlayerStart_PvE")
    for team, color in (("blue", (0.16, 0.25, 0.46)), ("orange", (0.5, 0.29, 0.13))):   # muted: a mark on the floor, not a beacon
        mat = ns.solid_material("CasPad" + team.capitalize(), color)
        for i, (x, y, z, _) in enumerate(layout["spawns"][team]):
            b.box(mu(x, y, z + 0.02), (2.2, 2.2, 0.04), mat, "SpawnPad", mesh=ns.CYLINDER)
            ns.place(unreal.PlayerStart, mu(x, y, z + 1.05), mu(0, 0, z + 1.05), 0.0, "PlayerStart_%s%d" % (team.capitalize(), i + 1))
    # PvE: enemy spawn markers with their kinds (PvEOnly: PvP respawns skip them).
    for i, (x, y, z, kinds) in enumerate(layout["pve"]["enemies"]):
        point = actors.spawn_actor_from_class(unreal.TargetPoint, mu(x, y, z + 0.1))
        point.set_editor_property("tags", ["EnemySpawn", "PvEOnly"] + kinds)
        point.set_actor_label("EnemySpawn_%d" % i)
    # Falling into the chasm kills here (UWWOHealthComponent reads the KillZ point's height).
    kill = actors.spawn_actor_from_class(unreal.TargetPoint, mu(0, 0, KILL_Z))
    kill.set_editor_property("tags", ["KillZ"])
    kill.set_actor_label("KillZ")
    # Invisible boundary: a ring of walls at r 80 m from the water to z 40, and a ceiling.
    bd = layout["boundary"]
    n = 24
    for i in range(n):
        a = 2.0 * math.pi * (i + 0.5) / n
        r = bd["radius"]
        chord = 2.0 * (r + 1.0) * math.tan(math.pi / n) + 0.5
        zc = (cascade_layout.WATER_Z + bd["wall_height"]) * 0.5
        b.box(mu(r * math.cos(a), r * math.sin(a), zc), (chord, 1.0, bd["wall_height"] - cascade_layout.WATER_Z), None, "Boundary",
              yaw=90.0 - (math.degrees(a) + 90.0), hidden=True)
    b.box(mu(0, 0, bd["ceiling_z"] + 0.5), (2 * bd["radius"] + 10, 2 * bd["radius"] + 10, 1.0), None, "BoundaryCeiling", hidden=True)
    # Nav: one volume over the whole footprint.
    nav = layout["nav"]
    cx, cy, cz = nav["bounds_center"]
    ex, ey, ez = nav["bounds_extent"]
    vol = actors.spawn_actor_from_class(unreal.NavMeshBoundsVolume, mu(cx, cy, cz))
    ext = max(vol.get_actor_bounds(False)[1].x, 1.0)
    vol.set_actor_scale3d(unreal.Vector(ey * 100.0 / ext, ex * 100.0 / ext, ez * 100.0 / ext))
    levels.save_current_level()
    ns.log("L_Cascade: %d boxes, %d kinds merged, %d crates, %d kit instances (%d maples, %d edge ivy, %d light slits, %d buttresses), %d waterfalls, %d lights, %d + %d spawns, %d enemy markers"
           % (len(layout["boxes"]), merged, crates, instances, maples, ivy, slits, buttresses, len(layout["waterfalls"]), len(layout["lights"]),
              len(layout["spawns"]["blue"]), len(layout["spawns"]["orange"]), len(layout["pve"]["enemies"])))
