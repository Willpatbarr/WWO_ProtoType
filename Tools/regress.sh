#!/bin/bash
# The regression scenarios (-WWORegress, see docs/TESTING.md "Regression scenarios").
#   Tools/regress.sh [norender|drawn-only|both|render|update-new <names>] [extra switches...]
#   e.g. Tools/regress.sh norender             the default check: no rendering, every scenario (about 20 s)
#        Tools/regress.sh drawn-only           the drawn window, only the scenarios with per-mode golden
#                                              entries (names@render): a change to view, marks, real throw, HUD
#        Tools/regress.sh update-new area.,hp. new scenarios: no-draw run of everything, then the window run
#                                              of those names only; adds their missing golden entries, never
#                                              touches an existing one, and checks the two modes agree on them
#        Tools/regress.sh norender -WWORegressOnly=perk.
#        Tools/regress.sh both                 both modes, every scenario (about 14 min; when the owner asks)
#        Tools/regress.sh both -WWORegressUpdate (an intended behaviour change only, in its own commit)
# Closes a running game first. Prints the DIFF lines and the result of each mode; in "both" and
# "update-new", also fails if the two modes disagree on a scenario meant to give the same numbers in both.
# Exit code 0 only if every mode passed. Never builds. UE (engine folder) can be set in the environment.
UE="${UE:-/Volumes/UnrealSSD/Epic Games/UE_5.8}"
PROJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="$PROJ_DIR/WhatWasOurs.uproject"
BIN="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
MODE="both"
case "$1" in both|norender|render|drawn-only) MODE="$1"; shift;; update-new) MODE="$1"; NEW="$2"; shift 2;; esac
if [ "$MODE" = "update-new" ] && [ -z "$NEW" ]; then echo "update-new needs the new scenarios' names or prefixes (comma-separated)"; exit 2; fi
OUT="$PROJ_DIR/Saved/Regress"
mkdir -p "$OUT"
LIMIT="${REGRESS_TIMEOUT:-2400}" # seconds before a stuck run is killed (the window mode runs at the display's 60 Hz: about real time)

# game_pids and close_game: only the game's UnrealEditor processes, never a shell (Tools/close_game.sh).
source "$PROJ_DIR/Tools/close_game.sh"

run_mode() { # $1 = norender | render, then extra switches
	local mode="$1"; shift
	local view
	if [ "$mode" = "render" ]; then view=(-windowed -ResX=640 -ResY=360); else view=(-nullrhi); fi
	local log="$OUT/run-$mode.log"
	close_game
	local started=$(date +%s)
	( sleep "$LIMIT"; pids=$(game_pids); [ -n "$pids" ] && kill -9 $pids ) > /dev/null 2>&1 &
	local watchdog=$!
	"$BIN" "$PROJ" /Game/WWO/Maps/L_TestRange -game "${view[@]}" -nosound -WWORegress "$@" -abslog="$log" > /dev/null 2>&1
	local code=$?
	pkill -P "$watchdog" sleep 2> /dev/null; kill "$watchdog" 2> /dev/null; wait "$watchdog" 2> /dev/null
	local took=$(( $(date +%s) - started ))
	grep -E "Regress DIFF|Regress: saved loadout|Regress: golden|Regress: [0-9]+ scenarios run" "$log" | sed 's/^\[[^]]*\]\[[^]]*\]//; s/^LogWWO: \(Warning: \|Display: \)\{0,1\}//'
	local result=$(grep -E "Regress result:" "$log" | tail -1 | sed 's/.*Regress result:/Regress result:/')
	[ -z "$result" ] && result="Regress result: FAIL (no result line: the run crashed or was killed)" && code=1
	echo "[$mode] $result, exit $code, ${took} s wall"
	return $code
}

status=0
case "$MODE" in
	both) run_mode norender "$@" || status=1; run_mode render "$@" || status=1;;
	drawn-only) run_mode render -WWORegressPerModeOnly "$@" || status=1;;
	update-new)
		run_mode norender -WWORegressUpdateNew "$@" || status=1
		run_mode render -WWORegressUpdateNew -WWORegressOnly="$NEW" "$@" || status=1;;
	*) run_mode "$MODE" "$@" || status=1;;
esac

if [ "$MODE" = "both" ] || [ "$MODE" = "update-new" ]; then
	# Scenarios without a per-mode golden entry must agree between the modes, value for value.
	python3 - "$OUT/results-norender.json" "$OUT/results-render.json" <<'PY' || status=1
import json, sys
a, b = (json.load(open(p)) for p in sys.argv[1:3])
bad = 0
for name in sorted(set(a) & set(b)):
    if "@" in name: continue
    for key in sorted(set(a[name]) | set(b[name])):
        if a[name].get(key) != b[name].get(key):
            print("Regress MODES DIFF %s.%s: norender %s, render %s" % (name, key, a[name].get(key), b[name].get(key))); bad += 1
print("Regress modes: %s" % ("agree" if bad == 0 else "DISAGREE (%d): mark them PerMode, or find what drawing changes" % bad))
sys.exit(1 if bad else 0)
PY
fi
close_game
exit $status
