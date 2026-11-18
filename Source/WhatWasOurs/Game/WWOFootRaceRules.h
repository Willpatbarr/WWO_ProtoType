#pragma once

// FootRace, the game mode's rules on the FootRace map (L_FootRaceTest, or any map opened with
// ?footrace): two teams race through their own lane of PvE rooms to a boss, and a hub between the
// lanes is where they fight each other. The design: LaminarFlow, Game Modes, "PvE Race Mode - v1
// Design"; the build order: the FootRace plan (map, enemies, portals, energy, levers, boss, cores, bots).
//
// So far: teams (each player joins the smaller one), each team's spawn room, the lane rooms' enemies,
// the breach portals and breach energy.
//
// Lane rooms (RoomArea markers: Spawn, Room1-3, Boss, in that order along the lane): a team's furthest
// room, the furthest any of its players has stood in, is its live room; Room1 is live from the start.
// The live room fills to RoomEnemies at the room's EnemySpawn markers (one every RoomSpawnStagger s) and
// refills one every RoomRefillTime s after a death; rooms the team has moved past keep what is left but
// don't refill, the boss room has none (the boss comes later). Each enemy guards its lane (AWWOEnemy::Lane):
// it only goes after that team's players. The kinds per room are RoomKind's (Room1 mostly Swarm, Room2
// mostly Soldiers, Room3 Soldiers with a Tank first).
// The game state says PvP, so the teams can hurt each other and the health component respawns players
// at PvPSpawnFor (SpawnSpot here: their team's spawn room).
//
// Breach portals (World/WWOBreachPortal.h): one doorway in each lane room on the hub side, one per team
// in the hub. Stepping into one of your team's room doorways puts you in front of your team's hub
// doorway; stepping into your hub doorway puts you back in front of the room doorway you came through
// (Room1's if you never came through one). The other team's doorways do nothing for you (hijacking
// comes later). Players in the hub are breachers; leaving through the hub doorway (or dying) ends it.
//
// Breach energy: one pool per team (AWWOGameState::BreachEnergy), the game mode's FootRace tunables.
// - Earned: EnergyPerEnemy for each AI enemy a player of the team kills (the enemy's LastHitBy), and
//   EnergyPerPlayerKill for each player of the other team they kill (the health component's LastAttacker,
//   as team deathmatch credits a kill).
// - The hub: a room doorway lets a player in only while the pool is at BreachThreshold or more.
// - Drained: BreachDrain per second for each of the team's breachers.
// - Out of energy: every breacher of that team goes back through the hub doorway to their room.
// - A breacher's death costs the team BreachDeathLoss of its pool.
// - (Later: the hijack costs and bonuses.)
//
// Doors and levers (World/WWOBreachDoor.h, WWOBreachLever.h; DoorSlot and LeverSlot markers): Door1-3
// close the doorways out of Room1-3. A door's lever in the hub is live only when its door is the
// team's next one and the team's current breach went in by the room that door leads out of (one breach
// opens at most one door). A breacher holds the Revive key at it for LeverHoldTime and it opens.
// Leaving the hub lands a player in the furthest room their team has opened.
//
// The boss (Enemies/WWOEnemyTypes.h, AWWOBoss; a BossSpawn marker per lane): spawned when the team
// opens Door3, shielded. OpenBossWindow drops the shield for a damage window, BossWindow s long the
// first time and a third of that longer each time after (t, 1.33t, 1.67t...); then the shield is back.
// The first team to kill its boss wins the match.
//
// Boss cores (World/WWOBossCore.h; the hub's CoreSpawn marker): CoreDelay s after a team's boss is up and
// shielded, a core for that team appears in the hub (on the team's side of the marker), and a wave of
// BossWave enemies in its boss room so the team can earn the energy to breach for it. Its team picks it
// up by walking over it; carried out through the team's hub doorway it opens the boss's next window. A
// carrier who dies drops it where they fell: then either team can take it, and the other team carrying
// it out through their own hub doorway heals its boss CoreHeal (up to full). A dropped core goes back to
// its spot after CoreDropTime. The next core comes CoreDelay s after the shield is back.
// (The console's wwo.BossWindow still opens a window without a core, for testing.)
//
// Two pools (the design doc's 2.1): a door opened while the team's breachers are in the hub freezes
// the pool they are on. They keep draining it; what the team earns goes to the next pool
// (AWWOGameState::BreachNext) instead, but a kill made in the hub is split evenly between the two. Nobody
// on the team can breach until that breach ends; once no one is in the hub the rest of the frozen pool
// is thrown away and the next pool becomes the pool.
//
// The level's markers (Tools/footrace_layout.py) are TargetPoints tagged with what they are and where:
// TeamSpawn Lane<n>; PortalAnchor Lane<n> Room<k>|Boss; HubPortal Lane<n>. Lane n is team n's.

