// -WWOAutoTestNet: a two-machine (or two-copy) check of online play. Run it on the host
// (-WWOHost -WWOAutoTestNet) and on the joiner (-WWOJoin=<ip> -WWOAutoTestNet).
// Once both players are in, the joiner walks, sprints, slides, stops, crouch walks, jumps
// (single and double) and runs at a crate to mantle, logging the host's corrections in each step
// (`AutoTest net: <step> ...`, each one in detail as `NetCorrection ...`). Both log the joiner's position every second (compare the two logs),
// and take screenshots of the other player (50_net_host_N every 3 s, 50_net_client_N at the stops).
// Both then shoot the nearest dummy, and the joiner punches one (the host checks its hits; forged ones are refused).
// Then the host restarts the run (online that's a ServerTravel): both should arrive on the fresh map
// together (`AutoTest net: round 2 ...`). The joiner then quits; the host quits once the joiner has left.

#include "Player/WWOAutoTest.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOTestRun.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Flora/WWOFloraEffects.h"
#include "Weapons/WWOGrenade.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Player/WWOCharacterMovement.h"
#include "Player/WWOHealthComponent.h"
#include "Game/WWOGameState.h"
#include "Game/WWOGameMode.h"
#include "Enemies/WWOEnemyProjectile.h"
#include "World/WWOTarget.h"
#include "Characters/WWOBodyComponent.h"
#include "Player/WWOWeaponComponent.h"
#include "Data/WWOData.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Net/WWONetSubsystem.h"
#include "UI/WWOSettingsMenu.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	// The joiner's steps: start time (seconds after both players are in) and name.
	struct FNetStep { float At; const TCHAR* Name; };
	const FNetStep NetSteps[] = {
		{ 0.f, TEXT("idle") }, { 3.f, TEXT("walk") }, { 5.f, TEXT("sprint") }, { 6.5f, TEXT("slide") },
		{ 8.f, TEXT("stop") }, { 12.f, TEXT("crouch_walk") }, { 15.f, TEXT("jump") }, { 19.f, TEXT("mantle") },
		{ 22.f, TEXT("shoot") }, { 25.5f, TEXT("melee") }, { 31.5f, TEXT("revive") }, { 46.5f, TEXT("end") },
	};
	constexpr int32 NumNetSteps = UE_ARRAY_COUNT(NetSteps);
	constexpr float SettleSeconds = 4.f; // after both are in: the joiner's world loads, bodies get their models
	constexpr float WaitLimit = 180.f;   // give up waiting for the other player
	constexpr float HostLimit = 120.f;   // the host quits by then even if the joiner never left
	constexpr float RestartAt = 48.5f;  // the host downs both players: the lost round restarts the run (the joiner is done at 48)
	constexpr float ReviveDownAt = 32.f, ReviveReport = 46.f; // the host goes down; the joiner runs over and revives it
	constexpr float ShootFrom = 22.f, ShootTo = 25.f; // both shoot their nearest dummy (the host checks the joiner's hits)
	constexpr float MeleeFrom = 25.5f;                // the joiner walks up to the nearest dummy and punches it
	const float MeleePunches[] = { 28.f, 29.f, 30.f };
	constexpr float MeleeForge = 28.5f, MeleeReport = 31.1f; // forged between the first punches, while the dummy still stands
	// Which round this process is in: 0 = the moves, 1 = after the host's restart. The controller
	// (and its test state) is new after a map change; this isn't.
	int32 GAutoNetRound = 0;
	TWeakObjectPtr<APlayerController> GAutoNetRound1; // the controller that finished round 1 (round 2 runs on the next one)
}

