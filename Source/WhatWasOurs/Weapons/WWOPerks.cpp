// The perk table and the gun's modded stats (see Weapons/WWOPerks.h).

#include "Weapons/WWOPerks.h"
#include "Data/WWOData.h"
#include "Flora/WWOFloraEffects.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "WWOTypes.h"
#include "WWOIds.h"

using namespace WWOPerk;

namespace
{
	FString Lit(bool bOn, const TCHAR* Text) { return bOn ? FString(Text) : FString(); }

	bool UnderHalf(const IWWOTargetable& T, float Share)
	{
		return T.GetHealth() + T.GetShield() < Share * (T.GetMaxHealth() + T.GetMaxShield());
	}

	// The arcs, the same on the shooter's machine and on the host (which runs them for a joiner).
	void RicochetArc(UWorld* World, AActor* From, AActor* Shooter, float Damage) // one bounce
	{
		AWWOChainLightning::SpawnFrom(World, From, Shooter, FLinearColor(1.f, 0.62f, 0.2f), Damage, 8.f, 1.f, 1, 0.04f);
	}
	void StaticArc(UWorld* World, AActor* From, AActor* Shooter, float Damage) // two jumps
	{
		AWWOChainLightning::SpawnFrom(World, From, Shooter, FLinearColor(0.45f, 0.75f, 1.f), Damage, 6.f, 0.85f, 2, 0.07f);
	}
	constexpr int32 StaticEvery = 10; // hits per Static arc
}

