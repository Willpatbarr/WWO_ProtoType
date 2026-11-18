#!/bin/bash
# Closes a running What Was Ours game (any copy: -game runs of this project), safely.
#   Tools/close_game.sh          close it: TERM, up to 10 s, then KILL
#   Tools/close_game.sh --list   print the game's pids, close nothing
#   source Tools/close_game.sh   define game_pids and close_game for a script (Tools/regress.sh does)
# Never `pkill -f "WhatWasOurs.uproject.*-game"`: that matches any process whose command line holds
# the text, including the shell that runs it (or its caller, a wrapper, an editor), and kills it.

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
	[ -z "$pids" ] && return 0
	kill $pids 2> /dev/null
	for _ in 1 2 3 4 5 6 7 8 9 10; do [ -z "$(game_pids)" ] && return 0; sleep 1; done
	pids=$(game_pids); [ -n "$pids" ] && kill -9 $pids 2> /dev/null
	return 0
}

# Run as a command (not sourced): do it.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
	case "$1" in
		--list) game_pids;;
		*) close_game;;
	esac
fi
