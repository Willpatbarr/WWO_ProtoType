#pragma once

// A bot player (PvP: the host's "Bots" setting or -WWOBots=N). It drives an ordinary player
// character the way a person would: it holds buttons and pushes the stick, and the character reads
// them instead of a player controller's (AWWOCharacter::Pressed / JustPressed / MoveInput). It has
// a player state, so it has a team.
//
// What it does (tuning: AWWOGameMode's Bot* values, Settings -> Bots):
// - Moves on the navmesh (WWONav::FindPath), so it goes around walls and up stairs.
// - Sees only what's in its view cone, in range, in a clear line and not hidden by fog or Veil
//   (anything within a few meters is noticed whichever way it faces). Being shot by someone it
//   can't see tells it where they are.
// - Takes a moment to react to a new enemy, then fights: turns at a limited speed with an aim
//   error that settles the longer it's on target, and only fires when its aim is on them. Fire
//   rhythm and fighting distance follow the gun (taps, bursts, shotgun close, sniper far).
// - Remembers an enemy it lost for a while and runs to where it last saw them.
// - Otherwise roams between spawn spots, reloading when it's quiet.
// - Has its own random kit (a flora with one of its grenades and two of its abilities, two guns),
//   and uses it: punches up close, throws its grenade on an arc that lands on the enemy, uses its
//   abilities and ult when they fit the moment, and swaps to the gun that suits the distance.
// - Badly hurt, it runs to the nearest spot the enemy can't see, and waits there to recover.
// - Plays as a team: an enemy one bot sees (or that is shooting a human teammate, or that the
//   host is shooting at) is called out to teammates nearby, who come to help; it prefers the
//   enemy its teammates are already on and the weaker one; some bots escort the human on their
//   team and the rest move in pairs, the follower covering its leader's flank.
// - In FootRace (Game/WWOFootRaceRules.h), when no enemy player has its attention, it plays the race
//   (FootRaceTick): it fights its lane's AI enemies in the team's furthest room, breaches when the team
//   has the energy and a reason (a lever to pull, a core to fetch; at most two at once), in the hub picks
//   up a core it can take, pulls the live lever, and leaves; carrying a core it heads for its hub doorway.
//   In a boss window it shoots the boss. Enemy players in the hub are fought as in team deathmatch.

#include "CoreMinimal.h"
#include "AIController.h"
#include "WWOTypes.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWONav.h"
#include "WWOBotController.generated.h"

class AWWOCharacter;
class AWWOEnemy;
class FWWOFootRaceRules;

UCLASS()
class WHATWASOURS_API AWWOBotController : public AAIController
{
	GENERATED_BODY()

public:
	AWWOBotController();
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;

	bool IsHeld(EWWOAction Action) const { return Held[(int32)Action]; }
	bool WasJustPressed(EWWOAction Action) const { return Just[(int32)Action]; }
	FVector2D GetMoveAxis() const { return Move; }

	// For logs and tests.
	FString DebugState() const;
	int32 StateIndex() const { return (int32)State; }
	// every bot, this run
	static int32 Sightings;
	static int32 PathsFound;
	static int32 PathsFailed;
	static int32 ShotsFired;
	static int32 StuckJumps;
	static int32 Regoals;
	static int32 Melees;
	static int32 Grenades;
	static int32 AbilitiesUsed;
	static int32 Ults;
	static int32 Retreats;
	static int32 NoCover;
	static int32 Swaps;
	static int32 StuckIn[5]; // stuck hops by state
	static int32 Breaches;   // FootRace: breaches it walked into
	static int32 LeverPulls; // FootRace: levers it held
	static int32 CoresCarried;
	FString JobName() const { return Job; } // FootRace: what it's doing (logs and tests)
	static int32 Callouts;   // callouts a teammate acted on
	AWWOCharacter* GetFoe() const { return Foe.Get(); }
	bool IsFollowing() const { return Leader.IsValid(); }
	bool bParked = false; // -WWOBotsLineUp (performance tests): it stands where it was put and does nothing

private:
	// --- Buttons and stick (what the character reads) ---
	bool Held[(int32)EWWOAction::Count] = {};
	bool Just[(int32)EWWOAction::Count] = {};
	FVector2D Move = FVector2D::ZeroVector;
	void Press(EWWOAction Action) { Held[(int32)Action] = true; Just[(int32)Action] = true; }
	void Hold(EWWOAction Action) { Held[(int32)Action] = true; }

	// --- What it knows ---
	TWeakObjectPtr<AWWOCharacter> Foe;  // the enemy it's fighting or hunting
	bool bFoeVisible = false;
	bool bAware = false;       // it has reacted to this foe (until then it carries on as before)
	bool bSpotInSight = false; // hunting: it can see the place the foe was last seen
	FVector FoeSpot = FVector::ZeroVector;  // where its chest was when last seen
	FVector FoeFeet = FVector::ZeroVector;
	FVector FoeVelocity = FVector::ZeroVector;
	float FoeSeenAgo = 0.f;  // s since it was last in sight
	float ReactLeft = 0.f;   // s before it acts on a new sighting
	float OnTarget = 0.f;    // s it has been fighting this foe (the aim settles with it)
	float SenseLeft = 0.f;
	void Sense(AWWOCharacter* Me);
	bool CanSee(const AWWOCharacter* Me, const AWWOCharacter* Other, float RangeCm, float ConeCos) const;
	void OnHurt(float Amount);
	TWeakObjectPtr<APawn> HurtBound;

