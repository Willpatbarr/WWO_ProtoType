#pragma once

// FootRace, the game mode's rules on the FootRace map (L_FootRaceTest, or any map opened with
// ?footrace): two teams race through their own lane of PvE rooms to a boss, and a hub between the
// lanes is where they fight each other. The design: LaminarFlow, Game Modes, "PvE Race Mode - v1
// Design"; the build order: the FootRace plan (map, enemies, portals, energy, levers, boss, cores, bots).
//
// So far: teams (each player joins the smaller one), each team's spawn room, and the breach portals.
// The game state says PvP, so the teams can hurt each other and the health component respawns players
// at PvPSpawnFor (SpawnSpot here: their team's spawn room).
//
// Breach portals (World/WWOBreachPortal.h): one doorway in each lane room on the hub side, one per team
// in the hub. Stepping into one of your team's room doorways puts you in front of your team's hub
// doorway; stepping into your hub doorway puts you back in front of the room doorway you came through
// (Room1's if you never came through one). The other team's doorways do nothing for you (hijacking
// comes later). Nothing gates them yet: breach energy comes with its own commit.
//
// The level's markers (Tools/footrace_layout.py) are TargetPoints tagged with what they are and where:
// TeamSpawn Lane<n>; PortalAnchor Lane<n> Room<k>|Boss; HubPortal Lane<n>. Lane n is team n's.

#include "CoreMinimal.h"
#include "Game/WWOMatchRules.h"

class AWWOCharacter;
class AWWOBreachPortal;

class FWWOFootRaceRules final : public FWWOMatchRules
{
public:
	using FWWOMatchRules::FWWOMatchRules;

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

private:
	void SpawnPortals();
	// Steps Body through Portal: to its team's hub doorway, or back to the room doorway it came from.
	void GoThrough(AWWOCharacter* Body, int32 Team, AWWOBreachPortal* Portal);
	static void Teleport(AWWOCharacter* Body, const FTransform& Spot);

	TArray<TWeakObjectPtr<AWWOBreachPortal>> Portals;
	TSet<TWeakObjectPtr<AWWOCharacter>> Placed;                                    // bodies put in their spawn room
	TMap<TWeakObjectPtr<AWWOCharacter>, TWeakObjectPtr<AWWOBreachPortal>> CameFrom; // the room doorway each one entered the hub by
	TMap<TWeakObjectPtr<AWWOCharacter>, double> LastThrough;                        // when each last went through a doorway
};
