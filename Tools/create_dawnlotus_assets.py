# Creates or updates ONLY the DawnLotus data assets (flora, abilities, grenades, traits) from Tools/wwo_data.json.
# Touches no material, texture, mesh or any other flora's asset (Tools/create_assets.py rebuilds every
# material: don't use it for this). The live numbers are UWWODawnLotusTuning (Flora/WWODawnLotus.h).
# UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script="<full path to this file>"
import json
import os
import unreal

ROOT = os.path.dirname(os.path.abspath(__file__))
FLORA = "dawnlotus"
GRENADE_KINDS = {"explosive": unreal.WWOGrenadeKind.EXPLOSIVE, "heal_pool": unreal.WWOGrenadeKind.HEAL_POOL, "reveille": unreal.WWOGrenadeKind.REVEILLE}
ABILITY_KINDS = {"triggered": unreal.WWOAbilityKind.TRIGGERED, "active": unreal.WWOAbilityKind.ACTIVE}
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def make(name, folder, cls):
    path = "%s/%s" % (folder, name)
    if lib.does_asset_exist(path):
        return lib.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    return tools.create_asset(name, folder, cls, factory)


def fill(asset, item, kinds=None):
    for key, value in item.items():
        if key == "kind":
            value = kinds[value]
        elif key == "ult":
            value = unreal.WWOUltKind.BEAM
        elif key == "mods":
            mods = []
            for m in value:
                mod = unreal.WWOStatModifier()
                mod.set_editor_property("stat", m["stat"])
                mod.set_editor_property("op", [unreal.WWOStatOp.FLAT, unreal.WWOStatOp.PERCENT_ADD, unreal.WWOStatOp.MULTIPLY][int(m.get("op", 1))])
                mod.set_editor_property("value", float(m.get("value", 0.0)))
                mods.append(mod)
            value = mods
        elif isinstance(value, list):
            value = unreal.LinearColor(*value)
        asset.set_editor_property(key, value)
    if not lib.save_loaded_asset(asset, False):
        raise RuntimeError("could not save " + asset.get_name())
    unreal.log("DawnLotus asset: " + asset.get_path_name())


data = json.load(open(os.path.join(ROOT, "wwo_data.json"), encoding="utf-8"))
for item in data["flora"]:
    if item["id"] == FLORA:
        fill(make("DA_Flora_" + item["id"], "/Game/WWO/Data/Flora", unreal.WWOFloraData), item)
for item in data["abilities"]:
    if item.get("flora") == FLORA:
        fill(make("DA_Ability_" + item["id"], "/Game/WWO/Data/Abilities", unreal.WWOAbilityData), item, ABILITY_KINDS)
for item in data["grenades"]:
    if item.get("flora") == FLORA:
        fill(make("DA_Grenade_" + item["id"], "/Game/WWO/Data/Grenades", unreal.WWOGrenadeData), item, GRENADE_KINDS)
for item in data["traits"]:
    if item.get("flora") == FLORA:
        fill(make("DA_Trait_" + item["id"], "/Game/WWO/Data/Traits", unreal.WWOTraitData), item)
unreal.log("DawnLotus assets done")
