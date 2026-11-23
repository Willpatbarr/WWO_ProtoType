"""Imports Cascade's art kit (rocks, ruin architecture, foliage, water pieces) and makes its materials.

Source: ~/Documents/WWO_Cascade/export (WWO_CASCADE_SRC), made in Blender by ~/Documents/WWO_Cascade/tools
(make_rock_kit.py, make_arch_kit.py, make_foliage_kit.py; the brief they follow is ~/Documents/WWO_Cascade/SPEC.md):
SM_Cas_<Name>.fbx with SM_Cas_<Name>_LOD1.fbx / _LOD2.fbx beside it, textures/T_Cas_<Set>_D/N/ORM.png.

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/import_cascade_kit.py -unattended -nullrhi
WWO_CAS_ONLY=materials (or textures, meshes; comma separated) redoes just that part.

Everything lands in /Game/WWO/Maps/CascadeKit:
- Textures: _D color (alpha = cutout), _N normal (green flipped if the manifest says OpenGL), _ORM and
  T_Cas_Noise as masks (linear).
- Meshes: one static mesh per piece with its LODs (LOD_SCREEN), no collision, Nanite off. The pieces are
  dressing only; the map's gameplay collision stays with the layout's boxes.
- Materials, all built for the mobile renderer (few samples, no screen-space tricks):
  - M_WWO_CasStone: hewn stone projected in world space (one sample per texture: floors from above, walls
    along the wall), so the map's merged boxes and the scaled kit blocks line up whatever their UVs. Blotchy
    variation and wall streaks from T_Cas_Noise, moss on up-facing surfaces and in the joints, vertex color
    R as baked contact shade, G as the moss mask.
  - M_WWO_CasRock: the same idea for the rock meshes, on their own UVs (4 m per repeat).
  - M_WWO_CasFoliage: alpha-cut leaf cards from the leaf atlas, two-sided foliage, wind by vertex color B,
    a little brightness difference per instance.
  - M_WWO_CasBark, M_WWO_CasCloth (banners), M_WWO_CasFalls (waterfall sheets: scrolling streaks and foam),
    M_WWO_CasMist, M_WWO_CasFoam (see-through, unlit).
  - Instances MI_Cas_<slot> for every slot name the kit uses (SLOTS); meshes get theirs by slot name.
Safe to rerun: assets are rebuilt in place (materials keep their object, so the map's references hold).
Writes Tools/import_cascade_kit.log (each mesh's LOD triangles and bounds).
"""
import json
import os

import unreal

SRC = os.path.expanduser(os.environ.get("WWO_CASCADE_SRC", "~/Documents/WWO_Cascade/export"))
ROOT = "/Game/WWO/Maps/CascadeKit"
TEX, MESHES, MATS = ROOT + "/Textures", ROOT + "/Meshes", ROOT + "/Materials"
ONLY = [s for s in os.environ.get("WWO_CAS_ONLY", "").split(",") if s]
LOD_SCREEN = {"default": [1.0, 0.40, 0.12], "foliage": [1.0, 0.30, 0.09]}
FOLIAGE = ("Maple", "Shrub", "Fern", "Ivy", "Vine", "Moss", "Roots")

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
ST = unreal.MaterialSamplerType
REPORT = []


def log(msg):
    REPORT.append(msg)
    unreal.log_warning("WWO cascade kit: " + msg)


def want(part):
    return not ONLY or part in ONLY


# --- Textures -------------------------------------------------------------------------------------

def opengl_normals():
    """Texture sets whose normal maps are OpenGL style (green up), from the kits' manifests."""
    out = set()
    for f in sorted(os.listdir(SRC)):
        if not (f.startswith("manifest_") and f.endswith(".json")):
            continue
        try:
            text = open(os.path.join(SRC, f)).read()
        except OSError:
            continue
        low = text.lower()
        if "opengl" in low and "directx" not in low:
            out.add(f)
    return out


