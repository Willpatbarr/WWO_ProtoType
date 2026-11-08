#pragma once

// What every player needs to know about the match, replicated from the host: the wave status line
// ("WAVE 3 / 8 • 12 enemies left", "ARENA CLEARED"...) the game mode writes each frame.
// Also relays a few gameplay visuals the host makes (lightning, discharges) so everyone sees them.

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/NetSerialization.h"
#include "Weapons/WWOGrenade.h"
#include "WWOGameState.generated.h"

UCLASS()
class WHATWASOURS_API AWWOGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated) FString StatusLine;
	UPROPERTY(Replicated) FLinearColor StatusColor = FLinearColor::White;
	// PvP (team deathmatch): set by the host when the match is PvP; both teams' kills.
	UPROPERTY(Replicated) bool bPvP = false;
	UPROPERTY(Replicated) TArray<int32> TeamScores;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Spawn here and, from the host, on everyone else's screen too. Skip: a player whose own
	// machine already showed it (its predicted punch), so it isn't drawn twice there.
	static void Bolt(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Lifetime,
		AActor* FromActor = nullptr, AActor* ToActor = nullptr);
	static int32 BlastsSeen; // blasts this machine drew for the host (-WWOAutoTestNet)
	static void Blast(UWorld* World, const FWWOGrenadeLook& Look, const FVector& Center, float RadiusM, AActor* Ignore = nullptr);
	static void Text(UWorld* World, const FString& Words, const FVector& At, const FLinearColor& Color); // "BLINDED", "SLOWED"
	static void Poison(AActor* Target, float DamagePerSecond, float Seconds); // the host's poison (its looks and numbers everywhere)
	static void Tracer(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color);
	static void Shockwave(UWorld* World, const FVector& At, const FLinearColor& Color, float RadiusM, float Seconds);
	static void Discharge(UWorld* World, AActor* Target, const FVector& Impact, const FVector& Away, const FLinearColor& Color,
		int32 Arcs, float RadiusM, float Lifetime, AActor* Skip = nullptr);

private:
	UFUNCTION(NetMulticast, Reliable) void MulticastBlast(const FWWOGrenadeLook& Look, FVector_NetQuantize Center, float RadiusM);
	UFUNCTION(NetMulticast, Unreliable) void MulticastText(const FString& Words, FVector_NetQuantize At, FLinearColor Color);
	UFUNCTION(NetMulticast, Reliable) void MulticastPoison(AActor* Target, float DamagePerSecond, float Seconds);
	UFUNCTION(NetMulticast, Unreliable) void MulticastTracer(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color);
	UFUNCTION(NetMulticast, Unreliable) void MulticastShockwave(FVector_NetQuantize At, FLinearColor Color, float RadiusM, float Seconds);
	UFUNCTION(NetMulticast, Unreliable) void MulticastBolt(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color, float Lifetime, AActor* FromActor, AActor* ToActor);
	UFUNCTION(NetMulticast, Unreliable) void MulticastDischarge(AActor* Target, FVector_NetQuantize Impact, FVector_NetQuantizeNormal Away, FLinearColor Color,
		int32 Arcs, float RadiusM, float Lifetime, AActor* Skip);
};
