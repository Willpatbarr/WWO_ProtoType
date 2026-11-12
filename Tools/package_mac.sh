#!/bin/bash
# Packages a Mac build of What Was Ours (Development) as /Volumes/UnrealSSD/Builds/WhatWasOurs/WhatWasOurs.app
# (one app with everything in it; copy it to another Mac to play there).
# Ships your current Settings values (Saved/WWO/SavedSettings.json) as the build's starting defaults
# (it copies them over Config/ShippedSettings.json, which is tracked: commit that change or not as you mean to).
# Close any running game first (it competes for memory and CPU). Takes a while the first time
# (shaders); later runs reuse the derived data cache.
set -e
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="/Volumes/UnrealSSD/Epic Games/UE_5.8/Engine"
OUT="/Volumes/UnrealSSD/Builds/WhatWasOurs"
pkill -f "WhatWasOurs.uproject.*-game" || true
# Config/ShippedSettings.json is in git (the tuning every build starts from). This Mac's saved
# Settings values replace it when there are any; on a fresh clone the one in git ships as it is.
if [ -f "$PROJECT_DIR/Saved/WWO/SavedSettings.json" ]; then
  cp "$PROJECT_DIR/Saved/WWO/SavedSettings.json" "$PROJECT_DIR/Config/ShippedSettings.json"
fi
"$ENGINE/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PROJECT_DIR/WhatWasOurs.uproject" \
  -platform=Mac -clientconfig=Development -build -cook -stage -pak \
  -noP4 -utf8output -nocompileeditor -skipbuildeditor
# UAT's -archive copies the bare compiled app on Mac (no content or libraries); the staged app
# (Saved/StagedBuilds/Mac/WhatWasOurs.app, content in Contents/UE) is the complete one.
mkdir -p "$OUT"
rm -rf "$OUT/WhatWasOurs.app" "$OUT/Mac"
ditto "$PROJECT_DIR/Saved/StagedBuilds/Mac/WhatWasOurs.app" "$OUT/WhatWasOurs.app"
# Sign with this Mac's Apple Development identity when there is one (WWO_SIGN_ID picks another).
# An ad-hoc signature changes every build, and macOS then asks for Local Network access again on
# every Mac the new build runs on; a stable identity keeps the permission across builds.
SIGN_ID="${WWO_SIGN_ID:-$(security find-identity -v -p codesigning 2>/dev/null | awk '/Apple Development/ {print $2; exit}')}"
if [ -n "$SIGN_ID" ]; then
  ENTS="$(mktemp -t wwo_ents).plist"
  codesign -d --entitlements :- "$OUT/WhatWasOurs.app" > "$ENTS" 2>/dev/null
  codesign --force --deep --timestamp=none --entitlements "$ENTS" --sign "$SIGN_ID" "$OUT/WhatWasOurs.app"
  rm -f "$ENTS"
  echo "Signed with $SIGN_ID"
fi
echo "Packaged to $OUT/WhatWasOurs.app"
