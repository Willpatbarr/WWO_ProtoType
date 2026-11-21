"""Gives the anchor grafts' copied pieces their detail levels: VoltVine's leaves (SM_VV_Leaf01-06,
Tools/import_vine_pieces.py) and every flora kit's pieces (/Game/WWO/Flora/<Flora>/Kit/SM_*,
Tools/import_graft_kit.py) get LODs 1-3, reduced by the engine's mesh reducer to PERCENT of LOD 0's triangles.

The game never draws these meshes as they are: it copies one of their LODs into the meshes it builds per bone
(Characters/WWOVineGraft.cpp, ReadShape), a coarser one for each farther detail level of the grafts (the
recipes' "detail" in Config/GraftRecipes.json pick which: leaf_lod, kit_lod). So the vertices stay on the CPU
and the meshes never stream (every LOD must be readable in a packaged build).

Run headless from the project folder (after the two imports; they rebuild the meshes with one LOD, so run this
again after either):
  UnrealEditor-Cmd WhatWasOurs.uproject -run=pythonscript -script=<abs path>/Tools/make_graft_piece_lods.py -unattended -nullrhi

Writes Tools/make_graft_piece_lods.log: each mesh's triangles per LOD. Safe to rerun (LODs 1-3 are made again
from LOD 0).
"""
import os

import unreal

FOLDERS = ["/Game/WWO/Characters/RobotFrame/Grafts/Pieces", "/Game/WWO/Flora"]
PREFIXES = ("SM_VV_Leaf", "SM_VS_", "SM_IB_")
PERCENT = [1.0, 0.30, 0.10, 0.04]   # LOD 0 (kept), then 1-3
SCREEN = [1.0, 0.5, 0.25, 0.1]      # (only if one is ever drawn as it is)
eal = unreal.EditorAssetLibrary
REPORT = []


def log(msg):
    REPORT.append(msg)
    unreal.log_warning("WWO piece LODs: " + msg)


def set_lods(mesh):
    # The editor subsystem where it is loaded; in a commandlet it is not, and the (older) library does the same.
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if sub is None:  # its module is not loaded in a commandlet
        unreal.SystemLibrary.execute_console_command(None, "Module Load StaticMeshEditor")
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if sub is not None:
        options, item = unreal.StaticMeshReductionOptions(), unreal.StaticMeshReductionSettings
    else:
        options, item = unreal.EditorScriptingMeshReductionOptions(), unreal.EditorScriptingMeshReductionSettings
    settings = []
    for pct, screen in zip(PERCENT, SCREEN):
        s = item()
        s.set_editor_property("percent_triangles", pct)
        s.set_editor_property("screen_size", screen)
        settings.append(s)
    options.set_editor_property("auto_compute_lod_screen_size", False)
    options.set_editor_property("reduction_settings", settings)
    return sub.set_lods(mesh, options) if sub is not None else unreal.EditorStaticMeshLibrary.set_lods(mesh, options)


def main():
    done = 0
    for folder in FOLDERS:
        for path in eal.list_assets(folder, recursive=True, include_folder=False):
            name = path.split("/")[-1].split(".")[0]
            if not name.startswith(PREFIXES):
                continue
            mesh = eal.load_asset(path)
            if not isinstance(mesh, unreal.StaticMesh):
                continue
            got = set_lods(mesh)
            mesh.set_editor_property("allow_cpu_access", True)
            mesh.set_editor_property("never_stream", True)
            eal.save_loaded_asset(mesh)
            tris = [mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())]
            log("%s: %d LODs (set_lods %d), triangles %s" % (name, mesh.get_num_lods(), got, tris))
            done += 1
    log("done: %d meshes" % done)


try:
    main()
finally:
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_graft_piece_lods.log"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