void FWWOAutoTest::UpdateAutoNetTest(float DeltaTime)
{
	UWorld* World = PC->GetWorld();
	AWWOCharacter* P = PC->GetWWOCharacter();
	const ENetMode Mode = World->GetNetMode();
	const bool bHost = Mode == NM_ListenServer;
	AutoNetWait += DeltaTime;

	// Before the host reopens the map as a listen server, or before the joiner arrives: wait.
	if (Mode == NM_Standalone || !P)
	{
		if (AutoNetWait > WaitLimit) { UE_LOG(LogWWO, Warning, TEXT("AutoTest net: FAIL never went online")); WWOTest::Exit(); }
		return;
	}
	TArray<AWWOCharacter*> Players = UWWOWorldSubsystem::Get(PC)->GetPlayers();
	AWWOCharacter* Other = nullptr;
	for (AWWOCharacter* C : Players) if (C != P) { Other = C; break; }

	// Nobody goes down to the waves during the test (the host owns health; the joiner's copy is
	// harmless). The revive and loss steps down players on purpose (Kill ignores god mode).
	for (AWWOCharacter* C : Players) C->Health->GodMode = 1.f;

	UWWOCharacterMovement::bLogNetCorrections = true;
	if (!bAutoNetSawOther)
	{
		if (!Other)
		{
			if (AutoNetWait > WaitLimit) { UE_LOG(LogWWO, Warning, TEXT("AutoTest net: FAIL no other player after %.0f s"), AutoNetWait); WWOTest::Exit(); }
			return;
		}
		AutoNetClock = -SettleSeconds; // counts up to 0, then the steps start
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: %s%s sees %d players on %s (net mode %d)"), GAutoNetRound > 0 ? TEXT("round 2: after the restart, ") : TEXT(""),
			bHost ? TEXT("host") : TEXT("joiner"), Players.Num(), *World->GetMapName(), (int32)Mode);
		bAutoNetSawOther = true;
	}
	const float Before = AutoNetClock;
	// Your own body must stay hidden from your own view (shadow only) on both sides, all run long.
	if (AutoNetClock >= 1.f && !P->bUlting && !P->Health->bDead && P->GetMesh() && !P->GetMesh()->bOwnerNoSee && !bAutoNetOwnBodyShown) // (the ult shows it, in third person)
	{
		bAutoNetOwnBodyShown = true;
		UE_LOG(LogWWO, Warning, TEXT("AutoTest net: FAIL %s sees its own body at t=%.1f"), bHost ? TEXT("the host") : TEXT("the joiner"), AutoNetClock);
	}
	AutoNetClock += DeltaTime;
	if (AutoNetClock < 0.f) return;
	const float T = AutoNetClock;
	auto Crossed = [Before, T](float At) { return WWOTest::Crossed(Before, T, At); };
	if (bHost && GAutoNetRound1.Get() == PC) return; // restarting: this controller's world is on its way out
	if (GAutoNetRound > 0 && GAutoNetRound1.Get() != PC)
	{
		if (!bHost && T > 1.f) { UE_LOG(LogWWO, Display, TEXT("AutoTest net: round 2 done, joiner leaving")); WWOTest::Exit(); }
		if (bHost && bAutoNetSawOther && !Other) { UE_LOG(LogWWO, Display, TEXT("AutoTest net: host done, the joiner left after the restart")); WWOTest::Exit(); }
		return;
	}

	// Health and dummies are the host's: it hurts the joiner and the first dummy; the joiner checks
	// what arrived (its health, the damage feedback, the dummy's health).
	if (!bHost && !bAutoNetBound)
	{
		bAutoNetBound = true;
		P->Health->OnDamaged.AddWeakLambda(PC, [this](float) { ++AutoNetDamageEvents; });
	}
	TArray<AWWOTarget*> Dummies;
	for (TActorIterator<AWWOTarget> It(World); It; ++It) Dummies.Add(*It);
	if (bHost && Crossed(1.0f) && Other)
	{
		Other->Health->GodMode = 0.f;
		Other->Health->TakeDamage(30.f);
		Other->Health->GodMode = 1.f;
		if (Dummies.Num() > 0) Dummies[0]->TakeHit(50.f, false, Dummies[0]->GetActorLocation());
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: host hit the joiner for 30 (now health %.0f shield %.0f) and %d dummies' first for 50 (now %.0f)"),
			Other->Health->Health, Other->Health->Shield, Dummies.Num(), Dummies.Num() > 0 ? Dummies[0]->GetHealth() : 0.f);
	}
	if (!bHost && Crossed(2.5f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner health %.0f shield %.0f, damage events %d; %d dummies, first at health %.0f"),
			P->Health->Health, P->Health->Shield, AutoNetDamageEvents, Dummies.Num(), Dummies.Num() > 0 ? Dummies[0]->GetHealth() : 0.f);
	}

	// The joiner's loadout reaches the host (its copy answers for anything the joiner owns).
	if (Crossed(3.0f))
	{
		auto Describe = [](const UWWOLoadoutSubsystem* L)
		{
			if (!L) return FString(TEXT("none"));
			const FWWONetLoadout N = L->ToNet();
			return FString::Printf(TEXT("flora %s grenade %s abilities %s traits %d frame %d weapon %s mods %d%s"), *N.Flora.ToString(), *N.Grenade.ToString(),
				*FString::JoinBy(N.Abilities, TEXT("+"), [](const FName& X) { return X.ToString(); }), N.Traits.Num(), N.FrameParts.Num(), *N.Weapon.ToString(),
				N.ModIds.Num(), L->IsCopy() ? TEXT(" (host's copy)") : TEXT(""));
		};
		if (bHost && Other) UE_LOG(LogWWO, Display, TEXT("AutoTest net: host sees the joiner's loadout: %s; its own: %s"), *Describe(UWWOLoadoutSubsystem::Get(Other->Weapon)), *Describe(UWWOLoadoutSubsystem::Get(P)));
		if (!bHost) UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner's own loadout: %s"), *Describe(UWWOLoadoutSubsystem::Get(P)));
	}

	// Both: the Settings menu's Multiplayer tab (status line), and online menus don't pause the match.
	if (Crossed(2.0f))
	{
		PC->ToggleSettingsMenu();
		const bool bTab = PC->SettingsMenu && PC->SettingsMenu->ShowPage(TEXT("Multiplayer"));
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: %s opened Settings, Multiplayer tab %d"), bHost ? TEXT("host") : TEXT("joiner"), bTab);
	}
	if (Crossed(2.5f))
	{
		const UWWONetSubsystem* Net = UWWONetSubsystem::Get(PC);
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: menu open %d, game paused %d, status \"%s\""), PC->IsMenuOpen(), PC->IsPaused(), Net ? *Net->DescribeStatus(PC) : TEXT("-"));
		Shot(bHost ? TEXT("51_net_menu_host") : TEXT("51_net_menu_client"));
	}
	if (Crossed(2.8f) && PC->IsMenuOpen()) PC->CloseMenus();

	// Shooting (22 to 25 s): each side picks the nearest dummy it can see and fires at its head.
	auto ShootDummy = [this, P, World, &Dummies, Crossed, T]()
	{
		AWWOTarget* Dummy = Cast<AWWOTarget>(AutoNetShootTarget.Get());
		if (!Dummy || Dummy->IsDead())
		{
			float BestD = 1e9f;
			Dummy = nullptr;
			for (AWWOTarget* D : Dummies)
			{
				if (D->IsDead()) continue;
				FHitResult Sight;
				FCollisionQueryParams SightParams(SCENE_QUERY_STAT(WWOAutoTestNet), false, P);
				SightParams.AddIgnoredActor(D);
				if (World->LineTraceSingleByChannel(Sight, P->EyeLocation(), D->GetAimParts()[0].Position, ECC_Visibility, SightParams)) continue;
				const float Dist = FVector::Dist(D->GetActorLocation(), P->GetActorLocation());
				if (Dist < BestD) { BestD = Dist; Dummy = D; }
			}
			if (Dummy && !AutoNetShootTarget.IsValid())
			{
				AutoNetShootHealth = Dummy->GetHealth();
				UE_LOG(LogWWO, Display, TEXT("AutoTest net: shooting %s %.1f m away (health %.0f)"), *Dummy->GetName(), BestD / WWO::M, AutoNetShootHealth);
			}
			AutoNetShootTarget = Dummy;
		}
		if (!Dummy) return;
		const FVector Dir = (Dummy->GetAimParts()[0].Position - P->EyeLocation()).GetSafeNormal();
		P->SetViewRotation(FMath::Asin(Dir.Z), Dir.Rotation().Yaw);
		const int32 Pull = FMath::FloorToInt((T - ShootFrom - 0.4f) / 0.5f) + 1; // the hand cannon is semi-auto: a fresh pull each shot
		PC->Held[(int32)EWWOAction::Fire] = Pull > AutoNetShotPulls;
		PC->JustPressed[(int32)EWWOAction::Fire] = Pull > AutoNetShotPulls;
		if (Pull > AutoNetShotPulls) AutoNetShotPulls = Pull;
	};
	if (T >= ShootFrom && T < ShootTo)
	{
		PC->Held[(int32)EWWOAction::Aim] = true; // aimed in: no hip spread at this range
		if (T >= ShootFrom + 0.4f) ShootDummy(); // once the sights are up
	}
	if (Crossed(ShootTo))
	{
		PC->Held[(int32)EWWOAction::Fire] = false;
		PC->Held[(int32)EWWOAction::Aim] = false;
		// A forged shot the host must refuse: far more damage than the gun can do.
		// (If the real shots already killed the dummy, any other one does.)
		if (!bHost && !AutoNetShootTarget.IsValid()) { if (TActorIterator<AWWOTarget> It(World); It) AutoNetShootTarget = *It; }
		if (!bHost && AutoNetShootTarget.IsValid())
		{
			FWWOGunShot Fake;
			Fake.WeaponId = P->Weapon->GetWeaponData() ? P->Weapon->GetWeaponData()->Id : NAME_None;
			Fake.Origin = P->EyeLocation();
			Fake.Hits.Add({ AutoNetShootTarget.Get(), 99999.f, true, AutoNetShootTarget->GetActorLocation() });
			P->ServerGunShot(Fake);
			// And one claiming a mod that doesn't fit this gun.
			Fake.Hits[0].Damage = 10.f;
			Fake.Mods = { "no_such_mod" };
			P->ServerGunShot(Fake);
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner sent a forged 99999-damage hit and a shot with a made-up mod (the host should reject both); mods on its gun %d"),
				UWWOLoadoutSubsystem::Get(PC) ? UWWOLoadoutSubsystem::Get(PC)->ModPercent("damage") != 0.f : -1);
		}
	}
	if (Crossed(ShootTo + 0.6f))
	{
		const AWWOTarget* Dummy = Cast<AWWOTarget>(AutoNetShootTarget.Get());
		if (bHost)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: host fired %d times at %s; joiner shots taken %d, hits applied %d, rejected %d"), AutoNetShotPulls,
				*GetNameSafe(Dummy), AWWOCharacter::NetShotsTaken, AWWOCharacter::NetHitsApplied, AWWOCharacter::NetHitsRejected);
			// The forged 99999 hit and the made-up mod are two shots the host must have turned down.
			if (AWWOCharacter::NetHitsRejected < 2) UE_LOG(LogWWO, Warning, TEXT("AutoTest net: FAIL the host rejected %d forged shots, not both"), AWWOCharacter::NetHitsRejected);
		}
		else
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner fired %d times at %s: its health %.0f -> %.0f (host's), dead %d; saw %d host shots"), AutoNetShotPulls,
				*GetNameSafe(Dummy), AutoNetShootHealth, Dummy ? Dummy->GetHealth() : 0.f, Dummy ? Dummy->IsDead() : 0, AWWOCharacter::NetShotsSeen);
		}
	}

	// Grenade (during the stop, 8 to 12 s): the joiner throws its grenade at the nearest dummy (the host
	// throws it from the joiner's loadout and everyone sees it), then a second throw with no charge left.
	if (!bHost && Crossed(9.f) && Dummies.Num() > 0)
	{
		AWWOTarget* Near = Dummies[0];
		for (AWWOTarget* D : Dummies) if (FVector::Dist(D->GetActorLocation(), P->GetActorLocation()) < FVector::Dist(Near->GetActorLocation(), P->GetActorLocation())) Near = D;
		const FVector To = Near->GetActorLocation() - P->EyeLocation();
		P->SetViewRotation(FMath::Asin(To.GetSafeNormal().Z) + 0.05f, To.Rotation().Yaw);
		AutoNetGrenadeTarget = Near;
		AutoNetGrenadeHealth = Near->GetHealth();
		PC->JustPressed[(int32)EWWOAction::Grenade] = true;
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner throwing its grenade at %s %.1f m away (health %.0f, charge %.2f)"), *Near->GetName(), To.Size() / WWO::M, AutoNetGrenadeHealth, P->Grenades->Charge);
	}
	if (!bHost && Crossed(9.8f))
	{
		// Two more at once: with the Extra Grenade ability one of them may be real, never both.
		P->ServerThrowGrenade(P->EyeLocation(), P->AimForward());
		P->ServerThrowGrenade(P->EyeLocation(), P->AimForward());
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner sent two more throws than it has grenades for (the host should refuse at least one)"));
	}
	// Ability: the joiner switches on Thornfist (whichever slot has it); the host's copy should have it on too.
	if (!bHost && Crossed(10.3f))
	{
		const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(P);
		const bool bFirst = L && L->GetAbility(0) && L->GetAbility(0)->Id == "thornfist";
		PC->JustPressed[(int32)(bFirst ? EWWOAction::Ability1 : EWWOAction::Ability2)] = true;
	}
	// The moves can end anywhere (each machine's own movement settings): before the shooting the host
	// puts the joiner back where it started, so the rest doesn't depend on where the run ended.
	if (bHost && Other && Crossed(0.5f)) AutoNetJoinerStart = Other->GetActorTransform();
	if (bHost && Other && Crossed(21.6f) && !AutoNetJoinerStart.GetLocation().IsZero())
	{
		Other->TeleportTo(AutoNetJoinerStart.GetLocation(), AutoNetJoinerStart.Rotator());
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: host put the joiner back at its start for the shooting"));
	}
	if (Crossed(11.2f))
	{
		const AWWOCharacter* Joiner = bHost ? Other : P;
		if (Joiner) UE_LOG(LogWWO, Display, TEXT("AutoTest net: %s sees the joiner's Thornfist on %d (abilities the host ran for joiners %d)"),
			bHost ? TEXT("host") : TEXT("joiner"), Joiner->Abilities->ThornfistActive() != nullptr, AWWOCharacter::NetAbilitiesUsed);
	}
	// Ult (end of the run): the joiner casts its ult; the host runs the blast and everyone sees it.
	if (!bHost && Crossed(46.7f)) PC->JustPressed[(int32)EWWOAction::Ult] = true;
	if (Crossed(48.2f))
	{
		const AWWOCharacter* Joiner = bHost ? Other : P;
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: %s: joiner ults the host cast %d, blasts seen from the host %d, joiner still ulting %d"),
			bHost ? TEXT("host") : TEXT("joiner"), AWWOCharacter::NetUltsCast, AWWOGameState::BlastsSeen, Joiner ? Joiner->bUlting : -1);
	}
	if (Crossed(11.5f))
	{
		int32 Pools = 0;
		FString PoolOwner;
		for (TActorIterator<AWWOSporePool> It(World); It; ++It) { ++Pools; PoolOwner = GetNameSafe(It->GetOwner()); }
		if (bHost)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: host threw %d joiner grenades, refused %d; %d pools now (owner %s)"),
				UWWOGrenadeComponent::NetThrows, UWWOGrenadeComponent::NetThrowsRejected, Pools, *PoolOwner);
			if (UWWOGrenadeComponent::NetThrowsRejected < 1) UE_LOG(LogWWO, Warning, TEXT("AutoTest net: FAIL the host threw the grenade the joiner didn't have"));
		}
		else
		{
			const AWWOTarget* Thrown = Cast<AWWOTarget>(AutoNetGrenadeTarget.Get());
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner saw %d grenades and %d pools from the host (%d pools now); %s health %.0f -> %.0f"),
				AWWOGrenade::SeenRemote, AWWOSporePool::SeenRemote, Pools, *GetNameSafe(Thrown), AutoNetGrenadeHealth, Thrown ? Thrown->GetHealth() : 0.f);
		}
	}

	// Melee (25.5 to 31.5 s): the joiner walks up to the nearest living dummy and punches it three times,
	// with a forged punch (far too much damage) and two real ones at once (the second is too soon) after the first.
	if (!bHost && T >= MeleeFrom && T < MeleeReport)
	{
		AWWOTarget* Dummy = Cast<AWWOTarget>(AutoNetMeleeTarget.Get());
		if (!AutoNetMeleeTarget.IsValid() && Crossed(MeleeFrom))
		{
			// The nearest of those you see most of (a punch needs a clear line to the part it hits).
			float BestD = 1e9f;
			int32 BestSeen = 0;
			for (AWWOTarget* D : Dummies)
			{
				if (D->IsDead()) continue;
				const float Dist = FVector::Dist(D->GetActorLocation(), P->GetActorLocation());
				FCollisionQueryParams SightParams(SCENE_QUERY_STAT(WWOAutoTestNet), false, P);
				SightParams.AddIgnoredActor(D);
				int32 Seen = 0;
				for (const FWWOAimPart& Part : D->GetAimParts())
				{
					FHitResult Sight;
					if (!World->LineTraceSingleByChannel(Sight, P->EyeLocation(), Part.Position, ECC_Visibility, SightParams)) ++Seen;
				}
				if (Seen > BestSeen || (Seen == BestSeen && Seen > 0 && Dist < BestD)) { BestSeen = Seen; BestD = Dist; Dummy = D; }
			}
			AutoNetMeleeTarget = Dummy;
			AutoNetMeleeHealth = Dummy ? Dummy->GetHealth() : 0.f;
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: punching %s %.1f m away (health %.0f)"), *GetNameSafe(Dummy), BestD / WWO::M, AutoNetMeleeHealth);
		}
		if (Dummy)
		{
			const FVector Aim = Dummy->GetAimParts().Last().Position;
			const FVector Dir = (Aim - P->EyeLocation()).GetSafeNormal();
			if (!P->Weapon->bMeleeing) P->SetViewRotation(FMath::Asin(Dir.Z), Dir.Rotation().Yaw);
			const float Flat = FVector2D(Dummy->GetActorLocation() - P->GetActorLocation()).Size() / WWO::M;
			PC->MoveValue = Flat > 1.8f && T < MeleePunches[0] + 2.5f ? FVector2D(0.f, 1.f) : FVector2D::ZeroVector;
			PC->Held[(int32)EWWOAction::Sprint] = Flat > 6.f && T < MeleePunches[0]; // far away: run there
			for (const float At : MeleePunches) if (Crossed(At)) PC->JustPressed[(int32)EWWOAction::Melee] = true;
			if (Crossed(MeleeForge))
			{
				P->ServerMeleeHit(Dummy, 99999.f, P->EyeLocation(), Aim);
				P->ServerMeleeHit(Dummy, 40.f, P->EyeLocation(), Aim);
				P->ServerMeleeHit(Dummy, 40.f, P->EyeLocation(), Aim);
				UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner sent a forged 99999 punch and two 40s at once from %.1f m (the host should take one 40)"),
					FVector::Dist(Aim, P->EyeLocation()) / WWO::M);
			}
		}
	}
	if (Crossed(MeleeReport))
	{
		if (bHost)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: host saw %d joiner swings; melee hits applied %d, rejected %d; kills credited to the joiner %d"),
				AWWOCharacter::NetMeleeSwings, AWWOCharacter::NetMeleeApplied, AWWOCharacter::NetMeleeRejected, AWWOCharacter::NetKillsCredited);
			// Of the three punches sent at once (one forged), at most one may land.
			// (No punches arrived at all: the joiner found no dummy left to punch, so the step didn't run.)
			if (AWWOCharacter::NetMeleeApplied + AWWOCharacter::NetMeleeRejected == 0) { UE_LOG(LogWWO, Display, TEXT("AutoTest net: melee step skipped (no punches arrived)")); }
			else if (AWWOCharacter::NetMeleeRejected < 2) UE_LOG(LogWWO, Warning, TEXT("AutoTest net: FAIL the host took more than one of three punches sent at once (rejected %d)"), AWWOCharacter::NetMeleeRejected);
		}
		else
		{
			const AWWOTarget* Dummy = Cast<AWWOTarget>(AutoNetMeleeTarget.Get());
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner punched %s: its health %.0f -> %.0f (host's), dead %d; kills the host confirmed %d"),
				*GetNameSafe(Dummy), AutoNetMeleeHealth, Dummy ? Dummy->GetHealth() : 0.f, Dummy ? Dummy->IsDead() : 0, AWWOCharacter::NetKillsCredited);
		}
	}

	// Revive (31.5 to 46.5 s): the host goes down where it stands; the joiner sprints over and,
	// once within 1.5 m, holds Revive until it's back up (ReviveTime, 3 s).
	if (bHost && Crossed(ReviveDownAt))
	{
		P->Health->Kill();
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: host went down (dead %d, respawn timer %.1f)"), P->Health->bDead, P->Health->RespawnLeft);
	}
	if (!bHost && Other && T >= ReviveDownAt + 0.2f && T < ReviveReport)
	{
		const FVector To = Other->GetActorLocation() - P->GetActorLocation();
		P->SetViewRotation(-0.5f, To.Rotation().Yaw);
		const bool bClose = FVector2D(To).Size() <= 1.5f * WWO::M || P->Health->FindReviveTarget() == Other; // in reach (the prompt's own test)
		PC->MoveValue = bClose ? FVector2D::ZeroVector : FVector2D(0.f, 1.f);
		PC->Held[(int32)EWWOAction::Sprint] = !bClose && FVector2D(To).Size() > 4.f * WWO::M;
		const bool bWasHolding = PC->Held[(int32)EWWOAction::Revive];
		PC->Held[(int32)EWWOAction::Revive] = bClose && Other->Health->bDead;
		AutoNetReviveSeen = FMath::Max(AutoNetReviveSeen, Other->Health->BeingRevived);
		if (PC->Held[(int32)EWWOAction::Revive] && !bWasHolding)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner holding Revive %.1f m from the host (host down %d, its clip %s, revive target %s)"),
				To.Size() / WWO::M, Other->Health->bDead, *Other->Body->CurrentAnim().ToString(), *GetNameSafe(P->Health->FindReviveTarget()));
		}
	}
	if (Crossed(ReviveReport))
	{
		PC->Held[(int32)EWWOAction::Revive] = false;
		PC->Held[(int32)EWWOAction::Sprint] = false;
		PC->MoveValue = FVector2D::ZeroVector;
		if (bHost)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: host after the revive: dead %d, revived %d, health %.0f shield %.0f"),
				P->Health->bDead, P->Health->bRevived, P->Health->Health, P->Health->Shield);
		}
		else if (Other)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner sees the host dead %d, health %.0f; revive bar reached %.2f; host clip %s"),
				Other->Health->bDead, Other->Health->Health, AutoNetReviveSeen, *Other->Body->CurrentAnim().ToString());
		}
	}

	auto LookAt = [P](const AActor* A)
	{
		const FVector Dir = A->GetActorLocation() + FVector(0.f, 0.f, 40.f) - P->EyeLocation();
		P->SetViewRotation(FMath::Asin(Dir.GetSafeNormal().Z), Dir.Rotation().Yaw);
	};

	// Once a second: where the joiner is, from each side (the host logs its copy of the joiner).
	if (Crossed(FMath::FloorToFloat(T)) || Before < 0.f)
	{
		const AWWOCharacter* Joiner = bHost ? Other : P;
		if (Joiner)
		{
			// Body clips: the host picks the joiner's (it replicates to the others); the joiner's own picks
			// its own locally, and the host's arrives from the host.
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: t=%.0f %s joiner at %s speed %.2f crouched %d sliding %d on floor %d, joiner clip %s, other clip %s"), FMath::FloorToFloat(T),
				bHost ? TEXT("host's copy:") : TEXT("own:"), *Joiner->GetActorLocation().ToCompactString(),
				FVector2D(Joiner->GetVelocity()).Size() / WWO::M, Joiner->bIsCrouched, Joiner->MoveState.bSliding, Joiner->IsOnFloor(),
				*Joiner->Body->CurrentAnim().ToString(), Other ? *Other->Body->CurrentAnim().ToString() : TEXT("-"));
		}
	}

	if (bHost)
	{
		// Look at the joiner every 3 s and screenshot it.
		if (Other && T >= AutoNetNextShot && (T < ShootFrom - 0.5f || T > ShootTo + 0.5f))
		{
			AutoNetNextShot = T + 3.f;
			LookAt(Other);
			Shot(*FString::Printf(TEXT("50_net_host_%02d"), AutoNetShots++));
		}
		if (Crossed(RestartAt) && Other)
		{
			// Everyone down loses the round, and a lost round restarts the run (online that's a ServerTravel).
			GAutoNetRound = 1;
			GAutoNetRound1 = PC;
			Other->Health->Kill();
			P->Health->Kill();
			const AWWOGameMode* GM = World->GetAuthGameMode<AWWOGameMode>();
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: host downed both players (%d players): round lost %d"), Players.Num(), GM && GM->MatchState() == EWWOWaveState::Lost);
			return;
		}
		if (Crossed(RestartAt + 8.f))
		{
			UE_LOG(LogWWO, Warning, TEXT("AutoTest net: FAIL everyone down didn't restart the run; restarting by hand"));
			if (AWWOGameMode* GM = World->GetAuthGameMode<AWWOGameMode>()) GM->Restart();
			return;
		}
		if (bAutoNetSawOther && !Other)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: host done, the joiner left at %.1f s"), T);
			WWOTest::Exit();
		}
		else if (T > HostLimit)
		{
			UE_LOG(LogWWO, Warning, TEXT("AutoTest net: host gave up at %.0f s (the joiner never left)"), T);
			WWOTest::Exit();
		}
		return;
	}

	// --- The joiner's moves --------------------------------------------------------------------
	auto FaceOpen = [this, P]()
	{
		// The longest clear line, swept body-wide on the Pawn channel so walls, crates, the other
		// player and enemies all count (the Visibility channel misses players' capsules).
		float BestYaw = P->GetActorRotation().Yaw, BestLen = 0.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOAutoTestNet), false, P);
		// (a little narrower than the body, so the other player standing beside you at spawn doesn't block every way)
		const FCollisionShape Body = FCollisionShape::MakeSphere(P->GetCapsuleComponent()->GetScaledCapsuleRadius() - 5.f);
		for (int32 i = 0; i < 24; ++i)
		{
			const float Yaw = i * 15.f;
			const FVector From = P->GetActorLocation();
			const FVector To = From + FRotator(0.f, Yaw, 0.f).Vector() * 4000.f;
			FHitResult Hit;
			const float Len = PC->GetWorld()->SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_Pawn, Body, Params) ? Hit.Distance : 4000.f;
			if (Len > BestLen) { BestLen = Len; BestYaw = Yaw; }
		}
		P->SetViewRotation(0.f, BestYaw);
		return BestLen / WWO::M;
	};
	auto Press = [this](EWWOAction A) { PC->JustPressed[(int32)A] = true; PC->Held[(int32)A] = true; };
	auto Release = [this](EWWOAction A) { PC->Held[(int32)A] = false; };

	// End of the step that was running: its corrections.
	auto EndStep = [this, P]()
	{
		if (AutoNetStep < 0) return;
		const int32 N = UWWOCharacterMovement::NetCorrections;
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: step %s corrections %d (error max %.1f cm, avg %.1f cm)"), NetSteps[AutoNetStep].Name, N,
			UWWOCharacterMovement::NetCorrectionErrorMax, N > 0 ? UWWOCharacterMovement::NetCorrectionErrorSum / N : 0.f);
		AutoNetTotalCorrections += N;
	};

	while (AutoNetStep + 1 < NumNetSteps && T >= NetSteps[AutoNetStep + 1].At)
	{
		EndStep();
		++AutoNetStep;
		UWWOCharacterMovement::ResetNetCorrections();
		const FString Step = NetSteps[AutoNetStep].Name;
		if (Step == TEXT("idle"))
		{
			const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner HUD wave line \"%s\", other player %s, flora %s"), GS ? *GS->StatusLine : TEXT("(no game state)"),
				Other ? *Other->GetActorLocation().ToCompactString() : TEXT("none"), Other ? *Other->NetFlora.ToString() : TEXT("-"));
		}
		else if (Step == TEXT("walk"))
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: clear run %.1f m"), FaceOpen());
			PC->MoveValue = FVector2D(0.f, 1.f);
		}
		else if (Step == TEXT("sprint")) { UE_LOG(LogWWO, Display, TEXT("AutoTest net: clear run %.1f m"), FaceOpen()); Press(EWWOAction::Sprint); }
		else if (Step == TEXT("slide")) { Press(EWWOAction::Crouch); }
		else if (Step == TEXT("stop")) { PC->MoveValue = FVector2D::ZeroVector; }
		else if (Step == TEXT("crouch_walk")) { FaceOpen(); Press(EWWOAction::Crouch); PC->MoveValue = FVector2D(0.f, 1.f); }
		else if (Step == TEXT("jump")) { Press(EWWOAction::Crouch); FaceOpen(); } // stand back up, then run and jump
		else if (Step == TEXT("mantle"))
		{
			// Run at the nearest crate stack tall enough to mantle (a wall at chest height, 1.2 m) and jump at it.
			PC->MoveValue = FVector2D::ZeroVector;
			float BestD = 1e9f;
			AutoNetCrate = FVector::ZeroVector;
			AutoNetCrateBox = FBox(ForceInit);
			for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
			{
				// The bottom crate of a stack (its top crate isn't tagged): the stack's top 1.4 to 2.5 m up.
				if (!It->Tags.Contains(FName("Cover"))) continue;
				FBox Box = It->GetComponentsBoundingBox();
				FBox Stack = Box;
				for (TActorIterator<AStaticMeshActor> Up(World); Up; ++Up)
				{
					const FBox B = Up->GetComponentsBoundingBox();
					if (*Up != *It && B.Min.Z > Box.Max.Z - 20.f && B.Min.Z < Box.Max.Z + 20.f && FVector2D(B.GetCenter() - Box.GetCenter()).Size() < 40.f) Stack += B;
				}
				if (Stack.Max.Z < 140.f || Stack.Max.Z > 250.f) continue;
				const float D = FVector::Dist(It->GetActorLocation(), P->GetActorLocation());
				if (D < BestD) { BestD = D; AutoNetCrate = It->GetActorLocation(); AutoNetCrateBox = Stack; }
			}
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: mantle crate %s, %.1f m away"), AutoNetCrate.IsZero() ? TEXT("none") : *AutoNetCrate.ToCompactString(), BestD / WWO::M);
			bAutoNetMantleJumped = false;
			bAutoNetSawMantle = false;
		}
		else if (Step == TEXT("shoot") || Step == TEXT("melee")) { PC->MoveValue = FVector2D::ZeroVector; }
		else if (Step == TEXT("end"))
		{
			PC->MoveValue = FVector2D::ZeroVector;
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner done, %d corrections in all, saw mantle %d, saw %d enemy shots from the host"),
				AutoNetTotalCorrections, bAutoNetSawMantle, AWWOEnemyProjectile::SeenRemote);
		}
	}
	const FString Step = AutoNetStep >= 0 ? NetSteps[AutoNetStep].Name : TEXT("");

	// Inside the steps.
	if (Step == TEXT("slide") && Crossed(6.6f)) Release(EWWOAction::Crouch);
	if (Step == TEXT("crouch_walk") && Crossed(12.1f)) Release(EWWOAction::Crouch);
	if (Step == TEXT("jump"))
	{
		if (Crossed(15.1f)) { Release(EWWOAction::Crouch); PC->MoveValue = FVector2D(0.f, 1.f); }
		if (Crossed(15.6f)) Press(EWWOAction::Jump);
		if (Crossed(15.7f) || Crossed(17.2f)) Release(EWWOAction::Jump);
		if (Crossed(17.f) || Crossed(17.3f)) Press(EWWOAction::Jump); // ground jump, then the air jump
		if (Crossed(17.4f)) Release(EWWOAction::Jump);
		if (Crossed(18.6f)) PC->MoveValue = FVector2D::ZeroVector;
	}
	if (Step == TEXT("mantle") && !AutoNetCrate.IsZero())
	{
		const FVector To = AutoNetCrate - P->GetActorLocation();
		// Steer at the stack the whole way (until up on it), pushing forward; jump once its face is
		// within 60 cm at chest height (what the mantle itself looks for).
		const float HalfHeight = P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float FeetZ = P->GetActorLocation().Z - HalfHeight;
		if (FeetZ < AutoNetCrateBox.Max.Z - 20.f && !P->IsMantling())
		{
			P->SetViewRotation(0.f, To.Rotation().Yaw);
			PC->MoveValue = FVector2D(0.f, 1.f);
		}
		if (!bAutoNetMantleJumped && P->IsOnFloor())
		{
			const FVector Chest(P->GetActorLocation().X, P->GetActorLocation().Y, FeetZ + 120.f);
			FHitResult Wall;
			FCollisionQueryParams WallParams(SCENE_QUERY_STAT(WWOAutoTestNet), false, P);
			const FVector Ahead = FVector(To.X, To.Y, 0.f).GetSafeNormal();
			if (World->LineTraceSingleByChannel(Wall, Chest, Chest + Ahead * (P->GetCapsuleComponent()->GetScaledCapsuleRadius() + 60.f), ECC_Visibility, WallParams))
			{
				Press(EWWOAction::Jump);
				bAutoNetMantleJumped = true;
				UE_LOG(LogWWO, Display, TEXT("AutoTest net: mantle jump, the stack's face %.0f cm ahead"), Wall.Distance);
			}
		}
		else if (bAutoNetMantleJumped) Release(EWWOAction::Jump);
		bAutoNetSawMantle |= P->IsMantling();
		if (Crossed(21.8f))
		{
			PC->MoveValue = FVector2D::ZeroVector;
			UE_LOG(LogWWO, Display, TEXT("AutoTest net: mantle run ended %.0f cm from the stack's middle (top %.0f cm), feet %.0f cm up, jumped %d"), FVector2D(To).Size(), AutoNetCrateBox.Max.Z,
				P->GetActorLocation().Z - P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), bAutoNetMantleJumped);
		}
	}

	// Screenshots of the host, at the still moments (turning would bend the runs).
	const float ShotTimes[] = { 1.5f, 11.f, 24.f };
	for (float At : ShotTimes)
	{
		if (Other && Crossed(At)) { LookAt(Other); Shot(*FString::Printf(TEXT("50_net_client_%02d"), AutoNetShots++)); }
	}
	if (Crossed(5.8f)) Shot(*FString::Printf(TEXT("50_net_client_%02d"), AutoNetShots++)); // mid-sprint, HUD and all

	// Done: wait for the host's restart (it carries this joiner along to the fresh map).
	if (Crossed(NetSteps[NumNetSteps - 1].At + 1.5f))
	{
		GAutoNetRound = 1;
		GAutoNetRound1 = PC;
		UE_LOG(LogWWO, Display, TEXT("AutoTest net: joiner waiting for the host's restart"));
	}
	if (T > RestartAt + 30.f) { UE_LOG(LogWWO, Warning, TEXT("AutoTest net: FAIL the host's restart never arrived")); WWOTest::Exit(); }
}
