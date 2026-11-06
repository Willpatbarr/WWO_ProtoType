#pragma once

// Weapon stat bars for the character menu's Weapons tile (draft mods). Painted, not built from
// widgets, so the menu can update them every frame as the cursor moves over mods.
// Every bar reads "longer is better": half the bar is the bare frame's value (a tick marks it),
// a full bar is twice as good. While a mod is previewed, what it would add shows green and what
// it would take away shows red.

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WWOStatBars.generated.h"

struct FWWOStatBar
{
	FString Label;
	float Fill = 0.f;        // 0..1, with the equipped mods
	float PreviewFill = 0.f; // 0..1, with the previewed mod instead
	FString Value;
	FString PreviewValue;
	bool bPreview = false;
};

UCLASS()
class WHATWASOURS_API UWWOStatBars : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr float RowHeight = 34.f; // Godot pixels
	static constexpr float Width = 560.f;
	TArray<FWWOStatBar> Rows;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
};
