# Makes /Game/WWO/Meshes/SM_Rifle_NoSights: Lyra's rifle with its iron sights gone, for the rifle
# and the burst rifle (Config/WeaponModels.json), whose sight mods are holo sights.
# The Lyra mesh is one welded shell, so nothing can be lifted off it cleanly: the sights are
# pressed flat onto the top rail instead (every vertex above the rail, at the rear ring and the
# front post, moves down to the rail's height), which leaves no holes.
# Mesh space: y along the barrel (the muzzle at +y), z up, cm before the model's scale.
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<full path>/Tools/create_rifle_nosights.py
import unreal

SOURCE = "/Game/Weapons/Rifle/Mesh/SM_Rifle"
TARGET = "/Game/WWO/Meshes/SM_Rifle_NoSights"
RAIL_TOP = 7.7                           # the top rail's height
SIGHTS = ((-22.0, -11.0), (10.5, 20.0))  # y ranges: the rear ring, the front post
eal = unreal.EditorAssetLibrary


def main():
    # An existing one is remade from the source's shape (deleting and copying in one run fails).
    mesh = eal.load_asset(TARGET) if eal.does_asset_exist(TARGET) else eal.duplicate_asset(SOURCE, TARGET)
    if not mesh:
        raise RuntimeError("couldn't copy %s" % SOURCE)
    source = unreal.load_asset(SOURCE)
    desc = source.get_static_mesh_description(0)
    moved = 0
    seen = 0
    index = 0
    count = desc.get_vertex_count()
    while seen < count and index < count * 4:
        vertex = unreal.VertexID(index)
        index += 1
        if not desc.is_vertex_valid(vertex):
            continue
        seen += 1
        p = desc.get_vertex_position(vertex)
        if p.z > RAIL_TOP and any(lo <= p.y <= hi for lo, hi in SIGHTS):
            desc.set_vertex_position(vertex, unreal.Vector(p.x, p.y, RAIL_TOP))
            moved += 1
    mesh.build_from_static_mesh_descriptions([desc])
    mesh.set_editor_property("static_materials", source.get_editor_property("static_materials"))
    eal.save_loaded_asset(mesh)
    unreal.log("rifle without sights: %d of %d vertices pressed down to the rail, saved %s (top now %.1f)" % (
        moved, seen, TARGET, mesh.get_bounds().origin.z + mesh.get_bounds().box_extent.z))


main()
