# Imports the flora symbols (Tools/art/ui/flora_<id>_symbol.png and _badge.png, made by
# Tools/make_flora_symbols.py) as UI textures:
#   /Game/WWO/Textures/UI/T_Flora_<Id>        the symbol alone, white on clear (512 x 512)
#   /Game/WWO/Textures/UI/T_Flora_<Id>_Badge  the symbol over a dark shade of the flora's colour
# for <Id> VoltVine, VeilSpore, IronBark. WWOUI::FloraSymbol / FloraBadge load them by flora id.
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<full path>/Tools/import_flora_symbols.py
import os
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
DEST = "/Game/WWO/Textures/UI"
FLORAS = {"voltvine": "VoltVine", "veilspore": "VeilSpore", "ironbark": "IronBark"}


def main():
    tasks, names = [], []
    for flora, title in FLORAS.items():
        for suffix, part in (("", "symbol"), ("_Badge", "badge")):
            t = unreal.AssetImportTask()
            t.filename = os.path.join(HERE, "art", "ui", "flora_%s_%s.png" % (flora, part))
            t.destination_path = DEST
            t.destination_name = "T_Flora_%s%s" % (title, suffix)
            t.automated = True
            t.replace_existing = True
            t.save = False
            tasks.append(t)
            names.append(t.destination_name)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for name in names:
        path = "%s/%s" % (DEST, name)
        tex = unreal.EditorAssetLibrary.load_asset(path)
        if not tex:
            raise RuntimeError("import failed: " + path)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)  # drawn smaller than it is
        tex.set_editor_property("srgb", True)
        unreal.EditorAssetLibrary.save_asset(path)
        unreal.log_warning("WWO flora symbols: %s %d x %d" % (path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))


main()
