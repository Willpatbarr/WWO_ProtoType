#pragma once

// A player's place in the match, replicated to everyone: their PvP team and score.

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "WWOPlayerState.generated.h"

UCLASS()
class WHATWASOURS_API AWWOPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	double LastSwitch = -100.0; // host: when this player last changed teams
	UPROPERTY(Replicated) int32 Team = 0; // PvP: 0 = Blue, 1 = Orange
	UPROPERTY(Replicated) int32 Kills = 0;
	UPROPERTY(Replicated) int32 PrecisionKills = 0; // of those, the ones whose killing hit was a head hit
	UPROPERTY(Replicated) int32 Deaths = 0;

	static FString TeamName(int32 Team) { return Team == 0 ? TEXT("BLUE") : TEXT("ORANGE"); }
	static FLinearColor TeamColor(int32 Team) { return Team == 0 ? FLinearColor(0.35f, 0.6f, 1.f) : FLinearColor(1.f, 0.6f, 0.25f); }
};
