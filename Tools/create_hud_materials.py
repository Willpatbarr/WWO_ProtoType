"""M_WWO_HUDGun: the HUD's weapon pictures (bottom right; UI/WWOHUDWidget, AWWOGunPreviewActor in
picture mode). A UI material that shows a scene capture of the gun cut out of its empty background.
- Tex: the capture's render target (scene colour in RGB, inverse opacity in A: 0 where the gun is).
- Gain: brightness of the capture's linear colour (it is not tone mapped). Dim: 0 to 1, pulls the
  picture toward a flat grey of its own brightness (the stowed weapon). Alpha: its opacity.

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/create_hud_materials.py -unattended -nullrhi
Safe to rerun.
"""
import unreal

MATS = "/Game/WWO/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

CODE = """
float3 c = max(T.rgb, 0.0) * Gain;
c = c / (1.0 + c);                       // a soft shoulder, so glowing parts keep their colour
c = pow(c, 1.0 / 1.6);                   // lift the dark gunmetal off the picture behind it
float grey = dot(c, float3(0.3, 0.59, 0.11));
c = lerp(c, float3(grey, grey, grey), Dim);
return float4(c, saturate(1.0 - T.a) * Alpha);
"""


def new(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(m, name, default, x, y):
    return new(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=default)


def main():
    path = MATS + "/M_WWO_HUDGun"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset("M_WWO_HUDGun", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    tex = new(m, unreal.MaterialExpressionTextureSampleParameter2D, -800, -100, parameter_name="Tex",
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
              texture=unreal.load_asset("/Engine/EngineResources/Black"))
    c = new(m, unreal.MaterialExpressionCustom, -400, 0, code=CODE,
            output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4, description="HUDGun")
    names = ["T", "Gain", "Dim", "Alpha"]
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    mel.connect_material_expressions(tex, "RGBA", c, "T")
    mel.connect_material_expressions(scalar(m, "Gain", 3.0, -800, 150), "", c, "Gain")
    mel.connect_material_expressions(scalar(m, "Dim", 0.0, -800, 250), "", c, "Dim")
    mel.connect_material_expressions(scalar(m, "Alpha", 1.0, -800, 350), "", c, "Alpha")
    rgb = new(m, unreal.MaterialExpressionComponentMask, -150, 0, r=True, g=True, b=True, a=False)
    a = new(m, unreal.MaterialExpressionComponentMask, -150, 150, r=False, g=False, b=False, a=True)
    mel.connect_material_expressions(c, "", rgb, "")
    mel.connect_material_expressions(c, "", a, "")
    mel.connect_material_property(rgb, "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(a, "", MP.MP_OPACITY)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    unreal.log_warning("WWO hud: M_WWO_HUDGun done")


main()
