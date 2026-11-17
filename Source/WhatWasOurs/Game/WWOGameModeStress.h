#pragma once

// The game mode's stress and screenshot harness (Game/WWOGameModeStress.cpp): test code, not game
// rules. AWWOGameMode makes one only when the command line has a WWOStress switch, and calls it
// from two places in its tick.

#include "CoreMinimal.h"

class AWWOGameMode;

struct FWWOStressHarness
{
	// Called from the game mode's Tick in PvP, before the roster adds the next bot. Any switch containing "WWOStress":
	// the "Bots:" lines every 10 s, and -WWOStressShots' pictures.
	void LogBots(AWWOGameMode& GM);
	// Called once a tick, after the match and bots have updated. -WWOStress exactly: the load lines
	// every 5 s, the staged screenshot and kit runs, the GPU profile, and the exit after -WWOStressTime=.
	void Tick(AWWOGameMode& GM, float DeltaSeconds);

private:
	float BotLogNext = 10.f, BotLogClock = 0.f;
	float StressGame = 0.f, StressRender = 0.f, StressGpu = 0.f; // ms summed since the last stress line
	float StressClock = 0.f, StressFrames = 0.f, StressWorst = 0.f, StressNext = 5.f;
};