def import_textures():
    folder = os.path.join(SRC, "textures")
    flip = bool(opengl_normals())
    count = 0
    for f in sorted(os.listdir(folder)):
        if not (f.startswith("T_Cas_") and f.endswith(".png")):
            continue
        name = os.path.splitext(f)[0]
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(folder, f))
        task.set_editor_property("destination_path", TEX)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tools.import_asset_tasks([task])
        tex = eal.load_asset(TEX + "/" + name)
        if not tex:
            log("FAILED texture %s" % f)
            continue
        if name.endswith("_N"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("flip_green_channel", flip)
        elif name.endswith("_ORM") or name.endswith("_Noise"):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        else:
            tex.set_editor_property("srgb", True)
        if "Leaves" in name:   # cutouts keep their coverage in the small mips, so far trees don't thin out
            for k, v in (("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SHARPEN4),):
                try:
                    tex.set_editor_property(k, v)
                except Exception as e:  # noqa: BLE001
                    log("%s %s: %s" % (name, k, e))
        eal.save_loaded_asset(tex)
        count += 1
    log("%d textures (normals %s)" % (count, "flipped from OpenGL" if flip else "as given"))


def tex(name, fallback="/Engine/EngineResources/WhiteSquareTexture"):
    path = TEX + "/" + name
    if eal.does_asset_exist(path):
        return eal.load_asset(path)
    log("missing texture %s: using a stand-in" % name)
    return unreal.load_asset("/Engine/EngineMaterials/DefaultNormal" if name.endswith("_N") else fallback)


# --- Material graph helpers -----------------------------------------------------------------------

def link(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        log("could not connect %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def new(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(m, name, value, x=-1600, y=0):
    return new(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def color(m, name, rgb, x=-1600, y=0):
    return new(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name,
               default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))


def sample(m, name, texture, sampler, uv=None, x=-900, y=0):
    e = new(m, unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=texture, sampler_type=sampler)
    if uv is not None:
        link(uv[0], uv[1], e, "UVs")
    return e


def custom(m, desc, code, out, inputs, x=-500, y=0):
    """A Custom node. inputs: (name, expression, output name). out: 1 to 4 floats."""
    types = {1: unreal.CustomMaterialOutputType.CMOT_FLOAT1, 2: unreal.CustomMaterialOutputType.CMOT_FLOAT2,
             3: unreal.CustomMaterialOutputType.CMOT_FLOAT3, 4: unreal.CustomMaterialOutputType.CMOT_FLOAT4}
    c = new(m, unreal.MaterialExpressionCustom, x, y, code=code, output_type=types[out], description=desc)
    ins = []
    for n, _, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    for n, src, src_out in inputs:
        link(src, src_out, c, n)
    return c


def mask(m, src, src_out, channels, x=-200, y=0):
    e = new(m, unreal.MaterialExpressionComponentMask, x, y, r="r" in channels, g="g" in channels, b="b" in channels, a="a" in channels)
    link(src, src_out, e, "")
    return e


def material(name, **props):
    """The material, emptied (kept as an object so instances and maps still point at it)."""
    path = "/Game/WWO/Materials/" + name
    m = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(name, "/Game/WWO/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(m)
    for k, v in props.items():
        m.set_editor_property(k, v)
    m.set_editor_property("used_with_instanced_static_meshes", True)   # the prop fields; a -game run can't add the flag itself
    return m


def finish(m):
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def instance(name, parent, scalars=None, colors=None, textures=None):
    path = MATS + "/" + name
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        name, MATS, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    mel.clear_all_material_instance_parameters(mi)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (colors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(mi, k, v)
    eal.save_loaded_asset(mi)
    return mi


WORLD_UV = """
// Floors and ceilings are mapped from above; walls along the wall and down it, whatever way they face.
float3 n = normalize(VN);
float2 uv;
if (abs(n.z) > 0.7) { uv = P.xy; }
else { float2 t = normalize(float2(-n.y, n.x)); uv = float2(dot(P.xy, t), -P.z); }
return uv / Size;
"""
WORLD_NORMAL = """
// The normal map's bump turned from that projection into world space.
float3 n = normalize(VN);
float3 T; float3 B;
if (abs(n.z) > 0.7) { T = float3(1, 0, 0); B = float3(0, 1, 0); }
else { T = normalize(float3(-n.y, n.x, 0)); B = float3(0, 0, -1); }
return normalize(T * TN.x * Bump + B * TN.y * Bump + n * max(TN.z, 0.05));
"""
STONE_COLOR = """
float cav = saturate((0.45 - ORM.b) * 3.0) * Joints;   // Joints 0: the texture's blue channel isn't a height (bronze: a metal mask)
float3 c = lerp(D, Mortar, cav * 0.75) * Tint;      // the joints as worn mortar, not black lines
c *= lerp(1.0 - Variation, 1.0 + Variation * 0.6, Nz.r);
float wall = 1.0 - saturate(abs(VN.z) * 3.0);
c *= 1.0 - Streaks * wall * Nz.b * 0.6;
float up = saturate((WN.z - MossSlope) * 4.0);
float patch = saturate((Nz.a - 0.52) * 3.5);
// Moss in patches on what faces up, thicker in the joints there, and a trace in the walls' joints.
float m = saturate(up * patch * 0.85 + up * cav * (0.35 + Nz.r) * 0.8 + cav * patch * 0.5) * (0.3 + 0.7 * VC.g);
m = saturate(m * MossAmount);
c = lerp(c, Moss * MossTint, m);
c *= lerp(1.0, VC.r, Shade) * lerp(1.0, ORM.r, 0.3);
return float4(c, m);
"""
ROCK_COLOR = """
float3 c = D * Tint;
c *= lerp(1.0 - Variation, 1.0 + Variation * 0.6, Nz.r);
float up = saturate((WN.z - MossSlope) * 3.0);
float m = saturate((up * (0.3 + Nz.a * 1.5) * (0.35 + 0.65 * VC.g) + VC.g * Nz.r * 0.7 - 0.3) * 2.5);
m = saturate(m * MossAmount);
c = lerp(c, Moss * MossTint, m);
c *= lerp(1.0, VC.r, Shade) * lerp(1.0, ORM.r, 0.6);
return float4(c, m);
"""
WIND = """
float ph = P.x * 0.011 + P.y * 0.013;
float s = sin(T * Speed + ph) + 0.5 * sin(T * Speed * 2.3 + ph * 1.7);
return float3(s, s * 0.6, sin(T * Speed * 1.7 + ph) * 0.3) * Amp * W;
"""
FALLS_UV = """
// Along the sheet and down it in world space (so a scaled sheet keeps its streaks' size), scrolling down.
float3 n = normalize(VN);
float2 t = abs(n.z) > 0.95 ? float2(1, 0) : normalize(float2(-n.y, n.x));
float a = dot(P.xy, t) / Size;
float v = -P.z / Size;
return float4(a * 2.0, v * 0.35 - T * Speed, a * 3.1 + 0.37, v * 0.8 - T * Speed * 1.7);
"""
FALLS_COLOR = """
float foam = saturate((A.b * 0.7 + B.a * 0.7 - Thresh) * 3.0);
float edge = smoothstep(0.0, 0.12, UV.x) * smoothstep(0.0, 0.12, 1.0 - UV.x);
float3 c = lerp(Deep, Foam, foam) * Glow;
return float4(c, saturate(Opacity * (0.5 + 0.6 * foam)) * edge);
"""


def lit_outputs(m, col, rough_base, moss_rough=0.95):
    """Base color from col.rgb, roughness eased to moss_rough where col.a (the moss) is."""
    mel.connect_material_property(mask(m, col, "", "rgb", -150, -200), "", MP.MP_BASE_COLOR)
    rough = new(m, unreal.MaterialExpressionLinearInterpolate, -150, 150)
    link(rough_base[0], rough_base[1], rough, "A")
    link(new(m, unreal.MaterialExpressionConstant, -350, 250, r=moss_rough), "", rough, "B")
    link(mask(m, col, "", "a", -350, 180), "", rough, "Alpha")
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, "Specular", 0.25, -350, 400), "", MP.MP_SPECULAR)


def make_stone():
    m = material("M_WWO_CasStone", two_sided=False, tangent_space_normal=False)
    p = new(m, unreal.MaterialExpressionWorldPosition, -1600, -300)
    vn = new(m, unreal.MaterialExpressionVertexNormalWS, -1600, -200)
    vc = new(m, unreal.MaterialExpressionVertexColor, -1600, -100)
    uv = custom(m, "WorldUV", WORLD_UV, 2, [("P", p, ""), ("VN", vn, ""), ("Size", scalar(m, "StoneSize", 400.0, -1600, 0), "")], -1300, -300)
    d = sample(m, "StoneColor", tex("T_Cas_Stone_D"), ST.SAMPLERTYPE_COLOR, (uv, ""), -1000, -500)
    n = sample(m, "StoneNormal", tex("T_Cas_Stone_N"), ST.SAMPLERTYPE_NORMAL, (uv, ""), -1000, -250)
    orm = sample(m, "StoneORM", tex("T_Cas_Stone_ORM"), ST.SAMPLERTYPE_MASKS, (uv, ""), -1000, 0)
    nuv = new(m, unreal.MaterialExpressionMultiply, -1150, 250)
    link(uv, "", nuv, "A")
    link(scalar(m, "NoiseScale", 0.137, -1300, 320), "", nuv, "B")
    nz = sample(m, "Noise", tex("T_Cas_Noise"), ST.SAMPLERTYPE_MASKS, (nuv, ""), -1000, 250)
    muv = new(m, unreal.MaterialExpressionMultiply, -1150, 500)
    link(uv, "", muv, "A")
    link(scalar(m, "MossScale", 2.0, -1300, 570), "", muv, "B")
    moss = sample(m, "MossColor", tex("T_Cas_Moss_D"), ST.SAMPLERTYPE_COLOR, (muv, ""), -1000, 500)
    wn = custom(m, "WorldNormal", WORLD_NORMAL, 3, [("VN", vn, ""), ("TN", n, "RGB"), ("Bump", scalar(m, "Bump", 0.65, -900, -100), "")], -650, -250)
    mel.connect_material_property(wn, "", MP.MP_NORMAL)
    col = custom(m, "StoneColor", STONE_COLOR, 4, [
        ("D", d, "RGB"), ("Tint", color(m, "Tint", (1.0, 1.0, 1.0), -900, 700), ""), ("Nz", nz, "RGBA"), ("ORM", orm, "RGB"),
        ("Mortar", color(m, "Mortar", (0.12, 0.115, 0.105), -900, 780), ""),
        ("Moss", moss, "RGB"), ("MossTint", color(m, "MossTint", (1.0, 1.0, 1.0), -900, 850), ""), ("WN", wn, ""), ("VN", vn, ""), ("VC", vc, ""),
        ("Variation", scalar(m, "Variation", 0.35, -900, 1000), ""), ("Streaks", scalar(m, "Streaks", 0.6, -900, 1080), ""),
        ("MossSlope", scalar(m, "MossSlope", 0.55, -900, 1160), ""), ("MossAmount", scalar(m, "MossAmount", 1.0, -900, 1240), ""),
        ("Shade", scalar(m, "Shade", 0.8, -900, 1320), ""), ("Joints", scalar(m, "Joints", 1.0, -900, 1400), "")], -450, 300)
    lit_outputs(m, col, (orm, "G"))
    ao = new(m, unreal.MaterialExpressionMultiply, -150, 500)
    link(orm, "R", ao, "A")
    link(vc, "R", ao, "B")
    mel.connect_material_property(ao, "", MP.MP_AMBIENT_OCCLUSION)
    metal = new(m, unreal.MaterialExpressionMultiply, -150, 650)     # bronze: the ORM's blue channel is its metal mask
    link(orm, "B", metal, "A")
    link(scalar(m, "Metal", 0.0, -350, 700), "", metal, "B")
    mel.connect_material_property(metal, "", MP.MP_METALLIC)
    return finish(m)


def make_decal():
    """Flat floor cards (fallen leaves, cracks, stains): alpha-cut, lit, a hair above the floor they lie on."""
    m = material("M_WWO_CasDecal", two_sided=False, blend_mode=unreal.BlendMode.BLEND_MASKED, opacity_mask_clip_value=0.4)
    d = sample(m, "DecalColor", tex("T_Cas_Decals_D"), ST.SAMPLERTYPE_COLOR, None, -1000, -400)
    rnd = new(m, unreal.MaterialExpressionPerInstanceRandom, -1600, 100)
    col = custom(m, "DecalColor", "return D * Tint * lerp(0.75, 1.1, R);", 3, [("D", d, "RGB"), ("Tint", color(m, "Tint", (1.0, 1.0, 1.0), -1300, 200), ""), ("R", rnd, "")], -550, -300)
    mel.connect_material_property(col, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(d, "A", MP.MP_OPACITY_MASK)
    mel.connect_material_property(scalar(m, "Roughness", 0.85, -550, 250), "", MP.MP_ROUGHNESS)
    return finish(m)


def make_rock():
    m = material("M_WWO_CasRock", two_sided=False, tangent_space_normal=True)
    p = new(m, unreal.MaterialExpressionWorldPosition, -1600, 300)
    vc = new(m, unreal.MaterialExpressionVertexColor, -1600, -100)
    uv0 = new(m, unreal.MaterialExpressionTextureCoordinate, -1600, -400)
    uv = new(m, unreal.MaterialExpressionMultiply, -1350, -400)
    link(uv0, "", uv, "A")
    link(scalar(m, "UVScale", 1.0, -1600, -300), "", uv, "B")
    d = sample(m, "RockColor", tex("T_Cas_Rock_D"), ST.SAMPLERTYPE_COLOR, (uv, ""), -1000, -500)
    n = sample(m, "RockNormal", tex("T_Cas_Rock_N"), ST.SAMPLERTYPE_NORMAL, (uv, ""), -1000, -250)
    orm = sample(m, "RockORM", tex("T_Cas_Rock_ORM"), ST.SAMPLERTYPE_MASKS, (uv, ""), -1000, 0)
    nuv = custom(m, "NoiseUV", "return (P.xy + P.z * 0.37) / Size;", 2, [("P", p, ""), ("Size", scalar(m, "NoiseSize", 2600.0, -1600, 400), "")], -1300, 300)
    nz = sample(m, "Noise", tex("T_Cas_Noise"), ST.SAMPLERTYPE_MASKS, (nuv, ""), -1000, 250)
    muv = new(m, unreal.MaterialExpressionMultiply, -1150, 500)
    link(uv, "", muv, "A")
    link(scalar(m, "MossScale", 2.0, -1300, 570), "", muv, "B")
    moss = sample(m, "MossColor", tex("T_Cas_Moss_D"), ST.SAMPLERTYPE_COLOR, (muv, ""), -1000, 500)
    mel.connect_material_property(n, "RGB", MP.MP_NORMAL)
    wn = new(m, unreal.MaterialExpressionPixelNormalWS, -900, 620)
    col = custom(m, "RockColor", ROCK_COLOR, 4, [
        ("D", d, "RGB"), ("Tint", color(m, "Tint", (1.0, 1.0, 1.0), -900, 700), ""), ("Nz", nz, "RGBA"), ("ORM", orm, "RGB"),
        ("Moss", moss, "RGB"), ("MossTint", color(m, "MossTint", (1.0, 1.0, 1.0), -900, 850), ""), ("WN", wn, ""), ("VC", vc, ""),
        ("Variation", scalar(m, "Variation", 0.35, -900, 1000), ""), ("MossSlope", scalar(m, "MossSlope", 0.5, -900, 1160), ""),
        ("MossAmount", scalar(m, "MossAmount", 1.0, -900, 1240), ""), ("Shade", scalar(m, "Shade", 0.85, -900, 1320), "")], -450, 300)
    lit_outputs(m, col, (orm, "G"))
    ao = new(m, unreal.MaterialExpressionMultiply, -150, 500)
    link(orm, "R", ao, "A")
    link(vc, "R", ao, "B")
    mel.connect_material_property(ao, "", MP.MP_AMBIENT_OCCLUSION)
    return finish(m)


def wind(m, amp, speed):
    p = new(m, unreal.MaterialExpressionWorldPosition, -1600, 900)
    t = new(m, unreal.MaterialExpressionTime, -1600, 1000)
    vc = new(m, unreal.MaterialExpressionVertexColor, -1600, 1100)
    w = custom(m, "Wind", WIND, 3, [("P", p, ""), ("T", t, ""), ("W", vc, "B"), ("Amp", scalar(m, "WindAmount", amp, -1600, 1200), ""),
                                    ("Speed", scalar(m, "WindSpeed", speed, -1600, 1280), "")], -900, 950)
    mel.connect_material_property(w, "", MP.MP_WORLD_POSITION_OFFSET)


def make_foliage():
    m = material("M_WWO_CasFoliage", two_sided=True, blend_mode=unreal.BlendMode.BLEND_MASKED,
                 shading_model=unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE, opacity_mask_clip_value=0.33)
    d = sample(m, "LeafColor", tex("T_Cas_Leaves_D"), ST.SAMPLERTYPE_COLOR, None, -1000, -400)
    vc = new(m, unreal.MaterialExpressionVertexColor, -1600, -100)
    rnd = new(m, unreal.MaterialExpressionPerInstanceRandom, -1600, 100)
    col = custom(m, "LeafColor", "return D * Tint * lerp(ShadeMin, 1.0, VC) * lerp(1.0 - Vary, 1.0 + Vary, R);", 3, [
        ("D", d, "RGB"), ("Tint", color(m, "Tint", (1.0, 1.0, 1.0), -1300, 200), ""), ("VC", vc, "R"), ("R", rnd, ""),
        ("ShadeMin", scalar(m, "ShadeMin", 0.65, -1300, 350), ""), ("Vary", scalar(m, "Vary", 0.18, -1300, 430), "")], -550, -300)
    mel.connect_material_property(col, "", MP.MP_BASE_COLOR)
    sub = new(m, unreal.MaterialExpressionMultiply, -300, 0)
    link(col, "", sub, "A")
    link(scalar(m, "Subsurface", 0.6, -550, 80), "", sub, "B")
    mel.connect_material_property(sub, "", MP.MP_SUBSURFACE_COLOR)
    # A little of its own color as light: stands in for the sun coming through the canopy, so the tops of the
    # trees and the shaded side of a curtain don't go black (the mobile renderer has no bounce to lift them).
    back = new(m, unreal.MaterialExpressionMultiply, -300, 450)
    link(col, "", back, "A")
    link(scalar(m, "Backlight", 0.22, -550, 500), "", back, "B")
    mel.connect_material_property(back, "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(d, "A", MP.MP_OPACITY_MASK)
    mel.connect_material_property(scalar(m, "Roughness", 0.75, -550, 250), "", MP.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, "Specular", 0.12, -550, 330), "", MP.MP_SPECULAR)
    wind(m, 6.0, 1.3)
    return finish(m)


def make_bark():
    m = material("M_WWO_CasBark", two_sided=False)
    d = sample(m, "BarkColor", tex("T_Cas_Bark_D"), ST.SAMPLERTYPE_COLOR, None, -1000, -400)
    n = sample(m, "BarkNormal", tex("T_Cas_Bark_N"), ST.SAMPLERTYPE_NORMAL, None, -1000, -150)
    vc = new(m, unreal.MaterialExpressionVertexColor, -1600, -100)
    col = custom(m, "BarkColor", "return D * Tint * lerp(0.5, 1.0, VC);", 3, [("D", d, "RGB"), ("Tint", color(m, "Tint", (1.0, 1.0, 1.0), -1300, 200), ""), ("VC", vc, "R")], -550, -300)
    mel.connect_material_property(col, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(n, "RGB", MP.MP_NORMAL)
    mel.connect_material_property(scalar(m, "Roughness", 0.9, -550, 250), "", MP.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, "Specular", 0.15, -550, 330), "", MP.MP_SPECULAR)
    return finish(m)


def make_cloth():
    m = material("M_WWO_CasCloth", two_sided=True)
    uv = new(m, unreal.MaterialExpressionTextureCoordinate, -1600, -400)
    nz = sample(m, "Noise", tex("T_Cas_Noise"), ST.SAMPLERTYPE_MASKS, (uv, ""), -1000, -400)
    col = custom(m, "ClothColor", "return Tint * lerp(0.55, 1.1, Nz.r) * lerp(0.8, 1.0, Nz.g);", 3,
                 [("Tint", color(m, "Tint", (0.42, 0.035, 0.02), -1300, 200), ""), ("Nz", nz, "RGBA")], -550, -300)
    mel.connect_material_property(col, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(scalar(m, "Roughness", 0.85, -550, 250), "", MP.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, "Specular", 0.1, -550, 330), "", MP.MP_SPECULAR)
    wind(m, 9.0, 1.0)
    return finish(m)


def make_falls():
    m = material("M_WWO_CasFalls", two_sided=True, blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT, shading_model=unreal.MaterialShadingModel.MSM_UNLIT)
    p = new(m, unreal.MaterialExpressionWorldPosition, -1600, -300)
    vn = new(m, unreal.MaterialExpressionVertexNormalWS, -1600, -200)
    t = new(m, unreal.MaterialExpressionTime, -1600, -100)
    uvs = custom(m, "FallsUV", FALLS_UV, 4, [("P", p, ""), ("VN", vn, ""), ("T", t, ""), ("Size", scalar(m, "Size", 400.0, -1600, 0), ""),
                                           ("Speed", scalar(m, "Speed", 0.9, -1600, 80), "")], -1300, -300)
    a = sample(m, "Noise", tex("T_Cas_Noise"), ST.SAMPLERTYPE_MASKS, (mask(m, uvs, "", "rg", -1150, -350), ""), -950, -400)
    b = new(m, unreal.MaterialExpressionTextureSample, -950, -100, texture=tex("T_Cas_Noise"), sampler_type=ST.SAMPLERTYPE_MASKS)
    link(mask(m, uvs, "", "ba", -1150, -100), "", b, "UVs")
    uv = new(m, unreal.MaterialExpressionTextureCoordinate, -1600, 300)
    col = custom(m, "FallsColor", FALLS_COLOR, 4, [
        ("A", a, "RGBA"), ("B", b, "RGBA"), ("UV", uv, ""), ("Deep", color(m, "Deep", (0.30, 0.52, 0.60), -1300, 400), ""),
        ("Foam", color(m, "Foam", (0.92, 0.97, 1.0), -1300, 550), ""), ("Glow", scalar(m, "Glow", 1.0, -1300, 700), ""),
        ("Opacity", scalar(m, "Opacity", 0.62, -1300, 780), ""), ("Thresh", scalar(m, "Thresh", 0.55, -1300, 860), "")], -550, -200)
    mel.connect_material_property(mask(m, col, "", "rgb", -250, -250), "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(mask(m, col, "", "a", -250, -50), "", MP.MP_OPACITY)
    return finish(m)


def make_mist():
    m = material("M_WWO_CasMist", two_sided=True, blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT, shading_model=unreal.MaterialShadingModel.MSM_UNLIT)
    uv = new(m, unreal.MaterialExpressionTextureCoordinate, -1600, -400)
    t = new(m, unreal.MaterialExpressionTime, -1600, -300)
    rnd = new(m, unreal.MaterialExpressionPerInstanceRandom, -1600, -200)
    nuv = custom(m, "MistUV", "return UV * 0.6 + float2(R * 7.0, R * 3.0 - T * Drift);", 2,
                 [("UV", uv, ""), ("T", t, ""), ("R", rnd, ""), ("Drift", scalar(m, "Drift", 0.05, -1600, -100), "")], -1300, -400)
    nz = sample(m, "Noise", tex("T_Cas_Noise"), ST.SAMPLERTYPE_MASKS, (nuv, ""), -1000, -400)
    depth = new(m, unreal.MaterialExpressionPixelDepth, -1600, 100)
    al = custom(m, "MistAlpha", """
float r = length(UV - 0.5) * 2.0;
float soft = saturate(1.0 - r);
float near = saturate((Depth - 150.0) / 700.0);   // thins out as you walk into it
return soft * soft * saturate(Nz * 1.6 - 0.1) * Opacity * near;
""", 1, [("UV", uv, ""), ("Nz", nz, "A"), ("Depth", depth, ""), ("Opacity", scalar(m, "Opacity", 0.9, -1300, 200), "")], -550, -100)
    mel.connect_material_property(color(m, "Color", (0.85, 0.92, 1.0), -550, -400), "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(al, "", MP.MP_OPACITY)
    return finish(m)


def make_foam():
    m = material("M_WWO_CasFoam", two_sided=False, blend_mode=unreal.BlendMode.BLEND_TRANSLUCENT, shading_model=unreal.MaterialShadingModel.MSM_UNLIT)
    uv = new(m, unreal.MaterialExpressionTextureCoordinate, -1600, -400)
    t = new(m, unreal.MaterialExpressionTime, -1600, -300)
    nuv = custom(m, "FoamUV", "return float2(UV.x * 5.0, UV.y * 1.4 - T * Speed);", 2, [("UV", uv, ""), ("T", t, ""), ("Speed", scalar(m, "Speed", 0.12, -1600, -200), "")], -1300, -400)
    nz = sample(m, "Noise", tex("T_Cas_Noise"), ST.SAMPLERTYPE_MASKS, (nuv, ""), -1000, -400)
    vc = new(m, unreal.MaterialExpressionVertexColor, -1600, 0)
    al = custom(m, "FoamAlpha", "float rim = saturate((1.0 - UV.y) * 1.6); return saturate((Nz.a * 0.8 + Nz.g * 0.5 - 0.35) * 2.5) * rim * rim * saturate(VC * 1.5 + 0.4) * Opacity;", 1,
                [("Nz", nz, "RGBA"), ("VC", vc, "R"), ("UV", uv, ""), ("Opacity", scalar(m, "Opacity", 0.8, -1300, 200), "")], -550, -100)
    mel.connect_material_property(color(m, "Color", (0.80, 0.90, 0.92), -550, -400), "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(al, "", MP.MP_OPACITY)
    return finish(m)


GLOW = """
// A light, not paint: hot at full strength (it clips toward yellow-white and blooms), with slow drifting
// variation along the strip so it reads as something burning inside the stone.
float n = Nz.r * 0.6 + Nz.a * 0.4;
float k = lerp(1.0 - Flicker, 1.0 + Flicker, n);
return lerp(Color, Hot, saturate(n * 1.6 - 0.35) * Core) * Glow * k;
"""


def glow_parent():
    """M_WWO_CasGlow: Cascade's light strips (unlit, emissive well above 1 so the bloom makes a halo)."""
    m = material("M_WWO_CasGlow", shading_model=unreal.MaterialShadingModel.MSM_UNLIT)
    p = new(m, unreal.MaterialExpressionWorldPosition, -1600, -300)
    t = new(m, unreal.MaterialExpressionTime, -1600, -200)
    uv = custom(m, "GlowUV", "return float2(P.x + P.y, P.z) / 330.0 + float2(T * 0.03, -T * 0.05);", 2, [("P", p, ""), ("T", t, "")], -1300, -300)
    nz = sample(m, "Noise", tex("T_Cas_Noise"), ST.SAMPLERTYPE_MASKS, (uv, ""), -1000, -300)
    col = custom(m, "Glow", GLOW, 3, [("Nz", nz, "RGBA"), ("Color", color(m, "Color", (1.0, 0.17, 0.015), -900, 0), ""),
                                      ("Hot", color(m, "Hot", (1.0, 0.45, 0.10), -900, 150), ""), ("Glow", scalar(m, "Glow", 7.0, -900, 300), ""),
                                      ("Flicker", scalar(m, "Flicker", 0.25, -900, 380), ""), ("Core", scalar(m, "Core", 0.8, -900, 460), "")], -500, -100)
    mel.connect_material_property(col, "", MP.MP_EMISSIVE_COLOR)
    return finish(m)


# Slot name -> instance. Tints are sRGB-looking linear multipliers over the kit's textures.
def make_materials():
    stone, rock, leaf = make_stone(), make_rock(), make_foliage()
    bark, cloth, falls, mist, foam = make_bark(), make_cloth(), make_falls(), make_mist(), make_foam()
    decal = make_decal()

    def surface(name, tint, **scalars):
        """The world-projected stone material wearing another texture set (T_Cas_<name>_D/N/ORM)."""
        return instance("MI_Cas_" + name, stone, scalars=scalars, colors={"Tint": tint, "MossTint": (0.62, 0.72, 0.5)},
                        textures={"StoneColor": tex("T_Cas_%s_D" % name), "StoneNormal": tex("T_Cas_%s_N" % name), "StoneORM": tex("T_Cas_%s_ORM" % name)})

    out = {
        "Cas_Stone": instance("MI_Cas_Stone", stone, scalars={"StoneSize": 700.0, "MossAmount": 0.9}, colors={"Tint": (1.45, 1.42, 1.4), "MossTint": (0.62, 0.72, 0.5)}),
        "Cas_Rock": instance("MI_Cas_Rock", rock, colors={"Tint": (1.55, 1.45, 1.35), "MossTint": (0.7, 0.8, 0.5)}),
        "Cas_Moss": instance("MI_Cas_Moss", rock, scalars={"MossAmount": 3.0, "MossSlope": 0.0}),
        "Cas_Glow": instance("MI_Cas_Glow", glow_parent(), scalars={"Glow": 7.0}),
        "Cas_Bark": instance("MI_Cas_Bark", bark),
        "Cas_LeafRed": instance("MI_Cas_LeafRed", leaf, colors={"Tint": (1.0, 0.72, 0.66)}, scalars={"Backlight": 0.14}),
        "Cas_LeafGreen": instance("MI_Cas_LeafGreen", leaf, colors={"Tint": (0.55, 0.62, 0.4)}),
        "Cas_Ivy": instance("MI_Cas_Ivy", leaf, scalars={"WindAmount": 3.0, "Backlight": 0.04}, colors={"Tint": (0.22, 0.30, 0.17)}),
        "Cas_Fern": instance("MI_Cas_Fern", leaf, scalars={"WindAmount": 2.0, "Backlight": 0.06}, colors={"Tint": (0.36, 0.46, 0.24)}),
        "Cas_Cloth": instance("MI_Cas_Cloth", cloth),
        "Cas_Slab": surface("Slab", (2.3, 2.3, 2.4), StoneSize=600.0, MossAmount=0.7, Variation=0.2),
        "Cas_Paver": surface("Paver", (1.1, 1.08, 1.02), StoneSize=420.0, MossAmount=0.8, Streaks=0.0),
        "Cas_Bronze": surface("Bronze", (0.85, 0.85, 0.9), StoneSize=300.0, MossAmount=0.0, Joints=0.0, Metal=0.85, Streaks=0.2, Variation=0.15),
        "Cas_Ground": surface("Ground", (1.15, 1.12, 1.05), StoneSize=420.0, MossAmount=0.9, Joints=0.0, Streaks=0.0),
        "Cas_Decal": instance("MI_Cas_Decal", decal),
        "Cas_Water": instance("MI_Cas_Falls", falls),
        "Cas_Mist": instance("MI_Cas_Mist", mist),
        "Cas_Foam": instance("MI_Cas_Foam", foam),
    }
    # The far scenery (spires, the giant arches) sits back in the haze: paler, less moss, no small bump.
    instance("MI_Cas_RockFar", rock, scalars={"MossAmount": 0.6, "Variation": 0.25}, colors={"Tint": (1.7, 1.7, 1.75)})
    instance("MI_Cas_StoneFar", stone, scalars={"MossAmount": 0.7, "StoneSize": 1600.0}, colors={"Tint": (1.7, 1.72, 1.8)})
    log("%d slot materials" % len(out))
    return out


# --- Meshes ---------------------------------------------------------------------------------------

def mesh_options():
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("asset_type_sub_folders", False)
    p.set_editor_property("use_source_name_for_asset", True)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("force_all_mesh_as_type", unreal.InterchangeForceMeshType.IFMT_STATIC_MESH)
    try:
        common.set_editor_property("vertex_color_import_option", unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    except Exception as e:  # noqa: BLE001
        log("vertex colors: %s" % e)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("import_static_meshes", True)
    meshes.set_editor_property("import_skeletal_meshes", False)
    for prop in ("collision", "generate_collision", "import_collision"):
        try:
            meshes.set_editor_property(prop, False)
        except Exception:  # noqa: BLE001
            pass
    try:
        meshes.set_editor_property("combine_static_meshes", True)
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


def import_fbx(file, scratch):
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(SRC, file))
    task.set_editor_property("destination_path", scratch)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", mesh_options())
    tools.import_asset_tasks([task])
    for p in eal.list_assets(scratch, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            return a
    return None


def mesh_tools():
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if sub is None:  # its module is not loaded in a commandlet
        unreal.SystemLibrary.execute_console_command(None, "Module Load StaticMeshEditor")
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    return sub or unreal.EditorStaticMeshLibrary


def import_meshes(slots):
    sub = mesh_tools()
    files = sorted(f for f in os.listdir(SRC) if f.startswith("SM_Cas_") and f.endswith(".fbx"))
    bases = [f for f in files if "_LOD" not in f]
    done = 0
    for f in bases:
        name = os.path.splitext(f)[0]
        mesh = import_fbx(f, MESHES + "/Import_" + name)
        if not mesh:
            log("FAILED: no static mesh came out of %s" % f)
            continue
        dest = MESHES + "/" + name
        if eal.does_asset_exist(dest):
            eal.delete_asset(dest)
        eal.duplicate_asset(mesh.get_path_name(), dest)
        eal.delete_directory(MESHES + "/Import_" + name)
        out = eal.load_asset(dest)
        lods = 1
        for i in (1, 2):
            lf = "%s_LOD%d.fbx" % (name, i)
            if lf not in files:
                break
            lod = import_fbx(lf, MESHES + "/Import_" + name + "_L")
            if not lod:
                log("FAILED LOD %s" % lf)
                break
            got = sub.set_lod_from_static_mesh(out, i, lod, 0, True)
            sections = [str(m.get_editor_property("material_slot_name")) for m in lod.get_editor_property("static_materials")]
            eal.delete_directory(MESHES + "/Import_" + name + "_L")
            if got < 0:
                log("%s: LOD %d not set" % (name, i))
                break
            # The LOD's sections come in pointing at slot 0; point each at the slot of its own name.
            names = [str(m.get_editor_property("material_slot_name")) for m in out.get_editor_property("static_materials")]
            for section, slot in enumerate(sections):
                try:
                    sub.set_lod_material_slot(out, names.index(slot), i, section)
                except Exception as e:  # noqa: BLE001
                    log("%s LOD %d section %d -> %s: %s" % (name, i, section, slot, e))
            lods += 1
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
            if key in slots:
                slot.set_editor_property("material_interface", slots[key])
            else:
                missing.append(key)
        out.set_editor_property("static_materials", mats)
        short = name[len("SM_Cas_"):]
        screens = LOD_SCREEN["foliage" if short.startswith(FOLIAGE) else "default"][:lods]
        try:
            sub.set_lod_screen_sizes(out, screens)
        except Exception as e:  # noqa: BLE001
            log("%s: screen sizes not set (%s)" % (name, e))
        try:
            out.set_editor_property("generate_mesh_distance_field", False)
        except Exception:  # noqa: BLE001
            pass
        eal.save_loaded_asset(out)
        b = out.get_bounding_box()
        log("%s: LODs %d, triangles %s, slots %s%s, min (%.0f, %.0f, %.0f) max (%.0f, %.0f, %.0f)" % (
            name, out.get_num_lods(), [out.get_num_triangles(i) for i in range(out.get_num_lods())],
            [str(s.get_editor_property("material_slot_name")) for s in mats], (" MISSING %s" % missing) if missing else "",
            b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z))
        done += 1
    log("%d meshes" % done)


def main():
    if not os.path.isdir(SRC):
        log("FAILED: no folder %s" % SRC)
        return
    if want("textures"):
        import_textures()
    slots = None
    if want("materials"):
        slots = make_materials()
    if want("meshes"):
        if slots is None:
            names = {"Cas_Water": "MI_Cas_Falls"}
            slots = {}
            for p in eal.list_assets(MATS, recursive=False, include_folder=False):
                n = p.split("/")[-1].split(".")[0]
                slots["Cas_" + n[len("MI_Cas_"):]] = eal.load_asset(p)
            for k, v in names.items():
                slots[k] = eal.load_asset(MATS + "/" + v)
        import_meshes(slots)


try:
    main()
except Exception as e:  # noqa: BLE001
    import traceback
    log("FAILED: %s\n%s" % (e, traceback.format_exc()))
finally:
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "import_cascade_kit.log"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
