"""Materials for the character menu's preview studio (UI/WWOPreviewActor):
- M_WWO_PreviewBackdrop: unlit, opaque. The backdrop's colour, Color (written like M_WWO_Unlit's: sRGB,
  squared to linear here, so AWWOPreviewActor::BackdropFor matches it to the page), lifted by Glow (a neutral grey of
  its own brightness) toward the middle of the picture in a soft round pool (a studio light on the backdrop). The pool is
  laid out in the capture's own frame (screen space), Center and Radius in fractions of its height,
  and fades to exactly Color before the frame's edges, so the render's border meets the page.
- M_WWO_PreviewShadow: unlit, translucent black, for a plane under the feet: a soft round shadow,
  Alpha at its middle, nothing at its rim (UV distance from the plane's middle, Falloff shapes it).

Run headless from the project folder:
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/create_preview_materials.py -unattended -nullrhi
Safe to rerun.
"""
import unreal

MATS = "/Game/WWO/Materials"
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MP = unreal.MaterialProperty

BACKDROP_CODE = """
float2 p = UV - Center.xy;
p.x *= Aspect;
float r = length(p) / max(Radius, 0.01);
float pool = 1.0 - smoothstep(0.0, 1.0, r);
pool *= pool;
float3 c = pow(max(C.rgb, 0.0), 2.2);
float grey = dot(c, float3(0.3, 0.59, 0.11));
return c + Glow * pool * grey;   // a neutral light: the pool doesn't push the page's blue
"""

SHADOW_CODE = """
float r = length(UV * 2.0 - 1.0);
float a = pow(saturate(1.0 - r), Falloff);
return saturate(a * Alpha);
"""


def log(msg):
    unreal.log_warning("WWO preview: " + msg)


def new(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def scalar(m, name, default, x, y):
    return new(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=default)


def vector(m, name, default, x, y):
    return new(m, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*default))


def custom(m, code, inputs, out_type, x, y, desc):
    e = new(m, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type, description=desc)
    ins = []
    for n, _ in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    e.set_editor_property("inputs", ins)
    for n, (src, out) in inputs:
        if not mel.connect_material_expressions(src, out, e, n):
            log("could not connect %s" % n)
    return e


def fresh(name):
    path = MATS + "/" + name
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    return tools.create_asset(name, MATS, unreal.Material, unreal.MaterialFactoryNew())


def finish(m):
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def make_backdrop():
    m = fresh("M_WWO_PreviewBackdrop")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)   # the backdrop plane is seen from its back
    screen = new(m, unreal.MaterialExpressionScreenPosition, -900, 0)
    c = custom(m, BACKDROP_CODE, [("UV", (screen, "ViewportUV")), ("C", (vector(m, "Color", (0.05, 0.05, 0.05, 1), -900, 150), "")),
                                  ("Center", (vector(m, "Center", (0.5, 0.42, 0, 0), -900, 300), "")),
                                  ("Radius", (scalar(m, "Radius", 0.55, -900, 450), "")),
                                  ("Aspect", (scalar(m, "Aspect", 0.77, -900, 550), "")),
                                  ("Glow", (scalar(m, "Glow", 0.6, -900, 650), ""))],
               unreal.CustomMaterialOutputType.CMOT_FLOAT3, -500, 200, "PreviewBackdrop")
    mel.connect_material_property(c, "", MP.MP_EMISSIVE_COLOR)
    finish(m)


def make_shadow():
    m = fresh("M_WWO_PreviewShadow")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    uv = new(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    a = custom(m, SHADOW_CODE, [("UV", (uv, "")), ("Alpha", (scalar(m, "Alpha", 0.6, -900, 150), "")),
                                ("Falloff", (scalar(m, "Falloff", 1.6, -900, 250), ""))],
               unreal.CustomMaterialOutputType.CMOT_FLOAT1, -500, 100, "PreviewShadow")
    black = new(m, unreal.MaterialExpressionConstant3Vector, -500, -100, constant=unreal.LinearColor(0, 0, 0, 1))
    mel.connect_material_property(black, "", MP.MP_EMISSIVE_COLOR)
    mel.connect_material_property(a, "", MP.MP_OPACITY)
    finish(m)


make_backdrop()
make_shadow()
log("done")
