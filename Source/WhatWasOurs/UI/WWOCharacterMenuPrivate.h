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
}
