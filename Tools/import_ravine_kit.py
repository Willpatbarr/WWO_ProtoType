"""Imports the Ravine art kit (the FootRace map's dressing) and makes its material instances.

Source: ~/Documents/WWO_FootRace/export (WWO_RAVINE_SRC), made in Blender by ~/Documents/WWO_FootRace/tools
(make_ravine_struct.py, make_ravine_foliage.py; brief: ~/Documents/WWO_FootRace/SPEC.md): SM_Rav_<Name>.fbx with
its _LOD1 / _LOD2 files, textures/T_Rav_<Set>_D/N/ORM.png.

Run headless from the project folder, after Tools/import_cascade_kit.py (this kit wears Cascade's materials:
M_WWO_CasStone, M_WWO_CasRock, M_WWO_CasFoliage, M_WWO_CasBark, and its rock, ashlar, moss and noise textures):
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/import_ravine_kit.py -unattended -nullrhi
WWO_CAS_ONLY=materials (or textures, meshes) redoes just that part.

Everything lands in /Game/WWO/Maps/RavineKit (Textures, Meshes, Materials). No new parent materials: instances
MI_Rav_<slot> of Cascade's parents with this kit's textures (SLOTS below), assigned to the meshes by slot name.
Writes Tools/import_ravine_kit.log. Safe to rerun.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ["WWO_KIT_LIB"] = "1"
import import_cascade_kit as kit  # noqa: E402
import unreal  # noqa: E402

CAS_TEX = "/Game/WWO/Maps/CascadeKit/Textures/"
kit.SRC = os.path.expanduser(os.environ.get("WWO_RAVINE_SRC", "~/Documents/WWO_FootRace/export"))
kit.ROOT = "/Game/WWO/Maps/RavineKit"
kit.TEX, kit.MESHES, kit.MATS = kit.ROOT + "/Textures", kit.ROOT + "/Meshes", kit.ROOT + "/Materials"
kit.PREFIX = "Rav"
MOSS = (0.5, 0.66, 0.36)


def parent(name):
    m = unreal.load_asset("/Game/WWO/Materials/" + name)
    if not m:
        raise RuntimeError("missing %s: run Tools/import_cascade_kit.py first" % name)
    return m


def surface(name, tint, **scalars):
    """Cascade's world-projected stone wearing one of this kit's texture sets."""
    return kit.instance("MI_Rav_" + name, parent("M_WWO_CasStone"), scalars=scalars, colors={"Tint": tint, "MossTint": MOSS},
                        textures={"StoneColor": kit.tex("T_Rav_%s_D" % name), "StoneNormal": kit.tex("T_Rav_%s_N" % name),
                                  "StoneORM": kit.tex("T_Rav_%s_ORM" % name)})


def make_materials():
    leaf = {"LeafColor": kit.tex("T_Rav_Leaves_D")}
    out = {
        "Rav_Concrete": surface("Concrete", (1.0, 1.0, 0.97), StoneSize=600.0, MossAmount=1.3, Streaks=0.9, Variation=0.3),
        "Rav_Forest": surface("Forest", (0.7, 0.72, 0.62), StoneSize=500.0, MossAmount=0.0, Joints=0.0, Streaks=0.0, Variation=0.3),
        "Rav_Streambed": surface("Streambed", (1.0, 1.0, 1.0), StoneSize=400.0, MossAmount=0.4, Joints=0.0, Streaks=0.0),
        "Rav_Iron": surface("Iron", (1.1, 1.05, 1.0), StoneSize=400.0, MossAmount=0.0, Joints=0.0, Metal=0.8, Streaks=0.3),
        # Cascade's rock and ashlar, mossier and greener for the forest.
        "Rav_Rock": kit.instance("MI_Rav_Rock", parent("M_WWO_CasRock"), scalars={"MossAmount": 1.9, "MossSlope": 0.3},
                                 colors={"Tint": (1.35, 1.35, 1.25), "MossTint": MOSS}),
        "Rav_Stone": kit.instance("MI_Rav_Stone", parent("M_WWO_CasStone"), scalars={"StoneSize": 600.0, "MossAmount": 1.5},
                                  colors={"Tint": (1.5, 1.48, 1.4), "MossTint": MOSS}),
        "Rav_Wood": kit.instance("MI_Rav_Wood", parent("M_WWO_CasBark"), colors={"Tint": (1.2, 1.15, 1.0)},
                                 textures={"BarkColor": kit.tex("T_Rav_Bark_D"), "BarkNormal": kit.tex("T_Rav_Bark_N")}),
        "Rav_Leaf": kit.instance("MI_Rav_Leaf", parent("M_WWO_CasFoliage"), colors={"Tint": (0.34, 0.44, 0.24)},
                                 scalars={"Backlight": 0.05, "WindAmount": 5.0}, textures=leaf),
        "Rav_Mutant": kit.instance("MI_Rav_Mutant", parent("M_WWO_CasFoliage"), colors={"Tint": (0.6, 0.68, 0.66)},
                                   scalars={"Backlight": 0.35, "WindAmount": 3.0}, textures=leaf),
        "Rav_Water": unreal.load_asset("/Game/WWO/Materials/M_WWO_CascadeWater"),
        "Cas_Glow": unreal.load_asset("/Game/WWO/Maps/CascadeKit/Materials/MI_Cas_Glow"),
    }
    out["Rav_Bark"] = out["Rav_Wood"]
    # The mutants' pods and nodules: a faint cold glow, not the lamps' orange.
    out["Rav_Pod"] = kit.instance("MI_Rav_Pod", parent("M_WWO_Lamp"), scalars={"Glow": 2.2}, colors={"Color": (0.45, 1.0, 0.9)})
    kit.log("%d slot materials" % len(out))
    return out


def main():
    if not os.path.isdir(kit.SRC):
        kit.log("FAILED: no folder %s" % kit.SRC)
        return
    if kit.want("textures"):
        kit.import_textures()
    slots = make_materials()
    if kit.want("meshes"):
        kit.import_meshes(slots)


kit.run(main, "import_ravine_kit.log")
