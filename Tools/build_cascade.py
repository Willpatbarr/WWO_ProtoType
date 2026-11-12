"""Builds L_Cascade (the 6v6 PvP map, also run by the PvE waves) from Tools/cascade_layout.py's boxes.

UNTESTED: written without running Unreal. The first build is the test (HANDOFF.md, "Cascade").

Not run on its own: Tools/build_maps.py calls build(ns) when WWO_MAPS has "cascade", passing its
own globals (Builder, mu, place, new_level, lighting's constants, solid_material, kit_mesh,
merge_kind, concrete_instance, lamp_instance, fit_mesh, actors, levels, eal, tools, mel, log...),
so this follows build_arboretum() call for call. It needs the Arboretum's helpers in build_maps.py
(concrete_instance, lamp_instance, fit_mesh, merge_kind(kind, group, base, prefix)), and
Tools/create_kit_concrete.py and Tools/import_flora.py to have run.

- Structure: every box kind merged into one mesh in /Game/WWO/Maps/Cascade (collision merged too),
  in the weathered concrete tinted darker and cooler, the balcony a little mossy. Cover (low walls,
  blocks, pillars, monoliths, the hall column) stays separate actors tagged Cover; crates are the
  kit's crates (fit_mesh). Ramps are the stairs' and bridges' collision, hidden in game; the visual
  steps, arches, brackets, trim, ivy, giant arches and mesas have no collision.
- Orange trim: M_WWO_Lamp ("Orange", 6). Lights: the layout's 42 warm point lights, no shadows.
- Plants: actors like the Arboretum (about 150, so no instancing): red maples (the spring tree's
  trunk, crown and petals, the crown recolored red) on the rock islets, green ferns (the tropical
  plants with Recolor 0).
- Waterfalls: translucent sheets (M_WWO_Waterfall, made here on first use: falling streaks from
  UV and time in a Custom node, as M_WWO_Ground does) from top_z to the water, with mist puffs
  (M_WWO_SoftAlpha instances on spheres) where they land and spill.
- Chasm: a dark teal water plane at z -10 (no collision). Falling kills at z -9: a TargetPoint
  tagged KillZ, which UWWOHealthComponent reads (the default is -30 m).
- Looks pass (2026-11-11): the sky light is movable (stationary gave no light without a bake, which
  left every shaded face black) and tinted warm against the blue it captures; the sun is turned 35
  degrees off the axes; the concrete is lighter, the far arches and mesas pale and clean; the water is
  a lit glossy teal plane 6 km across (no visible edge); each waterfall has three soft mist puffs; the
  room lights are brighter and pulled toward white; the spawn pads are muted; the post process adds
  bloom, a vignette, contact shade and a little saturation and contrast.
- Lighting: lighting()'s setup with the sun at -38 degrees, warm white, a bluish height fog, and its
  own sky (M_WWO_Sky_Cascade, made here: M_WWO_Sky's Godot shader with the concept's bright blue
  instead of Godot's gray-blue, so the other maps' sky is untouched).
- Gameplay: a PlayerStart per team spawn (6 + 6), the PvE start first (ChoosePlayerStart takes the
  first), EnemySpawn markers tagged PvEOnly (so PvP respawns don't use them) with their kinds, one
  NavMeshBoundsVolume, invisible boundary walls (r 80 m, to z 40) and a ceiling.
"""
import math
import os
import random
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cascade_layout  # noqa: E402