TConstArrayView<FWWOPerk> WWOPerk::Table()
{
	// Built on first use (FNames), in the HUD line's order.
	static const FWWOPerk Perks[] = {
		{ // Focus: after 5 hits in a row (a miss ends the streak), more damage.
			.Id = WWOIds::Perk::Focus,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView& V) { if (V.State.HitStreak >= 5) G.Damage *= Focus; },
			.Hud = [](const FWWOPerkView& V) { return V.State.HitStreak >= 5 ? FString(TEXT("Focus ACTIVE")) : FString::Printf(TEXT("Focus %d/5"), V.State.HitStreak); },
			.HostDamage = Focus,
		},
		{ // Static: every 10th hit arcs to two more targets for twice the gun's damage.
			.Id = WWOIds::Perk::Static,
			.Hud = [](const FWWOPerkView& V) { return FString::Printf(TEXT("Static %d/10"), V.State.StaticHits); },
			.OnShot = [](FWWOPerkShot& S)
			{
				if (S.HitCount <= 0) return;
				S.State.StaticHits += 1;
				if (S.State.StaticHits >= StaticEvery && (S.FirstHit || S.bJoiner))
				{
					S.State.StaticHits = 0;
					if (!S.bJoiner) StaticArc(S.World, S.FirstHit, S.Shooter, S.Damage * 2.f);
				}
			},
			.HostShot = [](FWWOPerkHostShot& S)
			{
				if (S.FirstHit && ++S.StaticHits >= StaticEvery)
				{
					S.StaticHits = 0;
					StaticArc(S.World, S.FirstHit, S.Shooter, S.Round * 2.f);
				}
			},
		},
		{ // Adrenal: faster reloads for a while after a kill (UWWOWeaponComponent::UpdateReload).
			.Id = WWOIds::Perk::Adrenal,
			.Hud = [](const FWWOPerkView& V) { return V.State.AdrenalLeft > 0.f ? FString::Printf(TEXT("Adrenal %.1fs"), V.State.AdrenalLeft) : FString(); },
			.OnKill = [](FWWOPerkKill& K) { K.State.AdrenalLeft = AdrenalTime; },
		},
		{ // Brace: less recoil crouched or sliding.
			.Id = WWOIds::Perk::Brace,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView& V)
			{
				if (V.bCrouching || V.bSliding)
				{
					G.RecoilUp *= 0.6f;
					G.RecoilSide *= 0.6f;
				}
			},
			.Hud = [](const FWWOPerkView& V) { return Lit(V.bCrouching || V.bSliding, TEXT("Braced")); },
		},
		{ // Hunter: crits hit harder the further you're aimed in.
			.Id = WWOIds::Perk::Hunter,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView& V) { G.CritMultiplier += HunterCrit * V.AdsAmount; },
			.Hud = [](const FWWOPerkView& V) { return Lit(V.AdsAmount > 0.5f, TEXT("Hunter")); },
			.HostCrit = HunterCrit,
		},
		{ // Overpressure: the first shot after a reload.
			.Id = WWOIds::Perk::Overpressure,
			.ShotOn = [](const FWWOPerkView& V) { return V.State.bOverpressure; },
			.ShotScale = Overpressure,
			.Hud = [](const FWWOPerkView& V) { return Lit(V.State.bOverpressure, TEXT("Overpressure READY")); },
			.HostDamage = Overpressure,
		},
		{ // Final Round: the last round in the magazine (Ammo drops after the shot).
			.Id = WWOIds::Perk::FinalRound,
			.ShotOn = [](const FWWOPerkView& V) { return V.Ammo == 1; },
			.ShotScale = FinalRound,
			.Hud = [](const FWWOPerkView& V) { return Lit(V.Ammo == 1, TEXT("Final Round")); },
			.HostDamage = FinalRound,
		},
		{ // Anchor: half the recoil aimed in and standing still.
			.Id = WWOIds::Perk::Anchor,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView& V)
			{
				if (V.bAnchored)
				{
					G.RecoilUp *= 0.5f;
					G.RecoilSide *= 0.5f;
				}
			},
			.Hud = [](const FWWOPerkView& V) { return Lit(V.bAnchored, TEXT("Anchored")); },
		},
		{ // Patience: a crit after PatienceTime fully aimed in hits harder.
			.Id = WWOIds::Perk::Patience,
			.ShotOn = [](const FWWOPerkView& V) { return V.State.AimHeld >= PatienceTime; },
			.ShotCrit = PatienceCrit,
			.Hud = [](const FWWOPerkView& V) { return V.AdsAmount > 0.5f ? FString(V.State.AimHeld >= PatienceTime ? TEXT("Patience READY") : TEXT("Patience")) : FString(); },
			.HostCrit = PatienceCrit,
		},
		{ // Hammer: faster fire for a while after a kill.
			.Id = WWOIds::Perk::Hammer,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView& V) { if (V.State.HammerLeft > 0.f) G.FireRate *= HammerRate; },
			.Hud = [](const FWWOPerkView& V) { return V.State.HammerLeft > 0.f ? FString::Printf(TEXT("Hammer %.1fs"), V.State.HammerLeft) : FString(); },
			.OnKill = [](FWWOPerkKill& K) { K.State.HammerLeft = HammerTime; },
			.HostRate = HammerRate,
		},
		{ // Cold Bore: the first shot after ColdBoreTime without firing.
			.Id = WWOIds::Perk::ColdBore,
			.ShotOn = [](const FWWOPerkView& V) { return V.State.SinceShot >= ColdBoreTime; },
			.ShotScale = ColdBore,
			.Hud = [](const FWWOPerkView& V) { return Lit(V.State.SinceShot >= ColdBoreTime, TEXT("Cold Bore READY")); },
			.HostDamage = ColdBore,
			.HostOnShot = [](float SinceShot) { return SinceShot >= ColdBoreTime - 0.5f; },
		},
		{ // Tighten: the pellets close up as you aim in.
			.Id = WWOIds::Perk::Tighten,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView& V) { G.PelletSpread *= 1.f - 0.3f * V.AdsAmount; },
			.Hud = [](const FWWOPerkView& V) { return Lit(V.AdsAmount > 0.5f, TEXT("Tightened")); },
		},
		{ // Marksman: a crit's round comes back (UWWOWeaponComponent::FireRound).
			.Id = WWOIds::Perk::Refund,
			.Hud = [](const FWWOPerkView&) { return FString(TEXT("Marksman")); },
			.OnShot = [](FWWOPerkShot& S) { if (S.bCritical) ++S.State.RefundRounds; },
		},
		{ // Fourth Round: a burst gun fires one more round a burst.
			.Id = WWOIds::Perk::FourthRound,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView&) { if (G.BurstCount() > 1) G.Burst = G.BurstCount() + 1.f; },
			.Hud = [](const FWWOPerkView&) { return FString(TEXT("Fourth Round")); },
			.HostBurst = 1,
		},
		{ // Long Shot: far targets.
			.Id = WWOIds::Perk::LongShot,
			.TargetOn = [](float Meters, const IWWOTargetable&) { return Meters >= LongShotRange; },
			.TargetScale = LongShot,
			.Hud = [](const FWWOPerkView& V) { return Lit(V.Aimed && V.AimedMeters >= LongShotRange, TEXT("Long Shot")); },
			.HostDamage = LongShot,
			.HostOnTarget = [](float Meters, const IWWOTargetable&) { return Meters >= LongShotRange - 3.f; },
		},
		{ // Point Blank: close targets.
			.Id = WWOIds::Perk::PointBlank,
			.TargetOn = [](float Meters, const IWWOTargetable&) { return Meters <= PointBlankRange; },
			.TargetScale = PointBlank,
			.Hud = [](const FWWOPerkView& V) { return Lit(V.Aimed && V.AimedMeters <= PointBlankRange, TEXT("Point Blank")); },
			.HostDamage = PointBlank,
			.HostOnTarget = [](float Meters, const IWWOTargetable&) { return Meters <= PointBlankRange + 2.f; },
		},
		{ // Finisher: targets under half their health and shield.
			.Id = WWOIds::Perk::Executioner,
			.TargetOn = [](float, const IWWOTargetable& T) { return UnderHalf(T, 0.5f); },
			.TargetScale = Finisher,
			.Hud = [](const FWWOPerkView& V) { return Lit(V.Aimed && UnderHalf(*V.Aimed, 0.5f), TEXT("Finisher")); },
			.HostDamage = Finisher,
			.HostOnTarget = [](float, const IWWOTargetable& T) { return UnderHalf(T, 0.65f); },
		},
		{ // Held Breath: aimed in for BreathTime, the gun steadies.
			.Id = WWOIds::Perk::Breath,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView& V)
			{
				if (V.State.AimHeld >= BreathTime)
				{
					G.Sway *= 0.1f;
					G.RecoilUp *= 0.7f;
					G.RecoilSide *= 0.7f;
				}
			},
			.Hud = [](const FWWOPerkView& V) { return V.AdsAmount > 0.5f ? FString(V.State.AimHeld >= BreathTime ? TEXT("Breath HELD") : TEXT("Breath")) : FString(); },
		},
		{ // Slideshot: while sliding, tighter pellets and more damage.
			.Id = WWOIds::Perk::SlideShot,
			.Stats = [](FWWOGunStats& G, const FWWOPerkView& V)
			{
				if (V.bSliding)
				{
					G.PelletSpread *= 0.5f;
					G.Damage *= SlideShot;
				}
			},
			.Hud = [](const FWWOPerkView& V) { return Lit(V.bSliding, TEXT("Slideshot")); },
			.HostDamage = SlideShot,
		},
		{ // Full Burst: the burst after one that landed every round hits harder.
			.Id = WWOIds::Perk::FullBurst,
			.ShotOn = [](const FWWOPerkView& V) { return V.State.bBurstBoosted; },
			.ShotScale = FullBurst,
			.Hud = [](const FWWOPerkView& V) { return Lit(V.State.bFullBurst || V.State.bBurstBoosted, TEXT("Full Burst READY")); },
			.OnBurstEnd = [](FWWOPerkState& State, bool bEveryRoundHit) { State.bFullBurst = bEveryRoundHit; },
			.HostDamage = FullBurst,
		},
		{ // Ricochet: a crit bounces to one more target for 60% of the gun's damage.
			.Id = WWOIds::Perk::Ricochet,
			.OnShot = [](FWWOPerkShot& S) { if (!S.bJoiner && S.bCritical && S.FirstHit) RicochetArc(S.World, S.FirstHit, S.Shooter, S.Damage * 0.6f); },
			.HostShot = [](FWWOPerkHostShot& S) { if (S.FirstCrit) RicochetArc(S.World, S.FirstCrit, S.Shooter, S.Round * 0.6f); },
		},
		{ // Scavenger: a kill puts a fifth of a magazine back.
			.Id = WWOIds::Perk::Scavenger,
			.OnKill = [](FWWOPerkKill& K) { K.Ammo = FMath::Min(K.Ammo + FMath::Max(1, FMath::RoundToInt(K.MagazineSize * 0.2f)) * K.Kills, K.MagazineSize); },
		},
	};
	return MakeArrayView(Perks);
}

