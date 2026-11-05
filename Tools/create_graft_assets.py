"""Creates the assets the body and its flora grafts use (Godot: character_model.gd,
region_color.gdshader, flora/vine_graft.gd, bark_graft.gd, spore_graft.gd).

Run headless from the project folder (the Godot project path is read from WWO_GODOT, default
~/Documents/WhatWasOurs):
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=Tools/create_graft_assets.py -unattended -nullrhi

- Textures from the Godot build's nature pack (Quaternius Stylized Nature MegaKit, CC0):
  /Game/WWO/Textures/T_Leaves, T_Bark, T_Bark_N, T_Mushrooms.
- The two pack mushrooms VeilSpore wears: /Game/WWO/Meshes/SM_Mushroom_Common, SM_Mushroom_Laetiporus.
- Graft materials (the Godot graft shaders, colors written as sRGB like Godot's):
  M_WWO_GraftVine, _Leaf, _Pod, _Bark, _Seed, _Cap, _Gill, _Mushroom.
- The body: M_WWO_Body, a copy of the mannequin material whose paint tint is picked per region
  (head, arms, core, legs) like region_color.gdshader, and MI_WWO_Body_01 / _02 for its two slots.
Safe to rerun: everything is rebuilt in place.
"""
import os

import unreal

GODOT = os.path.expanduser(os.environ.get("WWO_GODOT", "~/Documents/WhatWasOurs"))
NATURE = os.path.join(GODOT, "flora/models/nature")
MATS = "/Game/WWO/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty
F1, F3, F4 = (unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
              unreal.CustomMaterialOutputType.CMOT_FLOAT4)


def log(msg):
    unreal.log_warning("WWO grafts: " + msg)


# --- Imports ----------------------------------------------------------------------------------

