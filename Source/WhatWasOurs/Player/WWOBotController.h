#pragma once

// A bot player (PvP stress tests: the host runs -WWOBots=N). It drives an ordinary player
// character the way a person would: it holds buttons and pushes the stick, and the character reads
// them instead of a player controller's (AWWOCharacter::Pressed / JustPressed / MoveInput). It
// hunts the nearest player of the other team it can see (strafing, aiming in, firing) and otherwise
// wanders between spawn spots, jumping now and then. It has a player state, so it has a team.

#include "CoreMinimal.h"
#include "AIController.h"
#include "WWOTypes.h"
#include "WWOBotController.generated.h"

UCLASS()
class WHATWASOURS_API AWWOBotController : public AAIController
{
	GENERATED_BODY()

public:
	AWWOBotController();
	virtual void Tick(float DeltaSeconds) override;

	bool IsHeld(EWWOAction Action) const { return Held[(int32)Action]; }
	bool WasJustPressed(EWWOAction Action) const { return Just[(int32)Action]; }
	FVector2D GetMoveAxis() const { return Move; }

private:
	bool Held[(int32)EWWOAction::Count] = {};
	bool Just[(int32)EWWOAction::Count] = {};
	FVector2D Move = FVector2D::ZeroVector;
	TWeakObjectPtr<AActor> Foe;
	FVector Goal = FVector::ZeroVector;
	float ThinkLeft = 0.f, StrafeLeft = 0.f, StrafeDir = 1.f, NextShot = 0.f, NextJump = 2.f, Stuck = 0.f, Clock = 0.f;
	FVector LastSpot = FVector::ZeroVector;
	void Press(EWWOAction Action) { Held[(int32)Action] = true; Just[(int32)Action] = true; }
	void PickGoal();
};
