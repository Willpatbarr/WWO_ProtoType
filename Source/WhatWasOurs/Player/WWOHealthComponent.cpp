#include "Player/WWOHealthComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOAbilityComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

UWWOHealthComponent::UWWOHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UWWOHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UWWOHealthComponent, Health);
	DOREPLIFETIME(UWWOHealthComponent, Shield);
	DOREPLIFETIME(UWWOHealthComponent, bDead);
}

bool UWWOHealthComponent::IsHost() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

void UWWOHealthComponent::OnRep_Health()
{
	// Damage the host dealt: the same feedback as alone (HUD flash, rumble...).
	const float Now = Health + Shield;
	if (LastSeen >= 0.f && Now < LastSeen - 0.01f && !bDead) OnDamaged.Broadcast(LastSeen - Now);
	LastSeen = Now;
	if (Now < MaxHealth() + MaxShield()) SinceDamage = 0.f;
}

void UWWOHealthComponent::OnRep_Dead()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!P) return;
	if (bDead)
	{
		DieLocally();
	}
	else
	{
		// Respawned on the host (it moved you to the start); face the way the start faces.
		P->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		if (P->IsLocallyControlled()) P->SetViewRotation(0.f, Spawn.Rotator().Yaw);
		P->ClearStatusEffects();
		SinceDamage = 99.f;
		OnRespawned.Broadcast();
	}
}

void UWWOHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	Spawn = GetOwner()->GetActorTransform();
	TActorIterator<APlayerStart> Start(GetWorld());
	if (Start) Spawn = Start->GetActorTransform();
	Health = MaxHealth();
	Shield = MaxShield();
	LastSeen = Health + Shield;
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
	if (!IsHost()) return; // online, only the host deals damage (it replicates)
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
	if (bDead || !IsHost()) return;
	Shield = 0.f;
	Health = 0.f;
	Die();
}

void UWWOHealthComponent::Die()
{
	Health = 0.f;
	bDead = true;
	RespawnLeft = RespawnTime;
	DieLocally();
}

void UWWOHealthComponent::DieLocally()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	P->GetCharacterMovement()->StopMovementImmediately();
	P->GetCharacterMovement()->DisableMovement(); // no moving, shooting or throwing
	OnDied.Broadcast();
}

void UWWOHealthComponent::Respawn()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!IsHost()) return;
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
	if (!IsHost()) return; // a joiner's own health runs on the host and replicates here
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
