#pragma once

// The life of a lingering world effect (row 4.11): an age, a grow-in, a hold for its duration, a tail
// (the way it goes: a shrink, a sink; none when it fades inside its duration) and the end, where the
// host destroys it and a joiner's copy waits to go with the host's. Each effect keeps one as a member,
// states its own numbers (they differ, on purpose) and does its own per-tick work around Advance.
// - AWWOSporePool, AWWOSporeFog: no tail, they fade inside their duration and are over at its end.
// - AWWOSporePatch: a tail on the same clock (over at duration + tail).
// - AWWOBarkWall, AWWOEntrenchRoots: a tail on its own clock, started again from 0 when the duration
//   runs out (bRestartTail).
// The duration is passed to Advance, not kept here: it is the effect's own (replicated) property.
// See docs/RECIPES.md, "A lingering effect".

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOTypes.h"

enum class EWWOLifeStep : uint8
{
	Holding, // within its duration
	Leaving, // the tick its duration ran out, when it has a tail: it begins to go
	Tail,    // going
	Over,    // done: End it (and on every later tick it is still there: a joiner's copy)
};

struct FWWOEffectLife
{
	float GrowTime = 0.25f;    // seconds to grow in (GrowIn)
	float TailTime = 0.f;      // seconds it takes to go once its duration is over (0: over at the duration)
	bool bRestartTail = false; // the tail runs on its own clock from 0 instead of the age running on

	// Seconds since it appeared; in a restarted tail, since the tail began. The restart happens at the
	// next Advance, so the Leaving tick's own work still sees the age that ran out.
	float Age = 0.f;

	FWWOEffectLife() = default;
	FWWOEffectLife(float InGrowTime, float InTailTime, bool bInRestartTail)
		: GrowTime(InGrowTime), TailTime(InTailTime), bRestartTail(bInRestartTail) {}

	// One tick: adds its time and says where in its life the effect is.
	EWWOLifeStep Advance(float DeltaSeconds, float Duration)
	{
		if (bRestartPending)
		{
			Age = 0.f;
			bRestartPending = false;
		}
		Age += DeltaSeconds;
		if (!bLeft)
		{
			if (Age < Duration) return EWWOLifeStep::Holding;
			bLeft = true;
			if (bRestartTail)
			{
				bRestartPending = true;
				return EWWOLifeStep::Leaving;
			}
			return Age >= Duration + TailTime ? EWWOLifeStep::Over : EWWOLifeStep::Leaving;
		}
		const bool bOver = bRestartTail ? Age >= TailTime : Age >= Duration + TailTime;
		return bOver ? EWWOLifeStep::Over : EWWOLifeStep::Tail;
	}

	bool InTail() const { return bLeft; }
	float GrowIn() const { return WWO::BackOut(Age / GrowTime); }                     // 0 -> 1 (overshoots)
	float FadeIn(float Seconds) const { return FMath::Clamp(Age / Seconds, 0.f, 1.f); } // 0 -> 1, linear
	float LeftOf(float Duration, float Seconds) const { return FMath::Clamp((Duration - Age) / Seconds, 0.f, 1.f); } // 1 -> 0 over the duration's last Seconds
	// 1 -> 0 over the tail, not clamped (above 1 before it, below 0 after it).
	float TailLeft(float Duration) const { return 1.f - (bRestartTail ? Age : Age - Duration) / TailTime; }

	// The end: the host destroys it and a joiner's copy goes when the host's does. bAnyMachine: an
	// effect that isn't replicated (the roots) destroys itself wherever it is. True if it was destroyed.
	static bool End(AActor& Effect, bool bAnyMachine = false)
	{
		if (!bAnyMachine && !Effect.HasAuthority()) return false;
		Effect.Destroy();
		return true;
	}

private:
	bool bLeft = false;
	bool bRestartPending = false;
};