FWWOPerkList WWOPerk::Collect(TFunctionRef<bool(FName Id)> Has)
{
	FWWOPerkList Out;
	for (const FWWOPerk& Perk : Table()) if (Has(Perk.Id)) Out.Add(&Perk);
	return Out;
}

FWWOPerkList WWOPerk::Equipped(const UWWOLoadoutSubsystem& Loadout)
{
	return Collect([&Loadout](FName Id) { return Loadout.HasPerk(Id); });
}

FWWOGunStats WWOPerk::ComputeGunStats(const UWWOWeaponData& Base, TFunctionRef<float(FName Stat)> Percent)
{
	auto Scaled = [&Percent](float Value, FName Stat) { return Value * FMath::Max(1.f + Percent(Stat), 0.f); };
	FWWOGunStats G;
	G.FireRange = Scaled(Base.FireRange, "range");
	G.RecoilSide = Scaled(Base.RecoilSide, "recoil_side");
	G.RecoilUp = Scaled(Base.RecoilUp, "recoil_up");
	G.MagazineSize = Scaled(Base.MagazineSize, "magazine_size");
	G.AdsMoveSpeed = Scaled(Base.AdsMoveSpeed, "ads_move_speed");
	G.Sway = Scaled(Base.Sway, "sway");
	G.Damage = Scaled(Base.Damage, "damage");
	G.FireRate = Scaled(Base.FireRate, "fire_rate");
	G.CritMultiplier = Scaled(Base.CritMultiplier, "crit_multiplier");
	G.PelletSpread = Scaled(Base.PelletSpread, "pellet_spread");
	G.HipSpread = Scaled(Base.HipSpread, "hip_spread");
	// Zoom is a magnification: +40% means 40% more of the zoom past 1x.
	G.Zoom = 1.f + (Base.Zoom - 1.f) * FMath::Max(1.f + Percent("zoom"), 0.f);
	G.MagazineSize = FMath::Max(1.f, FMath::RoundToFloat(G.MagazineSize));
	G.Burst = Base.Burst;
	G.Pellets = Base.Pellets;
	return G;
}

void FWWOGunStats::WriteTo(UWWOWeaponData& Gun) const
{
	Gun.FireRange = FireRange;
	Gun.RecoilSide = RecoilSide;
	Gun.RecoilUp = RecoilUp;
	Gun.MagazineSize = MagazineSize;
	Gun.AdsMoveSpeed = AdsMoveSpeed;
	Gun.Sway = Sway;
	Gun.Damage = Damage;
	Gun.FireRate = FireRate;
	Gun.CritMultiplier = CritMultiplier;
	Gun.PelletSpread = PelletSpread;
	Gun.HipSpread = HipSpread;
	Gun.Zoom = Zoom;
	Gun.Burst = Burst;
	Gun.Pellets = Pellets;
}
