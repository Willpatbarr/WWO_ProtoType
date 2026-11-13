# Imports the menu logos (Tools/art/ui/*.png, white on clear) as UI textures:
#   /Game/WWO/Textures/UI/T_LogoTitle  the "WHAT WAS OURS" wordmark, 2048 x 256 (the mark fills the width)
#   /Game/WWO/Textures/UI/T_LogoMark   the round leaf mark, 256 x 256
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<full path>/Tools/import_ui_logos.py
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
DEST = "/Game/WWO/Textures/UI"


def main():
    tasks = []
    for src, name in (("logo_title.png", "T_LogoTitle"), ("logo_mark.png", "T_LogoMark")):
        t = unreal.AssetImportTask()
        t.filename = os.path.join(HERE, "art", "ui", src)
        t.destination_path = DEST
        t.destination_name = name
        t.automated = True
        t.replace_existing = True
        t.save = False
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for name in ("T_LogoTitle", "T_LogoMark"):
        path = "%s/%s" % (DEST, name)
        tex = unreal.EditorAssetLibrary.load_asset(path)
        if not tex:
            raise RuntimeError("import failed: " + path)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)  # drawn much smaller than it is
        tex.set_editor_property("srgb", True)
        unreal.EditorAssetLibrary.save_asset(path)
        unreal.log_warning("WWO logos: %s %d x %d" % (path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))


main()
