"""Gives the robot frame (SK_RobotFrame, the three floras' body) its own surface: M_WWO_Robot and one
instance per part role, assigned to the mesh's slots by name.

Run headless from the project folder (after Tools/import_robot_frame.py, which re-imports the FBX's
plain Phong materials and so undoes this):
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/create_robot_materials.py -unattended -nullrhi

M_WWO_Robot (/Game/WWO/Materials): opaque, lit, for skeletal meshes. The look is the concept sheets'
(~/Documents/WWO_Flora/reference/sheets): dark charcoal hard-surface panels, slightly rough, with fine
wear speckle and soft blotches; near-black glossy joints and inner parts; a white visor slit.
- Color (linear), Roughness, Metallic, Specular: the part's base.
- Detail from T_Grime_Mask (Tools/make_grime_mask.py: R blotches, B speckle), projected on the mesh's
  pre-skinned (bind pose) position, so it sticks to the body as it animates. The robot has no usable
  UVs on most parts; it is faceted, so one planar projection per face (its dominant axis) shows no
  seams inside a face. Three texture reads.
  - DetailSize: cm per repeat of the fine speckle. Blotch: base value swing of the large patches
    (0.2 = +-20 %). Speckle: light wear flecks (0 to 1). SpeckleColor: their colour.
  - RoughnessVar: roughness swing from the speckle and patches.
  - Bump: strength of a fine dent normal from the speckle (world-space normal; 0 = flat facets).
- EmissiveColor x Glow: the visor (and nothing else).
Instances, /Game/WWO/Characters/RobotFrame/Materials/MI_Robot_<role>; SLOTS maps the FBX's slots
(named for their Blender colours) to roles. The roles were found by rendering each slot alone.
Safe to rerun.
"""
import unreal

MATS = "/Game/WWO/Materials"
OUT = "/Game/WWO/Characters/RobotFrame/Materials"
MESH = "/Game/WWO/Characters/RobotFrame/SK_RobotFrame"
GRIME = "/Game/WWO/Kit/Concrete/T_Grime_Mask"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

# Roles: base colour (linear), roughness, metallic, speckle, blotch, emissive colour, glow.
ROLES = {
    # The faceted armour shells: head, chest, shoulders, limb plates, pelvis, feet.
    "Armour": dict(Color=(0.056, 0.057, 0.06), Roughness=0.55, Metallic=0.12, Speckle=0.18, Blotch=0.1, RoughnessVar=0.07, Bump=0.05),
    # Trim beside the armour (shoulder sides, chest hem): a little darker, a little smoother.
    "Trim": dict(Color=(0.04, 0.041, 0.044), Roughness=0.45, Metallic=0.2, Speckle=0.15, Blotch=0.08, RoughnessVar=0.1, Bump=0.04),
    # Joints, pistons, abdomen ribs, ear pods, hands: near-black and glossy, the mechanical inner parts.
    "Joint": dict(Color=(0.018, 0.018, 0.02), Roughness=0.3, Metallic=0.6, Speckle=0.08, Blotch=0.08, RoughnessVar=0.08, Bump=0.02),
    # The visor slit: white light.
    "Visor": dict(Color=(0.8, 0.82, 0.85), Roughness=0.2, Metallic=0.0, Speckle=0.0, Blotch=0.0, RoughnessVar=0.0, Bump=0.0,
                  EmissiveColor=(0.9, 0.95, 1.0), Glow=16.0),
}
# The visor slit's light bar is an inside-out box in the source mesh (its front faces point into the
# head), so one-sided it is culled and only a scrap at the top shows past the dark part behind it.
TWO_SIDED = {"Visor"}
SLOTS = {
    "M_Robot_00_0c0c0c": "Joint",   # neck inner bits
    "M_Robot_01_9e9e9e": "Joint",   # inside the chest (hidden)
    "M_Robot_02_070707": "Joint",   # neck top
    "M_Robot_03_212123": "Joint",   # abdomen ribs
    "M_Robot_04_050505": "Joint",   # joints, limb skeleton, hips, hands
    "M_Robot_05_e5720c": "Joint",   # orange kitbash bits behind the shoulders (not in the sheets)
    "M_Robot_06_161616": "Armour",  # limb plates, pelvis, feet
    "M_Robot_07_ffffff": "Joint",   # shoulder inner parts
    "M_Robot_08_7f7f7f": "Trim",    # shoulder side plates
    "M_Robot_09_262626": "Trim",    # chest hem
    "M_Robot_10_0f1114": "Joint",   # ear pods, collar bars
    "M_Robot_11_1c2166": "Joint",   # small blue part at the back
    "M_Robot_12_cccccc": "Visor",   # the visor slit
    "M_Robot_13_1c1e1e": "Armour",  # head shell, chest, shoulder pads
}

DETAIL_CODE = """
float3 a = abs(Nl);
float2 uv = (a.x >= a.y && a.x >= a.z) ? P.yz : ((a.y >= a.z) ? P.xz : P.xy);
uv /= max(Size, 1.0);
float4 fine = Texture2DSample(Tex, TexSampler, uv);
float patch = Texture2DSample(Tex, TexSampler, uv * 0.11 + float2(0.37, 0.61)).r;
float e = 2.0 / 1024.0;
float hx = Texture2DSample(Tex, TexSampler, uv + float2(e, 0)).b - fine.b;
float hy = Texture2DSample(Tex, TexSampler, uv + float2(0, e)).b - fine.b;
return float4(fine.b, patch, hx, hy);
"""

BASE_CODE = """
float fleck = smoothstep(0.78, 0.92, D.x) * Speckle;
float3 c = C.rgb * (1.0 + Blotch * (D.y * 2.0 - 1.0)) * (0.96 + 0.08 * D.x);
return lerp(c, SC.rgb, saturate(fleck * 0.55));
"""

