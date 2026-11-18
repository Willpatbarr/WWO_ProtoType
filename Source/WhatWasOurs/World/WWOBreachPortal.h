#pragma once

// A breach portal (FootRace, Game/WWOFootRaceRules.h): a doorway that teleports. Each lane room
// (Room1-3, the boss room) has one on its hub side, at the level's PortalAnchor marker, and the hub
// has one per team, at HubPortal. Walking into a room's doorway puts you in front of your team's hub
// doorway; walking into the hub doorway puts you back in front of the room doorway you came through.
//
// This actor only shows the doorway and answers "is this spot in the opening?"; the rules spawn the
// portals (host), watch who steps in and do the teleport. The doorway faces along its X axis: the side
// people arrive on and walk in from.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOBreachPortal.generated.h"

class UStaticMeshComponent;

UCLASS()
class WHATWASOURS_API AWWOBreachPortal : public AActor
{
	GENERATED_BODY()

public:
	AWWOBreachPortal();

	UPROPERTY(ReplicatedUsing = OnRep_Look) int8 Team = 0;  // whose doorway (its colour)
	UPROPERTY(ReplicatedUsing = OnRep_Look) bool bHub = false;
	FName Room; // host: the lane room it stands in (Room1..Room3, Boss); None in the hub

	// The opening, m: width and height, and how far in front of and behind its plane a body counts as in it.
	static constexpr float Width = 3.f;
	static constexpr float Height = 3.6f;
	static constexpr float Depth = 0.5f;
	// How far in front of the doorway someone coming through it lands, m (clear of the opening).
	static constexpr float ArriveAhead = 2.5f;

	// Where a body arriving through this doorway is put: ArriveAhead in front, facing away from it.
	// HalfHeight is the body's capsule half height (cm), so its feet land on the floor.
	FTransform ArrivalSpot(float HalfHeight) const;
	// Is a body standing in the opening? Feet: the bottom of its capsule.
	bool Contains(const FVector& Feet) const;

	static FLinearColor TeamColor(int32 Team);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void OnRep_Look();
	void Build();   // the frame and the panel, from basic shapes (every machine, in BeginPlay)
	void Recolor(); // the team colour on the frame and panel

	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Frame;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Panel;
};
