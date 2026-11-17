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
// - (Later: it resets when the team opens a door; the hijack costs and bonuses.)
//
// The level's markers (Tools/footrace_layout.py) are TargetPoints tagged with what they are and where:
// TeamSpawn Lane<n>; PortalAnchor Lane<n> Room<k>|Boss; HubPortal Lane<n>. Lane n is team n's.

#include "CoreMinimal.h"
#include "Game/WWOMatchRules.h"

class AWWOCharacter;
class AWWOBreachPortal;
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

private:
	void SpawnPortals();
	// Steps Body through Portal: to its team's hub doorway, or back to the room doorway it came from.
	void GoThrough(AWWOCharacter* Body, int32 Team, AWWOBreachPortal* Portal);
	// The hub doorway's way out: back in front of the room doorway Body came in by.
	void LeaveHub(AWWOCharacter* Body, int32 Team);
	void BindDeaths();                       // each new player's and enemy's death, once
	void OnPlayerDied(AWWOCharacter* Victim);
	void OnEnemyDied(AWWOEnemy* Enemy);
	void Drain(float DeltaSeconds);
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
	TMap<TWeakObjectPtr<AWWOCharacter>, TWeakObjectPtr<AWWOBreachPortal>> CameFrom; // the room doorway each one entered the hub by
	TMap<TWeakObjectPtr<AWWOCharacter>, double> LastThrough;                        // when each last went through a doorway
	TSet<TWeakObjectPtr<AWWOCharacter>> InHub;                                      // breachers
	TSet<TWeakObjectPtr<AWWOCharacter>> PlayersBound;
	TSet<TWeakObjectPtr<AWWOEnemy>> EnemiesBound;
	TArray<TPair<TWeakObjectPtr<UWWOHealthComponent>, FDelegateHandle>> PlayerBindings; // removed when these rules go
	TArray<TPair<TWeakObjectPtr<AWWOEnemy>, FDelegateHandle>> EnemyBindings;
};
