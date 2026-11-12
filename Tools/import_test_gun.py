# Imports a gun model for local testing only (never shipped, never committed: Content/WWO/Test is
# git-ignored). The file is a glTF binary with many small meshes; they are combined into one
# static mesh with their places baked in, so Config/WeaponModels.json can use it like any gun.
#   WWO_TEST_GUN=<path to .glb> UnrealEditor-Cmd <uproject> -run=pythonscript -script=<full path>/Tools/import_test_gun.py
import os
import unreal

SRC = os.environ.get("WWO_TEST_GUN", os.path.expanduser("~/Downloads/ace_of_spades_destiny_2.glb"))
DEST = "/Game/WWO/Test/AceOfSpades"
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log_warning("WWO test gun: " + msg)


def main():
    if not os.path.exists(SRC):
        raise RuntimeError("no file at %s" % SRC)
    if eal.does_directory_exist(DEST):
        eal.delete_directory(DEST)
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("combine_static_meshes_behavior", unreal.InterchangeCombineStaticMeshesBehavior.ALL)
    mesh.set_editor_property("import_skeletal_meshes", False)
    common = pipeline.get_editor_property("common_meshes_properties")
    for name, value in (("bake_meshes", True), ("bake_pivot_meshes", False)):
        try:
            common.set_editor_property(name, value)
        except Exception as e:
            log("common_meshes_properties has no %s (%s)" % (name, e))
    source = unreal.InterchangeManager.create_source_data(SRC)
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", True)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipeline.get_path_name())])
    manager = unreal.InterchangeManager.get_interchange_manager_scripted()
    ok = manager.import_asset(DEST, source, params)
    log("import returned %s" % ok)
    for path in eal.list_assets(DEST, recursive=True, include_folder=False):
        asset = eal.load_asset(path)
        extra = ""
        if isinstance(asset, unreal.StaticMesh):
            b = asset.get_bounds()
            extra = " origin %s extent %s, %d material slots" % (b.origin, b.box_extent, len(asset.get_editor_property("static_materials")))
        log("%s (%s)%s" % (path, type(asset).__name__, extra))
        eal.save_loaded_asset(asset)


main()