#include "CoreMinimal.h"
#include "Game/WWOMatchRules.h"

class AWWOCharacter;
class AWWOBreachPortal;
class AWWOBreachDoor;
class AWWOBreachLever;
class AWWOBoss;
class AWWOBossCore;
class AWWOEnemy;
class UWWOHealthComponent;

class FWWOFootRaceRules final : public FWWOMatchRules
{
public:
	using FWWOMatchRules::FWWOMatchRules;
	virtual ~FWWOFootRaceRules() override;

	// Clears the level's enemies and AI teammates, says PvP, spawns the portals.
	virtual void Begin() override;
	// Gives a team to anyone without one, puts each new body in its team's spawn room once, and
	// takes whoever stepped into a portal through it.
	virtual void Update(float DeltaSeconds) override;
	virtual void OnPlayerJoined(APlayerController* NewPlayer) override;
	virtual FString StatusText(FLinearColor& OutColor) const override;
	virtual bool IsOver() const override { return State == EWWOWaveState::MatchOver; }
	virtual FWWOFootRaceRules* AsFootRace() override { return this; }

	// A spot in the player's team's spawn room (respawns, and a body's first placing).
	FTransform SpawnSpot(const AWWOCharacter* Player) const;

	// Seconds after going through a doorway before another one takes you (no ping-pong at the arrival spot).
	static constexpr float PortalCooldown = 0.75f;

	// The doorways, for tests and (later) the HUD: a team's room doorways by room, and its hub doorway.
	AWWOBreachPortal* RoomPortal(int32 Team, FName Room) const;
	AWWOBreachPortal* HubPortal(int32 Team) const;

	// A team's pool (0 for a team that isn't 0 or 1), and changes to it (host; never below 0).
	float Energy(int32 Team) const;
	void SetEnergy(int32 Team, float Value);
	void AddEnergy(int32 Team, float Amount, const TCHAR* Why);
	bool IsBreaching(const AWWOCharacter* Body) const { return InHub.Contains(Body); }
	int32 Breachers(int32 Team) const; // the team's living players in the hub

	// The lane rooms: index 0 Spawn, 1-3 Room1-3, 4 Boss.
	static FName RoomName(int32 Index);
	int32 FurthestRoom(int32 Team) const { return Team >= 0 && Team <= 1 ? Furthest[Team] : 0; }
	int32 AliveIn(int32 Team, FName Room) const { return EnemiesIn(Team, Room).Num(); }
	TArray<AWWOEnemy*> EnemiesIn(int32 Team, FName Room) const; // the room's living enemies
	int32 RoomAt(int32 Team, const FVector& Where) const; // which of the team's rooms a spot is in (-1: none)
	// What a room spawns next: its kind by the room and how many it has spawned.
	static FName RoomKind(int32 Index, int32 Spawned);

