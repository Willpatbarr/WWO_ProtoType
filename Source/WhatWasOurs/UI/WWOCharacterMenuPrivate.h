#pragma once

// Shared by the character menu's files only (WWOCharacterMenu*.cpp): the menu's scale and the trait
// colors the Loadout and Flora tabs both use. Each file says `using namespace WWOCharacterMenuUI;`.

#include "CoreMinimal.h"
#include "UI/WWOUI.h"

namespace WWOCharacterMenuUI
{
	constexpr float K = WWOUI::K;
	inline const FLinearColor TraitFill(0.16f, 0.22f, 0.34f), TraitEdge(0.55f, 0.7f, 1.f);       // an equipped trait, as on the Flora tab
	inline const FLinearColor TraitOffFill(0.2f, 0.21f, 0.26f), TraitOffEdge(0.4f, 0.42f, 0.5f); // one that isn't
	inline const FLinearColor OnEdge(0.9f, 0.9f, 0.95f); // the chosen one of a neutral card (a weapon, a shell part)

	// One card of the editors (UWWOCharacterMenu::ItemCard): an item's badge at its left, its name over
	// a line of detail. Coloured cards take the item's colour (filled with its badge's edge shade, so the
	// badge shows no seam); a neutral one (no colour) is the menu's grey card.
	struct FItemCard
	{
		FName Kind, Id;          // the badge (WWOUI::SymbolTexture); none: no badge
		FLinearColor Color = FLinearColor(0, 0, 0, 0); // the item's colour; A == 0: a neutral card
		FString Title, Sub;      // the name, and a line (or a few, wrapped) of detail under it
		bool bOn = false;        // chosen (or its picker is open): a bright, wider edge
		bool bMark = true;       // with bOn, the mark of the chosen one too: a dot in the top right corner
		float Badge = 48.f;      // the badge's side (Godot pixels)
		float MinHeight = 72.f;  // the card's least height (Godot pixels)
		FString Tip;             // a hover tip
		bool bCenter = false;    // words centred (a card with no badge)
	};
	// The gap between cards in a grid (Godot pixels): the grids' slot padding is half of it each side.
	constexpr float CardGap = 12.f;
}
