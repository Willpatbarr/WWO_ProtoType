// -WWOAutoTestPvP: a two-player check of team deathmatch. Host: -WWOHostPvP -WWOAutoTestPvP
// -WWOKillTarget=3; joiner: -WWOJoin=<ip> -WWOAutoTestPvP. Once both are in:
// - teams: the two players are on different teams (the joiner joined the smaller one);
// - 2-8 s: the joiner shoots the host's head until it dies: the host's health drops on the host,
//   the kill is credited to the joiner and its team scores, the host respawns after the delay;
// - 9-12.5 s: friendly fire: the host puts the joiner on its own team and stands in front of it;
//   the joiner's shots must not hurt it (the host refuses them);
// - 13.5 s on: the joiner goes back; the host kills it (credited) until the kill target ends the
//   match; the match restarts (still PvP) and both arrive on the fresh map: round 2.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Engine/NetConnection.h"
#include "Player/WWOCharacterMovement.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOWeaponComponent.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Engine/World.h"

namespace
{
	constexpr float PvPSettle = 4.f, PvPWaitLimit = 180.f;
	int32 GPvPRound = 0;
	TWeakObjectPtr<APlayerController> GPvPRound1;
}

void AWWOPlayerController::UpdateAutoPvPTest(float DeltaTime)
{
	UWorld* World = GetWorld();
	AWWOCharacter* P = GetWWOCharacter();
	const ENetMode Mode = World->GetNetMode();
	const bool bHost = Mode == NM_ListenServer;
	AutoNetWait += DeltaTime;
	if (Mode == NM_Standalone || !P)
	{
		if (AutoNetWait > PvPWaitLimit) { UE_LOG(LogWWO, Warning, TEXT("AutoTest pvp: FAIL never went online")); FPlatformMisc::RequestExit(false); }
		return;
	}
	AWWOCharacter* Other = nullptr;
	for (AWWOCharacter* C : UWWOWorldSubsystem::Get(this)->GetPlayers()) if (C != P) { Other = C; break; }
	if (!bAutoNetSawOther)
	{
		if (!Other)
		{
			if (AutoNetWait > PvPWaitLimit) { UE_LOG(LogWWO, Warning, TEXT("AutoTest pvp: FAIL no other player after %.0f s"), AutoNetWait); FPlatformMisc::RequestExit(false); }
			return;
		}
		AutoNetClock = -PvPSettle;
		bAutoNetSawOther = true;
		UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: %s%s sees the other player (PvP %d)"), GPvPRound > 0 ? TEXT("round 2: ") : TEXT(""), bHost ? TEXT("host") : TEXT("joiner"),
			UWWOWorldSubsystem::IsPvP(this));
	}
	const float Before = AutoNetClock;
	AutoNetClock += DeltaTime;
	if (AutoNetClock < 0.f) return;
	const float T = AutoNetClock;
	auto Crossed = [Before, T](float At) { return Before < At && T >= At; };
	if (bHost && GPvPRound1.Get() == this) return;
	if (GPvPRound > 0 && GPvPRound1.Get() != this)
	{
		if (!bHost && T > 1.f) { UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: round 2 done (PvP %d), joiner leaving"), UWWOWorldSubsystem::IsPvP(this)); FPlatformMisc::RequestExit(false); }
		if (bHost && !Other) { UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: host done, the joiner left after the restart")); FPlatformMisc::RequestExit(false); }
		return;
	}
	if (T > 90.f) { UE_LOG(LogWWO, Warning, TEXT("AutoTest pvp: FAIL the match never ended")); FPlatformMisc::RequestExit(false); return; }
	if (!Other) return;

	const AWWOGameState* GS = World->GetGameState<AWWOGameState>();
	auto Score = [GS]() { return FString::Printf(TEXT("BLUE %d ORANGE %d"), GS && GS->TeamScores.Num() > 0 ? GS->TeamScores[0] : -1, GS && GS->TeamScores.Num() > 1 ? GS->TeamScores[1] : -1); };
	const AWWOPlayerState* MyPS = GetPlayerState<AWWOPlayerState>();
	const AWWOPlayerState* OtherPS = Other->GetPlayerState<AWWOPlayerState>();
	AWWOGameMode* GM = World->GetAuthGameMode<AWWOGameMode>();
	if (Crossed(1.f))
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: %s on team %d, the other on %d; can hurt it %d; %s; status \"%s\""), bHost ? TEXT("host") : TEXT("joiner"),
			MyPS ? MyPS->Team : -1, OtherPS ? OtherPS->Team : -1, UWWOWorldSubsystem::CanHurt(P, Other), *Score(), GS ? *GS->StatusLine : TEXT("-"));
	}

	if (Crossed(1.6f))
	{
		const TArray<float> Base = P->BaseMoveStats();
		UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: %s moves by walk %.2f sprint %.2f jump %.2f (the host's settings)"), bHost ? TEXT("host") : TEXT("joiner"), Base[0], Base[1], Base[5]);
	}
	if (!bHost && Crossed(1.8f))
	{
		const FVector To = Other->EyeLocation() - P->EyeLocation();
		P->SetViewRotation(FMath::Asin(To.GetSafeNormal().Z), To.Rotation().Yaw);
	}
	if (!bHost && Crossed(2.1f)) Shot(TEXT("60_pvp_joiner_sees_host"));

	// The joiner shoots the host's head (semi-auto: a fresh pull every half second).
	auto ShootAt = [this, P, Other, T](float From)
	{
		const FVector Dir = (Other->GetAimParts()[0].Position - P->EyeLocation()).GetSafeNormal();
		P->SetViewRotation(FMath::Asin(Dir.Z), Dir.Rotation().Yaw);
		Held[(int32)EWWOAction::Aim] = true;
		const int32 Pull = FMath::FloorToInt((T - From) / 0.5f) + 1;
		JustPressed[(int32)EWWOAction::Fire] = Pull > AutoNetShotPulls;
		Held[(int32)EWWOAction::Fire] = Pull > AutoNetShotPulls;
		AutoNetShotPulls = FMath::Max(AutoNetShotPulls, Pull);
	};
	if (bHost && Crossed(2.f)) AutoPvPHostHealth = P->Health->Health + P->Health->Shield;
	if (!bHost && T >= 2.4f && T < 8.f && !Other->Health->bDead && (!MyPS || MyPS->Kills < 1)) ShootAt(2.4f); // until the first kill
	if (Crossed(8.f))
	{
		Held[(int32)EWWOAction::Aim] = Held[(int32)EWWOAction::Fire] = false;
		UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: %s after the joiner's shots: %s; joiner kills %d, host deaths %d; host dead %d"),
			bHost ? TEXT("host") : TEXT("joiner"), *Score(), bHost ? (OtherPS ? OtherPS->Kills : -1) : (MyPS ? MyPS->Kills : -1),
			bHost ? (MyPS ? MyPS->Deaths : -1) : (OtherPS ? OtherPS->Deaths : -1), bHost ? P->Health->bDead : Other->Health->bDead);
	}

	// Friendly fire: the host takes the joiner onto its team and stands in front of it.
	if (bHost && Crossed(9.f) && GM)
	{
		if (P->Health->bDead) P->Health->Respawn();
		GM->SetTeam(const_cast<AWWOPlayerState*>(OtherPS), MyPS ? MyPS->Team : 0);
		const FVector Ahead = Other->GetActorLocation() + Other->GetActorForwardVector() * 4.f * WWO::M;
		P->TeleportTo(Ahead, P->GetActorRotation());
		AutoPvPRejected = AWWOCharacter::NetHitsRejected;
	}
	if (bHost && Crossed(9.6f)) AutoPvPHostHealth = P->Health->Health + P->Health->Shield;
	if (!bHost && T >= 9.8f && T < 12.f) ShootAt(9.8f);
	if (Crossed(12.5f))
	{
		Held[(int32)EWWOAction::Aim] = Held[(int32)EWWOAction::Fire] = false;
		if (bHost)
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: friendly fire: same team %d, host health %.0f -> %.0f, teammate hits refused %d"),
				MyPS && OtherPS && MyPS->Team == OtherPS->Team, AutoPvPHostHealth, P->Health->Health + P->Health->Shield, AWWOCharacter::NetHitsRejected - AutoPvPRejected);
		}
		else
		{
			UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: joiner fired at its teammate; can hurt it %d"), UWWOWorldSubsystem::CanHurt(P, Other));
		}
	}
	if (bHost && Crossed(13.f) && GM) GM->SetTeam(const_cast<AWWOPlayerState*>(OtherPS), MyPS ? 1 - MyPS->Team : 1);

	// The host kills the joiner (credited to the host) until the kill target ends the match.
	if (bHost && GM && T >= 13.5f && GM->State == EWWOWaveState::Match && !Other->Health->bDead)
	{
		FWWODamageBy By(P);
		Other->TakeHit(99999.f, false, Other->GetActorLocation());
		UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: host killed the joiner at %.1f s: %s"), T, *Score());
	}
	if (bHost && GM && GM->State == EWWOWaveState::MatchOver && GPvPRound == 0)
	{
		GPvPRound = 1;
		GPvPRound1 = this;
		UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: match over at %.1f s: \"%s\"; host kills %d deaths %d, joiner kills %d deaths %d; restarting"), T,
			GS ? *GS->StatusLine : TEXT("-"), MyPS ? MyPS->Kills : -1, MyPS ? MyPS->Deaths : -1, OtherPS ? OtherPS->Kills : -1, OtherPS ? OtherPS->Deaths : -1);
	}
	if (!bHost && GS && GS->StatusLine.Contains(TEXT("WINS")) && GPvPRound == 0)
	{
		GPvPRound = 1;
		GPvPRound1 = this;
		UE_LOG(LogWWO, Display, TEXT("AutoTest pvp: joiner sees \"%s\""), *GS->StatusLine);
	}
}