CAS_MERGED = "/Game/WWO/Maps/Cascade"
CAS_COVER = ("low_cover", "block", "pillar", "monolith", "column")         # own actors, tagged Cover
CAS_SEPARATE = CAS_COVER + ("ramp",)                                       # not merged (ramps stay hidden collision)
CAS_NO_COLLISION = cascade_layout.NO_COLLISION
CAS_NO_SHADOW = ("trim", "ivy", "ramp", "giant_arch", "mesa", "tunnel_roof", "gallery_roof")
FLORA = "/Game/WWO/Flora/Meshes/SM_"
FERNS = ["Tropical_Plant_3", "Tropical_Plant_2_1", "Tropical_Plant_6", "Tropical_Plant_1_1", "Tropical_Plant_9", "Tropical_Plant_2_2"]
SUN_PITCH = -38.0
SUN_YAW = 35.0            # off the map's axes, so walls and arches throw slanted shadows
SKY_LIGHT = 0.8           # the sky's fill: with no bounce light, this is all that reaches the shade
SUN_COLOR = (1.0, 0.93, 0.82)
FOG_COLOR = (0.62, 0.74, 0.86)
LIGHT_CANDELAS = 200.0
MIST_COLOR = (0.86, 0.93, 1.0)
WATER_COLOR = (0.008, 0.045, 0.06)   # linear: deep teal (the sky's sheen on it brightens it a lot)
KILL_Z = -9.0
WATERFALL_COLOR = (0.55, 0.78, 0.86)

WATERFALL_CODE = """
// Falling streaks down a translucent sheet: V runs down the sheet, the streaks move with T.
float s = frac(UV.y * 5.0 - T * Speed + sin(UV.x * 31.0) * 0.12);
float streak = 0.65 + 0.35 * smoothstep(0.0, 0.35, s) * (1.0 - smoothstep(0.35, 1.0, s));
float edge = smoothstep(0.0, 0.06, UV.x) * smoothstep(0.0, 0.06, 1.0 - UV.x);
return float4(C.rgb * Glow * streak, Opacity * edge * (0.75 + 0.25 * streak));
"""


def waterfall_material(ns):
    """M_WWO_Waterfall (unlit, translucent, two-sided) and its instance, made on first use."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    path = "/Game/WWO/Materials/M_WWO_Waterfall"
    parent = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if not parent:
        m = tools.create_asset("M_WWO_Waterfall", "/Game/WWO/Materials", unreal.Material, unreal.MaterialFactoryNew())
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        m.set_editor_property("two_sided", True)
        uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, -200)
        time = mel.create_material_expression(m, unreal.MaterialExpressionTime, -900, -100)
        params = []
        for i, (name, default) in enumerate((("Glow", 1.6), ("Opacity", 0.6), ("Speed", 0.9))):
            e = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 100 + 100 * i)
            e.set_editor_property("parameter_name", name)
            e.set_editor_property("default_value", default)
            params.append((name, e))
        color = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, 0)
        color.set_editor_property("parameter_name", "Color")
        color.set_editor_property("default_value", unreal.LinearColor(0.78, 0.9, 1.0, 1.0))
        c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -550, 0)
        c.set_editor_property("code", WATERFALL_CODE)
        c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
        c.set_editor_property("description", "Waterfall")
        inputs = [("UV", uv, ""), ("T", time, ""), ("C", color, "RGBA")] + [(n, e, "") for n, e in params]
        custom_inputs = []
        for n, _, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            custom_inputs.append(ci)
        c.set_editor_property("inputs", custom_inputs)
        for n, src, out in inputs:
            mel.connect_material_expressions(src, out, c, n)
        for (r, g, b, a), prop, y in (((True, True, True, False), unreal.MaterialProperty.MP_EMISSIVE_COLOR, 0),
                                      ((False, False, False, True), unreal.MaterialProperty.MP_OPACITY, 150)):
            k = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, -250, y)
            for ch, on in zip("rgba", (r, g, b, a)):
                k.set_editor_property(ch, on)
            mel.connect_material_expressions(c, "", k, "")
            mel.connect_material_property(k, "", prop)
        mel.recompile_material(m)
        eal.save_loaded_asset(m)
        parent = m
    ipath = "/Game/WWO/Materials/Map/MI_Waterfall_Cascade"
    mi = eal.load_asset(ipath) if eal.does_asset_exist(ipath) else tools.create_asset(
        "MI_Waterfall_Cascade", "/Game/WWO/Materials/Map", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    # Pale blue-green water, dimmer and thinner than the parent's defaults (those read as white panels).
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*WATERFALL_COLOR, 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Glow", 0.9)
    mel.set_material_instance_scalar_parameter_value(mi, "Opacity", 0.45)
    eal.save_loaded_asset(mi)
    return mi


# M_WWO_Sky's shader (Tools/create_assets.py SKY_CODE) with the concept's clear blue sky (sRGB).
SKY_TOP, SKY_HORIZON = (0.26, 0.52, 0.86), (0.70, 0.83, 0.95)
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
if (sun < sun_max) {
    sky = lerp(float3(1, 1, 1), sky, saturate(1.0 - pow(saturate(1.0 - sun / sun_max), 1.0 / 0.15)));
}
c = (v - PI_2) / PI_2;
float3 ground = lerp(horizon, bottom, saturate(1.0 - pow(saturate(1.0 - c), 1.0 / 0.02)));
return (d.z >= 0.0 ? sky : ground) * Energy;
""" % (SKY_TOP + SKY_HORIZON)


