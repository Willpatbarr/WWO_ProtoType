#pragma once

// The bot players of a PvP match (AWWOBotController): how many the match wants, adding them one a
// tick on the smaller team at a spot away from the other team, one making room for a person who
// joins, one more for a person who leaves, and their health following the Bots settings. The game
// mode owns one (AWWOGameMode::Roster) and runs it in team deathmatch only. Plain data and steps, no
// UObject: the bots themselves are actors the world keeps.

#include "CoreMinimal.h"

class AWWOGameMode;
class AController;
class FWWOTeamDeathmatch;
class FWWOMatchRules;

struct FWWOBotRoster
{
	int32 BotsWanted = 0;  // -WWOBots=N
	int32 BotsSpawned = 0;
	int32 BotsExtra = 0;   // +1 for each person who left this match, -1 for each who joined it since
	int32 OptionBots = -1; // ?bots=N from the launch screen (-1: none given); kept across restarts

	// How many bots the match should have now.
	int32 Wanted(const AWWOGameMode& GM) const;
	// One bot a call, once the host's own player is in, while the match is on and short of Wanted.
	// The match's rules: team deathmatch, or FootRace (its bots are given their team here; FootRace places them).
	void Update(AWWOGameMode& GM, FWWOMatchRules& Match);
	// A person joined mid-match: one bot leaves, from the bigger team, so the match stays its size.
	void RetireFor(AWWOGameMode& GM, AController* Joiner);
	// A person left mid-match: a bot takes the place (Update adds it).
	void PersonLeft() { ++BotsExtra; }
	// Bots' health and shield follow the Bots settings (a player's own are on the Player page).
	void FollowSettings(AWWOGameMode& GM) const;
};
