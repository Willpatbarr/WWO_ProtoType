#pragma once

// A FootRace door lever (Game/WWOFootRaceRules.h), in the hub at the level's LeverSlot markers: one per
// team per door. A breacher of its team holds the Revive key next to it for the game mode's
// LeverHoldTime and the rules open the door. It can only be pulled while the rules say it's live (its
// door is the team's next, and the team has breached from the room that door leads out of on the pool
// it is in the hub on); then it glows. The rules run all of that on the host; this actor shows the state
// (replicated) and answers "is this body close enough?".

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOBreachLever.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class WHATWASOURS_API AWWOBreachLever : public AActor
{
	GENERATED_BODY()

public:
	AWWOBreachLever();

	UPROPERTY(ReplicatedUsing = OnRep_Look) int8 Team = 0;
	UPROPERTY(Replicated) int8 Door = 0;                    // 1-3
	UPROPERTY(ReplicatedUsing = OnRep_Look) bool bLive = false;   // can be pulled now (by the team's breachers)
	UPROPERTY(ReplicatedUsing = OnRep_Look) bool bPulled = false; // its door is open
	UPROPERTY(Replicated) float Progress = 0.f;              // 0-1: how far a hold has got

	static constexpr float Reach = 2.5f; // m from the lever's base a body can pull it from

	bool InReach(const FVector& BodyLocation) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void OnRep_Look();
	void Build();
	void Refresh(); // colours and the handle's angle from Team, bLive, bPulled, Progress

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Base;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Handle;
	UPROPERTY() TObjectPtr<USceneComponent> Pivot;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Lamp;
	bool bShownLive = false, bShownPulled = false; // what Refresh last showed (the host sets the state directly)
};
