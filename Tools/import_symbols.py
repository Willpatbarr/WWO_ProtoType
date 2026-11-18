# Imports the UI symbols (Tools/art/ui/<kind>_<id>_symbol.png and _badge.png, made by Tools/make_symbols.py)
# as UI textures, 512 x 512:
#   /Game/WWO/Textures/UI/T_<Kind>_<Name>        the symbol alone, white on clear
#   /Game/WWO/Textures/UI/T_<Kind>_<Name>_Badge  the symbol over a dark shade of its colour
# <Kind> Flora or Grenade; <Name> the id in title case (volt_grenade: VoltGrenade; the floras' as they're
# written: VoltVine, VeilSpore, IronBark). WWOUI::SymbolTexture / MakeSymbol load them by kind and id.
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<full path>/Tools/import_symbols.py
import os
import re
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, "art", "ui")
DEST = "/Game/WWO/Textures/UI"
FLORA_NAMES = {"voltvine": "VoltVine", "veilspore": "VeilSpore", "ironbark": "IronBark"}


def title(kind, item):
    if kind == "flora":
        return FLORA_NAMES[item]
    return "".join(part.capitalize() for part in item.split("_"))


def main():
    tasks, names = [], []
    for name in sorted(os.listdir(UI)):
        m = re.match(r"(flora|grenade)_(.+)_(symbol|badge)\.png$", name)
        if not m:
            continue
        kind, item, part = m.groups()
        t = unreal.AssetImportTask()
        t.filename = os.path.join(UI, name)
        t.destination_path = DEST
        t.destination_name = "T_%s_%s%s" % (kind.capitalize(), title(kind, item), "_Badge" if part == "badge" else "")
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
        unreal.log_warning("WWO symbols: %s %d x %d" % (path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))


main()
