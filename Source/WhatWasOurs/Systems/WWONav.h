#pragma once

// Shared AI locomotion, for the enemies (Enemies/WWOEnemy), the AI teammate (Allies/WWOAlly) and the bots
// (Player/WWOBotController): a navmesh path, a path follower, and the probes and turn they steer by.
// Every caller's own numbers (probe sizes and heights, reach, repath timing, turn rate) and what it does
// with a result (stand, sidestep, flip its strafe, fall back to a straight line) stay with the caller;
// these do the shared steps the way each caller did them. Host only, like the AI that calls them.
// Every call is one trace or none, and nothing on the heap (a new path is FindPath's own array).

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "WWOTypes.h"

class AActor;
class UWorld;

namespace WWONav
{
	// A navmesh path like Godot's map_get_path: a start or end that's off the navmesh (pressed
	// against a crate, a teammate standing in a corner) snaps to the closest navmesh point first,
	// and an unreachable end gives the path to the closest reachable spot (bOutPartial).
	// Empty when there's no navmesh here.
	WHATWASOURS_API TArray<FVector> FindPath(AActor* Querier, const FVector& Start, const FVector& End, bool& bOutPartial);
}

// What a probe looks for and with what shape.
struct FWWOProbe
{
	// Level geometry by object type (WorldStatic and WorldDynamic: never people), or, with bByChannel,
	// whatever blocks Channel.
	bool bByChannel = false;
	ECollisionChannel Channel = ECC_Visibility;
	// A capsule of this radius and half height, or a line when Radius is 0.
	float Radius = 0.f;
	float HalfHeight = 0.f;
	// A capsule sweep that only asks whether anything is in the way (a sweep test: no hit, no normal).
	bool bAnyHit = false;
};

namespace WWONav
{
	// Anything in the way of the probe moved from From by Step (the caller's direction times its
	// distance)? OutNormal: the flat normal of the first thing hit (not with bAnyHit).
	WHATWASOURS_API bool WallAhead(const UWorld* World, const FWWOProbe& Probe, const FVector& From, const FVector& Step,
		const FCollisionQueryParams& Params, FVector* OutNormal = nullptr);

	// No floor within Drop cm straight down from Point (the caller's step out): a line, whatever the
	// probe's radius.
	WHATWASOURS_API bool LedgeAhead(const UWorld* World, const FWWOProbe& Probe, const FVector& Point, float Drop, const FCollisionQueryParams& Params);

	// Current eased toward Target (degrees) at Rate (1/s), frame-rate independent: the next yaw. The step is
	// worked out in Target's type: the enemies pass a float yaw (float maths), the ally a rotator's
	// double yaw (double maths, rounded to float at the end), as each did.
	template <typename TTarget>
	float TurnYaw(float Current, TTarget Target, float Rate, float DeltaSeconds)
	{
		return Current + FMath::FindDeltaAngleDegrees(Current, Target) * WWO::Damp(Rate, DeltaSeconds);
	}
}

// How a caller follows its path: when it paths again, and when a path point counts as reached.
struct FWWOPathRules
{
	float RepathAfter = 0.3f;      // s: paths again once its path is older than this
	float RepathMoved = -1.f;      // cm: ... or once the goal is more than this from where it pathed to (< 0: never)
	bool bMovedSquared = false;    // that test on squared distances (DistSquared > Moved^2), not Dist > Moved
	float Reach = 60.f;            // cm, flat: a path point nearer than this counts as reached
	bool bReachInclusive = false;  // ... at exactly Reach too
	bool bDist2D = false;          // measured with FVector::Dist2D, not FVector2D's Size (the same distance; the rounding may differ)
};

// A path being walked: its points (the first is the next to reach), where it pathed to, how old it is.
struct WHATWASOURS_API FWWOPathFollower
{
	TArray<FVector> Points;
	FVector Goal = FVector::ZeroVector;
	float Age = 99.f;
	bool bPartial = false;

	FWWOPathFollower() = default;
	explicit FWWOPathFollower(const FVector& InGoal) : Goal(InGoal) {}

	// One tick: ages the path, paths again from From to To when Rules say it's due, then drops the points
	// already reached from Here (keeping at least one). True when it pathed again.
	bool Update(AActor* Querier, const FVector& From, const FVector& To, const FVector& Here, float DeltaSeconds, const FWWOPathRules& Rules);
};
