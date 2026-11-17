#include "Systems/WWOLoadoutStats.h"
#include "Systems/WWOLoadoutEquipped.h"

const TArray<FName>& WWOLoadoutStats::GroupOrder()
{
	static const TArray<FName> Order = { "agility", "aerial", "dexterity", "durability" };
	return Order;
}

const TArray<FName>& WWOLoadoutStats::StatsInGroup(FName Group)
{
	static const TMap<FName, TArray<FName>> Groups = {
		{ "agility", { "walk_speed", "sprint_speed", "crouch_speed", "slide_speed", "slide_distance", "melee_time", "throw_distance" } },
		{ "aerial", { "jump_height", "air_control" } },
		{ "dexterity", { "reload_time", "ads_time", "swap_time", "ready_time" } },
		{ "durability", { "max_health", "max_shield", "recharge_delay" } },
	};
	static const TArray<FName> Empty;
	const TArray<FName>* Found = Groups.Find(Group);
	return Found ? *Found : Empty;
}

FString WWOLoadoutStats::GroupTitle(FName Group)
{
	FString S = Group.ToString();
	return S.Left(1).ToUpper() + S.Mid(1);
}

namespace
{
	const TArray<TPair<FName, FString>>& StatNames()
	{
		static const TArray<TPair<FName, FString>> Names = {
			{ "walk_speed", TEXT("Walk speed") }, { "sprint_speed", TEXT("Sprint speed") },
			{ "crouch_speed", TEXT("Crouch speed") }, { "slide_speed", TEXT("Slide speed") },
			{ "slide_distance", TEXT("Slide distance") }, { "melee_time", TEXT("Melee time") },
			{ "throw_distance", TEXT("Throw distance") }, { "jump_height", TEXT("Jump height") },
			{ "air_control", TEXT("Air control") }, { "reload_time", TEXT("Reload time") },
			{ "ads_time", TEXT("Aim-in time") }, { "swap_time", TEXT("Swap time") },
			{ "ready_time", TEXT("Handling (ready time)") }, { "max_health", TEXT("Health") },
			{ "max_shield", TEXT("Shields") }, { "recharge_delay", TEXT("Recovery delay") },
			{ "shield_regen", TEXT("Shield refill speed") }, { "melee_damage", TEXT("Melee damage") },
			{ "grenade_recharge", TEXT("Grenade recharge time") },
			// Weapon mod stats (read off the weapon, not through Apply's group points).
			{ "range", TEXT("Range") }, { "recoil_side", TEXT("Side recoil") }, { "recoil_up", TEXT("Vertical recoil") },
			{ "magazine_size", TEXT("Magazine size") }, { "ads_move_speed", TEXT("Move speed while aiming") },
			{ "sway", TEXT("Sway") }, { "zoom", TEXT("Zoom") }, { "damage", TEXT("Damage") },
			{ "fire_rate", TEXT("Fire rate") }, { "crit_multiplier", TEXT("Crit multiplier") },
			{ "pellet_spread", TEXT("Pellet spread") }, { "hip_spread", TEXT("Hip-fire spread") },
		};
		return Names;
	}
}

bool WWOLoadoutStats::LowerIsBetter(FName Stat)
{
	static const TSet<FName> Lower = { "melee_time", "reload_time", "ads_time", "swap_time", "ready_time", "recharge_delay" };
	return Lower.Contains(Stat);
}

FString WWOLoadoutStats::StatName(FName Stat)
{
	for (const auto& Pair : StatNames())
	{
		if (Pair.Key == Stat) return Pair.Value;
	}
	return Stat.ToString();
}

FName WWOLoadoutStats::GroupOf(FName Stat)
{
	for (const FName& Group : GroupOrder())
	{
		if (StatsInGroup(Group).Contains(Stat)) return Group;
	}
	return NAME_None;
}

float WWOLoadoutStats::GroupPoints(const FWWOEquipped& E, FName Group)
{
	float Total = 0.f;
	for (const FName& Slot : FWWOEquipped::FrameSlots())
	{
		if (const UWWOFramePartData* Part = E.GetFramePart(Slot)) Total += Part->Points(Group);
	}
	return Total;
}

float WWOLoadoutStats::ModPercent(const FWWOEquipped& E, FName Stat)
{
	float Total = 0.f;
	if (!E.Weapon) return Total;
	if (const TMap<FName, TObjectPtr<UWWOModData>>* Frame = E.Mods.Find(E.Weapon->Id))
	{
		for (const auto& Pair : *Frame)
		{
			if (!Pair.Value) continue;
			for (const FWWOStatModifier& Mod : Pair.Value->Mods)
			{
				if (Mod.Stat == Stat && Mod.Op == EWWOStatOp::PercentAdd) Total += Mod.Value;
			}
		}
	}
	return Total;
}

float WWOLoadoutStats::TraitBonus(const FWWOEquipped& E, FName Effect)
{
	float Total = 0.f;
	for (const UWWOTraitData* T : E.Traits)
	{
		if (T->Effect == Effect) Total += T->Amount;
	}
	return Total;
}

float WWOLoadoutStats::Apply(const FWWOEquipped& E, float PointValue, FName Stat, float Base)
{
	float Flat = 0.f;
	float Percent = 0.f;
	float Multiply = 1.f;
	const FName Group = GroupOf(Stat);
	if (!Group.IsNone())
	{
		Percent += GroupPoints(E, Group) * PointValue * (LowerIsBetter(Stat) ? -1.f : 1.f);
	}
	for (const UWWOTraitData* T : E.Traits)
	{
		for (const FWWOStatModifier& Mod : T->Mods)
		{
			if (Mod.Stat != Stat) continue;
			switch (Mod.Op)
			{
			case EWWOStatOp::Flat: Flat += Mod.Value; break;
			case EWWOStatOp::PercentAdd: Percent += Mod.Value; break;
			case EWWOStatOp::Multiply: Multiply *= Mod.Value; break;
			}
		}
	}
	Percent += ModPercent(E, Stat);
	return (Base + Flat) * FMath::Max(1.f + Percent, 0.f) * Multiply;
}

TArray<FString> WWOLoadoutStats::GroupSummary(const FWWOEquipped& E, float PointValue)
{
	TArray<FString> Lines;
	for (const FName& Group : GroupOrder())
	{
		const float Pts = GroupPoints(E, Group);
		if (FMath::Abs(Pts) < 0.01f) continue;
		Lines.Add(FString::Printf(TEXT("%s %+d  (each stat %+d%%)"), *GroupTitle(Group), FMath::RoundToInt(Pts),
			FMath::RoundToInt(Pts * PointValue * 100.f)));
	}
	return Lines;
}
