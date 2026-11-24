#!/bin/bash
# Notarizes and staples an already built, Developer ID signed app, without rebuilding it:
#   Tools/notarize_mac.sh /Volumes/UnrealSSD/Builds/WhatWasOurs/WhatWasOurs.app [out.zip]
# With out.zip it also writes the stapled app zipped for a release (ditto, keeping the app folder).
# Logs in as Tools/notary_auth.sh says (an API key works with the Mac locked).
set -e
APP="${1:?usage: $0 <app> [out.zip]}"
source "$(cd "$(dirname "$0")" && pwd)/notary_auth.sh"
if ! notary_ok; then notary_help; exit 1; fi
echo "Notarizing $APP with $NOTARY_HOW"
ZIP="$(mktemp -d)/notarize.zip"
ditto -c -k --keepParent "$APP" "$ZIP"
xcrun notarytool submit "$ZIP" "${NOTARY_AUTH[@]}" --wait | tee /dev/stderr | grep -q "status: Accepted"
rm -f "$ZIP"
xcrun stapler staple "$APP"
spctl -a -vv "$APP"
if [ -n "$2" ]; then
  rm -f "$2"
  ditto -c -k --sequesterRsrc --keepParent "$APP" "$2"
  echo "Zipped to $2"
fi
echo "Notarized and stapled"