ROUGH_CODE = """
return saturate(R + RV * ((D.x - 0.5) * 0.8 + (D.y - 0.5) * 0.6) - smoothstep(0.70, 0.86, D.x) * RV * 0.5);
"""

NORMAL_CODE = """
float3 n = normalize(N);
float3 t = normalize(abs(n.z) < 0.95 ? cross(n, float3(0, 0, 1)) : cross(n, float3(1, 0, 0)));
float3 b = cross(n, t);
return normalize(n - Bump * 6.0 * (D.z * t + D.w * b));
"""


def log(msg):
    unreal.log_warning("WWO robot: " + msg)


def new(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(m, name, default, x, y):
    return new(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=default)


def vector(m, name, default, x, y):
    return new(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*default, 1.0))


def link(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        log("could not connect %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def custom(m, code, inputs, out_type, x, y, desc):
    e = new(m, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type, description=desc)
    ins = []
    for n, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    e.set_editor_property("inputs", ins)
    for n, (src, out) in inputs:
        link(src, out, e, n)
    return e


def interpolated(m, cls, x, y):
    # Pre-skinned attributes live in the vertex shader: carry them to the pixel shader.
    src = new(m, cls, x, y)
    vi = new(m, unreal.MaterialExpressionVertexInterpolator, x + 200, y)
    link(src, "", vi, "")
    return vi


def make_material():
    path = MATS + "/M_WWO_Robot"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset("M_WWO_Robot", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)
    m.set_editor_property("tangent_space_normal", False)   # the dent normal is built in world space
    F4, F3, F1 = (unreal.CustomMaterialOutputType.CMOT_FLOAT4, unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                  unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    tex = new(m, unreal.MaterialExpressionTextureObjectParameter, -1600, -200, parameter_name="DetailMask",
              texture=unreal.load_asset(GRIME), sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    pos = interpolated(m, unreal.MaterialExpressionPreSkinnedPosition, -1800, 0)
    nrm = interpolated(m, unreal.MaterialExpressionPreSkinnedNormal, -1800, 150)
    detail = custom(m, DETAIL_CODE, [("Tex", (tex, "")), ("P", (pos, "")), ("Nl", (nrm, "")),
                                     ("Size", (scalar(m, "DetailSize", 60.0, -1600, 300), ""))], F4, -1200, 0, "RobotDetail")
    color = vector(m, "Color", (0.05, 0.05, 0.055), -900, -400)
    base = custom(m, BASE_CODE, [("D", (detail, "")), ("C", (color, "")), ("SC", (vector(m, "SpeckleColor", (0.2, 0.2, 0.21), -900, -250), "")),
                                 ("Speckle", (scalar(m, "Speckle", 0.5, -900, -120), "")), ("Blotch", (scalar(m, "Blotch", 0.2, -900, -40), ""))],
                  F3, -600, -300, "RobotBase")
    rough = custom(m, ROUGH_CODE, [("D", (detail, "")), ("R", (scalar(m, "Roughness", 0.5, -900, 80), "")),
                                   ("RV", (scalar(m, "RoughnessVar", 0.2, -900, 160), ""))], F1, -600, 80, "RobotRoughness")
    normal = custom(m, NORMAL_CODE, [("D", (detail, "")), ("N", (new(m, unreal.MaterialExpressionVertexNormalWS, -900, 260), "")),
                                     ("Bump", (scalar(m, "Bump", 0.3, -900, 340), ""))], F3, -600, 260, "RobotNormal")
    glow = new(m, unreal.MaterialExpressionMultiply, -600, 500)
    link(vector(m, "EmissiveColor", (0, 0, 0), -900, 460), "", glow, "A")
    link(scalar(m, "Glow", 0.0, -900, 600), "", glow, "B")
    mel.connect_material_property(base, "", MP.MP_BASE_COLOR)
    mel.connect_material_property(rough, "", MP.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, "Metallic", 0.3, -900, 700), "", MP.MP_METALLIC)
    mel.connect_material_property(scalar(m, "Specular", 0.5, -900, 780), "", MP.MP_SPECULAR)
    mel.connect_material_property(normal, "", MP.MP_NORMAL)
    mel.connect_material_property(glow, "", MP.MP_EMISSIVE_COLOR)
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    return m


def make_instance(parent, role, values):
    path = "%s/MI_Robot_%s" % (OUT, role)
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "MI_Robot_" + role, OUT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", parent)
    for k, v in values.items():
        if isinstance(v, tuple):
            mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v, 1.0))
        else:
            mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    if role in TWO_SIDED:
        ov = mi.get_editor_property("base_property_overrides")
        ov.set_editor_property("override_two_sided", True)
        ov.set_editor_property("two_sided", True)
        mi.set_editor_property("base_property_overrides", ov)
        mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    return mi


def main():
    parent = make_material()
    instances = {role: make_instance(parent, role, values) for role, values in ROLES.items()}
    mesh = eal.load_asset(MESH)
    slots = []
    for slot in mesh.get_editor_property("materials"):
        name = str(slot.get_editor_property("material_slot_name"))
        role = SLOTS.get(name)
        if role:
            slot.set_editor_property("material_interface", instances[role])
        else:
            log("slot %s has no role, left as it is" % name)
        slots.append(slot)
    mesh.set_editor_property("materials", slots)
    eal.save_loaded_asset(mesh)
    log("assigned: %s" % [(str(s.get_editor_property("material_slot_name")), s.get_editor_property("material_interface").get_name())
                          for s in eal.load_asset(MESH).get_editor_property("materials")])


main()
