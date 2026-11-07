#pragma once

// What every player needs to know about the match, replicated from the host: the wave status line
// ("WAVE 3 / 8 • 12 enemies left", "ARENA CLEARED"...) the game mode writes each frame.

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "WWOGameState.generated.h"

UCLASS()
class WHATWASOURS_API AWWOGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated) FString StatusLine;
	UPROPERTY(Replicated) FLinearColor StatusColor = FLinearColor::White;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
