#pragma once

// Per-level registry and shared settings (Godot: the "targets", "enemies", "teammates",
// "spore_fog" groups, TargetManager and DamageNumbers).
// - Targets register here on BeginPlay; aim assist, melee, grenades and the HUD read them.
// - Floating damage numbers and words ("BLINDED") are queued here; the HUD draws them.
// - Target health / auto respawn settings (Settings -> Targets).

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "WWOWorldSubsystem.generated.h"

class AWWOEnemy;
class AWWOAlly;
class AWWOSporeFog;
class AWWOCharacter;

struct FWWOFloatingText
{
	FString Text;
	FVector Start;
	FLinearColor Color;
	bool bCritical = false;
	float Age = 0.f;
	float Jitter = 0.f;
	TWeakObjectPtr<AActor> Follow; // attached labels (ally status) stay over their actor

	// Godot's DamageNumbers tween: rise 0.6 m over 0.7 s; fade out over 0.7 s after 0.25 s.
	static constexpr float RiseTime = 0.7f;
	static constexpr float FadeDelay = 0.25f;
	static constexpr float FadeTime = 0.7f;
	static constexpr float Lifetime = FadeDelay + FadeTime;
	float Alpha() const { return 1.f - FMath::Clamp((Age - FadeDelay) / FadeTime, 0.f, 1.f); }
};

UCLASS()
class WHATWASOURS_API UWWOWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UWWOWorldSubsystem* Get(const UObject* WorldContext);

	// Settings -> Targets
	UPROPERTY(EditAnywhere, Category = "Targets") float TargetHealth = 200.f;
	UPROPERTY(EditAnywhere, Category = "Targets") float RespawnTime = 2.f; // 0 = only on reset

	void RegisterTarget(AActor* Actor);
	void UnregisterTarget(AActor* Actor);
	void RegisterTeammate(AWWOAlly* Ally);
	void UnregisterTeammate(AWWOAlly* Ally);
	void RegisterFog(AWWOSporeFog* Fog);
	void UnregisterFog(AWWOSporeFog* Fog);

	// Living targets (dummies and enemies that aren't dead).
	TArray<AActor*> LivingTargets() const;
	TArray<AActor*> AllTargets() const;
	TArray<AWWOEnemy*> LivingEnemies() const;
	TArray<AWWOAlly*> LivingTeammates() const;
	TArray<AWWOAlly*> AllTeammates() const;
	const TArray<TWeakObjectPtr<AWWOSporeFog>>& Fogs() const { return SporeFogs; }
	AWWOCharacter* GetPlayer() const; // the player on this machine
	TArray<AWWOCharacter*> GetPlayers() const; // every player in the match (online: all of them)

	// Bring every target back at full health; enemies reset to their spawn. Teammates too.
	void ResetTargets();
	void SetTargetHealth(float Value);

	// Damage numbers and floating words.
	static FLinearColor CritColor() { return FLinearColor(1.f, 0.85f, 0.2f); }
	void SpawnDamageNumber(float Damage, bool bCritical, const FVector& Point, FLinearColor Color = FLinearColor(0, 0, 0, 0));
	void ShowText(const FString& Text, bool bCritical, const FVector& Point, FLinearColor Color = FLinearColor(0, 0, 0, 0));
	const TArray<FWWOFloatingText>& FloatingTexts() const { return Texts; }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UWWOWorldSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableWhenPaused() const override { return false; }

private:
	TArray<TWeakObjectPtr<AActor>> Targets;
	TArray<TWeakObjectPtr<AWWOAlly>> Teammates;
	TArray<TWeakObjectPtr<AWWOSporeFog>> SporeFogs;
	TArray<FWWOFloatingText> Texts;
};
