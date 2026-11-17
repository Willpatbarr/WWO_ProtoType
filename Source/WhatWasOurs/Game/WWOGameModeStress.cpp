// The stress and screenshot harness for AWWOGameMode (bot matches run from the command line): the
// "Bots:" diagnostics every 10 s, the "Stress host:" load lines every 5 s, and the staged runs that
// ride on -WWOStress (-WWOBotShots, -WWOTestKills, -WWOTestFog, -WWOTestCharge, -WWOFxGallery,
// -WWOFogTest=, -WWOProfileGPU), ending the game after -WWOStressTime=. None of it is game rules.
//
// The clocks that were game mode members are members here (a new game mode, so a new harness, after
// a map restart). The function-local statics below stay statics on purpose: they last the whole
// process, so after a match restarts the picture numbers carry on and the next-kill / next-fog
// times stay where they were. Making them members would change that.

#include "Game/WWOGameModeStress.h"
#include "Player/WWOTestRun.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "WWOTypes.h"
#include "WWOSwitches.h"
#include "WhatWasOurs.h"
#include "Characters/WWOBodyComponent.h"
#include "Weapons/WWOGrenade.h"
#include "Flora/WWOFloraEffects.h"
#include "Flora/WWOSporeFog.h"
#include "Systems/WWOSound.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "Player/WWOBotController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "TimerManager.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "RenderTimer.h"
#include "UObject/UObjectIterator.h"
#include "Components/StaticMeshComponent.h"
#include "UnrealClient.h"
#include "RHI.h"

void FWWOStressHarness::LogBots(AWWOGameMode& GM)
{
	UWorld* World = GM.GetWorld();
	if (GM.Roster.BotsSpawned <= 0 || (BotLogClock += World->GetDeltaSeconds()) < BotLogNext) return;
	// How the bots are getting on (every 10 s).
	BotLogNext += WWOSwitches::Has(TEXT("WWOStressShots")) ? 4.f : 10.f;
	int32 Count[4] = {}, Following = 0;
	for (TActorIterator<AWWOBotController> It(World); It; ++It)
	{
		++Count[FMath::Clamp(It->StateIndex(), 0, 3)];
		Following += It->IsFollowing() ? 1 : 0;
	}
	UE_LOG(LogWWO, Display, TEXT("Bots team: %d following a leader, callouts acted on %d"), Following, AWWOBotController::Callouts);
	UE_LOG(LogWWO, Display, TEXT("Bots: t=%.0f roam %d hunt %d fight %d retreat %d; sightings %d, shots %d, kills %d, paths %d (%d failed), stuck hops %d, gave up %d"),
		BotLogClock, Count[0], Count[1], Count[2], Count[3], AWWOBotController::Sightings, AWWOBotController::ShotsFired, AWWOGameMode::PvPKills,
		AWWOBotController::PathsFound, AWWOBotController::PathsFailed, AWWOBotController::StuckJumps, AWWOBotController::Regoals);
	// -WWOStressShots: the host's player turns to the nearest other player in sight, a teammate and
	// a foe in turn, and the view is saved a moment later (Saved/AutoTest/41_pvp_view_<t>.png).
	if (WWOSwitches::Has(TEXT("WWOStressShots")))
	{
		APlayerController* PC = World->GetFirstPlayerController();
		AWWOCharacter* Me = PC ? Cast<AWWOCharacter>(PC->GetPawn()) : nullptr;
		static bool bWantFoe = false;
		bWantFoe = !bWantFoe;
		AWWOCharacter* Best = nullptr;
		float BestD = 6000.f;
		for (TActorIterator<AWWOCharacter> It(World); Me && It; ++It)
		{
			if (*It == Me || It->Health->bDead || UWWOWorldSubsystem::CanHurt(Me, *It) != bWantFoe) continue;
			const float D = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
			FHitResult Block;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOStressShot), false, Me);
			Params.AddIgnoredActor(*It);
			if (D < BestD && !World->LineTraceSingleByChannel(Block, Me->EyeLocation(), It->GetActorLocation(), ECC_Visibility, Params)) { BestD = D; Best = *It; }
		}
		if (Best)
		{
			const FVector Dir = (Best->GetActorLocation() - Me->EyeLocation()).GetSafeNormal();
			Me->SetViewRotation(FMath::Asin(Dir.Z), Dir.Rotation().Yaw);
			// And something for the HUD to show: a hit from behind and to the right, and a slow.
			Me->Health->DamageCameFrom(Me->GetActorLocation() - Dir * 400.f + FVector::CrossProduct(FVector::UpVector, Dir) * 400.f);
			Me->ApplySlow(0.4f, 3.f);
			const FString Path = WWOTest::ShotPath(FString::Printf(TEXT("41_pvp_view_%03d_%s"), FMath::RoundToInt(BotLogClock), bWantFoe ? TEXT("foe") : TEXT("mate")));
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, [Path] { FScreenshotRequest::RequestScreenshot(Path, true, false); }, 0.25f, false);
			UE_LOG(LogWWO, Display, TEXT("Bots view: %s at %.0f m, team %d (mine %d) -> %s"), bWantFoe ? TEXT("foe") : TEXT("teammate"), BestD / 100.f,
				UWWOWorldSubsystem::TeamOf(Best), UWWOWorldSubsystem::TeamOf(Me), *Path);
		}
	}
	UE_LOG(LogWWO, Display, TEXT("Bots sounds: %s"), *UWWOSound::Summary());
	int32 HeadShots = 0, HeadCrits = 0, HeadOldCrits = 0;
	float HeadOffEyes = 0.f;
	for (TActorIterator<AWWOCharacter> It(World); It; ++It) It->ProbeHeadShots(HeadShots, HeadCrits, HeadOldCrits, HeadOffEyes);
	UE_LOG(LogWWO, Display, TEXT("Bots heads: %d test shots at the drawn heads, %d count as headshots (%d under the old hitboxes); head up to %.0f cm from the eye point"),
		HeadShots, HeadCrits, HeadOldCrits, HeadOffEyes);
	UE_LOG(LogWWO, Display, TEXT("Bots kit: melees %d, grenades %d, abilities %d, ults %d, retreats %d (%d found no cover), gun swaps %d; stuck hops roaming %d hunting %d fighting %d retreating %d"),
		AWWOBotController::Melees, AWWOBotController::Grenades, AWWOBotController::AbilitiesUsed, AWWOBotController::Ults,
		AWWOBotController::Retreats, AWWOBotController::NoCover, AWWOBotController::Swaps,
		AWWOBotController::StuckIn[0], AWWOBotController::StuckIn[1], AWWOBotController::StuckIn[2], AWWOBotController::StuckIn[3]);
}

