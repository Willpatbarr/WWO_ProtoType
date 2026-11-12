# Makes /Game/WWO/Data/Weapons/DA_Weapon_burstrifle: the fifth weapon frame, a 3-round burst rifle
# with more range than the auto rifle.
# It starts as a copy of the rifle's asset with the rifle's saved Settings values on top
# (Saved/WWO/SavedSettings.json, "weapon/rifle": the tuning lives there, not in the asset), then
# takes the burst rifle's own values (the fields in OWN) from Tools/wwo_data.json's "burstrifle"
# entry. Run again after changing either to refill it.
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<full path>/Tools/create_burst_rifle.py
import json
import os
import re

import unreal

ROOT = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(ROOT)
FOLDER = "/Game/WWO/Data/Weapons"
SOURCE = FOLDER + "/DA_Weapon_rifle"
TARGET = FOLDER + "/DA_Weapon_burstrifle"
eal = unreal.EditorAssetLibrary
# What makes it the burst rifle (the rest stays as the rifle is tuned).
OWN = ("id", "display_name", "archetype", "order", "zoom", "ads_time", "fire_rate", "burst", "burst_rate", "damage",
       "crit_multiplier", "fire_range", "hip_spread", "recoil_up", "recoil_side", "magazine_size", "reload_time", "assist_range")


def snake(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def main():
    data = json.load(open(os.path.join(ROOT, "wwo_data.json")))
    weapons = {w["id"]: w for w in data["weapons"]}
    burst = weapons["burstrifle"]
    # An existing one is refilled in place (deleting and copying again in one commandlet run fails).
    asset = eal.load_asset(TARGET) if eal.does_asset_exist(TARGET) else eal.duplicate_asset(SOURCE, TARGET)
    if not asset:
        raise RuntimeError("couldn't copy %s" % SOURCE)
    saved = {}
    path = os.path.join(PROJECT, "Saved", "WWO", "SavedSettings.json")
    if os.path.exists(path):
        saved = json.load(open(path)).get("weapon/rifle", {})
    for key, value in saved.items():
        try:
            asset.set_editor_property(snake(key), float(value))
        except Exception as e:
            unreal.log_warning("burst rifle: skip saved %s (%s)" % (key, e))
    for key in OWN:
        try:
            asset.set_editor_property(key, burst[key])
        except Exception as e:
            unreal.log_warning("burst rifle: skip %s (%s)" % (key, e))
    eal.save_loaded_asset(asset)
    unreal.log("burst rifle: saved %s (id %s, %s, burst %s at %s rpm, %s bursts/min, damage %s, range %s m)" % (
        TARGET, asset.get_editor_property("id"), asset.get_editor_property("display_name"), asset.get_editor_property("burst"),
        asset.get_editor_property("burst_rate"), asset.get_editor_property("fire_rate"), asset.get_editor_property("damage"),
        asset.get_editor_property("fire_range")))


main()