def import_file(src, folder, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", src)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    return [str(p) for p in task.get_editor_property("imported_object_paths")]


def import_texture(file, name, normal=False):
    import_file(os.path.join(NATURE, file), "/Game/WWO/Textures", name)
    tex = eal.load_asset("/Game/WWO/Textures/" + name)
    if normal:
        # Godot reads normal maps OpenGL style (green up); Unreal's are DirectX style.
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("flip_green_channel", True)
    eal.save_loaded_asset(tex)
    return tex


def import_mushroom(file, name):
    # Imported into a scratch folder (Interchange also brings the file's material and texture),
    # then the static mesh is kept as /Game/WWO/Meshes/<name>.
    scratch = "/Game/WWO/Meshes/Import_" + name
    if eal.does_directory_exist(scratch):
        eal.delete_directory(scratch)
    paths = import_file(os.path.join(NATURE, file), scratch, name)
    mesh = None
    for p in eal.list_assets(scratch, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, unreal.StaticMesh):
            mesh = a
    if not mesh:
        log("no static mesh imported from %s (%s)" % (file, paths))
        return None
    dest = "/Game/WWO/Meshes/" + name
    if eal.does_asset_exist(dest):
        eal.delete_asset(dest)
    eal.duplicate_asset(mesh.get_path_name(), dest)
    eal.delete_directory(scratch)
    out = eal.load_asset(dest)
    out.set_material(0, eal.load_asset(MATS + "/M_WWO_GraftMushroom"))
    b = out.get_bounding_box()
    log("%s bounds %s .. %s" % (name, b.min, b.max))
    eal.save_loaded_asset(out)
    return out


# --- Material helpers -------------------------------------------------------------------------

def new_material(name, two_sided=True, masked=False):
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("two_sided", two_sided)
    if masked:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    return m


def expr(m, cls, x=-900, y=0, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def vec(m, name, default):
    return expr(m, unreal.MaterialExpressionVectorParameter, parameter_name=name,
                default_value=unreal.LinearColor(*(list(default) + [1.0])[:4]))


def scalar(m, name, default):
    return expr(m, unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=default)


def const(m, value):
    return expr(m, unreal.MaterialExpressionConstant, r=value)


def tex(m, name, path, uv=None, sampler=None):
    e = expr(m, unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name, texture=eal.load_asset(path))
    if sampler is not None:
        e.set_editor_property("sampler_type", sampler)
    if uv is not None:
        mel.connect_material_expressions(uv, "", e, "UVs")
    return e


def custom(m, code, inputs, out_type, desc):
    e = expr(m, unreal.MaterialExpressionCustom, -400, 0, code=code, output_type=out_type, description=desc)
    ins = []
    for n, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    e.set_editor_property("inputs", ins)
    for n, src in inputs:
        s, out = src if isinstance(src, tuple) else (src, "")
        mel.connect_material_expressions(s, out, e, n)
    return e


def out(src, prop, output=""):
    mel.connect_material_property(src, output, prop)


def finish(m):
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def common(m):
    vc = expr(m, unreal.MaterialExpressionVertexColor)
    rgb = expr(m, unreal.MaterialExpressionComponentMask, r=True, g=True, b=True, a=False)
    mel.connect_material_expressions(vc, "", rgb, "")
    return {
        "vc": vc,
        "vc_rgb": rgb,
        "n": expr(m, unreal.MaterialExpressionVertexNormalWS),
        "v": expr(m, unreal.MaterialExpressionCameraVectorWS),
        "t": expr(m, unreal.MaterialExpressionTime),
        "uv": expr(m, unreal.MaterialExpressionTextureCoordinate),
    }




# --- Graft materials --------------------------------------------------------------------------

def make_vine():
    # VineGraft's stalk material: vertex color (Godot uses it raw), faintly glowing when set.
    m = new_material("M_WWO_GraftVine")
    c = common(m)
    glow = vec(m, "Glow Color", (0, 0, 0))
    strength = scalar(m, "Glow Strength", 0.0)
    e = custom(m, "return pow(max(G.rgb, 0.0), 2.2) * S;", [("G", (glow, "RGB")), ("S", strength)], F3, "VineGlow")
    out(c["vc_rgb"], MP.MP_BASE_COLOR)
    out(e, MP.MP_EMISSIVE_COLOR)
    out(const(m, 0.85), MP.MP_ROUGHNESS)
    finish(m)


def make_leaf():
    # The leaf sheet, alpha scissor 0.4, both sides, times the per-leaf olive tint.
    m = new_material("M_WWO_GraftLeaf", masked=True)
    m.set_editor_property("opacity_mask_clip_value", 0.4)
    c = common(m)
    t = tex(m, "Leaves", "/Game/WWO/Textures/T_Leaves")
    mul = expr(m, unreal.MaterialExpressionMultiply)
    mel.connect_material_expressions(t, "RGB", mul, "A")
    mel.connect_material_expressions(c["vc_rgb"], "", mul, "B")
    out(mul, MP.MP_BASE_COLOR)
    out(t, MP.MP_OPACITY_MASK, "A")
    out(const(m, 0.7), MP.MP_ROUGHNESS)
    finish(m)


POD_CODE = """
float3 col = pow(max(C.rgb, 0.0), 2.2);
float facing = saturate(dot(normalize(N), normalize(V)));
float phase = Ph * 6.283; // each pod pulses on its own beat
float pulse = 0.8 + 0.2 * sin(T * 3.0 + phase) + 0.08 * sin(T * 17.0 + phase * 3.0);
float3 hot = lerp(col, float3(0.75, 0.88, 1.0), 0.55 * pow(facing, 8.0));
return hot * Glow * pulse * (0.3 + 0.7 * pow(facing, 2.0));
"""


def make_pod():
    # VineGraft.POD_CODE: faceted crystal, the middle burning white-hot, pulsing.
    m = new_material("M_WWO_GraftPod")
    c = common(m)
    color = vec(m, "Color", (0.25, 0.55, 1.0))
    glow = scalar(m, "Glow", 2.2)
    e = custom(m, POD_CODE,
               [("C", (color, "RGB")), ("N", c["n"]), ("V", c["v"]), ("T", c["t"]), ("Ph", (c["vc"], "R")), ("Glow", glow)], F3, "Pod")
    base = custom(m, "return pow(max(C.rgb, 0.0), 2.2) * 0.25;", [("C", (color, "RGB"))], F3, "PodBase")
    out(base, MP.MP_BASE_COLOR)
    out(e, MP.MP_EMISSIVE_COLOR)
    out(const(m, 0.12), MP.MP_ROUGHNESS)
    out(const(m, 0.9), MP.MP_SPECULAR)
    finish(m)


BARK_CODE = """
float3 albedo = B * pow(max(Tint.rgb, 0.0), 2.2) * 2.0;
float seed = Seed * 50.0;
float2 p = UV2;
// A crack wandering down the plate (UV2: 0-1 across and along it), with one side branch.
float path = 0.5 + 0.15 * sin(p.y * 6.0 + seed) + 0.07 * sin(p.y * 15.0 + seed * 1.7);
float fade = smoothstep(0.05, 0.2, p.y) * smoothstep(0.98, 0.75, p.y);
float crack = smoothstep(0.032, 0.0, abs(p.x - path)) * fade;
float lean = sin(seed) > 0.0 ? 1.0 : -1.0;
float branch_path = path + (p.y - 0.5) * 0.8 * lean;
float branch = smoothstep(0.022, 0.0, abs(p.x - branch_path)) * step(0.5, p.y) * smoothstep(0.85, 0.62, p.y);
float c = max(crack, branch) * Cracked * (1.0 - Side);
float pulse = 0.85 + 0.15 * sin(T * 1.5 + seed);
float3 glow = pow(max(Glow.rgb, 0.0), 2.2);
float3 base = lerp(lerp(albedo, pow(max(Edge.rgb, 0.0), 2.2), Side * 0.8), glow, c * 0.7);
float3 emit = glow * (c * 3.0 * pulse + Side * 0.12);
return Which > 0.5 ? emit : base;
"""


def make_bark():
    # BarkGraft.BARK_CODE: the dead-tree bark, warm edges, glowing orange cracks.
    m = new_material("M_WWO_GraftBark")
    c = common(m)
    uv2 = expr(m, unreal.MaterialExpressionTextureCoordinate, coordinate_index=1)
    bark = tex(m, "Bark", "/Game/WWO/Textures/T_Bark")
    normal = tex(m, "Bark Normal", "/Game/WWO/Textures/T_Bark_N", sampler=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    tint = vec(m, "Tint", (0.84, 0.76, 0.68))
    edge = vec(m, "Edge", (0.72, 0.45, 0.22))
    glow = vec(m, "Glow", (1.0, 0.5, 0.08))
    nodes = []
    for which in (0.0, 1.0):
        nodes.append(custom(m, BARK_CODE, [("B", (bark, "RGB")), ("UV2", uv2), ("Seed", (c["vc"], "R")), ("Side", (c["vc"], "G")),
                                           ("Cracked", (c["vc"], "B")), ("T", c["t"]), ("Tint", (tint, "RGB")), ("Edge", (edge, "RGB")),
                                           ("Glow", (glow, "RGB")), ("Which", const(m, which))], F3, "Bark"))
    out(nodes[0], MP.MP_BASE_COLOR)
    out(nodes[1], MP.MP_EMISSIVE_COLOR)
    out(normal, MP.MP_NORMAL, "RGB")
    out(const(m, 0.92), MP.MP_ROUGHNESS)
    finish(m)


def make_seed():
    # BarkGraft.SEED_CODE: glossy orange, glowing a little from inside, brighter at the edges.
    m = new_material("M_WWO_GraftSeed", two_sided=False)
    c = common(m)
    color = vec(m, "Color", (0.95, 0.5, 0.1))
    e = custom(m, "float facing = saturate(dot(normalize(N), normalize(V)));\nreturn pow(max(C.rgb, 0.0), 2.2) * (0.25 + 0.5 * pow(1.0 - facing, 2.0));",
               [("C", (color, "RGB")), ("N", c["n"]), ("V", c["v"])], F3, "Seed")
    base = custom(m, "return pow(max(C.rgb, 0.0), 2.2);", [("C", (color, "RGB"))], F3, "SeedBase")
    out(base, MP.MP_BASE_COLOR)
    out(e, MP.MP_EMISSIVE_COLOR)
    out(const(m, 0.22), MP.MP_ROUGHNESS)
    out(const(m, 0.7), MP.MP_SPECULAR)
    finish(m)


CAP_CODE = """
// Dark purple, lighter toward the rim, sprinkled with tiny glowing specks.
float2 grid = UV * float2(46.0, 14.0);
float2 id = floor(grid);
float h = frac(sin(dot(id, float2(12.9898, 78.233))) * 43758.5453);
float dot_shape = smoothstep(0.28, 0.0, length(frac(grid) - 0.5));
float sparkle = step(0.9, h) * dot_shape * (0.6 + 0.4 * sin(T * 2.0 + h * 40.0));
float3 base = lerp(pow(max(C.rgb, 0.0), 2.2), pow(max(Rim.rgb, 0.0), 2.2), smoothstep(0.7, 1.0, UV.y));
return Which > 0.5 ? pow(max(Speck.rgb, 0.0), 2.2) * sparkle * 1.6 : base;
"""


def make_cap():
    # SporeGraft.CAP_CODE: the shelf mushrooms' tops.
    m = new_material("M_WWO_GraftCap")
    c = common(m)
    color = vec(m, "Color", (0.2, 0.07, 0.28))
    rim = vec(m, "Rim", (0.5, 0.25, 0.62))
    speck = vec(m, "Speck", (0.55, 0.6, 1.0))
    nodes = [custom(m, CAP_CODE, [("UV", c["uv"]), ("T", c["t"]), ("C", (color, "RGB")), ("Rim", (rim, "RGB")), ("Speck", (speck, "RGB")),
                                  ("Which", const(m, w))], F3, "Cap") for w in (0.0, 1.0)]
    out(nodes[0], MP.MP_BASE_COLOR)
    out(nodes[1], MP.MP_EMISSIVE_COLOR)
    out(const(m, 0.85), MP.MP_ROUGHNESS)
    out(const(m, 0.2), MP.MP_SPECULAR)
    finish(m)


def make_gill():
    # SporeGraft.GILL_CODE: fine glowing lines fanning out from the body, brightest there.
    m = new_material("M_WWO_GraftGill")
    c = common(m)
    color = vec(m, "Color", (0.22, 0.32, 1.0))
    e = custom(m, "float lines = pow(abs(sin(UV.x * 110.0)), 5.0);\nfloat near = 1.0 - UV.y;\n"
                         "return pow(max(C.rgb, 0.0), 2.2) * (0.35 + 2.2 * lines) * (0.12 + 1.8 * near * near);",
               [("UV", c["uv"]), ("C", (color, "RGB"))], F3, "Gill")
    base = custom(m, "return pow(max(C.rgb, 0.0), 2.2) * 0.15;", [("C", (color, "RGB"))], F3, "GillBase")
    out(base, MP.MP_BASE_COLOR)
    out(e, MP.MP_EMISSIVE_COLOR)
    finish(m)


def make_mushroom():
    # SporeGraft.MUSHROOM_CODE: the pack mushrooms recolored VeilSpore purple by brightness,
    # their undersides (the gills) glowing blue.
    m = new_material("M_WWO_GraftMushroom")
    c = common(m)
    t = tex(m, "Mushrooms", "/Game/WWO/Textures/T_Mushrooms")
    dark = vec(m, "Dark", (0.2, 0.07, 0.28))
    light = vec(m, "Light", (0.68, 0.55, 0.78))
    glow = vec(m, "Glow", (0.22, 0.32, 1.0))
    base = custom(m, "float lum = dot(C, float3(0.3, 0.59, 0.11));\nreturn lerp(pow(max(D.rgb, 0.0), 2.2), pow(max(L.rgb, 0.0), 2.2), smoothstep(0.25, 0.85, lum));",
                  [("C", (t, "RGB")), ("D", (dark, "RGB")), ("L", (light, "RGB"))], F3, "MushroomBase")
    e = custom(m, "return pow(max(G.rgb, 0.0), 2.2) * saturate(-normalize(N).z * 2.0) * 1.4;", [("G", (glow, "RGB")), ("N", c["n"])], F3, "MushroomGlow")
    out(base, MP.MP_BASE_COLOR)
    out(e, MP.MP_EMISSIVE_COLOR)
    out(const(m, 0.55), MP.MP_ROUGHNESS)
    finish(m)


# --- The body ---------------------------------------------------------------------------------

# Which region (region_color.gdshader: head, arms, core, legs) a vertex of the mannequin is in,
# from where it sits in the mesh's reference pose (Godot picks the region of the bone that moves
# the vertex most; Unreal materials can't read bone weights). Manny's reference pose, in cm: x to
# its left, z up; the upper arm starts at (19, 143.6) and hangs out and down along (0.576, -0.817),
# neck_01 at 152.8, the thighs at 93.5.
REGION_CODE = """
float ax = abs(P.x);
float2 rel = float2(ax - 19.0, P.z - 143.6);
float2 d = float2(0.576, -0.817);
float along = dot(rel, d);
float perp = length(rel - along * d);
if (along > -4.0 && (perp < 9.0 || ax > 30.0)) return Arms;
if (P.z > 151.0 && ax < 12.0) return Head;
if (P.z < 89.0) return Legs;
return Core;
"""


def make_body():
    # A copy of the mannequin's material with its Paint Tint swapped for the region's tint.
    path = MATS + "/M_WWO_Body"
    for p in (MATS + "/MI_WWO_Body_02", MATS + "/MI_WWO_Body_01", path):
        if eal.does_asset_exist(p):
            eal.delete_asset(p)
    m = eal.duplicate_asset("/Game/Characters/Mannequins/Materials/M_Mannequin", path)
    lerp = mel.get_material_property_input_node(m, MP.MP_BASE_COLOR)
    names = [str(n) for n in mel.get_material_expression_input_names(lerp)]
    mul = mel.get_inputs_for_material_expression(m, lerp)[names.index("B")]
    pre = expr(m, unreal.MaterialExpressionPreSkinnedPosition, -1300, 600)
    interp = expr(m, unreal.MaterialExpressionVertexInterpolator, -1100, 600)
    mel.connect_material_expressions(pre, "", interp, "")
    tints = [vec(m, n + " Tint", (1, 1, 1)) for n in ("Head", "Arms", "Core", "Legs")]
    region = custom(m, REGION_CODE, [("P", interp)] + [(n, (t, "RGB")) for n, t in zip(("Head", "Arms", "Core", "Legs"), tints)], F3, "RegionTint")
    mel.connect_material_expressions(region, "", mul, "B")
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    first = eal.duplicate_asset("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_01_New", MATS + "/MI_WWO_Body_01")
    mel.set_material_instance_parent(first, m)
    eal.save_loaded_asset(first)
    second = eal.duplicate_asset("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_02_New", MATS + "/MI_WWO_Body_02")
    mel.set_material_instance_parent(second, first)
    eal.save_loaded_asset(second)


def main():
    import_texture("Leaves.png", "T_Leaves")
    import_texture("Bark_DeadTree.png", "T_Bark")
    import_texture("Bark_DeadTree_Normal.png", "T_Bark_N", normal=True)
    import_texture("Mushrooms.png", "T_Mushrooms")
    log("textures done")
    for fn in (make_vine, make_leaf, make_pod, make_bark, make_seed, make_cap, make_gill, make_mushroom):
        fn()
    log("graft materials done")
    import_mushroom("Mushroom_Common.gltf", "SM_Mushroom_Common")
    import_mushroom("Mushroom_Laetiporus.gltf", "SM_Mushroom_Laetiporus")
    log("mushrooms done")
    make_body()
    log("body material done")


main()
