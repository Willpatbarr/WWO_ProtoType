#!/bin/bash
# The regression scenarios (-WWORegress, see docs/TESTING.md "Regression scenarios").
#   Tools/regress.sh [both|norender|render] [extra switches...]
#   e.g. Tools/regress.sh                      both modes, compared with Tools/regress/golden.json
#        Tools/regress.sh norender -WWORegressOnly=perk.
#        Tools/regress.sh both -WWORegressUpdate (an intended behaviour change only, in its own commit)
# Closes a running game first. Prints the DIFF lines and the result of each mode; in "both", also
# fails if the two modes disagree on a scenario that is meant to give the same numbers in both.
# Exit code 0 only if every mode passed. Never builds. UE (engine folder) can be set in the environment.
UE="${UE:-/Volumes/UnrealSSD/Epic Games/UE_5.8}"
PROJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="$PROJ_DIR/WhatWasOurs.uproject"
BIN="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
MODE="both"
case "$1" in both|norender|render) MODE="$1"; shift;; esac
OUT="$PROJ_DIR/Saved/Regress"
mkdir -p "$OUT"
LIMIT="${REGRESS_TIMEOUT:-2400}" # seconds before a stuck run is killed (the window mode runs at the display's 60 Hz: about real time)

# The game's pids: UnrealEditor processes (by process name, not just command line) running this
# project with -game. A shell whose command line merely contains that text (this script's caller, a
# grep, an editor) is never matched, nor this script or any of its parents.
game_pids() {
	local skip=" $$ " p=$$
	while [ -n "$p" ] && [ "$p" -gt 1 ] 2> /dev/null; do p=$(ps -o ppid= -p "$p" 2> /dev/null | tr -d ' '); skip="$skip$p "; done
	local pid
	for pid in $(pgrep -f "UnrealEditor.*WhatWasOurs\.uproject.*-game"); do
		case "$skip" in *" $pid "*) continue;; esac
		[ "$(basename "$(ps -o comm= -p "$pid" 2> /dev/null)")" = "UnrealEditor" ] && echo "$pid"
	done
}

close_game() {
	local pids=$(game_pids)
	[ -z "$pids" ] && return
	kill $pids 2> /dev/null
	for _ in 1 2 3 4 5 6 7 8 9 10; do [ -z "$(game_pids)" ] && return; sleep 1; done
	pids=$(game_pids); [ -n "$pids" ] && kill -9 $pids 2> /dev/null
}

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
	grep -E "Regress DIFF|Regress: saved loadout|Regress: golden" "$log" | sed 's/^\[[^]]*\]\[[^]]*\]//; s/^LogWWO: \(Warning: \|Display: \)\{0,1\}//'
	local result=$(grep -E "Regress result:" "$log" | tail -1 | sed 's/.*Regress result:/Regress result:/')
	[ -z "$result" ] && result="Regress result: FAIL (no result line: the run crashed or was killed)" && code=1
	echo "[$mode] $result, exit $code, ${took} s wall"
	return $code
}

status=0
modes=("$MODE"); [ "$MODE" = "both" ] && modes=(norender render)
for m in "${modes[@]}"; do run_mode "$m" "$@" || status=1; done

if [ "$MODE" = "both" ]; then
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
print("Regress modes: %s" % ("agree" if bad == 0 else "DISAGREE (%d)" % bad))
sys.exit(1 if bad else 0)
PY
fi
close_game
exit $status
