#pragma once

// The ids the code tests for, named once. The data (the assets in Content/WWO/Data, Config/WeaponMods.json,
// saved loadouts) names them as strings; code compares against these constants, never a literal, so
// finding everything an id does is one search. The wire still carries the FName (NetFists, the loadout
// RPCs). Tests (WWOAutoTest*.cpp, WWORegress*.cpp, the stress harness) choose ids as data and keep strings.

#include "CoreMinimal.h"

namespace WWOIds
{
	// Flora (UWWOFloraData::Id; also a grenade's or ability's Flora, and a blast's Flavor).
	namespace Flora
	{
		inline const FName VoltVine(TEXT("voltvine"));
		inline const FName VeilSpore(TEXT("veilspore"));
		inline const FName IronBark(TEXT("ironbark"));
	}

	// Abilities (UWWOAbilityData::Id). Their behaviour: UWWOAbilityComponent.
	namespace Ability
	{
		inline const FName Overgrowth(TEXT("overgrowth"));
		inline const FName Thornfist(TEXT("thornfist"));
		inline const FName MeleeGuard(TEXT("melee_guard"));
		inline const FName StaticCharge(TEXT("static_charge"));
		inline const FName SporeFogDamage(TEXT("spore_fog_damage"));
		inline const FName SporeDash(TEXT("spore_dash"));
		inline const FName SporePatches(TEXT("spore_patches"));
		inline const FName RootBombs(TEXT("root_bombs"));
		inline const FName ShieldShare(TEXT("shield_share"));
		inline const FName ExtraGrenade(TEXT("extra_grenade"));
		inline const FName Seedburst(TEXT("seedburst"));
	}

	// Weapon perks (a mod's Perk in Config/WeaponMods.json). Their behaviour: WWOPerk::Table().
	namespace Perk
	{
		inline const FName Focus(TEXT("focus"));
		inline const FName Static(TEXT("static"));
		inline const FName Adrenal(TEXT("adrenal"));
		inline const FName Brace(TEXT("brace"));
		inline const FName Hunter(TEXT("hunter"));
		inline const FName Overpressure(TEXT("overpressure"));
		inline const FName FinalRound(TEXT("final_round"));
		inline const FName Anchor(TEXT("anchor"));
		inline const FName Patience(TEXT("patience"));
		inline const FName Hammer(TEXT("hammer"));
		inline const FName ColdBore(TEXT("cold_bore"));
		inline const FName Tighten(TEXT("tighten"));
		inline const FName Refund(TEXT("refund"));
		inline const FName FourthRound(TEXT("fourth_round"));
		inline const FName LongShot(TEXT("longshot"));
		inline const FName PointBlank(TEXT("point_blank"));
		inline const FName Executioner(TEXT("executioner"));
		inline const FName Breath(TEXT("breath"));
		inline const FName SlideShot(TEXT("slide_shot"));
		inline const FName FullBurst(TEXT("full_burst"));
		inline const FName Ricochet(TEXT("ricochet"));
		inline const FName Scavenger(TEXT("scavenger"));
	}

	// Grenades (UWWOGrenadeData::Id). Their behaviour goes by kind (EWWOGrenadeKind), not id; only
	// looks test an id.
	namespace Grenade
	{
		inline const FName VoltGrenade(TEXT("volt_grenade"));
		inline const FName ShockPool(TEXT("shock_pool"));
		inline const FName SnareMine(TEXT("snare_mine"));
		inline const FName SporePool(TEXT("spore_pool"));
		inline const FName PoisonSpike(TEXT("poison_spike"));
		inline const FName SporeBlast(TEXT("spore_blast"));
		inline const FName WallGrenade(TEXT("wall_grenade"));
		inline const FName IronGrenade(TEXT("iron_grenade"));
		inline const FName BlindingGrenade(TEXT("blinding_grenade"));
	}
}
