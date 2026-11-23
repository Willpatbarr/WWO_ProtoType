#pragma once

// A FootRace lane door (Game/WWOFootRaceRules.h): a slab filling one of the lane's doorways (the
// level's DoorSlot markers: Door1 between Room1 and Room2 ... Door5 Room5-boss).
// Closed it blocks bodies, shots and the navmesh; the rules open it when the team pulls the door's
// lever in the hub. Opening is replicated (bOpen) and shown on every machine.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOBreachDoor.generated.h"

class UStaticMeshComponent;

UCLASS()
class WHATWASOURS_API AWWOBreachDoor : public AActor
{
	GENERATED_BODY()

public:
	AWWOBreachDoor();

	UPROPERTY(Replicated) int8 Team = 0;
	int32 Door = 0; // host: 1-3
	UPROPERTY(ReplicatedUsing = OnRep_Open) bool bOpen = false;

	// The slab, m: the doorway's 4 x 4 opening and a little more, as thick as the wall.
	static constexpr float Width = 4.2f;
	static constexpr float Height = 4.2f;
	static constexpr float Thickness = 0.6f;

	void Open(); // host
	bool IsOpen() const { return bOpen; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void OnRep_Open();
	void ShowOpen(); // hidden, no collision, out of the navmesh

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Slab;
};