void FWWOStressHarness::Tick(AWWOGameMode& GM, float DeltaSeconds)
{
	if (!WWOSwitches::IsStress()) return;
	UWorld* World = GM.GetWorld();
	StressClock += DeltaSeconds;
	StressFrames += 1.f;
	StressWorst = FMath::Max(StressWorst, DeltaSeconds);
	StressGame += FPlatformTime::ToMilliseconds(GGameThreadTime);
	StressRender += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	StressGpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	// -WWOProfileGPU: one GPU pass breakdown in the log, 12 s in (or -WWOProfileAt=seconds), and a screenshot 2 s before it.
	// -WWOFogTest=N: N thick Veil-sized spore fogs around the host 3 s in (-WWOFogAhead=meters: how far in front).
	int32 FogCount = 0;
	if (StressClock >= 3.f && StressClock - DeltaSeconds < 3.f && WWOSwitches::Value(TEXT("WWOFogTest="), FogCount))
	{
		float Ahead = 0.f;
		WWOSwitches::Value(TEXT("WWOFogAhead="), Ahead);
		if (const AWWOCharacter* Host = UWWOWorldSubsystem::Get(&GM)->GetPlayer())
		{
			for (int32 i = 0; i < FogCount; ++i)
			{
				AWWOSporeFog::SpawnFog(World, Host->GetActorLocation() + Host->GetActorForwardVector() * (Ahead + i * 2.f) * WWO::M, 7.f, 600.f, true);
			}
		}
	}
	// -WWOBotShots (with -WWOHostPvP -WWOBots=N): the host stands still, unkillable, and keeps
	// looking at the nearest bot, saving a picture every 1.5 s (Saved/AutoTest/43_bot_NN.png): what
	// a bot coming at you and aiming at you looks like from where you stand.
	if (WWOSwitches::Has(TEXT("WWOBotShots")))
	{
		AWWOCharacter* Host = UWWOWorldSubsystem::Get(&GM)->GetPlayer();
		AWWOCharacter* Near = nullptr;
		if (Host)
		{
			for (TActorIterator<AWWOCharacter> It(World); It; ++It)
			{
				if (*It == Host || It->Health->bDead) continue;
				if (!Near || FVector::DistSquared(It->GetActorLocation(), Host->GetActorLocation()) < FVector::DistSquared(Near->GetActorLocation(), Host->GetActorLocation())) Near = *It;
			}
			Host->Health->GodMode = 1.f;
		}
		// -WWOTestKills: every 4 s the host kills an enemy bot, every other one with a precision hit,
		// and saves a picture half a second later (44_kill_NN.png): the "Kill +1" by the reticle.
		if (Host && WWOSwitches::Has(TEXT("WWOTestKills")))
		{
			static float NextKill = 8.f, ShotAt = -1.f;
			static int32 Done = 0;
			if (StressClock >= NextKill)
			{
				for (TActorIterator<AWWOCharacter> It(World); It; ++It)
				{
					if (*It == Host || It->Health->bDead || !UWWOWorldSubsystem::CanHurt(Host, *It)) continue;
					FWWODamageBy By(Host);
					It->TakeHit(99999.f, Done % 2 == 1, It->GetActorLocation());
					NextKill = StressClock + (Done % 3 == 2 ? 0.3f : 4.f); // now and then two close together
					ShotAt = StressClock + 0.5f;
					++Done;
					break;
				}
			}
			if (ShotAt > 0.f && StressClock >= ShotAt)
			{
				ShotAt = -1.f;
				WWOTest::Shot(FString::Printf(TEXT("44_kill_%02d"), Done));
			}
		}
		// -WWOTestFog (with -WWOBotsIdle): the host gets Spore Fog Damage, and every 12 s a fog of
		// theirs (4 m, 8 s) lands on an enemy bot; the bot's shield and health are logged each second.
		if (Host && WWOSwitches::Has(TEXT("WWOTestFog")))
		{
			static float NextFog = 8.f, NextLog = 0.f;
			static TWeakObjectPtr<AWWOCharacter> Fogged;
			if (StressClock >= NextFog)
			{
				NextFog = StressClock + 12.f;
				WWOTest::GuardLoadout(); // the ability equipped here is not left in the saved loadout
				UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(Host);
				if (Loadout && !Host->Abilities->Equipped("spore_fog_damage"))
				{
					for (UWWOAbilityData* A : Loadout->Catalog.Abilities) if (A && A->Id == "spore_fog_damage") Loadout->EquipAbility(0, A);
				}
				for (TActorIterator<AWWOCharacter> It(World); It; ++It)
				{
					if (*It == Host || It->Health->bDead || !UWWOWorldSubsystem::CanHurt(Host, *It)) continue;
					Fogged = *It;
					AWWOSporeFog::SpawnFog(World, It->GetActorLocation(), 4.f, 8.f, false, Host);
					UE_LOG(LogWWO, Display, TEXT("FogTest: fog on %s (equipped %d)"), *It->GetName(), Host->Abilities->Equipped("spore_fog_damage") ? 1 : 0);
					break;
				}
			}
			if (Fogged.IsValid() && StressClock >= NextLog)
			{
				NextLog = StressClock + 1.f;
				UE_LOG(LogWWO, Display, TEXT("FogTest: t %.0f %s shield %.0f health %.0f dead %d, hit markers %d"), StressClock, *Fogged->GetName(), Fogged->Health->Shield, Fogged->Health->Health, Fogged->Health->bDead ? 1 : 0, UWWOWorldSubsystem::HitsMarked);
			}
		}
		// -WWOTestCharge: the host gets Static Charge, filled (so it arms itself); pictures of the
		// screen with it charged (46_static_N.png).
		if (Host && WWOSwitches::Has(TEXT("WWOTestCharge")))
		{
			static int32 Shots = 0;
			static float NextShot = 9.f;
			WWOTest::GuardLoadout(); // the ability equipped here is not left in the saved loadout
			UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(Host);
			if (Loadout && !Host->Abilities->Equipped("static_charge"))
			{
				for (UWWOAbilityData* A : Loadout->Catalog.Abilities) if (A && A->Id == "static_charge") Loadout->EquipAbility(0, A);
			}
			if (StressClock > 6.f) Host->Abilities->OnDamageTaken(99999.f);
			if (StressClock >= NextShot && Shots < 3)
			{
				NextShot = StressClock + 0.7f;
				WWOTest::Shot(FString::Printf(TEXT("46_static_%d"), Shots++));
			}
		}
		if (Host && Near)
		{
			const FRotator To = (Near->GetActorLocation() - Host->GetActorLocation()).Rotation();
			Host->SetViewRotation(FMath::DegreesToRadians(To.Pitch), To.Yaw);
			static int32 Saved = 0;
			static float Next = 6.f;
			if (StressClock >= Next && FVector::Dist(Near->GetActorLocation(), Host->GetActorLocation()) < 1500.f)
			{
				Next = StressClock + 1.5f;
				WWOTest::Shot(FString::Printf(TEXT("43_bot_%02d"), Saved++));
			}
		}
	}
	// -WWOFxGallery (with -WWOHostPvP -WWOBots=1 -WWOBotsIdle, on L_TestRange): stages the kit effects
	// in front of the host one after another and saves each (Saved/AutoTest/42_fx_*.png).
	if (WWOSwitches::Has(TEXT("WWOFxGallery")))
	{
		auto At = [this, DeltaSeconds](float T) { return WWOTest::Crossed(StressClock - DeltaSeconds, StressClock, T); };
		auto Save = [World](const TCHAR* Name, float After)
		{
			const FString Path = WWOTest::ShotPath(FString::Printf(TEXT("42_fx_%s"), Name));
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, [Path] { FScreenshotRequest::RequestScreenshot(Path, true, false); }, FMath::Max(After, 0.03f), false); // (a timer of 0 never fires)
		};
		AWWOCharacter* Host = UWWOWorldSubsystem::Get(&GM)->GetPlayer();
		AWWOCharacter* Bot = nullptr;
		for (TActorIterator<AWWOCharacter> It(World); It; ++It) if (*It != Host && !Bot) Bot = *It;
		if (Host)
		{
			const FVector Fwd = Host->GetActorForwardVector(), Right = Host->GetActorRightVector(), Feet = Host->FeetLocation();
			const FLinearColor Orange(0.95f, 0.5f, 0.1f);
			if (At(3.f))
			{
				// Someone standing in front of you, a damage pool to the right, a spore fog beyond on the left.
				Host->SetViewRotation(-0.12f, Host->GetActorRotation().Yaw);
				if (Bot) Bot->TeleportTo(Host->GetActorLocation() + Fwd * 330.f - Right * 60.f, (-Fwd).Rotation());
				AWWOSporePool::Spawn(World, Feet + Fwd * 620.f + Right * 330.f, 2.6f, 0.f, 60.f, FLinearColor(0.3f, 0.6f, 1.f), "voltvine", nullptr);
				AWWOSporeFog::SpawnFog(World, Feet + Fwd * 1150.f - Right * 520.f + FVector(0, 0, 150.f), 3.6f, 60.f, true);
			}
			if (At(6.f)) Save(TEXT("1_pool_and_fog"), 0.f);
			if (At(6.5f) && Bot) { Bot->Body->SetHandGlow(Orange, 0.85f); Save(TEXT("2_glowing_fists"), 0.4f); }
			if (At(9.5f))
			{
				// A blast, then the scorch it leaves.
				FWWOGrenadeLook Look;
				Look.Color = Orange;
				Look.Flavor = "ironbark";
				AWWOGrenade::SpawnBlast(World, Look, Feet + Fwd * 560.f - Right * 300.f + FVector(0, 0, 30.f), 3.f);
				Save(TEXT("4_blast"), 0.08f);
				Save(TEXT("5_scorch"), 1.2f);
			}
		}
	}
	static const float ProfileAt = [] { float At = 12.f; WWOSwitches::Value(TEXT("WWOProfileAt="), At); return At; }(); // read once, not every frame
	if (StressClock >= ProfileAt - 2.f && StressClock - DeltaSeconds < ProfileAt - 2.f && WWOSwitches::Has(TEXT("WWOProfileGPU"))) WWOTest::Shot(TEXT("profile_view"));
	if (StressClock >= ProfileAt && StressClock - DeltaSeconds < ProfileAt && WWOSwitches::Has(TEXT("WWOProfileGPU")))
	{
		// What is casting shadows and can move (each one makes the sun's shadow map redraw around it).
		TMap<FString, FVector3f> Casters; // name -> count, biggest radius (m), visible count
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			const UPrimitiveComponent* C = *It;
			if (C->GetWorld() != World || !C->IsRegistered() || !C->CastShadow || C->Mobility != EComponentMobility::Movable) continue;
			if (!C->IsVisible() && !C->bCastHiddenShadow) continue;
			const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C);
			const FString Name = FString::Printf(TEXT("%s / %s / %s"), *GetNameSafe(C->GetOwner() ? C->GetOwner()->GetClass() : nullptr), *C->GetClass()->GetName(),
				SM ? *GetNameSafe(SM->GetStaticMesh()) : TEXT("-"));
			FVector3f& V = Casters.FindOrAdd(Name);
			V.X += 1.f;
			V.Y = FMath::Max(V.Y, (float)C->Bounds.SphereRadius / WWO::M);
		}
		Casters.ValueSort([](const FVector3f& A, const FVector3f& B) { return A.X * A.Y > B.X * B.Y; });
		for (const auto& Pair : Casters) UE_LOG(LogWWO, Display, TEXT("Shadow caster (movable): %3.0f x %s, biggest radius %.1f m"), Pair.Value.X, *Pair.Key, Pair.Value.Y);
	}
	if (StressClock >= ProfileAt && StressClock - DeltaSeconds < ProfileAt && WWOSwitches::Has(TEXT("WWOProfileGPU"))) GEngine->Exec(World, TEXT("ProfileGPU"));
	if (StressClock < StressNext) return;
	StressNext += 5.f;
	const UNetDriver* Driver = World->GetNetDriver();
	int32 Out = 0, In = 0, Lost = 0;
	if (Driver)
	{
		for (const UNetConnection* C : Driver->ClientConnections)
		{
			if (!C) continue;
			Out += C->OutBytesPerSecond;
			In += C->InBytesPerSecond;
			Lost += C->OutPacketsLost;
		}
	}
	UE_LOG(LogWWO, Display, TEXT("Stress host: t=%.0f %d players (%d bots), frame avg %.1f ms worst %.1f ms, out %.1f KB/s in %.1f KB/s to %d joiners, lost %d, kills %d, %s"),
		StressClock, UWWOWorldSubsystem::Get(&GM)->GetPlayers().Num(), GM.Roster.BotsSpawned, 5000.f / FMath::Max(StressFrames, 1.f), StressWorst * 1000.f,
		Out / 1024.f, In / 1024.f, Driver ? Driver->ClientConnections.Num() : 0, Lost, AWWOGameMode::PvPKills, *GM.GetGameState<AWWOGameState>()->StatusLine);
	UE_LOG(LogWWO, Display, TEXT("Stress threads: t=%.0f game %.1f ms, render %.1f ms, GPU %.1f ms (averages)"), StressClock,
		StressGame / FMath::Max(StressFrames, 1.f), StressRender / FMath::Max(StressFrames, 1.f), StressGpu / FMath::Max(StressFrames, 1.f));
	// What's alive right now (effects, grenades, fogs...), to line slow stretches up with.
	TMap<FString, int32> AliveNow;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const FString ClassName = It->GetClass()->GetName();
		if (ClassName.StartsWith(TEXT("WWO")) && !It->IsA<APawn>() && !It->IsA<AController>() && !It->IsA<AInfo>()) ++AliveNow.FindOrAdd(ClassName);
	}
	FString AliveText;
	for (const auto& Pair : AliveNow) AliveText += FString::Printf(TEXT(" %s %d"), *Pair.Key.RightChop(3), Pair.Value);
	UE_LOG(LogWWO, Display, TEXT("Stress alive: t=%.0f%s"), StressClock, AliveText.IsEmpty() ? TEXT(" nothing") : *AliveText);
	StressGame = StressRender = StressGpu = 0.f;
	StressFrames = 0.f;
	StressWorst = 0.f;
	FString Value;
	if (WWOSwitches::Value(TEXT("WWOStressTime="), Value) && StressClock >= FCString::Atof(*Value))
	{
		UE_LOG(LogWWO, Display, TEXT("Stress host: done"));
		FPlatformMisc::RequestExit(false);
	}
}
