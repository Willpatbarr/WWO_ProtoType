#pragma once

// A FootRace boss core (Game/WWOFootRaceRules.h): a glowing ball in a team's colour that appears in the
// hub once that team's boss is up and shielded. Its team picks it up by walking over it, and carrying it
// out through the team's hub doorway drops the boss shield for a damage window. A carrier who dies drops
// it; then either team can take it, and the other team carrying it through their own hub doorway heals
// this team's boss instead. Left lying, it goes back to its spot in the hub.
//
// The rules run all of that on the host; this actor shows where the core is: on the floor, bobbing, or
// above its carrier's head (Carrier is replicated, so every machine follows it).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOBossCore.generated.h"

class AWWOCharacter;
class UStaticMeshComponent;
class UPointLightComponent;

UCLASS()
class WHATWASOURS_API AWWOBossCore : public AActor
{
	GENERATED_BODY()

public:
	AWWOBossCore();

	UPROPERTY(Replicated) int8 Team = 0;                          // whose boss it serves
	UPROPERTY(Replicated) TObjectPtr<AWWOCharacter> Carrier;     // who has it (none: lying somewhere)
	UPROPERTY(Replicated) bool bDropped = false;                  // lying where a carrier fell (anyone can take it)
	FVector Home = FVector::ZeroVector;                           // host: its spot in the hub
	float DroppedLeft = 0.f;                                      // host: s before a dropped core goes home

	static constexpr float PickupReach = 1.6f; // m

	bool InReach(const FVector& BodyLocation) const;
	void PickUp(AWWOCharacter* By);           // host
	void Drop(const FVector& Where, float Seconds); // host: on the floor here, back home after Seconds
	void GoHome();                            // host

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	FVector Rest = FVector::ZeroVector; // where it lies (the actor's spot when not carried)
	float Clock = 0.f;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Ball;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Glow;
};
