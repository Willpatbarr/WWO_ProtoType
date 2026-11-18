#pragma once

// The loadout's stat maths (Godot: Modifiers): what an equipped loadout does to a stat, and the fixed
// tables it works from. Pure functions of an FWWOEquipped; UWWOLoadoutSubsystem::Apply and friends forward
// here with this machine's (or a copy's) equipped state and its PointValue.
//
// Stats: final = (base + sum of Flat) * max(1 + sum of PercentAdd, 0) * (product of Multiply).
// Frame parts give group points; each point moves every stat in its group by PointValue (5% by default)
// in the good direction. Equipped traits add their own modifiers; the equipped weapon's mods add percents.
// The order of the additions and multiplications is part of the numbers (the regression scenarios
// compare every bit): keep it.

#include "CoreMinimal.h"

struct FWWOEquipped;

namespace WWOLoadoutStats
{
	float Apply(const FWWOEquipped& E, float PointValue, FName Stat, float Base);
	float GroupPoints(const FWWOEquipped& E, FName Group);
	TArray<FString> GroupSummary(const FWWOEquipped& E, float PointValue);
	float ModPercent(const FWWOEquipped& E, FName Stat); // sum of the equipped weapon's mods for this stat
	float TraitBonus(const FWWOEquipped& E, FName Effect); // sum of Amount for equipped traits with this effect

	const TArray<FName>& GroupOrder();
	const TArray<FName>& StatsInGroup(FName Group);
	FString GroupTitle(FName Group);
	FString StatName(FName Stat);
	FName GroupOf(FName Stat);
	bool LowerIsBetter(FName Stat); // a point shortens these
}
