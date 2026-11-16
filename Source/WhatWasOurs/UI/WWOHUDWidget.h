#pragma once

// The in-game HUD (Godot: hud.gd), painted directly like the Godot _draw code. Sizes are in
// the Godot 1600 x 900 layout and scaled by min(width / 1600, height / 900), as Godot's stretch did.
// State and easing are in WWOHUDWidget.cpp; NativePaint and its element painters (one per part of
// the HUD, called in z-order through FWWOPainter, UI/WWOPainter.h) in WWOHUDPaint.cpp.
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
struct FWWOHUDFrame;

UCLASS()
class WHATWASOURS_API UWWOHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The radar. 8 sections of 45 degrees, section 0 centred on the way Viewer looks and the rest
	// going clockwise (2 is to the right, 4 behind). Each has Steps bands, band 0 at the rim: an
	// enemy at the edge of the range lights band 0, a nearer one lights more bands inward, and one
	// inside RadarFullRange lights them all. Out gets 8 * Steps alphas (section * Steps + band):
	// 0 for an unlit band, else how solid, which drops the more the enemy is above or below you.
	// Returns Steps.
	static int32 Radar(const class AWWOCharacter* Viewer, TArray<float>& Out);
	// Your living teammates for the radar (everyone you can't hurt: your team in PvP, every other
	// player in co-op). Each is X: the bearing from the way Viewer looks (degrees, 0 ahead, 90 right),
	// Y: how far, as a share of RadarRange (over 1: out of range, shown as an arrow on the rim).
	static void RadarMates(const class AWWOCharacter* Viewer, TArray<FVector2D>& Out);
	static int32 RadarBands(float Distance, float Range, float FullRange, int32 Steps); // how many bands an enemy this far lights (0: out of range)

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	static constexpr float HitMarkerTime = 0.18f;
	static constexpr float KillMarkerTime = 0.4f;
	TWeakObjectPtr<AWWOCharacter> BoundPlayer;
	float HitTime = 0.f;
	bool bHitCritical = false;
	mutable TMap<FString, float> ChipPeak; // status chips: the longest time each has shown (its bar drains against it)
	bool bHitKilled = false;
	float DamageFlash = 0.f;
	float StaticShown = 0.f; // 0-1: Static Charge is charged and waiting (the blue edges and the arcs)
	float HurtShown = 0.f; // the low-health vignette's strength now, 0-1 (eases toward how hurt you are)
	// Frame rate readout (top left): averaged over a quarter second of real time.
	float FpsShown = 0.f;
	float FpsClock = 0.f;
	int32 FpsFrames = 0;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> VignetteMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ScopeMat;

	AWWOCharacter* Player() const;
	void Bind(AWWOCharacter* P);

	// The element painters (WWOHUDPaint.cpp), in the order NativePaint calls them: later ones
	// draw over earlier ones.
	void PaintScreenEffects(const FWWOHUDFrame& F) const; // low-health vignette, Static Charge edges, ADS vignette and scope
	void PaintWorldLabels(const FWWOHUDFrame& F) const;   // floating damage numbers, AI teammate labels
	void PaintReticle(const FWWOHUDFrame& F) const;       // reticle, aimed-in mark, hit marker
	void PaintAssistDebug(const FWWOHUDFrame& F) const;   // aim assist debug cones, hidden assist markers
	void PaintHealthBars(const FWWOHUDFrame& F) const;    // bars over marked enemies and teammates in sight
	void PaintMateChevrons(const FWWOHUDFrame& F) const;  // PvP: a chevron over each teammate
	void PaintKillPopups(const FWWOHUDFrame& F) const;    // your own kills, right of the reticle
	void PaintKillFeed(const FWWOHUDFrame& F) const;      // killer > killee, bottom left
	void PaintRadar(const FWWOHUDFrame& F) const;         // top left
	void PaintAmmo(const FWWOHUDFrame& F) const;          // weapon name, perks, count, reload bar (bottom right), config toast
	void PaintAbilities(const FWWOHUDFrame& F) const;     // grenade, ability and ult boxes (bottom left)
	void PaintVitals(const FWWOHUDFrame& F) const;        // shield and health bars, status chips (top center)
	void PaintDamageSpikes(const FWWOHUDFrame& F) const;  // where damage came from
	void PaintOverlays(const FWWOHUDFrame& F) const;      // blind, damage flash, down / dead screens, revive prompt
	void PaintWaveLine(const FWWOHUDFrame& F) const;      // the match line (top center)
	void PaintFps(const FWWOHUDFrame& F) const;           // frame rate (top left, under the radar)
};
