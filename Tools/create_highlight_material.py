# Creates /Game/WWO/Materials/M_WWO_Highlight: the faint tint laid over the enemy under the reticle
# (UWWOBodyComponent::SetHighlight sets it as the body mesh's overlay material). Unlit translucent,
# Color at opacity Alpha all over plus Rim toward the outline (fresnel), allowed on skeletal meshes.
# Also the PvP marker tint (UWWOBodyComponent::SetTeamTint): a faint fill with a strong outline.
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

# Opacity = Alpha (a light tint over the whole body) + Rim * fresnel (a strong band where the body
# turns away from the eye, which reads as an outline around its silhouette).
rim = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 400)
rim.set_editor_property("parameter_name", "Rim")
rim.set_editor_property("default_value", 0.08)
fresnel = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -600, 250)
fresnel.set_editor_property("exponent", 2.2)
fresnel.set_editor_property("base_reflect_fraction", 0.0)
edge = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -350, 300)
mel.connect_material_expressions(rim, "", edge, "A")
mel.connect_material_expressions(fresnel, "", edge, "B")
add = mel.create_material_expression(m, unreal.MaterialExpressionAdd, -200, 150)
mel.connect_material_expressions(alpha, "", add, "A")
mel.connect_material_expressions(edge, "", add, "B")
mul = mel.create_material_expression(m, unreal.MaterialExpressionSaturate, -50, 150)
mel.connect_material_expressions(add, "", mul, "")

mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_OPACITY)
mel.recompile_material(m)
lib.save_loaded_asset(m)
open(__file__.replace(".py", ".log"), "w").write("created %s/%s\n" % (PATH, NAME))
