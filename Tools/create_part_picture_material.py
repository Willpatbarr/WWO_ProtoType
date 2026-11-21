"""M_WWO_PartPicture: the character menu's shell part pictures (UI/WWOPartPictures). A UI material that puts
two scene captures of a part together: the colour of a tone mapped capture (as the character preview's, so the
part looks as it does on the preview robot) cut out by the alpha of a scene colour capture.
- Color: the tone mapped capture (RTF_RGBA8, not sRGB: it holds display values, decoded here).
- Cut: the scene colour capture (its alpha is 0 where the part is).
- Alpha: the picture's opacity.
Where the cut-out is partly covered (the targets' smaller mips) the colour, which was drawn over black, is divided
back out by the coverage.

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/create_part_picture_material.py -unattended -nullrhi
Safe to rerun.
"""
import unreal

MATS = "/Game/WWO/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

CODE = """
float a = saturate(1.0 - K.a);
float3 s = saturate(C.rgb / max(a, 0.04));
float3 lo = s / 12.92;
float3 hi = pow((s + 0.055) / 1.055, 2.4);
float3 c = float3(s.r <= 0.04045 ? lo.r : hi.r, s.g <= 0.04045 ? lo.g : hi.g, s.b <= 0.04045 ? lo.b : hi.b);
return float4(c, a * Alpha);
"""


def new(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def main():
    path = MATS + "/M_WWO_PartPicture"
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    m = tools.create_asset("M_WWO_PartPicture", MATS, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    black = unreal.load_asset("/Engine/EngineResources/Black")
    color = new(m, unreal.MaterialExpressionTextureSampleParameter2D, -800, -150, parameter_name="Color",
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, texture=black)
    cut = new(m, unreal.MaterialExpressionTextureSampleParameter2D, -800, 100, parameter_name="Cut",
              sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, texture=black)
    alpha = new(m, unreal.MaterialExpressionScalarParameter, -800, 350, parameter_name="Alpha", default_value=1.0)
    c = new(m, unreal.MaterialExpressionCustom, -400, 0, code=CODE,
            output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4, description="PartPicture")
    ins = []
    for n in ["C", "K", "Alpha"]:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    mel.connect_material_expressions(color, "RGBA", c, "C")
    mel.connect_material_expressions(cut, "RGBA", c, "K")
    mel.connect_material_expressions(alpha, "", c, "Alpha")
    rgb = new(m, unreal.MaterialExpressionComponentMask, -150, 0, r=True, g=True, b=True, a=False)
    a = new(m, unreal.MaterialExpressionComponentMask, -150, 150, r=False, g=False, b=False, a=True)
    mel.connect_material_expressions(c, "", rgb, "")
    mel.connect_material_expressions(c, "", a, "")
    mel.connect_material_property(rgb, "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(a, "", MP.MP_OPACITY)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    unreal.log_warning("WWO part pictures: M_WWO_PartPicture done")


main()