	// --- Aim ---
	FVector2D AimError = FVector2D::ZeroVector, AimErrorGoal = FVector2D::ZeroVector; // degrees: yaw, pitch
	float AimErrorLeft = 0.f;
	void TurnView(AWWOCharacter* Me, const FVector& Direction, float MaxDegPerSecond, float DeltaSeconds, FVector2D OffsetDeg = FVector2D::ZeroVector);

	// --- Firing ---
	float NextShot = 0.f, BurstLeft = 0.f, PauseLeft = 0.f;
	bool bWasFiring = false;
	// One frame's aim at a target: where it is from here, and whether a shot now would be on it.
	struct FWWOBotShot
	{
		float Dist = 0.f;     // m
		float OffDeg = 180.f; // the aim's angle off it
		FVector ToFlat = FVector::ZeroVector;
		bool bInRange = false, bCanShoot = false;
	};
	// Turns the view onto Spot (leading Velocity, with the wandering aim error) and says whether to shoot.
	FWWOBotShot AimAt(AWWOCharacter* Me, const FVector& Spot, const FVector& Velocity, float AimErrorDeg, float TurnSpeed, float DeltaSeconds);
	// Fires to the gun's rhythm while the shot is on, reloads when empty, aims in at range.
	void Shoot(AWWOCharacter* Me, const FWWOBotShot& Shot, float DeltaSeconds);

	// --- Moving ---
	FWWOPathFollower Path;
	FVector PathDirection(AWWOCharacter* Me, const FVector& To, float DeltaSeconds);
	FVector Goal = FVector::ZeroVector; // where it's roaming to
	void PickGoal(const AWWOCharacter* Me);
	float StrafeLeft = 0.f, StrafeDir = 1.f, NextJump = 4.f, Stuck = 0.f, NextStuckJump = 0.f, Clock = 0.f;
	FVector LastSpot = FVector::ZeroVector;
	bool SideIsClear(const AWWOCharacter* Me, const FVector& Direction) const; // no wall or drop that way
	void SetMove(const AWWOCharacter* Me, const FVector& WorldDirection);

	// --- Its kit ---
	FWWONetLoadout Kit;  // picked once, kept across respawns
	TArray<FName> Guns;  // the two it carries
	bool bHasKit = false;
	void GiveKit(AWWOCharacter* Me);
	float NextBusy = 4.f; // -WWOBotsBusy: when it next throws or uses something for the sake of it
	float NextMelee = 0.f, NextGrenade = 3.f, NextAbility = 0.f, NextUlt = 5.f, NextSwap = 0.f;
	// Throwing: it turns to the arc, lets go, and holds the aim until the grenade has left its hand.
	bool bThrowing = false, bThrown = false;
	float ThrowLeft = 0.f;
	FVector ThrowAt = FVector::ZeroVector;
	bool ThrowDirection(const AWWOCharacter* Me, const FVector& At, FVector& OutDirection) const;
	void UseKit(AWWOCharacter* Me, AWWOCharacter* Target, float Dist, float OffDeg, bool bInRange, float HealthLeft);
	void PickGun(AWWOCharacter* Me, float Dist);

	// --- Backing off ---
	bool bRetreating = false;
	FVector CoverSpot = FVector::ZeroVector;
	float RetreatLeft = 0.f, NextRetreat = 0.f;
	bool FindCover(const AWWOCharacter* Me, FVector& OutSpot) const;

	// --- Its team ---
	TWeakObjectPtr<AWWOCharacter> Leader; // who it sticks with while nothing's going on (the human, or its pair's leader)
	float TeamLeft = 0.f, CalloutLeft = 0.f, FlankSide = 1.f;
	bool bClosingUp = false; // running to catch up with its leader
	void PickLeader(const AWWOCharacter* Me);
	void CallOut(const AWWOCharacter* Me, AWWOCharacter* Enemy);
	void HearCallout(const AWWOCharacter* From, AWWOCharacter* Enemy);
	void ListenToHumans(const AWWOCharacter* Me);

	// --- FootRace ---
	TWeakObjectPtr<AWWOEnemy> Mob; // the lane enemy it's shooting
	bool bMobVisible = false;
	float BreachReady = 0.f;       // s the team has had a reason and the energy to breach
	float Patience = 0.f;          // s it waits for that before it goes (each bot its own)
	bool bCarrying = false, bWasInHub = false, bPulling = false;
	FString Job;
	void SenseMobs(AWWOCharacter* Me, const FWWOFootRaceRules& Race);
	void FootRaceTick(AWWOCharacter* Me, FWWOFootRaceRules& Race, float DeltaSeconds, bool& bMoving, bool& bWantSprint);
	float GoTo(AWWOCharacter* Me, const FVector& Feet, float DeltaSeconds, bool& bMoving, bool& bWantSprint); // returns how far (m)

	enum class EState : uint8 { Roam, Hunt, Fight, Retreat, Objective };
	EState State = EState::Roam;
	// The regression scenarios (-WWORegress, Player/WWORegress.h) call private steps directly; no gameplay code uses this.
	friend class FWWORegress;
};
