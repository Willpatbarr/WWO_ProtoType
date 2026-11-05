#pragma once

// The player's health and shields, Destiny-style (Godot: player_health.gd).
// Damage hits the shield first. After RechargeDelay without damage, health refills, then
// the shield. At 0 health you die and respawn at the Player Start after RespawnTime.
// Falling below KillZ counts as a death too. Durability points raise the maximums.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WWOHealthComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FWWOOnDamaged, float);
DECLARE_MULTICAST_DELEGATE(FWWOOnDied);

UCLASS(ClassGroup = (WWO))
class WHATWASOURS_API UWWOHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWWOHealthComponent();
	virtual void BeginPlay() override;
	void UpdateHealth(float DeltaSeconds); // called by the character

	UPROPERTY(EditAnywhere, Category = "Player") float BaseHealth = 100.f;
	UPROPERTY(EditAnywhere, Category = "Player") float BaseShield = 100.f;
	UPROPERTY(EditAnywhere, Category = "Player") float RechargeDelay = 4.f;
	UPROPERTY(EditAnywhere, Category = "Player") float HealthRegen = 40.f;
	UPROPERTY(EditAnywhere, Category = "Player") float ShieldRegen = 60.f;
	UPROPERTY(EditAnywhere, Category = "Player") float RespawnTime = 2.f;
	UPROPERTY(EditAnywhere, Category = "Player") float GodMode = 0.f;

	static constexpr float KillZ = -3000.f; // cm

	float Health = 100.f;
	float Shield = 100.f;
	bool bDead = false;
	float SinceDamage = 99.f;
	float RespawnLeft = 0.f;

	FWWOOnDamaged OnDamaged;
	FWWOOnDied OnDied;
	FWWOOnDied OnRespawned;

	float MaxHealth() const;
	float MaxShield() const;
	void TakeDamage(float Amount, bool bMelee = false); // bMelee: a bite, punch or slam
	void Kill();
	void Respawn();

private:
	FTransform Spawn;
	void Die();
};