def sky_material(ns):
    """M_WWO_Sky_Cascade: M_WWO_Sky with Cascade's colors (remade every build, so color changes apply)."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    path = "/Game/WWO/Materials/M_WWO_Sky_Cascade"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset("M_WWO_Sky_Cascade", "/Game/WWO/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("is_sky", True)
    view = mel.create_material_expression(m, unreal.MaterialExpressionCameraVectorWS, -700, -100)
    sun = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -700, 0)
    sun.set_editor_property("parameter_name", "SunDir")
    sun.set_editor_property("default_value", unreal.LinearColor(-0.5, 0.0, 0.866, 0.0))
    energy = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -700, 150)
    energy.set_editor_property("parameter_name", "Energy")
    energy.set_editor_property("default_value", 1.0)
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -400, 0)
    c.set_editor_property("code", SKY_CODE)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    c.set_editor_property("description", "CascadeSky")
    ins = []
    for n in ("V", "SunDir", "Energy"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    mel.connect_material_expressions(view, "", c, "V")
    mel.connect_material_expressions(sun, "RGB", c, "SunDir")
    mel.connect_material_expressions(energy, "", c, "Energy")
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


def soft_instance(ns, name, color, alpha):
    """A soft-edged see-through ball (M_WWO_SoftAlpha, the effects' material) as a fixed instance: mist."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    path = "/Game/WWO/Materials/Map/" + name
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        name, "/Game/WWO/Materials/Map", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", unreal.load_asset("/Game/WWO/Materials/M_WWO_SoftAlpha"))
    mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(color[0], color[1], color[2], 1.0))
    for k, v in (("Alpha", alpha), ("Edge", 1.0), ("Rim", 0.0), ("Core", 0.0)):
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    eal.save_loaded_asset(mi)
    return mi


def water_material(ns):
    """M_WWO_CascadeWater: lit, dark teal and glossy, so the sky and the sun's glare sit on it. Remade each build."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    path = "/Game/WWO/Materials/M_WWO_CascadeWater"
    m = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "M_WWO_CascadeWater", "/Game/WWO/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(m)
    color = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -400, 0)
    color.set_editor_property("constant", unreal.LinearColor(WATER_COLOR[0], WATER_COLOR[1], WATER_COLOR[2], 1.0))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for value, prop, y in ((0.4, unreal.MaterialProperty.MP_ROUGHNESS, 150), (0.4, unreal.MaterialProperty.MP_SPECULAR, 300)):   # a sheen, not a mirror (sharp screen reflections broke into blocks)
        k = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -400, y)
        k.set_editor_property("r", value)
        mel.connect_material_property(k, "", prop)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


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
    fc.set_editor_property("fog_density", 0.006)
    fc.set_editor_property("fog_height_falloff", 0.2)
    fc.set_editor_property("start_distance", 2000.0)
    fc.set_editor_property("fog_max_opacity", 0.75)
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


def flora_instance(ns, name, parent_path, **scalars):
    """A copy of one of import_flora.py's instances with some scalars changed (Recolor)."""
    eal, tools, mel = ns.eal, ns.tools, ns.mel
    folder = "/Game/WWO/Flora/Materials"
    path = folder + "/" + name
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", unreal.load_asset(parent_path))
    for k, v in scalars.items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    eal.save_loaded_asset(mi)
    return mi


