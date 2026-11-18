#pragma once

// Shared by the settings menu's files only (WWOSettings*.cpp): the menu's scale, the key and side
// buttons' look, and two names the section table and the item lists both show. Each file says
// `using namespace WWOSettingsUI;`.

#include "CoreMinimal.h"
#include "UI/WWOUI.h"
#include "WWOTypes.h"

namespace WWOSettingsUI
{
	constexpr float K = WWOUI::K;

	// The side buttons and the key buttons: a card whose hovered and pressed shades are fixed (not
	// lightened from its own fill), with the filled button's centered text.
	inline WWOUI::FButtonLook KeyLook(const FLinearColor& Fill, const FLinearColor& Edge, float EdgeWidth, const FMargin& Padding)
	{
		WWOUI::FButtonLook Look = WWOUI::FilledLook(Fill, Padding);
		Look.Normal = WWOUI::RoundBrush(Fill, WWOUI::Corner, Edge, EdgeWidth);
		Look.Hovered = WWOUI::RoundBrush(WWOUI::CardHovered, WWOUI::Corner, WWOUI::HoverEdge, WWOUI::BoxEdge);
		Look.Pressed = WWOUI::RoundBrush(WWOUI::CardPressed, WWOUI::Corner, FLinearColor::White, WWOUI::BoxEdge);
		return Look;
	}

	inline FString Capitalize(FString S)
	{
		S.ReplaceInline(TEXT("_"), TEXT(" "));
		return S.Left(1).ToUpper() + S.Mid(1);
	}

	inline FString KindName(EWWOAbilityKind Kind)
	{
		switch (Kind)
		{
		case EWWOAbilityKind::Active: return TEXT("Active");
		case EWWOAbilityKind::Triggered: return TEXT("Triggered");
		case EWWOAbilityKind::Charged: return TEXT("Charged");
		default: return TEXT("Passive");
		}
	}
}
