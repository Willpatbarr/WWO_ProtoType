# Turns Nanite on for the maps' own meshes (and, with WWO_NANITE_FLORA=1, the plants). Nanite meshes
# cost almost nothing in the virtual shadow maps and the depth pass; ordinary ones are drawn again
# for every shadow level each frame (about 4-6 ms of GPU on an M4 before this).
# Meshes with a see-through material are left alone (Nanite can't draw those).
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<full path>/Tools/enable_nanite.py
import os
import unreal

eal = unreal.EditorAssetLibrary
FOLDERS = ["/Game/WWO/Maps/Arboretum", "/Game/WWO/Maps/Cascade", "/Game/WWO/Maps/Ring"]
if os.environ.get("WWO_NANITE_FLORA") == "1":
    FOLDERS.append("/Game/WWO/Flora/Meshes")
OFF = os.environ.get("WWO_NANITE_OFF") == "1"  # put them back


def see_through(mesh):
    for sm in mesh.get_editor_property("static_materials"):
        mat = sm.get_editor_property("material_interface")
        if not mat:
            continue
        base = mat.get_base_material()
        if base and base.get_editor_property("blend_mode") in (unreal.BlendMode.BLEND_TRANSLUCENT, unreal.BlendMode.BLEND_ADDITIVE, unreal.BlendMode.BLEND_MODULATE):
            return True
    return False


def full_fallback(settings):
    """The mesh drawn where Nanite is off (the Mac) is the whole mesh, not a reduced one. True if it changed anything."""
    changed = False
    for name, value in (("fallback_target", unreal.NaniteFallbackTarget.PERCENT_TRIANGLES), ("fallback_percent_triangles", 1.0), ("fallback_relative_error", 0.0)):
        try:
            if settings.get_editor_property(name) != value:
                settings.set_editor_property(name, value)
                changed = True
        except Exception as e:
            unreal.log_warning("WWO nanite: no setting %s (%s)" % (name, e))
    return changed


def main():
    changed = 0
    # The kit's meshes came in as Nanite already: they only need the full fallback.
    for folder in FOLDERS + ["/Game/WWO/Kit"]:
        for path in eal.list_assets(folder, recursive=True, include_folder=False):
            mesh = eal.load_asset(path)
            if not isinstance(mesh, unreal.StaticMesh):
                continue
            settings = mesh.get_editor_property("nanite_settings")
            was = bool(settings.get_editor_property("enabled"))
            want = was if folder == "/Game/WWO/Kit" else ((not OFF) and not see_through(mesh))
            fallback = want and full_fallback(settings)
            if was == want and not fallback:
                continue
            settings.set_editor_property("enabled", want)
            mesh.set_editor_property("nanite_settings", settings)
            for sm in mesh.get_editor_property("static_materials"):
                mat = sm.get_editor_property("material_interface")
                base = mat.get_base_material() if mat else None
                if base and want and not base.get_editor_property("used_with_nanite"):
                    base.set_editor_property("used_with_nanite", True)
                    eal.save_asset(base.get_path_name())
            eal.save_asset(path)
            changed += 1
            unreal.log_warning("WWO nanite: %s -> %s" % (path, want))
    unreal.log_warning("WWO nanite: %d meshes changed" % changed)


main()