def static_actor(ns, mesh, at, yaw, scale, label, material=None):
    a = ns.actors.spawn_actor_from_class(unreal.StaticMeshActor, at, unreal.Rotator(0.0, 0.0, yaw))
    a.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
    a.static_mesh_component.set_static_mesh(mesh)
    if material:
        a.static_mesh_component.set_material(0, material)
    a.set_actor_scale3d(unreal.Vector(1, 1, 1) * scale)
    a.set_actor_label(label)
    return a


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
    ci = ns.concrete_instance
    # Lighter than the first build (it read as near-black in shade): warm pale floors, cooler walls, darker
    # stone for the piers, and the far arches and mesas pale and clean so they sit back in the haze.
    floor = ci("Cas_Floor", (0.47, 0.46, 0.44), KitContrast=0.0, DirtStrength=0.75)
    moss = ci("Cas_Moss", (0.33, 0.42, 0.28), KitContrast=0.0, DirtStrength=1.0)
    wall = ci("Cas_Wall", (0.40, 0.42, 0.46))
    roof = ci("Cas_Roof", (0.36, 0.39, 0.38), KitContrast=0.0, Grime=0.6)
    stone = ci("Cas_Stone", (0.33, 0.34, 0.36), KitContrast=0.0, DirtStrength=0.8)
    far = ci("Cas_Far", (0.50, 0.53, 0.58), KitContrast=0.0, DirtStrength=0.2)
    cover = ci("Cas_Cover", (0.52, 0.50, 0.46), KitContrast=0.0, BaseStrength=0.3, BaseHeight=30.0, StreakStrength=0.6)
    dark = ns.solid_material("CasRail", (0.08, 0.08, 0.09))
    mats = {"floor": floor, "floor_z3": floor, "floor_z5": floor, "dais": floor, "landing": floor, "bridge": floor, "plinth": floor,
            "steps": floor, "balcony": moss, "hall_wall": wall, "body": wall, "wall": wall, "tunnel_wall": wall, "gallery_wall": wall,
            "roof": roof, "tunnel_roof": roof, "gallery_roof": roof, "pier": stone, "islet": stone, "arch": stone, "bracket": stone,
            "giant_arch": far, "mesa": far, "low_cover": cover, "block": cover, "pillar": cover, "monolith": stone, "column": stone,
            "rail": dark, "trim": ns.lamp_instance("CasTrim", (1.0, 0.42, 0.1), 4.5), "ivy": ns.solid_material("CasIvy", (0.1, 0.2, 0.07)),
            "ramp": None}
    crate_meshes = [ns.kit_mesh("Prop_Crate3"), ns.kit_mesh("Prop_Crate4")]
    groups = {}
    crates = 0
    for box in layout["boxes"]:
        if box.kind == "crate":
            ns.fit_mesh(crate_meshes[crates % 2], box.x, box.y, box.z - box.height * 0.5, (box.length, box.thickness, box.height),
                        box.yaw, "Cas_crate_%d" % crates, ("Cover",))
            crates += 1
            continue
        a = b.box(mu(box.x, box.y, box.z), (box.length, box.thickness, box.height), mats[box.kind], "Cas_" + box.kind,
                  ("Cover",) if box.kind in CAS_COVER else (), yaw=90.0 - box.yaw, pitch=box.pitch,
                  hidden=box.kind == "ramp", mesh=ns.CYLINDER if box.shape == "cylinder" else None)
        if box.kind in CAS_NO_SHADOW:
            a.static_mesh_component.set_editor_property("cast_shadow", False)
        if box.kind in CAS_NO_COLLISION:
            a.static_mesh_component.set_collision_profile_name("NoCollision")
            a.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)
        if box.kind not in CAS_SEPARATE:
            groups.setdefault(box.kind, []).append(a)
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
                if kind in CAS_NO_COLLISION:
                    # The merged mesh carries the pieces' simple collision; these kinds are looks only.
                    m.static_mesh_component.set_collision_profile_name("NoCollision")
                    m.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)
        except Exception as e:  # keep the separate pieces if merging isn't available
            ns.log("cascade: couldn't merge %s (%s); keeping %d pieces" % (kind, e, len(group)))
    if merged:
        eal.save_directory(CAS_MERGED, only_if_is_dirty=False, recursive=True)

    # Plants: red maples on the islets, green ferns by the blocks and monoliths.
    crown = flora_instance(ns, "MI_Flora_Cascade_MapleCrown", "/Game/WWO/Flora/Materials/MI_Flora_SpringTree_Crown", Recolor=1.0)
    fern = flora_instance(ns, "MI_Flora_Cascade_Fern", "/Game/WWO/Flora/Materials/MI_Flora_Tropical", Recolor=0.0)
    maples = ferns = 0
    for i, (kind, x, y, z) in enumerate(layout["plants"]):
        if kind == "maple":
            yaw = rng.uniform(0.0, 360.0)
            scale = rng.uniform(1.0, 1.3)
            for part in ("Trunk", "Crown", "Petals"):
                static_actor(ns, unreal.load_asset(FLORA + "SpringTree_" + part), mu(x, y, z - 0.05), yaw, scale, "Cas_maple%d_%s" % (maples, part),
                             crown if part != "Trunk" else None)
            maples += 1
        else:
            static_actor(ns, unreal.load_asset(FLORA + rng.choice(FERNS)), mu(x, y, z - 0.05), rng.uniform(0.0, 360.0), rng.uniform(0.7, 1.1),
                         "Cas_fern_%d" % ferns, fern)
            ferns += 1

    # Waterfalls: a translucent sheet from top_z down to the water, facing out of the map.
    falls = waterfall_material(ns)
    mist = soft_instance(ns, "MI_Cascade_Mist", MIST_COLOR, 0.30)
    mist_thin = soft_instance(ns, "MI_Cascade_MistThin", MIST_COLOR, 0.16)
    for i, w in enumerate(layout["waterfalls"]):
        x, y = w["at"]
        z0, z1 = cascade_layout.WATER_Z, w["top_z"]
        a = b.box(mu(x, y, (z0 + z1) * 0.5), (0.05, w["width"], z1 - z0), falls, "Cas_waterfall", yaw=90.0 - w["facing_deg"])
        a.static_mesh_component.set_collision_profile_name("NoCollision")
        a.static_mesh_component.set_editor_property("cast_shadow", False)
        a.static_mesh_component.set_editor_property("can_ever_affect_navigation", False)
        # Mist: soft pale puffs where it lands (wider than the sheet) and a thin one where it spills over.
        for j, (dz, across, tall, alpha_mat) in enumerate(((1.2, 1.9, 4.5, mist), (3.2, 1.3, 5.5, mist_thin), (z1 - z0 - 0.4, 1.15, 1.6, mist_thin))):
            puff = actors.spawn_actor_from_class(unreal.StaticMeshActor, mu(x, y, z0 + dz), unreal.Rotator(0.0, 0.0, 90.0 - w["facing_deg"]))
            pc = puff.static_mesh_component
            pc.set_mobility(unreal.ComponentMobility.STATIC)
            pc.set_static_mesh(ns.SPHERE)
            pc.set_material(0, alpha_mat)
            pc.set_collision_profile_name("NoCollision")
            pc.set_editor_property("cast_shadow", False)
            pc.set_editor_property("can_ever_affect_navigation", False)
            puff.set_actor_scale3d(unreal.Vector(w["width"] * across * 0.6, w["width"] * across, tall))
            puff.set_actor_label("Cas_mist_%d_%d" % (i, j))

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
    ns.log("L_Cascade: %d boxes, %d kinds merged, %d crates, %d maples, %d ferns, %d waterfalls, %d lights, %d + %d spawns, %d enemy markers"
           % (len(layout["boxes"]), merged, crates, maples, ferns, len(layout["waterfalls"]), len(layout["lights"]),
              len(layout["spawns"]["blue"]), len(layout["spawns"]["orange"]), len(layout["pve"]["enemies"])))
