#pragma once

// The in-game HUD (Godot: hud.gd), painted directly like the Godot _draw code. Sizes are in
// the Godot 1600 x 900 layout and scaled by min(width / 1600, height / 900), as Godot's stretch did.
// - Reticle (red over a target, fades as you aim in), hit / crit / kill markers.
// - ADS vignette and the sniper scope overlay.
// - Target health bar (and the Tank's shield bar) under the reticle target.
// - Ammo counter, weapon name and reload bar (bottom right).
// - Grenade, ability and ult squares with their charge states (bottom left).
// - Shield and health bars (bottom center), damage flash, death screen, status tags.
// - A red vignette around the edges that grows as your health and shield drop and fades as they
//   come back (AWWOCharacter::HealthVignette scales it).
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
	mutable TMap<FString, float> ChipPeak; // status chips: the longest time each has shown (its bar drains against it)
	bool bHitKilled = false;
	float DamageFlash = 0.f;
	float HurtShown = 0.f; // the low-health vignette's strength now, 0-1 (eases toward how hurt you are)
	// Frame rate readout (top left): averaged over a quarter second of real time.
	float FpsShown = 0.f;
	float FpsClock = 0.f;
	int32 FpsFrames = 0;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> VignetteMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ScopeMat;

	AWWOCharacter* Player() const;
	void Bind(AWWOCharacter* P);
};
