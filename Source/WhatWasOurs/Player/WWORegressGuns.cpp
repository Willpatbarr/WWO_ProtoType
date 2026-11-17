// Regression scenarios: guns. Every weapon bare against a dummy and a Soldier (body and head), every
// mod perk with its condition met and not met, and every mod's stat changes. Generated from the
// catalogs (the weapon assets and Config/WeaponMods.json), so a new gun or mod is covered as it lands.

#include "Player/WWORegress.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOWeaponComponent.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "World/WWOTarget.h"
#include "Enemies/WWOEnemy.h"
#include "Data/WWOData.h"
#include "Camera/CameraComponent.h"

namespace
{
	// One scripted shot (or a few), with everything the perks look at set up around it.
	struct FShotPlan
	{
		FName Weapon;
		TArray<FName> Mods;
		FName Target = "dummy";   // dummy or soldier
		float Distance = 10.f;    // m
		bool bHead = false;
		float TargetHealth = -1.f; // a dummy's max health (-1: the Targets setting)
		float PreDamage = 0.f;     // taken off the target before the shot (no attacker)
		int32 Extras = 0;          // more dummies 3 m to each side of the target (chains)
		int32 AdsTicks = 0;        // aimed in this long before the shot (and through it)
		bool bCrouch = false;
		bool bSlide = false;
		int32 Pulls = 1;           // trigger pulls, PullGap ticks apart
		int32 PullGap = 40;
		bool bReloadAfter = false; // then reload, timing it
		bool bRateAfter = false;   // then hold the trigger at nothing for a second, counting rounds
		TFunction<void(UWWOWeaponComponent&)> Before; // the gun's state set just before the first pull
	};

	struct FShotState
	{
		TWeakObjectPtr<AActor> Target;
		TArray<TWeakObjectPtr<AActor>> Extras;
		float TargetBefore = 0.f, ExtrasBefore = 0.f;
		int32 AmmoBefore = 0, FireTick = 0, SlideTick = -1, ReloadTicks = 0, RateRounds = 0, RateAmmo = 0, RateStart = -1;
		float PitchBefore = 0.f, YawBefore = 0.f;
		bool bReloadStarted = false;
	};

	// A view angle, to 0.0001 degrees: the aim starts from the player's eye, whose height carries a tiny
	// float residue that depends on what ran before (see FWWORegress::RecordVec).
	FString Angle(double Degrees)
	{
		const FString T = FString::Printf(TEXT("%.4f"), Degrees);
		return T == TEXT("-0.0000") ? FString(TEXT("0.0000")) : T;
	}

	float PitchDeg(const AWWOCharacter* P) { return FMath::RadiansToDegrees(P->GetPitch()); }

	FVector PartOf(AActor* Target, bool bHead)
	{
		const IWWOTargetable* T = Cast<IWWOTargetable>(Target);
		if (!T) return Target ? Target->GetActorLocation() : FVector::ZeroVector;
		const TArray<FWWOAimPart> Parts = T->GetAimParts();
		for (const FWWOAimPart& Part : Parts) if (Part.bCritical == bHead) return Part.Position;
		return Parts.Num() > 0 ? Parts.Last().Position : Target->GetActorLocation();
	}

	// A reason as one golden value: spaces to underscores, no quotes (Quote's escape for ' isn't JSON).
	FString Words(const FString& Why)
	{
		return Why.Replace(TEXT(" "), TEXT("_")).Replace(TEXT("'"), TEXT(""));
	}

	FString PerkLine(const UWWOWeaponComponent& W)
	{
		FString S = W.PerkStatus();
		S.ReplaceInline(TEXT("  ·  "), TEXT("|"));
		S.ReplaceInline(TEXT(" "), TEXT("_"));
		return S.IsEmpty() ? FString(TEXT("-")) : S;
	}
}

