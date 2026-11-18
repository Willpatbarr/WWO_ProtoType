#pragma once

// The rules of one kind of match, run by the game mode on the host only. AWWOGameMode is the shell:
// it handles logins, spawning players, map changes, the status line and the restart timer, and it
// owns exactly one rules object, chosen in InitGame from the map's options:
//   FWWOWaveRules       PvE waves (the default; idle when the map has no enemy spawn points or Enabled is 0)
//   FWWOTeamDeathmatch  PvP (?pvp): teams, kill credit and score, the kill and time limits
//   FWWOTesterRules     the firing tester (?tester, -WWOTester): one dummy, nothing to fight
//   FWWOFootRaceRules   FootRace (?footrace, or a map named FootRace): two lanes of PvE rooms and a PvP hub
// plus the bot roster (FWWOBotRoster, PvP only). Replicated match state stays on AWWOGameState and
// the player states; a rules object writes it, nothing else about replication changes.
//
// Plain C++ objects, not UObjects: they hold the actors they track by weak pointer, and remove their
// own death bindings when destroyed. The game mode calls them in a fixed order each tick (see
// AWWOGameMode::Tick): Update where the mode's bookkeeping always ran (before the bots, the status
// line and a due restart), Advance last (the wave state machine).

#include "CoreMinimal.h"
#include "WWOMatchRules.generated.h"

class AWWOGameMode;
class AWWOCharacter;
class AController;
class APlayerController;
class FWWOWaveRules;
class FWWOTeamDeathmatch;
class FWWOTesterRules;
class FWWOFootRaceRules;

// The match's phase. Waves: Idle, Countdown, Spawning, Won, Lost. Team deathmatch: Idle (before
// Begin), Match, MatchOver. The tester: Idle.
UENUM()
enum class EWWOWaveState : uint8 { Idle, Countdown, Spawning, Won, Lost, Match, MatchOver };

class FWWOMatchRules
{
public:
	explicit FWWOMatchRules(AWWOGameMode& InMode) : Mode(InMode) {}
	virtual ~FWWOMatchRules() = default;
	FWWOMatchRules(const FWWOMatchRules&) = delete;
	FWWOMatchRules& operator=(const FWWOMatchRules&) = delete;

	EWWOWaveState State = EWWOWaveState::Idle; // the match's phase (AWWOGameMode::MatchState)

	// StartPlay: clears what this mode doesn't use from the level and sets the first state.
	virtual void Begin() = 0;
	// Every tick, after the advertised game is refreshed: the mode's bookkeeping (binding deaths, clocks).
	virtual void Update(float DeltaSeconds) {}
	// Every tick, last: after the status line and a due restart (the waves' state machine).
	virtual void Advance(float DeltaSeconds) {}
	// PostLogin, after the engine's own.
	virtual void OnPlayerJoined(APlayerController* NewPlayer) {}
	// Logout: before the engine's own (the leaver still counts), and after it.
	virtual void OnPlayerLeaving(AController* Exiting) {}
	virtual void OnPlayerLeft(AController* Exiting) {}
	// The match is decided (won, lost, a match over): what is left is the restart or the host's pick.
	virtual bool IsOver() const { return false; }
	// The line every player's HUD shows (AWWOGameState::StatusLine) and its colour.
	virtual FString StatusText(FLinearColor& OutColor) const { OutColor = FLinearColor::White; return FString(); }

	virtual FWWOWaveRules* AsWaves() { return nullptr; }
	virtual FWWOTeamDeathmatch* AsDeathmatch() { return nullptr; }
	virtual FWWOTesterRules* AsTester() { return nullptr; }
	virtual FWWOFootRaceRules* AsFootRace() { return nullptr; }

protected:
	AWWOGameMode& Mode;
};