void AWWOPlayerController::LogJoinerStress(float DeltaTime)
{
	// A joiner in a busy match: its frame time, what arrives, and how often the host corrects it.
	UWWOCharacterMovement::bLogNetCorrections = false;
	StressClock += DeltaTime;
	StressFrames += 1.f;
	StressWorst = FMath::Max(StressWorst, DeltaTime);
	if (StressClock < StressNext) return;
	StressNext += 5.f;
	const UNetConnection* Conn = GetNetConnection();
	const int32 Now = UWWOCharacterMovement::NetCorrections;
	const AWWOGameState* GS = GetWorld()->GetGameState<AWWOGameState>();
	UE_LOG(LogWWO, Display, TEXT("Stress joiner: t=%.0f %d players, frame avg %.1f ms worst %.1f ms, in %.1f KB/s out %.1f KB/s, lost %d, corrections %d, %s"),
		StressClock, UWWOWorldSubsystem::Get(this)->GetPlayers().Num(), 5000.f / FMath::Max(StressFrames, 1.f), StressWorst * 1000.f,
		Conn ? Conn->InBytesPerSecond / 1024.f : 0.f, Conn ? Conn->OutBytesPerSecond / 1024.f : 0.f, Conn ? Conn->InPacketsLost : 0, Now - StressCorrections,
		GS ? *GS->StatusLine : TEXT("-"));
	StressCorrections = Now;
	StressFrames = 0.f;
	StressWorst = 0.f;
	FString Value;
	if (FParse::Value(FCommandLine::Get(), TEXT("WWOStressTime="), Value) && StressClock >= FCString::Atof(*Value))
	{
		UE_LOG(LogWWO, Display, TEXT("Stress joiner: done"));
		FPlatformMisc::RequestExit(false);
	}
}
