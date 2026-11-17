#pragma once

// The firing tester's rules (?tester from the launch screen, -WWOTester): no waves, no enemies, no
// AI teammates, and one target dummy straight ahead of where the player started, far enough to read
// the kick against. UWWOFiringTester is the screen over it (it puts the player back at Home).

#include "CoreMinimal.h"
#include "Game/WWOMatchRules.h"

class AWWOTarget;

class FWWOTesterRules final : public FWWOMatchRules
{
public:
	using FWWOMatchRules::FWWOMatchRules;

	FTransform Home;                  // where the player started (the strafe run is centred on it)
	bool bHomeSet = false;
	TWeakObjectPtr<AWWOTarget> Target;
	float TargetDistance = 20.f;      // m

	virtual void Begin() override;
	// The dummy, once the player is in (and again whenever it is gone), at the same spot.
	virtual void Update(float DeltaSeconds) override;
	virtual FWWOTesterRules* AsTester() override { return this; }
};
