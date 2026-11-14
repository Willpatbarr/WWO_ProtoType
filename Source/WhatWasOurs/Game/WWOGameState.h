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
	UPROPERTY(Replicated) bool bMatchOver = false; // the PvP match ended: every machine shows the scoreboard (UWWOScoreboard)
	UPROPERTY(Replicated) int8 MatchWinner = -1;   // the team that won it (-1: a draw)

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// The kill feed: "killer > killee" lines, newest last. The host reports each credited kill
	// (ReportKill) and every machine keeps its own short list for the HUD to draw. How is the
	// method of death, for when the feed says it (nothing sets or shows it yet).
	struct FKillLine
	{
		FString Killer, Victim;
		int32 KillerTeam = -1, VictimTeam = -1;
		FName How;        // NAME_None: a plain kill; KillPrecision: the killing hit was a precision (head) hit
		int32 Points = 1; // what it added to the killer's team's score
		bool bMine = false; // this machine's own player got it (the HUD's "Kill +1" by the reticle)
		double At = 0.0; // real seconds (FPlatformTime) it arrived
	};
	TArray<FKillLine> KillFeed;
	static const FName KillPrecision;
	static constexpr int32 KillPoints = 1, PrecisionKillPoints = 2; // team score for a kill, and for a precision kill
	void ReportKill(const class AWWOPlayerState* KillerState, const FString& Killer, int32 KillerTeam, const FString& Victim, int32 VictimTeam, FName How, int32 Points);

	// Spawn here and, from the host, on everyone else's screen too. Skip: a player whose own
	// machine already showed it (its predicted punch), so it isn't drawn twice there.
	static void Bolt(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Lifetime,
		AActor* FromActor = nullptr, AActor* ToActor = nullptr);
	static int32 BlastsSeen; // blasts this machine drew for the host (-WWOAutoTestNet)
	static void Blast(UWorld* World, const FWWOGrenadeLook& Look, const FVector& Center, float RadiusM, AActor* Ignore = nullptr);
	static void Text(UWorld* World, const FString& Words, const FVector& At, const FLinearColor& Color); // "BLINDED", "SLOWED"
	static void Sound(UWorld* World, FName Event, const FVector& At, float Volume = 1.f); // a Sounds.json event, heard on every machine
	static void Poison(AActor* Target, float DamagePerSecond, float Seconds); // the host's poison (its looks and numbers everywhere)
	static void Tracer(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color);
	static void Shockwave(UWorld* World, const FVector& At, const FLinearColor& Color, float RadiusM, float Seconds);
	static void Discharge(UWorld* World, AActor* Target, const FVector& Impact, const FVector& Away, const FLinearColor& Color,
		int32 Arcs, float RadiusM, float Lifetime, AActor* Skip = nullptr);

private:
	UFUNCTION(NetMulticast, Reliable) void MulticastKill(const class AWWOPlayerState* KillerState, const FString& Killer, int8 KillerTeam, const FString& Victim, int8 VictimTeam, FName How, uint8 Points);
	UFUNCTION(NetMulticast, Reliable) void MulticastBlast(const FWWOGrenadeLook& Look, FVector_NetQuantize Center, float RadiusM);
	UFUNCTION(NetMulticast, Unreliable) void MulticastSound(FName Event, FVector_NetQuantize At, float Volume);
	UFUNCTION(NetMulticast, Unreliable) void MulticastText(const FString& Words, FVector_NetQuantize At, FLinearColor Color);
	UFUNCTION(NetMulticast, Reliable) void MulticastPoison(AActor* Target, float DamagePerSecond, float Seconds);
	UFUNCTION(NetMulticast, Unreliable) void MulticastTracer(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color);
	UFUNCTION(NetMulticast, Unreliable) void MulticastShockwave(FVector_NetQuantize At, FLinearColor Color, float RadiusM, float Seconds);
	UFUNCTION(NetMulticast, Unreliable) void MulticastBolt(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color, float Lifetime, AActor* FromActor, AActor* ToActor);
	UFUNCTION(NetMulticast, Unreliable) void MulticastDischarge(AActor* Target, FVector_NetQuantize Impact, FVector_NetQuantizeNormal Away, FLinearColor Color,
		int32 Arcs, float RadiusM, float Lifetime, AActor* Skip);
};
