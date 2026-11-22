#include "Player/WWOHealthComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOAbilityComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Game/WWOGameMode.h"
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
	DOREPLIFETIME(UWWOHealthComponent, SpawnYaw);
}

bool UWWOHealthComponent::IsHost() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

bool UWWOHealthComponent::IsOnline() const
{
	return WWO::IsOnline(GetOwner());
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
		if (P->IsLocallyControlled() && !bRevived) P->SetViewRotation(0.f, SpawnYaw);
		P->ClearStatusEffects();
		SinceDamage = 99.f;
		OnRespawned.Broadcast();
	}
}

float UWWOHealthComponent::MapKillHeight(const UWorld* World)
{
	// Looked up once per map (the level's TargetPoint tagged KillZ, else the default).
	static TWeakObjectPtr<const UWorld> For;
	static float Height = KillZ;
	if (For.Get() != World)
	{
		For = World;
		Height = KillZ;
		if (World) for (TActorIterator<ATargetPoint> It(World); It; ++It)
		{
			if (It->ActorHasTag("KillZ")) Height = FMath::Max(KillZ, It->GetActorLocation().Z);
		}
	}
	return Height;
}

void UWWOHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	Spawn = GetOwner()->GetActorTransform();
	TActorIterator<APlayerStart> Start(GetWorld());
	if (Start) Spawn = Start->GetActorTransform();
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag("KillZ")) MapKillZ = FMath::Max(KillZ, It->GetActorLocation().Z);
	}
	// The host starts everyone full. A joiner's copy keeps what the host sent (a player already hurt
	// or down when this machine joined).
	if (IsHost())
	{
		Health = MaxHealth();
		Shield = MaxShield();
	}
	LastSeen = Health + Shield;
	SpawnYaw = Spawn.Rotator().Yaw;
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

FWWOHealthRestored UWWOHealthComponent::Heal(float Amount)
{
	FWWOHealthRestored Done;
	if (!IsHost() || bDead || Amount <= 0.f) return Done;
	const float ToHealth = FMath::Clamp(MaxHealth() - Health, 0.f, Amount);
	const float ToShield = FMath::Clamp(MaxShield() - Shield, 0.f, Amount - ToHealth);
	Health += ToHealth;
	Shield += ToShield;
	Done.Restored = ToHealth + ToShield;
	Done.Overheal = Amount - Done.Restored;
	return Done;
}

void UWWOHealthComponent::DamageCameFrom(const FVector& From)
{
	AWWOCharacter* P = Cast<AWWOCharacter>(GetOwner());
	if (!IsHost() || bDead || GodMode >= 0.5f || P->bUlting || P->EntrenchLeft > 0.f || !P->IsPlayerControlled()) return; // as TakeDamage; bots have no HUD
	if (P->IsLocallyControlled()) AddDamageMark(From);
	else P->ClientDamageFrom(From);
}

void UWWOHealthComponent::AddDamageMark(const FVector& From)
{
	const double Now = GetWorld()->GetTimeSeconds();
	DamageMarks.RemoveAll([Now](const FDamageMark& M) { return Now - M.Time > 3.0; });
	// More from about the same spot (a burst, a second pellet) refreshes its mark.
	for (FDamageMark& M : DamageMarks)
	{
		if (FVector::DistSquared(M.From, From) < FMath::Square(300.f)) { M.From = From; M.Time = Now; return; }
	}
	DamageMarks.Add({ From, Now });
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
	// Down mid-revive: the teammate you were bringing back stops showing a bar, and you start over next time.
	if (ReviveTarget.IsValid()) ReviveTarget->Health->BeingRevived = 0.f;
	ReviveTarget = nullptr;
	ReviveProgress = 0.f;
	// Alone: respawn shortly. Co-op online: down until a teammate revives you. PvP: respawn after its delay.
	const AWWOGameMode* GM = GetWorld()->GetAuthGameMode<AWWOGameMode>();
	if (UWWOWorldSubsystem::IsPvP(this)) RespawnLeft = GM ? FMath::Max(GM->PvPRespawnTime, 0.1f) : 3.f;
	else RespawnLeft = IsOnline() ? -1.f : RespawnTime;
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
	FTransform Where = Spawn;
	if (const AWWOGameMode* GM = GetWorld()->GetAuthGameMode<AWWOGameMode>(); GM && UWWOWorldSubsystem::IsPvP(this)) Where = GM->PvPSpawnFor(P);
	Spawn = Where;
	SpawnYaw = Where.Rotator().Yaw; // a joiner faces it too (OnRep_Dead)
	P->TeleportTo(Where.GetLocation(), Where.Rotator());
	P->SetViewRotation(0.f, Where.Rotator().Yaw);
	LastAttacker = nullptr;
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
	if (P->GetActorLocation().Z < MapKillZ)
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
	if (!P || bDead || !IsOnline() || UWWOWorldSubsystem::IsPvP(this)) return nullptr; // PvP: no reviving, you respawn
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
