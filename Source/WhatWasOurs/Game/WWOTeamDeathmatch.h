#pragma once

// Team deathmatch, the game mode's rules when the map was opened with ?pvp. Players join the
// smaller team, kill the other team's players, respawn after the game mode's PvPRespawnTime away
// from them; the first team to KillTarget points wins (a precision kill scores 2), or the most when
// TimeLimit runs out (either can be 0 = off). Then the scoreboard (a test run restarts by itself).
// The match's bots are the game mode's roster (FWWOBotRoster); the tunables are the game mode's
// (its PvP category: Settings binds them there). The score, the match's end and its winner are
// AWWOGameState's replicated fields; kills, precision kills and deaths the player states'.
//
// Respawning is the health component's: it waits the game mode's PvPRespawnTime and asks the game
// mode's PvPSpawnFor (SpawnSpot here) whenever the game state says PvP.

#include "CoreMinimal.h"
#include "Game/WWOMatchRules.h"

class AWWOCharacter;
class AWWOPlayerState;
class AGameStateBase;
class UWWOHealthComponent;

class FWWOTeamDeathmatch final : public FWWOMatchRules
{
public:
	using FWWOMatchRules::FWWOMatchRules;
	virtual ~FWWOTeamDeathmatch() override;

	float MatchLeft = 0.f;    // seconds (the status line's clock)
	float MatchElapsed = 0.f; // seconds of this match played
	int32 Winner = -1;        // the team that won (-1: a draw or not over)

	virtual void Begin() override;
	// Binds each new player's death (and gives a team to one PostLogin didn't), runs the time limit.
	virtual void Update(float DeltaSeconds) override;
	// A person arriving: a team (mid-match: a bot makes room first, and they start at a spawn spot).
	virtual void OnPlayerJoined(APlayerController* NewPlayer) override;
	// A person leaving mid-match: a bot takes the place.
	virtual void OnPlayerLeaving(AController* Exiting) override;
	virtual bool IsOver() const override { return State == EWWOWaveState::MatchOver; }
	virtual FString StatusText(FLinearColor& OutColor) const override;
	virtual FWWOTeamDeathmatch* AsDeathmatch() override { return this; }

	// A player's death: counted, then the kill credited to whoever hit them last (OnKill).
	void OnDeath(AWWOCharacter* Victim);
	void EndMatch(int32 WinningTeam);
	// A player asks to change sides: only onto a team no bigger than theirs would become, at most
	// every 10 s, and they start over at a spawn spot (no switching mid-fight to dodge a shot).
	void SwitchTeam(AController* Player);

	static void SetTeam(AWWOPlayerState* PS, int32 Team);
	static void BalanceTeam(const AGameStateBase* GameState, AController* Player); // the smaller team
	// The spawn spot (enemy spawn points and player starts) farthest from the nearest living player
	// of the other team, one of the best three at random, facing the middle of the map.
	static FTransform SpawnSpot(const AWWOGameMode& GM, const AWWOCharacter* Player);

	TSet<TWeakObjectPtr<AWWOPlayerState>> TeamGiven;  // players a team was already given (PostLogin, the roster)

private:
	// The kill: points (2 for a precision kill), the player states, the kill feed, the kill limit.
	void OnKill(AWWOCharacter* Victim, AWWOPlayerState* VictimPS, AWWOPlayerState* KillerPS);

	TSet<TWeakObjectPtr<AWWOCharacter>> PvPBound; // players whose death the match scores
	TArray<TPair<TWeakObjectPtr<UWWOHealthComponent>, FDelegateHandle>> Bindings; // removed when these rules go

	friend class FWWORegress;
};
