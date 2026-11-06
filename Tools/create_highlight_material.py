# Creates /Game/WWO/Materials/M_WWO_Highlight: the faint tint laid over the enemy under the reticle
# (UWWOBodyComponent::SetHighlight sets it as the body mesh's overlay material). Unlit translucent,
# Color tinted by Alpha, stronger toward the outline (fresnel), allowed on skeletal meshes.
#
# Run headless:
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
import unreal

PATH = "/Game/WWO/Materials"
NAME = "M_WWO_Highlight"
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

if lib.does_asset_exist("%s/%s" % (PATH, NAME)):
    lib.delete_asset("%s/%s" % (PATH, NAME))
m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property("used_with_skeletal_mesh", True)
m.set_editor_property("used_with_morph_targets", True)
m.set_editor_property("used_with_clothing", True)

color = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -600, -100)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.15, 0.1, 1.0))
alpha = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 100)
alpha.set_editor_property("parameter_name", "Alpha")
alpha.set_editor_property("default_value", 0.15)

# Opacity = Alpha * lerp(0.5, 1, fresnel): a light tint all over, a bit more at the outline.
fresnel = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -600, 250)
fresnel.set_editor_property("exponent", 2.0)
fresnel.set_editor_property("base_reflect_fraction", 0.0)
lerp = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -350, 250)
lerp.set_editor_property("const_a", 0.5)
lerp.set_editor_property("const_b", 1.0)
mel.connect_material_expressions(fresnel, "", lerp, "Alpha")
mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -150, 150)
mel.connect_material_expressions(alpha, "", mul, "A")
mel.connect_material_expressions(lerp, "", mul, "B")

mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_OPACITY)
mel.recompile_material(m)
lib.save_loaded_asset(m)
open(__file__.replace(".py", ".log"), "w").write("created %s/%s\n" % (PATH, NAME))
