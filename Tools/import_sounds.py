# Imports every .wav in Tools/audio_src into /Game/WWO/Audio as a SoundWave (same name).
# Config/Sounds.json says which game event plays which of them.
#
# Run headless:
#   UnrealEditor-Cmd <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
import os
import unreal

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audio_src")
DEST = "/Game/WWO/Audio"
tasks = []
for name in sorted(os.listdir(SRC)):
    if not name.lower().endswith(".wav"):
        continue
    task = unreal.AssetImportTask()
    task.filename = os.path.join(SRC, name)
    task.destination_path = DEST
    task.automated = True
    task.replace_existing = True
    task.save = True
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
done = [t for t in tasks if unreal.EditorAssetLibrary.does_asset_exist("%s/%s" % (DEST, os.path.splitext(os.path.basename(t.filename))[0]))]
open(__file__.replace(".py", ".log"), "w").write("imported %d of %d sounds into %s\n" % (len(done), len(tasks), DEST))
