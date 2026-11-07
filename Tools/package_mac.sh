#!/bin/bash
# Packages a Mac build of What Was Ours (Development) as /Volumes/UnrealSSD/Builds/WhatWasOurs/WhatWasOurs.app
# (one app with everything in it; copy it to another Mac to play there).
# Ships your current Settings values (Saved/WWO/SavedSettings.json) as the build's starting defaults.
# Close any running game first (it competes for memory and CPU). Takes a while the first time
# (shaders); later runs reuse the derived data cache.
set -e
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="/Volumes/UnrealSSD/Epic Games/UE_5.8/Engine"
OUT="/Volumes/UnrealSSD/Builds/WhatWasOurs"
pkill -f "WhatWasOurs.uproject.*-game" || true
if [ -f "$PROJECT_DIR/Saved/WWO/SavedSettings.json" ]; then
  cp "$PROJECT_DIR/Saved/WWO/SavedSettings.json" "$PROJECT_DIR/Config/ShippedSettings.json"
else
  echo "{}" > "$PROJECT_DIR/Config/ShippedSettings.json"
fi
"$ENGINE/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PROJECT_DIR/WhatWasOurs.uproject" \
  -platform=Mac -clientconfig=Development -build -cook -stage -pak \
  -noP4 -utf8output -nocompileeditor -skipbuildeditor
# UAT's -archive copies the bare compiled app on Mac (no content or libraries); the staged app
# (Saved/StagedBuilds/Mac/WhatWasOurs.app, content in Contents/UE) is the complete one.
mkdir -p "$OUT"
rm -rf "$OUT/WhatWasOurs.app" "$OUT/Mac"
ditto "$PROJECT_DIR/Saved/StagedBuilds/Mac/WhatWasOurs.app" "$OUT/WhatWasOurs.app"
echo "Packaged to $OUT/WhatWasOurs.app"
