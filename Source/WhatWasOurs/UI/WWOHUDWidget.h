#pragma once

// The in-game HUD (Godot: hud.gd), painted directly like the Godot _draw code. Sizes are in
// the Godot 1600 x 900 layout and scaled by min(width / 1600, height / 900), as Godot's stretch did.
// - Reticle (red over a target, fades as you aim in), hit / crit / kill markers.
// - ADS vignette and the sniper scope overlay.
// - Target health bar (and the Tank's shield bar) under the reticle target.
// - Ammo counter, weapon name and reload bar (bottom right).
// - Grenade, ability and ult squares with their charge states (bottom left).
// - Shield and health bars (bottom center), damage flash, death screen, status tags.
// - Aim assist debug cones, floating damage numbers, teammate labels, the wave line.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WWOHUDWidget.generated.h"

class AWWOCharacter;
class UMaterialInstanceDynamic;

UCLASS()
class WHATWASOURS_API UWWOHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<AWWOCharacter> BoundPlayer;
	float HitTime = 0.f;
	bool bHitCritical = false;
	bool bHitKilled = false;
	float DamageFlash = 0.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> VignetteMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ScopeMat;

	AWWOCharacter* Player() const;
	void Bind(AWWOCharacter* P);
};
