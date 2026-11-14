#include "Game/WWOGameState.h"
#include "WhatWasOurs.h"
#include "GameFramework/PlayerController.h"
#include "Game/WWOPlayerState.h"
#include "FX/WWOFX.h"
#include "Flora/WWOFloraEffects.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

void AWWOGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWWOGameState, StatusLine);
	DOREPLIFETIME(AWWOGameState, StatusColor);
	DOREPLIFETIME(AWWOGameState, bPvP);
	DOREPLIFETIME(AWWOGameState, TeamScores);
	DOREPLIFETIME(AWWOGameState, bMatchOver);
	DOREPLIFETIME(AWWOGameState, MatchWinner);
}

void AWWOGameState::Bolt(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Lifetime, AActor* FromActor, AActor* ToActor)
{
	if (!World) return;
	AWWOLightningBolt::Spawn(World, From, To, Color, Lifetime, FromActor, ToActor);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastBolt(From, To, Color, Lifetime, FromActor, ToActor);
	}
}

void AWWOGameState::Blast(UWorld* World, const FWWOGrenadeLook& Look, const FVector& Center, float RadiusM, AActor* Ignore)
{
	if (!World) return;
	AWWOGrenade::SpawnBlast(World, Look, Center, RadiusM, Ignore);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastBlast(Look, Center, RadiusM);
	}
}

int32 AWWOGameState::BlastsSeen = 0;

void AWWOGameState::MulticastBlast_Implementation(const FWWOGrenadeLook& Look, FVector_NetQuantize Center, float RadiusM)
{
	if (HasAuthority()) return; // the host drew it already
	++BlastsSeen;
	AWWOGrenade::SpawnBlast(GetWorld(), Look, Center, RadiusM);
}

void AWWOGameState::Text(UWorld* World, const FString& Words, const FVector& At, const FLinearColor& Color)
{
	if (!World) return;
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(World)) WS->ShowText(Words, false, At, Color);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastText(Words, At, Color);
	}
}

const FName AWWOGameState::KillPrecision(TEXT("precision"));

void AWWOGameState::ReportKill(const AWWOPlayerState* KillerState, const FString& Killer, int32 KillerTeam, const FString& Victim, int32 VictimTeam, FName How, int32 Points)
{
	if (HasAuthority()) MulticastKill(KillerState, Killer, (int8)KillerTeam, Victim, (int8)VictimTeam, How, (uint8)Points);
}

void AWWOGameState::MulticastKill_Implementation(const AWWOPlayerState* KillerState, const FString& Killer, int8 KillerTeam, const FString& Victim, int8 VictimTeam, FName How, uint8 Points)
{
	// On the host and on every joiner alike: each keeps its own list.
	FKillLine Line;
	Line.Killer = Killer;
	Line.Victim = Victim;
	Line.KillerTeam = KillerTeam;
	Line.VictimTeam = VictimTeam;
	Line.Points = Points;
	const APlayerController* Local = GetWorld()->GetFirstPlayerController();
	Line.bMine = KillerState && Local && Local->PlayerState == KillerState;
	Line.How = How;
	Line.At = FPlatformTime::Seconds();
	KillFeed.Add(Line);
	if (!HasAuthority()) UE_LOG(LogWWO, Verbose, TEXT("Kill feed: %s > %s (%s, +%d, mine %d)"), *Killer, *Victim, *How.ToString(), (int32)Points, Line.bMine ? 1 : 0);
	if (KillFeed.Num() > 8) KillFeed.RemoveAt(0, KillFeed.Num() - 8);
}

void AWWOGameState::MulticastText_Implementation(const FString& Words, FVector_NetQuantize At, FLinearColor Color)
{
	if (HasAuthority()) return;
	if (UWWOWorldSubsystem* WS = UWWOWorldSubsystem::Get(this)) WS->ShowText(Words, false, At, Color);
}

void AWWOGameState::Poison(AActor* Target, float DamagePerSecond, float Seconds)
{
	if (!Target) return;
	UWWODamageOverTime::Apply(Target, DamagePerSecond, Seconds); // the damage (on a joiner its ticks only flash and show numbers)
	UWorld* World = Target->GetWorld();
	if (World && World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastPoison(Target, DamagePerSecond, Seconds);
	}
}

void AWWOGameState::MulticastPoison_Implementation(AActor* Target, float DamagePerSecond, float Seconds)
{
	if (!HasAuthority() && Target) UWWODamageOverTime::Apply(Target, DamagePerSecond, Seconds);
}

void AWWOGameState::Tracer(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color)
{
	if (!World) return;
	AWWOFXActor::Tracer(World, From, To, Color);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastTracer(From, To, Color);
	}
}

void AWWOGameState::MulticastTracer_Implementation(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color)
{
	if (!HasAuthority()) AWWOFXActor::Tracer(GetWorld(), From, To, Color);
}

void AWWOGameState::Shockwave(UWorld* World, const FVector& At, const FLinearColor& Color, float RadiusM, float Seconds)
{
	if (!World) return;
	AWWOFXActor::Shockwave(World, At, Color, RadiusM, Seconds);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastShockwave(At, Color, RadiusM, Seconds);
	}
}

void AWWOGameState::MulticastShockwave_Implementation(FVector_NetQuantize At, FLinearColor Color, float RadiusM, float Seconds)
{
	if (!HasAuthority()) AWWOFXActor::Shockwave(GetWorld(), At, Color, RadiusM, Seconds);
}

void AWWOGameState::Discharge(UWorld* World, AActor* Target, const FVector& Impact, const FVector& Away, const FLinearColor& Color,
	int32 Arcs, float RadiusM, float Lifetime, AActor* Skip)
{
	if (!World) return;
	AWWODischarge::Spawn(World, Target, Impact, Away, Color, Arcs, RadiusM, Lifetime);
	if (World->GetNetMode() == NM_ListenServer)
	{
		if (AWWOGameState* GS = World->GetGameState<AWWOGameState>()) GS->MulticastDischarge(Target, Impact, Away, Color, Arcs, RadiusM, Lifetime, Skip);
	}
}

void AWWOGameState::MulticastBolt_Implementation(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color, float Lifetime, AActor* FromActor, AActor* ToActor)
{
	if (HasAuthority()) return; // the host drew it already
	AWWOLightningBolt::Spawn(GetWorld(), From, To, Color, Lifetime, FromActor, ToActor);
}

void AWWOGameState::MulticastDischarge_Implementation(AActor* Target, FVector_NetQuantize Impact, FVector_NetQuantizeNormal Away, FLinearColor Color,
	int32 Arcs, float RadiusM, float Lifetime, AActor* Skip)
{
	if (HasAuthority()) return;
	const APawn* SkipPawn = Cast<APawn>(Skip);
	if (SkipPawn && SkipPawn->IsLocallyControlled()) return; // this machine predicted it
	if (!Target) return;
	AWWODischarge::Spawn(GetWorld(), Target, Impact, Away, Color, Arcs, RadiusM, Lifetime);
}
