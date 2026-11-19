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
"$PROJECT_DIR/Tools/close_game.sh"
# Config/ShippedSettings.json is in git (the tuning every build starts from). This Mac's saved
# Settings values replace it when there are any; on a fresh clone the one in git ships as it is.
if [ -f "$PROJECT_DIR/Saved/WWO/SavedSettings.json" ]; then
  cp "$PROJECT_DIR/Saved/WWO/SavedSettings.json" "$PROJECT_DIR/Config/ShippedSettings.json"
fi
# The same for key and button bindings (Config/ShippedKeybinds.json).
if [ -f "$PROJECT_DIR/Saved/WWO/Keybinds.json" ]; then
  cp "$PROJECT_DIR/Saved/WWO/Keybinds.json" "$PROJECT_DIR/Config/ShippedKeybinds.json"
fi
# WWO_CMDLINE: switches baked into the app (its UECommandLine.txt), e.g. " -FeatureLevelES31".
"$ENGINE/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PROJECT_DIR/WhatWasOurs.uproject" \
  -platform=Mac -clientconfig=Development -build -cook -stage -pak \
  -noP4 -utf8output -nocompileeditor -skipbuildeditor ${WWO_CMDLINE:+-cmdline="$WWO_CMDLINE"}
# UAT's -archive copies the bare compiled app on Mac (no content or libraries); the staged app
# (Saved/StagedBuilds/Mac/WhatWasOurs.app, content in Contents/UE) is the complete one.
mkdir -p "$OUT"
rm -rf "$OUT/WhatWasOurs.app" "$OUT/Mac"
ditto "$PROJECT_DIR/Saved/StagedBuilds/Mac/WhatWasOurs.app" "$OUT/WhatWasOurs.app"
# Sign with this Mac's Apple Development identity when there is one (WWO_SIGN_ID picks another).
# An ad-hoc signature changes every build, and macOS then asks for Local Network access again on
# every Mac the new build runs on; a stable identity keeps the permission across builds.
# A Developer ID Application certificate (paid Apple Developer account) takes over when there is one:
# the app is then signed for distribution and notarized below, so it opens on any Mac with a double-click.
DEVID="${WWO_DEVID:-$(security find-identity -v -p codesigning 2>/dev/null | awk '/Developer ID Application/ {print $2; exit}')}"
SIGN_ID="${WWO_SIGN_ID:-$(security find-identity -v -p codesigning 2>/dev/null | awk '/Apple Development/ {print $2; exit}')}"
if [ -n "$DEVID" ]; then SIGN_ID=""; fi
# A revoked certificate still signs, but macOS then kills the app at launch and removes it. Try the
# identity on a throwaway file first; when it is revoked, leave the app's ad-hoc signature alone
# (it runs anywhere; the only cost is the Local Network prompt again on each new build).
if [ -n "$SIGN_ID" ]; then
  PROBE="$(mktemp -t wwo_probe)"
  cp /usr/bin/true "$PROBE"
  codesign --force --timestamp=none --sign "$SIGN_ID" "$PROBE" 2>/dev/null || true
  if spctl -a -vv "$PROBE" 2>&1 | grep -q CERT_REVOKED; then
    echo "Signing certificate $SIGN_ID is revoked (renew it in Xcode > Settings > Accounts): keeping the ad-hoc signature"
    SIGN_ID=""
  fi
  rm -f "$PROBE"
fi
if [ -n "$SIGN_ID" ]; then
  ENTS="$(mktemp -t wwo_ents).plist"
  codesign -d --entitlements :- "$OUT/WhatWasOurs.app" > "$ENTS" 2>/dev/null
  codesign --force --deep --timestamp=none --entitlements "$ENTS" --sign "$SIGN_ID" "$OUT/WhatWasOurs.app"
  rm -f "$ENTS"
  echo "Signed with $SIGN_ID"
fi
if [ -n "$DEVID" ]; then
  APP="$OUT/WhatWasOurs.app"
  ENTS="$(mktemp -t wwo_ents).plist"
  # Hardened runtime (notarization requires it) with what an Unreal app needs to start under it.
  cat > "$ENTS" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>com.apple.security.cs.allow-jit</key><true/>
  <key>com.apple.security.cs.allow-unsigned-executable-memory</key><true/>
  <key>com.apple.security.cs.disable-library-validation</key><true/>
  <key>com.apple.security.network.client</key><true/>
  <key>com.apple.security.network.server</key><true/>
</dict></plist>
PLIST
  # Every library and helper inside first (Unreal keeps them under Contents/UE, where --deep does not
  # look), then the app itself.
  find "$APP" -type f \( -name "*.dylib" -o -name "*.so" -o -perm -u+x \) ! -path "$APP/Contents/MacOS/WhatWasOurs" -print0 |
    while IFS= read -r -d '' F; do
      if file -b "$F" | grep -q "Mach-O"; then
        codesign --force --timestamp --options runtime --entitlements "$ENTS" --sign "$DEVID" "$F"
      fi
    done
  codesign --force --timestamp --options runtime --entitlements "$ENTS" --sign "$DEVID" "$APP"
  codesign --verify --deep --strict "$APP"
  rm -f "$ENTS"
  echo "Signed with Developer ID $DEVID"
  # Notarize and staple when the credentials are stored (once, by hand):
  #   xcrun notarytool store-credentials wwo-notary --apple-id <apple id> --team-id <team id>
  PROFILE="${WWO_NOTARY_PROFILE:-wwo-notary}"
  if xcrun notarytool history --keychain-profile "$PROFILE" >/dev/null 2>&1; then
    ZIP="$OUT/WhatWasOurs-notarize.zip"
    ditto -c -k --keepParent "$APP" "$ZIP"
    xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait
    rm -f "$ZIP"
    xcrun stapler staple "$APP"
    spctl -a -vv "$APP"
    echo "Notarized and stapled"
  else
    echo "Not notarized: no '$PROFILE' credentials in the keychain (see the comment above this line in $0)"
  fi
fi
echo "Packaged to $OUT/WhatWasOurs.app"