void FWWORegress::AddGunScenarios(TArray<FWWORegressScenario>& Out, const UWWOLoadoutSubsystem& L)
{
	// The stats a mod or perk can change, as the gun has them this frame (RefreshStats).
	auto RecordStats = [](FWWORegress& R, const FString& Prefix)
	{
		const UWWOWeaponComponent* G = R.Player()->Weapon;
		const UWWOWeaponData* W = G->GetWeaponData();
		R.Record(Prefix + TEXT("damage"), W->Damage);
		R.Record(Prefix + TEXT("rate"), W->FireRate);
		R.Record(Prefix + TEXT("burst"), W->BurstCount());
		R.Record(Prefix + TEXT("crit_mult"), W->CritMultiplier);
		R.Record(Prefix + TEXT("recoil_up"), W->RecoilUp);
		R.Record(Prefix + TEXT("recoil_side"), W->RecoilSide);
		R.Record(Prefix + TEXT("pellet_spread"), W->PelletSpread);
		R.Record(Prefix + TEXT("hip_spread"), W->HipSpread);
		R.Record(Prefix + TEXT("sway"), W->Sway);
	};

	// --- The shot script every gun and perk scenario uses ---
	auto MakeShot = [RecordStats](const FString& Name, const FShotPlan& Plan)
	{
		FWWORegressScenario S;
		S.Name = Name;
		TSharedRef<FShotState> St = MakeShared<FShotState>();
		S.Setup = [Plan, St](FWWORegress& R)
		{
			*St = FShotState();
			R.EquipWeapon(Plan.Weapon);
			for (const FName& Mod : Plan.Mods) R.EquipMod(Mod);
			const FVector At = R.Ahead(Plan.Distance);
			AActor* T = Plan.Target == "dummy" ? (AActor*)R.SpawnDummy(At, Plan.TargetHealth) : (AActor*)R.SpawnEnemy(Plan.Target, At);
			St->Target = T;
			for (int32 i = 0; i < Plan.Extras; ++i) St->Extras.Add(R.SpawnDummy(R.Ahead(Plan.Distance, (i % 2 == 0 ? 3.f : -3.f) * (1 + i / 2)), 1000.f));
		};
		S.Step = [Plan, St, RecordStats](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* P = R.Player();
			UWWOWeaponComponent* G = P->Weapon;
			AActor* T = St->Target.Get();
			const bool bSemi = G->GetWeaponData()->IsSemiAuto();
			if (Tick == 0)
			{
				if (T && Plan.PreDamage > 0.f) Cast<IWWOTargetable>(T)->TakeHit(Plan.PreDamage, false, T->GetActorLocation());
				if (Plan.bCrouch) R.Hold(EWWOAction::Crouch, true);
				if (Plan.bSlide) { R.Move(FVector2D(0.f, 1.f)); R.Press(EWWOAction::Sprint); }
				St->FireTick = FMath::Max(Plan.AdsTicks, 30) + 2;
			}
			if (Plan.bSlide && St->SlideTick < 0)
			{
				if (Tick == 40) R.Hold(EWWOAction::Crouch, true);
				if (P->MoveState.bSliding) { St->SlideTick = Tick; St->FireTick = Tick + 4; }
				else if (Tick > 80) { St->SlideTick = 0; St->FireTick = Tick; } // never slid: shoot anyway (recorded)
				else return false;
			}
			if (Plan.AdsTicks > 0 && Tick == St->FireTick - Plan.AdsTicks) R.Hold(EWWOAction::Aim, true);
			const int32 F = St->FireTick;
			const int32 Last = F + (Plan.Pulls - 1) * Plan.PullGap;
			// Each pull: re-aim, set the perk state, the press (a semi-auto pull is read a tick later).
			for (int32 Pull = 0; Pull < Plan.Pulls; ++Pull)
			{
				const int32 At = F + Pull * Plan.PullGap;
				if (Tick == At - 1)
				{
					if (T) R.AimAt(PartOf(T, Plan.bHead));
					if (Pull == 0 && Plan.Before) Plan.Before(*G);
					if (bSemi) R.Press(EWWOAction::Fire);
				}
				if (Tick == At)
				{
					if (Pull == 0)
					{
						RecordStats(R, TEXT(""));
						R.Record(TEXT("perks"), PerkLine(*G));
						St->AmmoBefore = G->Ammo;
						St->TargetBefore = FWWORegress::HealthOf(T);
						St->ExtrasBefore = 0.f;
						for (const TWeakObjectPtr<AActor>& E : St->Extras) St->ExtrasBefore += FWWORegress::HealthOf(E.Get());
						St->PitchBefore = PitchDeg(P);
						R.RecordVec(TEXT("eye"), P->EyeLocation());
						St->YawBefore = (float)P->GetControlRotation().Yaw;
						if (Plan.bSlide) R.RecordBool(TEXT("sliding"), P->MoveState.bSliding);
					}
					if (!bSemi) R.Hold(EWWOAction::Fire, true);
				}
				if (Tick == At + 1) R.Hold(EWWOAction::Fire, false);
			}
			if (Tick == Last + 2 && !Plan.bSlide && Plan.AdsTicks == 0)
			{
				R.Record(TEXT("kick_pitch"), Angle(PitchDeg(P) - St->PitchBefore));
				R.Record(TEXT("kick_yaw"), Angle(FRotator::NormalizeAxis(P->GetControlRotation().Yaw - St->YawBefore)));
			}
			const int32 Settle = Last + 30;
			if (Tick == Settle)
			{
				const float After = FWWORegress::HealthOf(T);
				R.Record(TEXT("dealt"), St->TargetBefore - After);
				R.Record(TEXT("target_after"), After);
				R.RecordBool(TEXT("killed"), T && Cast<IWWOTargetable>(T)->IsDead());
				if (Plan.Extras > 0)
				{
					float Now = 0.f;
					for (const TWeakObjectPtr<AActor>& E : St->Extras) Now += FWWORegress::HealthOf(E.Get());
					R.Record(TEXT("chain_dealt"), St->ExtrasBefore - Now);
				}
				R.Record(TEXT("ammo_used"), St->AmmoBefore - G->Ammo);
				R.Record(TEXT("confirms"), R.Confirms);
				R.Record(TEXT("crits"), R.ConfirmCrits);
				R.Record(TEXT("kills"), R.ConfirmKills);
				R.Hold(EWWOAction::Aim, false);
				R.Hold(EWWOAction::Crouch, false);
				R.Move(FVector2D::ZeroVector);
				if (!Plan.bReloadAfter && !Plan.bRateAfter) return true;
				R.Record(TEXT("stats_after"), FString::Printf(TEXT("%.9g/%.9g"), G->GetWeaponData()->FireRate, G->GetWeaponData()->Damage));
			}
			if (Plan.bReloadAfter && Tick == Settle + 1) R.Press(EWWOAction::Reload);
			if (Plan.bReloadAfter && Tick > Settle + 1)
			{
				if (G->bReloading) { St->bReloadStarted = true; ++St->ReloadTicks; }
				else if (St->bReloadStarted || Tick > Settle + 20)
				{
					R.Record(TEXT("reload_ticks"), St->ReloadTicks);
					R.Record(TEXT("ammo_after_reload"), G->Ammo);
					return true;
				}
			}
			if (Plan.bRateAfter && Tick > Settle)
			{
				// At the sky: nothing to hit. Count the rounds a second of trigger gets out.
				if (Tick == Settle + 1) { R.Face(P->GetControlRotation().Yaw, 45.f); St->RateAmmo = G->Ammo; }
				if (Tick > Settle + 1 && Tick <= Settle + 61)
				{
					R.Hold(EWWOAction::Fire, true);
					R.Press(EWWOAction::Fire);
				}
				if (Tick == Settle + 62)
				{
					R.Hold(EWWOAction::Fire, false);
					R.Record(TEXT("rounds_in_1s"), St->RateAmmo - G->Ammo);
					return true;
				}
			}
			return false;
		};
		return S;
	};

	// --- (a) Every weapon, no mods: body and head, on a dummy and on an inert Soldier, at 10 m ---
	for (const UWWOWeaponData* W : L.Weapons)
	{
		if (!W) continue;
		for (const FName Target : { FName("dummy"), FName("soldier") })
		{
			for (const bool bHead : { false, true })
			{
				FShotPlan Plan;
				Plan.Weapon = W->Id;
				Plan.Target = Target;
				Plan.bHead = bHead;
				Plan.TargetHealth = Target == "dummy" ? 1000.f : -1.f; // big enough that no gun kills it in one
				Out.Add(MakeShot(FString::Printf(TEXT("gun.%s.%s.%s"), *W->Id.ToString(), *Target.ToString(), bHead ? TEXT("head") : TEXT("body")), Plan));
			}
		}
		// The default dummy (the Targets setting's health): kill or not.
		FShotPlan Plain;
		Plain.Weapon = W->Id;
		Plain.bHead = true;
		Out.Add(MakeShot(FString::Printf(TEXT("gun.%s.dummy_default.head"), *W->Id.ToString()), Plain));
		// Multi-pellet guns: how many land at a longer range, aimed in and from the hip.
		if (W->Pellets > 1.5f)
		{
			for (const float Range : { 5.f, 15.f })
			{
				for (const bool bAds : { false, true })
				{
					FShotPlan Spread;
					Spread.Weapon = W->Id;
					Spread.TargetHealth = 1000.f;
					Spread.Distance = Range;
					Spread.AdsTicks = bAds ? 40 : 0;
					Out.Add(MakeShot(FString::Printf(TEXT("gun.%s.pellets.%.0fm.%s"), *W->Id.ToString(), Range, bAds ? TEXT("ads") : TEXT("hip")), Spread));
				}
			}
		}
		// Five pulls in a row on the body: recoil, a burst's pacing, ammo.
		FShotPlan Five;
		Five.Weapon = W->Id;
		Five.TargetHealth = 5000.f;
		Five.Pulls = 5;
		Five.PullGap = 20;
		Out.Add(MakeShot(FString::Printf(TEXT("gun.%s.five_pulls"), *W->Id.ToString()), Five));
	}

	// --- (b) Every perk on every mod that carries it: the condition met, and not ---
	for (const UWWOModData* Mod : L.WeaponMods)
	{
		if (!Mod || Mod->Perk.IsNone()) continue;
		const FName Perk = Mod->Perk;
		for (const bool bMet : { true, false })
		{
			FShotPlan Plan;
			Plan.Weapon = Mod->Frame;
			Plan.Mods = { Mod->Id };
			Plan.TargetHealth = 1000.f;
			if (Perk == "focus") Plan.Before = [bMet](UWWOWeaponComponent& G) { G.HitStreak = bMet ? 5 : 0; };
			else if (Perk == "scavenger")
			{
				Plan.TargetHealth = bMet ? 5.f : 1000.f; // the shot kills it, or doesn't
				Plan.Before = [](UWWOWeaponComponent& G) { G.Ammo = FMath::Max(1, G.MagazineSize() / 2); };
			}
			else if (Perk == "brace") Plan.bCrouch = bMet;
			else if (Perk == "hunter") { Plan.bHead = true; Plan.AdsTicks = bMet ? 40 : 0; }
			else if (Perk == "static")
			{
				Plan.Extras = 2;
				Plan.Before = [bMet](UWWOWeaponComponent& G) { G.StaticHits = bMet ? 9 : 0; };
			}
			else if (Perk == "adrenal")
			{
				Plan.TargetHealth = bMet ? 5.f : 1000.f;
				Plan.bReloadAfter = true;
			}
			else if (Perk == "overpressure") Plan.Before = [bMet](UWWOWeaponComponent& G) { G.bOverpressure = bMet; };
			else if (Perk == "final_round") Plan.Before = [bMet](UWWOWeaponComponent& G) { if (bMet) G.Ammo = 1; };
			else if (Perk == "anchor") Plan.AdsTicks = bMet ? 40 : 0;
			else if (Perk == "patience") { Plan.bHead = true; Plan.AdsTicks = bMet ? 70 : 12; }
			else if (Perk == "ricochet") { Plan.bHead = bMet; Plan.Extras = 1; }
			else if (Perk == "hammer")
			{
				Plan.TargetHealth = bMet ? 5.f : 1000.f;
				Plan.bRateAfter = true;
			}
			else if (Perk == "point_blank") Plan.Distance = bMet ? 3.f : 10.f;
			else if (Perk == "slide_shot") { Plan.bSlide = bMet; Plan.Distance = 16.f; }
			else if (Perk == "tighten") Plan.AdsTicks = bMet ? 40 : 0;
			else if (Perk == "cold_bore") Plan.Before = [bMet](UWWOWeaponComponent& G) { G.SinceShot = bMet ? 100.f : 0.5f; };
			else if (Perk == "refund") Plan.bHead = bMet;
			else if (Perk == "breath") Plan.AdsTicks = bMet ? 90 : 20;
			else if (Perk == "longshot") Plan.Distance = bMet ? 35.f : 10.f;
			else if (Perk == "executioner") Plan.PreDamage = bMet ? 600.f : 0.f;
			else if (Perk == "full_burst") Plan.Before = [bMet](UWWOWeaponComponent& G) { G.bFullBurst = bMet; };
			else if (Perk == "fourth_round") { if (!bMet) Plan.Mods.Reset(); } // always on: "not met" is the gun without it
			else Plan.Before = nullptr; // a perk this list doesn't know: still shot once, both ways alike
			// The sniper misses from the hip (its hip spread): its perks are tried aimed in, unless the
			// perk is about aiming.
			if (Mod->Frame == "sniper" && Plan.AdsTicks == 0 && Perk != "breath") Plan.AdsTicks = 40;
			Out.Add(MakeShot(FString::Printf(TEXT("perk.%s.%s.%s"), *Mod->Frame.ToString(), *Mod->Id.ToString(), bMet ? TEXT("met") : TEXT("unmet")), Plan));
		}
	}

	// --- (c) Every weapon's stats bare and with each of its mods, as the gun reports them ---
	for (const UWWOWeaponData* W : L.Weapons)
	{
		if (!W) continue;
		TArray<const UWWOModData*> Mods = { nullptr };
		for (const UWWOModData* Mod : L.WeaponMods) if (Mod && Mod->Frame == W->Id) Mods.Add(Mod);
		for (const UWWOModData* Mod : Mods)
		{
			FWWORegressScenario S;
			S.Name = FString::Printf(TEXT("mods.%s.%s"), *W->Id.ToString(), Mod ? *Mod->Id.ToString() : TEXT("none"));
			const FName WeaponId = W->Id, ModId = Mod ? Mod->Id : NAME_None;
			S.Setup = [WeaponId, ModId](FWWORegress& R)
			{
				R.EquipWeapon(WeaponId);
				if (!ModId.IsNone()) R.EquipMod(ModId);
			};
			S.Step = [RecordStats](FWWORegress& R, int32 Tick)
			{
				if (Tick < 1) return false;
				AWWOCharacter* P = R.Player();
				const UWWOWeaponComponent* G = P->Weapon;
				const UWWOWeaponData* D = G->GetWeaponData();
				RecordStats(R, TEXT(""));
				R.Record(TEXT("magazine"), G->MagazineSize());
				R.Record(TEXT("ammo"), G->Ammo);
				R.Record(TEXT("range"), D->FireRange);
				R.Record(TEXT("pellets"), D->Pellets);
				R.Record(TEXT("zoom"), D->Zoom);
				R.Record(TEXT("ads_move"), D->AdsMoveSpeed);
				R.Record(TEXT("reload_time"), P->M("reload_time", D->ReloadTime));
				R.Record(TEXT("ads_time"), P->M("ads_time", D->AdsTime));
				R.Record(TEXT("swap_time"), P->M("swap_time", G->SwapTime));
				R.Record(TEXT("ready_time"), P->M("ready_time", G->ReadyTime));
				R.Record(TEXT("perks"), PerkLine(*G));
				return true;
			};
			Out.Add(S);
		}
	}

	// --- (d) The host's check of a joiner's shot (AWWOCharacter::GunLimits, CheckGunHit, ServerGunShot) ---
	// Every weapon bare, with each of its mods, with one perk mod in every slot, and two setups that
	// can't exist: the limits GunLimits works out, and the most damage CheckGunHit lets through for a
	// hit near and far, close, on a hurt target, after a pause and on the head.
	for (const UWWOWeaponData* W : L.Weapons)
	{
		if (!W) continue;
		struct FSetup { FString Name; TArray<FName> Mods; };
		TArray<FSetup> Setups = { { TEXT("none"), {} } };
		FSetup AllPerks = { TEXT("all_perks"), {} };
		TSet<FName> PerkSlots;
		TMap<FName, FName> FirstInSlot;
		FSetup TwoInSlot = { TEXT("two_in_slot"), {} };
		for (const UWWOModData* Mod : L.WeaponMods)
		{
			if (!Mod || Mod->Frame != W->Id) continue;
			Setups.Add({ Mod->Id.ToString(), { Mod->Id } });
			if (!Mod->Perk.IsNone() && !PerkSlots.Contains(Mod->Slot)) { PerkSlots.Add(Mod->Slot); AllPerks.Mods.Add(Mod->Id); }
			if (const FName* First = FirstInSlot.Find(Mod->Slot)) { if (TwoInSlot.Mods.Num() == 0) TwoInSlot.Mods = { *First, Mod->Id }; }
			else FirstInSlot.Add(Mod->Slot, Mod->Id);
		}
		if (AllPerks.Mods.Num() > 1) Setups.Add(AllPerks);
		if (TwoInSlot.Mods.Num() > 0) Setups.Add(TwoInSlot);
		for (const UWWOModData* Mod : L.WeaponMods)
		{
			if (Mod && Mod->Frame != W->Id) { Setups.Add({ TEXT("other_frame"), { Mod->Id } }); break; }
		}
		FWWORegressScenario S;
		S.Name = FString::Printf(TEXT("hostcheck.%s"), *W->Id.ToString());
		const FName WeaponId = W->Id;
		TSharedRef<TArray<TWeakObjectPtr<AActor>>> Dummies = MakeShared<TArray<TWeakObjectPtr<AActor>>>();
		S.Setup = [Dummies](FWWORegress& R)
		{
			Dummies->Reset();
			Dummies->Add(R.SpawnDummy(R.Ahead(10.f), 1000.f));       // near
			Dummies->Add(R.SpawnDummy(R.Ahead(35.f, 6.f), 1000.f));  // far (Long Shot)
			Dummies->Add(R.SpawnDummy(R.Ahead(3.f, 2.f), 1000.f));   // close (Point Blank)
			Dummies->Add(R.SpawnDummy(R.Ahead(10.f, -4.f), 1000.f)); // hurt (Finisher)
			Dummies->Add(R.SpawnDummy(R.Ahead(35.f, -8.f), 1000.f)); // far and hurt (every checked perk at once)
		};
		S.Step = [WeaponId, Setups, Dummies](FWWORegress& R, int32 Tick)
		{
			AWWOCharacter* P = R.Player();
			if (Tick == 0)
			{
				for (const int32 i : { 3, 4 }) if (AActor* Hurt = (*Dummies)[i].Get()) Cast<IWWOTargetable>(Hurt)->TakeHit(600.f, false, Hurt->GetActorLocation());
				return false;
			}
			const UWWOWeaponData* Gun = R.WeaponAsset(WeaponId);
			// The largest damage CheckGunHit accepts for this hit, exactly (bisecting the float's bits),
			// or why it accepts none.
			auto Allowance = [P](const FWWOGunShot& Shot, FWWOGunHit Hit, const FWWOGunLimits& Limits)
			{
				FString Why;
				Hit.Damage = 0.001f;
				if (!P->CheckGunHit(Shot, Hit, Limits, Why)) return Words(Why);
				uint32 Lo = FMath::AsUInt(0.001f), Hi = FMath::AsUInt(1.0e9f);
				Hit.Damage = 1.0e9f;
				if (P->CheckGunHit(Shot, Hit, Limits, Why)) return FString(TEXT("unbounded"));
				while (Hi - Lo > 1)
				{
					const uint32 Mid = Lo + (Hi - Lo) / 2;
					Hit.Damage = FMath::AsFloat(Mid);
					if (P->CheckGunHit(Shot, Hit, Limits, Why)) Lo = Mid; else Hi = Mid;
				}
				return FString::Printf(TEXT("%.9g"), FMath::AsFloat(Lo));
			};
			for (const FSetup& Setup : Setups)
			{
				const FString K = Setup.Name + TEXT(".");
				FWWOGunLimits Limits;
				FString Why;
				TSet<FName> Perks;
				if (!AWWOCharacter::GunLimits(R.Loadout(), Gun, Setup.Mods, Limits, Why, Perks))
				{
					R.Record(K + TEXT("refused"), Words(Why));
					continue;
				}
				R.Record(K + TEXT("damage"), Limits.Damage);
				R.Record(K + TEXT("rate"), Limits.Rate);
				R.Record(K + TEXT("range"), Limits.Range);
				R.Record(K + TEXT("round"), Limits.Round);
				R.Record(K + TEXT("always"), Limits.Always);
				R.Record(K + TEXT("crit"), Limits.Crit);
				R.Record(K + TEXT("burst"), Limits.Burst);
				R.Record(K + TEXT("pellets"), Limits.Pellets);
				R.Record(K + TEXT("checks"), FString::Printf(TEXT("%d%d%d%d"), Limits.bLongShot, Limits.bPointBlank, Limits.bFinisher, Limits.bColdBore));
				TArray<FString> PerkNames;
				for (const FName& Perk : Perks) PerkNames.Add(Perk.ToString());
				PerkNames.Sort();
				R.Record(K + TEXT("perks"), PerkNames.Num() > 0 ? FString::Join(PerkNames, TEXT("|")) : FString(TEXT("-")));
				FWWOGunShot Shot;
				Shot.WeaponId = WeaponId;
				Shot.Mods = Setup.Mods;
				Shot.Origin = P->EyeLocation();
				auto Hit = [](AActor* T, bool bHead) { FWWOGunHit H; H.Target = T; H.bCritical = bHead; H.Point = PartOf(T, bHead); return H; };
				Limits.SinceShot = 0.f;
				R.Record(K + TEXT("most.near"), Allowance(Shot, Hit((*Dummies)[0].Get(), false), Limits));
				R.Record(K + TEXT("most.far"), Allowance(Shot, Hit((*Dummies)[1].Get(), false), Limits));
				R.Record(K + TEXT("most.close"), Allowance(Shot, Hit((*Dummies)[2].Get(), false), Limits));
				R.Record(K + TEXT("most.hurt"), Allowance(Shot, Hit((*Dummies)[3].Get(), false), Limits));
				R.Record(K + TEXT("most.head"), Allowance(Shot, Hit((*Dummies)[0].Get(), true), Limits));
				Limits.SinceShot = 10.f;
				R.Record(K + TEXT("most.rested"), Allowance(Shot, Hit((*Dummies)[0].Get(), false), Limits));
				R.Record(K + TEXT("most.rested_head_far"), Allowance(Shot, Hit((*Dummies)[1].Get(), true), Limits));
				R.Record(K + TEXT("most.rested_far_hurt"), Allowance(Shot, Hit((*Dummies)[4].Get(), false), Limits));
				R.Record(K + TEXT("most.rested_far_hurt_head"), Allowance(Shot, Hit((*Dummies)[4].Get(), true), Limits));
			}
			return true;
		};
		Out.Add(S);
	}

	// --- (e) The host applying a joiner's shot: the arc perks it runs for the joiner, and kill credit ---
	{
		auto ModWith = [&L](FName Frame, FName Perk) -> FName
		{
			for (const UWWOModData* Mod : L.WeaponMods) if (Mod && Mod->Frame == Frame && Mod->Perk == Perk) return Mod->Id;
			return NAME_None;
		};
		struct FHostShot { FString Name; FName Weapon; FName Mod; bool bMet = false; bool bKill = false; };
		TArray<FHostShot> Shots;
		for (const bool bMet : { true, false })
		{
			const TCHAR* Met = bMet ? TEXT("met") : TEXT("unmet");
			Shots.Add({ FString::Printf(TEXT("hostshot.static.%s"), Met), "rifle", ModWith("rifle", "static"), bMet, false });
			Shots.Add({ FString::Printf(TEXT("hostshot.ricochet.%s"), Met), "handcannon", ModWith("handcannon", "ricochet"), bMet, false });
			Shots.Add({ FString::Printf(TEXT("hostshot.kill_credit.%s"), Met), "handcannon", ModWith("handcannon", "hammer"), bMet, true });
		}
		for (const FHostShot& H : Shots)
		{
			if (H.Mod.IsNone()) continue;
			FWWORegressScenario S;
			S.Name = H.Name;
			TSharedRef<FShotState> St = MakeShared<FShotState>();
			S.Setup = [H, St](FWWORegress& R)
			{
				*St = FShotState();
				// The host's own gun carries the mod for the kill-credit case (its kill perks run on the
				// joiner's machine from the host's word: here, the same player).
				R.EquipWeapon(H.Weapon);
				if (H.bKill) R.EquipMod(H.Mod);
				St->Target = R.SpawnDummy(R.Ahead(10.f), H.bKill && H.bMet ? 5.f : 1000.f);
				for (int32 i = 0; i < 2; ++i) St->Extras.Add(R.SpawnDummy(R.Ahead(10.f, i == 0 ? 3.f : -3.f), 1000.f));
			};
			S.Step = [H, St](FWWORegress& R, int32 Tick)
			{
				AWWOCharacter* P = R.Player();
				AActor* T = St->Target.Get();
				if (Tick == 0)
				{
					P->ShotBudgets.Reset();
					P->Weapon->HostStaticGun = H.Weapon;
					P->Weapon->HostStaticHits = H.Name.Contains(TEXT("static")) && H.bMet ? 9 : 0; // the 10th hit arcs
					St->TargetBefore = FWWORegress::HealthOf(T);
					for (const TWeakObjectPtr<AActor>& E : St->Extras) St->ExtrasBefore += FWWORegress::HealthOf(E.Get());
					const int32 Taken = AWWOCharacter::NetShotsTaken, Applied = AWWOCharacter::NetHitsApplied, Rejected = AWWOCharacter::NetHitsRejected, Credited = AWWOCharacter::NetKillsCredited;
					FWWOGunShot Shot;
					Shot.WeaponId = H.Weapon;
					Shot.Mods = { H.Mod };
					Shot.Origin = P->EyeLocation();
					FWWOGunHit Hit;
					Hit.Target = T;
					Hit.Damage = 10.f; // under every gun's one round
					Hit.bCritical = H.Name.Contains(TEXT("ricochet")) ? H.bMet : false;
					Hit.Point = PartOf(T, Hit.bCritical);
					Shot.Hits.Add(Hit);
					Shot.Ends.Add(Hit.Point);
					P->ServerGunShot_Implementation(Shot);
					R.Record(TEXT("taken"), AWWOCharacter::NetShotsTaken - Taken);
					R.Record(TEXT("applied"), AWWOCharacter::NetHitsApplied - Applied);
					R.Record(TEXT("rejected"), AWWOCharacter::NetHitsRejected - Rejected);
					R.Record(TEXT("credited"), AWWOCharacter::NetKillsCredited - Credited);
					R.Record(TEXT("static_hits"), P->Weapon->HostStaticHits);
					return false;
				}
				if (Tick < 60) return false;
				R.Record(TEXT("dealt"), St->TargetBefore - FWWORegress::HealthOf(T));
				float Now = 0.f;
				for (const TWeakObjectPtr<AActor>& E : St->Extras) Now += FWWORegress::HealthOf(E.Get());
				R.Record(TEXT("chain_dealt"), St->ExtrasBefore - Now);
				R.Record(TEXT("hammer_left"), P->Weapon->HammerLeft);
				R.Record(TEXT("confirms"), R.Confirms);
				R.Record(TEXT("kills"), R.ConfirmKills);
				return true;
			};
			Out.Add(S);
		}
	}
}