	// Doors and levers (Door 1-3).
	int32 DoorsOpen(int32 Team) const;
	int32 OpenedRoom(int32 Team) const { return DoorsOpen(Team) + 1; } // the furthest room it can reach: 1 Room1 .. 4 Boss
	AWWOBreachDoor* DoorOf(int32 Team, int32 Door) const;
	AWWOBreachLever* LeverOf(int32 Team, int32 Door) const;
	bool IsLeverLive(int32 Team, int32 Door) const;
	void OpenDoor(int32 Team, int32 Door);
	// The two pools.
	float NextEnergy(int32 Team) const;
	bool IsFrozen(int32 Team) const;
	// The bosses.
	AWWOBoss* BossOf(int32 Team) const { return Team >= 0 && Team <= 1 ? Bosses[Team].Get() : nullptr; }
	int32 BossCycle(int32 Team) const { return Team >= 0 && Team <= 1 ? Cycles[Team] : 0; }
	float BossWindowLeft(int32 Team) const { return Team >= 0 && Team <= 1 ? WindowLeft[Team] : 0.f; }
	float WindowLength(int32 Cycle) const; // the Cycle'th damage window (1-based)
	void OpenBossWindow(int32 Team);       // drops the team's boss shield for its next window (if it's up)
	int32 Winner = -1;
	// The boss cores.
	AWWOBossCore* CoreOf(int32 Team) const { return Team >= 0 && Team <= 1 ? Cores[Team].Get() : nullptr; }
	// A core of CoreTeam's carried out through CarrierTeam's hub doorway: CoreTeam's own opens its boss's
	// next window; the other team's heals CoreTeam's boss CoreHeal.
	void DeliverCore(int32 CoreTeam, int32 CarrierTeam);
	// Energy earned by Killer's team: to the pool, or (while frozen) to the next pool, or split in the hub.
	void CreditKill(int32 Team, float Amount, const AWWOCharacter* Killer, const TCHAR* Why);

private:
	void SpawnPortals();
	// Steps Body through Portal: to its team's hub doorway, or back to the room doorway it came from.
	void GoThrough(AWWOCharacter* Body, int32 Team, AWWOBreachPortal* Portal);
	// The hub doorway's way out: in front of the doorway of the furthest room the team has opened.
	void LeaveHub(AWWOCharacter* Body, int32 Team);
	void BindDeaths();                       // each new player's and enemy's death, once
	void OnPlayerDied(AWWOCharacter* Victim);
	void OnEnemyDied(AWWOEnemy* Enemy);
	void Drain(float DeltaSeconds);
	void SpawnDoorsAndLevers();
	void UpdateLevers(float DeltaSeconds);
	void EndBreachesDone(); // a team with nobody left in the hub: its breach is over (and a frozen pool swaps)
	void SetNext(int32 Team, float Value);
	void SpawnBoss(int32 Team);
	void UpdateBosses(float DeltaSeconds);
	void EndMatch(int32 WinningTeam);
	void UpdateCores(float DeltaSeconds);
	void SpawnCore(int32 Team);
	void SpawnBossWave(int32 Team);
	void RemoveCore(int32 Team);
	TWeakObjectPtr<AWWOBossCore> Cores[2];
	float CoreLeft[2] = { -1.f, -1.f }; // s to the team's next core (-1: not counting)
	TWeakObjectPtr<AWWOBoss> Bosses[2];
	int32 Cycles[2] = { 0, 0 };
	float WindowLeft[2] = { 0.f, 0.f };
	TArray<TWeakObjectPtr<AWWOBreachDoor>> Doors;
	TArray<TWeakObjectPtr<AWWOBreachLever>> Levers;
	TSet<int32> EnteredFrom[2]; // the rooms (1-4) the team's current breach went in by
	struct FRoom
	{
		int32 Team = 0;
		int32 Index = 0;
		FBox Area = FBox(ForceInit);
		TArray<FVector> Spawns;               // its EnemySpawn markers
		TArray<TWeakObjectPtr<AWWOEnemy>> Alive;
		int32 Spawned = 0;
		bool bFilled = false;                 // reached RoomEnemies once: from now on it refills slowly
		float SpawnLeft = 0.f;                // s to the next spawn
	};
	void FindRooms();
	void UpdateRooms(float DeltaSeconds);
	void SpawnInRoom(FRoom& Room);
	void BindEnemy(AWWOEnemy* Enemy);
	TArray<FRoom> Rooms;
	int32 Furthest[2] = { 1, 1 };
	static void Teleport(AWWOCharacter* Body, const FTransform& Spot);

	TArray<TWeakObjectPtr<AWWOBreachPortal>> Portals;
	TSet<TWeakObjectPtr<AWWOCharacter>> Placed;                                    // bodies put in their spawn room
	TMap<TWeakObjectPtr<AWWOCharacter>, double> LastThrough;                        // when each last went through a doorway
	TSet<TWeakObjectPtr<AWWOCharacter>> InHub;                                      // breachers
	TSet<TWeakObjectPtr<AWWOCharacter>> PlayersBound;
	TSet<TWeakObjectPtr<AWWOEnemy>> EnemiesBound;
	TArray<TPair<TWeakObjectPtr<UWWOHealthComponent>, FDelegateHandle>> PlayerBindings; // removed when these rules go
	TArray<TPair<TWeakObjectPtr<AWWOEnemy>, FDelegateHandle>> EnemyBindings;
};
