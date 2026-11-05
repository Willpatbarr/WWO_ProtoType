#include "Player/WWOHealthComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOAbilityComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"

UWWOHealthComponent::UWWOHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWWOHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	Spawn = GetOwner()->GetActorTransform();
	TActorIterator<APlayerStart> Start(GetWorld());
	if (Start) Spawn = Start->GetActorTransform();
	Health = MaxHealth();
	Shield = MaxShield();
}

float UWWOHealthComponent::MaxHealth() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	return FMath::Max(P->M("max_health", BaseHealth), 1.f);
}

float UWWOHealthComponent::MaxShield() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	return FMath::Max(P->M("max_shield", BaseShield), 0.f);
}

void UWWOHealthComponent::TakeDamage(float Amount, bool bMelee)
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (bDead || Amount <= 0.f || GodMode >= 0.5f || P->bUlting || P->EntrenchLeft > 0.f) return; // no damage during the ult or while entrenched
	if (bMelee) Amount *= P->Abilities->IncomingMeleeMultiplier();
	SinceDamage = 0.f;
	const float ToShield = FMath::Min(Amount, Shield);
	Shield -= ToShield;
	Health -= Amount - ToShield;
	OnDamaged.Broadcast(Amount);
	P->Abilities->OnDamageTaken(Amount); // Static Charge
	if (Health <= 0.f) Die();
}

void UWWOHealthComponent::Kill()
{
	if (bDead) return;
	Shield = 0.f;
	Health = 0.f;
	Die();
}

void UWWOHealthComponent::Die()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	Health = 0.f;
	bDead = true;
	RespawnLeft = RespawnTime;
	P->GetCharacterMovement()->StopMovementImmediately();
	P->GetCharacterMovement()->DisableMovement(); // no moving, shooting or throwing
	OnDied.Broadcast();
}

void UWWOHealthComponent::Respawn()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	bDead = false;
	P->TeleportTo(Spawn.GetLocation(), Spawn.Rotator());
	P->SetViewRotation(0.f, Spawn.Rotator().Yaw);
	P->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	P->ClearStatusEffects();
	Health = MaxHealth();
	Shield = MaxShield();
	SinceDamage = 99.f;
	OnRespawned.Broadcast();
}

void UWWOHealthComponent::UpdateHealth(float DeltaSeconds)
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (bDead)
	{
		RespawnLeft -= DeltaSeconds;
		if (RespawnLeft <= 0.f) Respawn();
		return;
	}
	if (P->GetActorLocation().Z < KillZ)
	{
		Kill();
		return;
	}
	// Frame part changes can lower the maximums.
	Health = FMath::Min(Health, MaxHealth());
	Shield = FMath::Min(Shield, MaxShield());
	SinceDamage += DeltaSeconds;
	if (SinceDamage >= P->M("recharge_delay", RechargeDelay))
	{
		if (Health < MaxHealth()) Health = FMath::Min(Health + HealthRegen * DeltaSeconds, MaxHealth());
		else Shield = FMath::Min(Shield + P->M("shield_regen", ShieldRegen) * DeltaSeconds, MaxShield());
	}
}
