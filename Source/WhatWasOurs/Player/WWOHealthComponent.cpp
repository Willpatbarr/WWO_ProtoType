#include "Player/WWOHealthComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOAbilityComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WWOTypes.h"
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
	DOREPLIFETIME(UWWOHealthComponent, bRevived);
	DOREPLIFETIME(UWWOHealthComponent, BeingRevived);
}

bool UWWOHealthComponent::IsHost() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

bool UWWOHealthComponent::IsOnline() const
{
	return GetOwner() && GetOwner()->GetNetMode() != NM_Standalone;
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
		// Respawned on the host (it moved you to the start; face the way the start faces), or
		// revived where you fell (keep looking where you were).
		SetBodyBlocks(true);
		P->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		if (P->IsLocallyControlled() && !bRevived) P->SetViewRotation(0.f, Spawn.Rotator().Yaw);
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
	BeingRevived = 0.f;
	RespawnLeft = IsOnline() ? -1.f : RespawnTime; // online: down until a teammate revives you
	DieLocally();
}

void UWWOHealthComponent::DieLocally()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	P->GetCharacterMovement()->StopMovementImmediately();
	P->GetCharacterMovement()->DisableMovement(); // no moving, shooting or throwing
	SetBodyBlocks(false);
	OnDied.Broadcast();
}

void UWWOHealthComponent::Respawn()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!IsHost()) return;
	bDead = false;
	bRevived = false;
	BeingRevived = 0.f;
	SetBodyBlocks(true);
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
		if (RespawnLeft > 0.f && (RespawnLeft -= DeltaSeconds) <= 0.f) Respawn();
		return;
	}
	UpdateRevive(DeltaSeconds);
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

// --- Revive (online) -----------------------------------------------------------------------------

AWWOCharacter* UWWOHealthComponent::FindReviveTarget() const
{
	const AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!P || bDead || !IsOnline()) return nullptr;
	AWWOCharacter* Best = nullptr;
	float BestDistance = ReviveRange * WWO::M;
	for (AWWOCharacter* Other : UWWOWorldSubsystem::Get(this)->GetPlayers())
	{
		if (Other == P || !Other->Health->bDead) continue;
		const float Distance = FVector::Dist(Other->GetActorLocation(), P->GetActorLocation());
		if (Distance <= BestDistance) { Best = Other; BestDistance = Distance; }
	}
	return Best;
}

void UWWOHealthComponent::UpdateRevive(float DeltaSeconds)
{
	// Holding Revive next to a downed teammate fills it up; letting go, moving away or switching
	// to someone else starts over.
	AWWOCharacter* Target = bWantsRevive ? FindReviveTarget() : nullptr;
	if (Target != ReviveTarget.Get())
	{
		if (AWWOCharacter* Old = ReviveTarget.Get()) Old->Health->BeingRevived = 0.f;
		ReviveTarget = Target;
		ReviveProgress = 0.f;
	}
	if (!Target) return;
	ReviveProgress += DeltaSeconds / FMath::Max(ReviveTime, 0.1f);
	Target->Health->BeingRevived = FMath::Min(ReviveProgress, 1.f);
	if (ReviveProgress >= 1.f)
	{
		Target->Health->Revive();
		ReviveTarget = nullptr;
		ReviveProgress = 0.f;
	}
}

void UWWOHealthComponent::Revive()
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!IsHost() || !bDead) return;
	bDead = false;
	bRevived = true;
	BeingRevived = 0.f;
	SetBodyBlocks(true);
	P->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	P->ClearStatusEffects();
	Health = FMath::Max(MaxHealth() * ReviveHealth, 1.f);
	Shield = 0.f;
	SinceDamage = 0.f; // regen waits its usual delay
	OnRespawned.Broadcast();
}

void UWWOHealthComponent::SetBodyBlocks(bool bBlocks)
{
	if (const ACharacter* C = Cast<ACharacter>(GetOwner()))
	{
		C->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, bBlocks ? ECR_Block : ECR_Ignore);
	}
}
