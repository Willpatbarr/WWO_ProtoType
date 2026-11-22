#include "Flora/WWODawnLotus.h"
#include "Allies/WWOAlly.h"
#include "Data/WWOData.h"
#include "FX/WWOFX.h"
#include "Game/WWOGameState.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOProf.h"
#include "Player/WWOWeaponComponent.h"
#include "Systems/WWOSound.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "WWOIds.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"

namespace
{
	const FLinearColor HealColor(0.55f, 1.f, 0.6f);
	constexpr float PoolTickSeconds = 0.5f; // the pool heals on this beat, as the damage pools do

	// The living teammates within RadiusM of Center: other players on the same side and AI teammates.
	TArray<AActor*> MatesAround(const AWWOCharacter& P, const FVector& Center, float RadiusM, const FCollisionQueryParams* Sight = nullptr)
	{
		FWWOAreaQuery Q;
		Q.Center = Center;
		Q.Radius = RadiusM * WWO::M;
		Q.Measure = EWWOAreaMeasure::Centre;
		Q.Targets = EWWOAreaTargets::None;
		Q.Players = EWWOAreaPlayers::FriendsOf;
		Q.bTeammates = true;
		Q.For = &P;
		if (Sight)
		{
			Q.SightParams = Sight;
			Q.SightFrom = Center + FVector(0, 0, 5.f);
		}
		TArray<AActor*> Mates;
		UWWOWorldSubsystem::Get(&P)->ForEachTargetInRadius(Q, [&Mates](const FWWOAreaHit& Hit) { Mates.Add(Hit.Actor); return true; });
		return Mates;
	}

	FLinearColor FloraColor(const AWWOCharacter& P)
	{
		const UWWOLoadoutSubsystem* L = P.GetLoadout();
		return L && L->GetFlora() ? L->GetFlora()->Color : HealColor;
	}
}

UWWODawnLotusTuning& WWODawnLotus::T()
{
	return *GetMutableDefault<UWWODawnLotusTuning>();
}

float WWODawnLotus::GrenadeDamage(const UWWOGrenadeData& G)
{
	if (G.Id == WWOIds::Grenade::BloomPool) return 0.f; // it only heals
	if (G.Id == WWOIds::Grenade::SolarBurst) return T().SolarBurstDamage;
	if (G.Id == WWOIds::Grenade::Reveille) return T().ReveilleDamage;
	return G.Damage;
}

float WWODawnLotus::GrenadeRadius(const UWWOGrenadeData& G)
{
	if (G.Id == WWOIds::Grenade::BloomPool) return T().BloomPoolRadius;
	if (G.Id == WWOIds::Grenade::SolarBurst) return T().SolarBurstRadius;
	if (G.Id == WWOIds::Grenade::Reveille) return T().ReveilleRadius;
	return G.BlastRadius;
}

float WWODawnLotus::GrenadeRecharge(const UWWOGrenadeData& G)
{
	if (G.Id == WWOIds::Grenade::BloomPool) return T().BloomPoolRecharge;
	if (G.Id == WWOIds::Grenade::SolarBurst) return T().SolarBurstRecharge;
	if (G.Id == WWOIds::Grenade::Reveille) return T().ReveilleRecharge;
	return G.RechargeMult;
}

float WWODawnLotus::AbilityCooldown(const UWWOAbilityData& A)
{
	return A.Id == WWOIds::Ability::FullBloom ? T().FullBloomCooldown : A.Cooldown;
}

float WWODawnLotus::WithHandling(FName Stat, float Value)
{
	static const FName Reload("reload_time"), Ads("ads_time"), Swap("swap_time"), Ready("ready_time");
	if (Stat != Reload && Stat != Ads && Stat != Swap && Stat != Ready) return Value;
	return Value / FMath::Max(T().ReveilleHandling, 0.1f);
}

FWWOHealResult WWODawnLotus::HealTeammate(AWWOCharacter& Healer, AActor* Mate, float Amount, EWWOHealSource Source)
{
	FWWOHealResult Result;
	if (!Healer.HasAuthority() || !Mate || Mate == &Healer || Amount <= 0.f) return Result;
	if (UWWOWorldSubsystem::CanHurt(&Healer, Mate)) return Result; // the other side
	Amount *= 1.f + UWWOLoadoutSubsystem::Bonus(&Healer, "heal_more"); // the Generous Bloom trait
	if (AWWOCharacter* Player = Cast<AWWOCharacter>(Mate))
	{
		const FWWOHealthRestored Done = Player->Health->Heal(Amount);
		Result.Actual = Done.Restored;
		Result.Overheal = Done.Overheal;
	}
	else if (AWWOAlly* Ally = Cast<AWWOAlly>(Mate))
	{
		Result.Actual = Ally->Heal(Amount);
		Result.Overheal = Ally->IsDown() ? 0.f : Amount - Result.Actual;
	}
	if (Result.Actual <= 0.f) return Result; // nothing restored: no credit, so a full teammate can't be farmed
	if (Result.Actual >= 1.f)
	{
		AWWOGameState::Text(Healer.GetWorld(), FString::Printf(TEXT("+%d"), FMath::RoundToInt(Result.Actual)), Mate->GetActorLocation() + FVector(0, 0, 100.f), HealColor);
	}
	Healer.DawnLotus->NoteHealLanded(Result.Actual);
	Healer.DawnLotus->ShowHealPulse(Mate);
	Healer.DawnLotus->OnHealed(Mate, Result, Source);
	return Result;
}

void WWODawnLotus::FullBloom(AWWOCharacter& P)
{
	const FVector At = P.GetActorLocation();
	const TArray<AActor*> Mates = MatesAround(P, At, T().FullBloomRadius);
	const float Share = Mates.Num() > 0 ? T().FullBloomPool / Mates.Num() : 0.f;
	float Healed = 0.f;
	for (AActor* Mate : Mates) Healed += HealTeammate(P, Mate, Share, EWWOHealSource::FullBloom).Actual;
	AWWOGameState::Shockwave(P.GetWorld(), P.GetActorLocation() - FVector(0, 0, 90.f), FloraColor(P), T().FullBloomRadius, 0.6f);
	UE_LOG(LogWWO, Display, TEXT("DawnLotus Full Bloom: %d teammates, %.1f each, %.1f really healed"), Mates.Num(), Share, Healed);
}

void WWODawnLotus::PetalfallBurst(AWWOCharacter& P)
{
	const TArray<AActor*> Mates = MatesAround(P, P.GetActorLocation(), T().PetalfallRadius);
	float Healed = 0.f;
	for (AActor* Mate : Mates) Healed += HealTeammate(P, Mate, T().PetalfallHeal, EWWOHealSource::Petalfall).Actual;
	P.DawnLotus->ShowPetalBurst(P.GetActorLocation(), T().PetalfallRadius, FloraColor(P));
	UE_LOG(LogWWO, Display, TEXT("DawnLotus Petalfall: %d teammates, %.1f really healed"), Mates.Num(), Healed);
}

void WWODawnLotus::ReveilleBurst(AWWOCharacter& Thrower, const FVector& Center, float RadiusM)
{
	// Walls stop it, as they stop the blast. Players' capsules don't block the Visibility channel.
	FCollisionQueryParams Sight(SCENE_QUERY_STAT(WWOReveille), false, &Thrower);
	TArray<AActor*> Caught = MatesAround(Thrower, Center, RadiusM, &Sight);
	if (FVector::Dist(Thrower.GetActorLocation(), Center) <= RadiusM * WWO::M)
	{
		FHitResult Wall;
		if (!Thrower.GetWorld()->LineTraceSingleByChannel(Wall, Center + FVector(0, 0, 5.f), Thrower.GetActorLocation(), ECC_Visibility, Sight)) Caught.Add(&Thrower);
	}
	int32 Players = 0;
	for (AActor* Actor : Caught)
	{
		AWWOCharacter* Mate = Cast<AWWOCharacter>(Actor); // an AI teammate has no abilities or magazine
		if (!Mate) continue;
		Mate->DawnLotus->GiveReveille(T().ReveilleEnergy, T().ReveilleHandlingTime);
		++Players;
	}
	UE_LOG(LogWWO, Display, TEXT("DawnLotus Reveille: %d players caught"), Players);
}

void WWODawnLotus::FireBeam(AWWOCharacter& P, const FVector& Aim, const FVector& Muzzle, const FLinearColor& Color)
{
	// Aimed from the eye (where the reticle is), drawn from the hands. It has some width: the first target
	// within BeamRadius of the line, in sight of the eye, takes it; else it stops at what the line met.
	const FVector From = P.EyeLocation();
	const FVector Dir = Aim.GetSafeNormal(UE_SMALL_NUMBER, P.GetActorForwardVector());
	const float Range = T().BeamRange * WWO::M;
	const float Width = T().BeamRadius * WWO::M;
	const float Damage = T().BeamBaseDamage + P.DawnLotus->SpendNectar();
	UWorld* World = P.GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOBeam), false, &P);
	FHitResult Line;
	const bool bLine = World->LineTraceSingleByChannel(Line, From, From + Dir * Range, ECC_Visibility, Params);
	FVector To = bLine ? FVector(Line.ImpactPoint) : From + Dir * Range;
	const FWWOAimPick Pick = UWWOWorldSubsystem::Get(&P)->BestAimPart(&P,
		[&](const AActor*, const FWWOAimPart& Part, float Best, float& OutScore)
		{
			const FVector ToPart = Part.Position - From;
			const float Along = ToPart | Dir;
			if (Along <= 0.f || Along > Range || Along >= Best) return false;
			if (FVector::Dist(From + Dir * Along, Part.Position) > Width + Part.Radius) return false;
			OutScore = Along;
			return true;
		},
		[&](const FVector& Point, FHitResult& OutHit) { return World->LineTraceSingleByChannel(OutHit, From, Point, ECC_Visibility, Params); });
	AActor* Struck = nullptr;
	if (Pick.IsValid())
	{
		if (IWWOTargetable* Target = Cast<IWWOTargetable>(Pick.Actor); Target && !Target->IsDead() && UWWOWorldSubsystem::CanHurt(&P, Pick.Actor))
		{
			To = Pick.Hit.ImpactPoint;
			FWWODamageBy By(&P);
			Target->TakeHit(Damage, Pick.Part.bCritical, To);
			Struck = Pick.Actor;
		}
	}
	P.DawnLotus->ShowBeam(Muzzle, To, Color, Struck != nullptr);
	UE_LOG(LogWWO, Display, TEXT("DawnLotus beam: %.0f damage, hit %s, %.1f m"), Damage, *GetNameSafe(Struck), FVector::Dist(From, To) / WWO::M);
}

// --- The component -------------------------------------------------------------------------------

UWWODawnLotusComponent::UWWODawnLotusComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // no clock of its own: its one timer is a world time
	SetIsReplicatedByDefault(true);
}

void UWWODawnLotusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UWWODawnLotusComponent, Nectar, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UWWODawnLotusComponent, UltBonus, COND_OwnerOnly);
}

AWWOCharacter* UWWODawnLotusComponent::Me() const
{
	return Cast<AWWOCharacter>(GetOwner());
}

void UWWODawnLotusComponent::SetNectar(float Value)
{
	const UWWODawnLotusTuning& N = WWODawnLotus::T();
	const float Max = FMath::Max(N.NectarMax, 1.f);
	Nectar = FMath::Clamp(Value, 0.f, Max);
	UltBonus = N.BeamDamageAtFullNectar * Nectar / Max;
}

float UWWODawnLotusComponent::SpendNectar()
{
	SetNectar(Nectar); // the bonus by the numbers as they are now
	const float Bonus = UltBonus;
	SetNectar(0.f);
	return Bonus;
}

void UWWODawnLotusComponent::OnHealed(AActor* Mate, const FWWOHealResult& Heal, EWWOHealSource Source)
{
	const AWWOCharacter* P = Me();
	const UWWOLoadoutSubsystem* L = P ? P->GetLoadout() : nullptr;
	if (!L || !L->GetFlora() || L->GetFlora()->Id != WWOIds::Flora::DawnLotus) return;
	const UWWODawnLotusTuning& N = WWODawnLotus::T();
	SetNectar(Nectar + (Heal.Actual + Heal.Overheal * N.NectarOverhealScale) * N.NectarPerHeal * (1.f + UWWOLoadoutSubsystem::Bonus(P, "nectar_more")));
	if (Source == EWWOHealSource::FullBloom || !P->Abilities->Equipped(WWOIds::Ability::Pollination)) return;
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = PollinatedAt.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() >= N.PollinationCooldown) It.RemoveCurrent();
	}
	if (PollinatedAt.Contains(Mate)) return;
	PollinatedAt.Add(Mate, Now);
	const float More = 1.f + UWWOLoadoutSubsystem::Bonus(P, "pollination_more");
	GiveEnergy(N.PollinationGrenadeEnergy * More, N.PollinationBloomEnergy * More);
	Light(WWOIds::Ability::Pollination);
}

void UWWODawnLotusComponent::OnDamageDealt(float Amount)
{
	AWWOCharacter* P = Me();
	if (!P || Amount <= 0.f || !P->Abilities->Equipped(WWOIds::Ability::Petalfall)) return;
	const float Need = FMath::Max(WWODawnLotus::T().PetalfallDamage * (1.f - FMath::Clamp(UWWOLoadoutSubsystem::Bonus(P, "petalfall_sooner"), 0.f, 0.9f)), 1.f);
	PetalDamage += Amount;
	if (PetalDamage < Need) return;
	PetalDamage = FMath::Fmod(PetalDamage, Need); // one burst per hit, however big
	Light(WWOIds::Ability::Petalfall);
	WWODawnLotus::PetalfallBurst(*P);
}

void UWWODawnLotusComponent::GiveEnergy(float GrenadeEnergy, float BloomEnergy)
{
	ApplyEnergy(GrenadeEnergy, BloomEnergy);
	if (Me()->IsHostCopyOfRemote()) ClientEnergy(GrenadeEnergy, BloomEnergy);
}

void UWWODawnLotusComponent::ApplyEnergy(float GrenadeEnergy, float BloomEnergy)
{
	const AWWOCharacter* P = Me();
	P->Grenades->Charge = FMath::Min(P->Grenades->Charge + GrenadeEnergy, P->Grenades->MaxCharges());
	P->Abilities->AddEnergy(BloomEnergy, WWOIds::Ability::FullBloom);
}

void UWWODawnLotusComponent::ClientEnergy_Implementation(float GrenadeEnergy, float BloomEnergy)
{
	ApplyEnergy(GrenadeEnergy, BloomEnergy);
}

void UWWODawnLotusComponent::GiveReveille(float AbilityEnergy, float HandlingSeconds)
{
	ApplyReveille(AbilityEnergy, HandlingSeconds);
	if (Me()->IsHostCopyOfRemote()) ClientReveille(AbilityEnergy, HandlingSeconds);
}

void UWWODawnLotusComponent::ApplyReveille(float AbilityEnergy, float HandlingSeconds)
{
	const AWWOCharacter* P = Me();
	P->Abilities->AddEnergy(AbilityEnergy);
	HandlingUntil = GetWorld()->GetTimeSeconds() + HandlingSeconds;
	if (P->IsLocallyControlled()) // the magazine is the machine's that fires the gun
	{
		P->Weapon->CancelReload();
		P->Weapon->Ammo = P->Weapon->MagazineSize();
	}
}

void UWWODawnLotusComponent::ClientReveille_Implementation(float AbilityEnergy, float HandlingSeconds)
{
	ApplyReveille(AbilityEnergy, HandlingSeconds);
}

void UWWODawnLotusComponent::MarkLit(FName AbilityId)
{
	Me()->Abilities->State(AbilityId).LitLeft = WWODawnLotus::T().DawnLitTime;
	if (AbilityId == WWOIds::Ability::Petalfall && Me()->IsLocalHuman())
	{
		PetalFlashAt = GetWorld()->GetTimeSeconds();
		UWWOSound::PlayFlat(this, "petalfall_chime");
		UWWOSound::PlayFlat(this, "petalfall_rise");
	}
}

void UWWODawnLotusComponent::Light(FName AbilityId)
{
	MarkLit(AbilityId);
	if (Me()->IsHostCopyOfRemote()) ClientLight(AbilityId);
}

void UWWODawnLotusComponent::ClientLight_Implementation(FName AbilityId)
{
	MarkLit(AbilityId);
}

void UWWODawnLotusComponent::NoteHealLanded(float Amount)
{
	if (Me()->IsHostCopyOfRemote()) ClientHealLanded(Amount);
	else if (Me()->IsLocalHuman()) ApplyHealLanded(Amount);
}

void UWWODawnLotusComponent::ClientHealLanded_Implementation(float Amount)
{
	ApplyHealLanded(Amount);
}

void UWWODawnLotusComponent::ApplyHealLanded(float Amount)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bFresh = Now - HealMarkAt > 0.7;
	HealMarkAmount = bFresh ? Amount : HealMarkAmount + Amount;
	if (Now - HealMarkAt > 0.25) UWWOSound::PlayFlat(this, "ability", 0.45f);
	HealMarkAt = Now;
}

void UWWODawnLotusComponent::MulticastHealPulse_Implementation(AActor* Mate)
{
	UWorld* World = GetWorld();
	if (!Mate || !World || World->GetNetMode() == NM_DedicatedServer) return;
	const FVector At = Mate->GetActorLocation();
	if (AWWOFXActor* FX = AWWOFXActor::Spawn(World, At, 0.65f))
	{
		FX->AttachToActor(Mate, FAttachmentTransformRules::KeepWorldTransform);
		UStaticMeshComponent* Glow = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), HealColor, 0.5f, 1.2f, true, false, 0.25f);
		FX->ScaleTween(Glow, FVector(0.9f, 0.9f, 1.5f), FVector(1.7f, 1.7f, 2.6f), 0.6f, EWWOEase::QuadOut);
		FX->ParamTween(WWOFX::MID(Glow), "Alpha", 0.5f, 0.f, 0.6f, EWWOEase::QuadOut);
		FX->LightTween(FX->AddLight(HealColor, 2500.f, 320.f), 2500.f, 0.f, 0.55f, EWWOEase::QuadOut);
	}
	AWWOParticles::Burst(World, At - FVector(0, 0, 40.f), FLinearColor(1.f, 0.86f, 0.45f), 16, 0.8f, 0.5f, 1.5f, 0.05f, FVector(0, 0, 1.6f));
	if (const AWWOCharacter* Healed = Cast<AWWOCharacter>(Mate); Healed && Healed->IsLocalHuman()) Healed->DawnLotus->HealedAt = World->GetTimeSeconds();
}

void UWWODawnLotusComponent::MulticastPetalBurst_Implementation(FVector_NetQuantize At, float RadiusM, FLinearColor Color)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	FVector Ground = At - FVector(0, 0, 90.f);
	WWOFX::GroundBelow(World, At, 3.f * WWO::M, Ground, GetOwner());
	AWWOFXActor::Shockwave(World, Ground, Color, RadiusM, 0.55f);
	AWWOFXActor::Shockwave(World, Ground + FVector(0, 0, 25.f), FLinearColor(1.f, 0.93f, 0.75f), RadiusM * 0.6f, 0.4f);
	if (AWWOFXActor* FX = AWWOFXActor::Spawn(World, At, 0.5f))
	{
		UStaticMeshComponent* Flash = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), Color, 0.6f, 1.f, true, false, 0.3f);
		const float Size = RadiusM * WWO::M / 50.f;
		FX->ScaleTween(Flash, FVector(Size * 0.1f), FVector(Size * 0.8f), 0.3f, EWWOEase::ExpoOut);
		FX->ParamTween(WWOFX::MID(Flash), "Alpha", 0.6f, 0.f, 0.45f, EWWOEase::QuadOut);
		FX->LightTween(FX->AddLight(Color, 14000.f, RadiusM * 2.f * WWO::M), 14000.f, 0.f, 0.45f, EWWOEase::QuadOut);
	}
	AWWOParticles::Burst(World, At, FLinearColor(1.f, 0.82f, 0.5f), 70, 1.1f, RadiusM * 0.5f, RadiusM * 1.3f, 0.08f, FVector(0, 0, -1.5f));
	UWWOSound::Play(this, "static_discharge_boom", At, 0.9f);
}

void UWWODawnLotusComponent::MulticastBeam_Implementation(FVector_NetQuantize From, FVector_NetQuantize To, FLinearColor Color, bool bHit)
{
	// A long stretched glow from the hands to exactly where it landed, thinning out, with a white core.
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	const FVector Line = FVector(To) - FVector(From);
	const float Half = FMath::Max(Line.Size() * 0.5f, 10.f) / 50.f;
	if (AWWOFXActor* FX = AWWOFXActor::Spawn(World, FVector(From) + Line * 0.5f, 0.6f))
	{
		FX->SetActorRotation(Line.Rotation());
		UStaticMeshComponent* Outer = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), Color, 0.65f, 1.f, true, false, 0.5f);
		UStaticMeshComponent* Core = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), FMath::Lerp(Color, FLinearColor::White, 0.75f), 1.f, 1.f, true, false, 0.8f);
		FX->ScaleTween(Outer, FVector(Half, 0.75f, 0.75f), FVector(Half, 0.08f, 0.08f), 0.5f, EWWOEase::QuadIn);
		FX->ScaleTween(Core, FVector(Half, 0.28f, 0.28f), FVector(Half, 0.03f, 0.03f), 0.45f, EWWOEase::QuadIn);
		FX->ParamTween(WWOFX::MID(Outer), "Alpha", 0.65f, 0.f, 0.55f, EWWOEase::QuadIn);
		FX->ParamTween(WWOFX::MID(Core), "Alpha", 1.f, 0.f, 0.5f, EWWOEase::QuadIn);
		// A solid bright rod down the middle: the glows above nearly vanish seen along the beam, from the ult's camera.
		UStaticMeshComponent* Rod = WWOFX::AddShape(FX, FX->GetRootComponent(), WWOFX::Cube(), TEXT("M_WWO_Solid"), FMath::Lerp(Color, FLinearColor::White, 0.55f), 9.f);
		Rod->SetCastShadow(false);
		const float Long = Line.Size() / 100.f;
		FX->ScaleTween(Rod, FVector(Long, 0.2f, 0.2f), FVector(Long, 0.01f, 0.01f), 0.5f, EWWOEase::QuadIn);
	}
	for (int32 i = 0; i < 3; ++i) AWWOLightningBolt::Spawn(World, From, To, FMath::Lerp(Color, FLinearColor::White, 0.3f), 0.3f + 0.08f * i);
	if (AWWOFXActor* Hit = AWWOFXActor::Spawn(World, To, 0.5f))
	{
		const float Size = bHit ? 2.6f : 1.4f;
		UStaticMeshComponent* Flash = WWOFX::AddSoftSphere(Hit, Hit->GetRootComponent(), FMath::Lerp(Color, FLinearColor::White, 0.4f), 0.9f, 1.f, true, false, 0.6f);
		Hit->ScaleTween(Flash, FVector(Size * 0.2f), FVector(Size), 0.2f, EWWOEase::ExpoOut);
		Hit->ParamTween(WWOFX::MID(Flash), "Alpha", 0.9f, 0.f, 0.45f, EWWOEase::QuadOut);
		Hit->LightTween(Hit->AddLight(Color, 22000.f, 900.f), 22000.f, 0.f, 0.45f, EWWOEase::QuadOut);
	}
	AWWOParticles::Burst(World, To, FLinearColor(1.f, 0.85f, 0.5f), bHit ? 46 : 20, 0.7f, 2.f, 7.f, 0.07f);
	UWWOSound::Play(this, bHit ? "static_discharge_boom" : "static_discharge", To, 1.f);
}

bool UWWODawnLotusComponent::IsHandlingUp() const
{
	return HandlingUntil > 0.0 && GetWorld() && GetWorld()->GetTimeSeconds() < HandlingUntil;
}

float UWWODawnLotusComponent::HandlingLeft() const
{
	return IsHandlingUp() ? (float)(HandlingUntil - GetWorld()->GetTimeSeconds()) : 0.f;
}

// --- The Bloom Pool --------------------------------------------------------------------------------

AWWOBloomPool::AWWOBloomPool()
	: Life(0.25f, 0.f, false) // grows in over 0.25 s, fades over the last 0.5 s of its duration
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bReplicates = true;
}

void AWWOBloomPool::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AWWOBloomPool, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOBloomPool, Duration, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AWWOBloomPool, Color, COND_InitialOnly);
}

AWWOBloomPool* AWWOBloomPool::Spawn(UWorld* World, const FVector& At, const FLinearColor& InColor, AActor* InThrower)
{
	FVector Floor = At; // a grenade can burst in the air: the pool lies on the ground under it
	if (FVector Ground; WWOFX::GroundBelow(World, At, 12.f * WWO::M, Ground, InThrower)) Floor = Ground;
	AWWOBloomPool* Pool = World->SpawnActorDeferred<AWWOBloomPool>(AWWOBloomPool::StaticClass(), FTransform(Floor), InThrower, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pool) return nullptr;
	Pool->Radius = WWODawnLotus::T().BloomPoolRadius;
	Pool->Duration = WWODawnLotus::T().BloomPoolTime;
	Pool->Color = InColor;
	Pool->Thrower = InThrower;
	Pool->FinishSpawning(FTransform(Floor)); // BeginPlay builds the looks
	return Pool;
}

void AWWOBloomPool::BeginPlay()
{
	Super::BeginPlay();
	const FVector At = GetActorLocation();
	Disc = WWOFX::AddGroundDisc(this, GetRootComponent(), Color, 0.1f, 0.55f, 0.8f);
	Disc->SetRelativeLocation(FVector(0, 0, 4.f));
	Mat = WWOFX::MID(Disc);
	Haze = WWOFX::AddSoftSphere(this, GetRootComponent(), Color, 0.22f, 1.1f, true, false, 0.1f);
	Haze->SetRelativeLocation(FVector(0, 0, 12.f));
	Glow = WWOFX::AddPointLight(this, GetRootComponent(), Color, 2200.f, Radius * 1.6f * WWO::M, FVector(0, 0, 60.f));
	if (AWWOParticles* B = AWWOParticles::Burst(GetWorld(), At, WWO::WithAlpha(WWO::Lightened(Color, 0.3f), 0.8f), FMath::RoundToInt(Radius * 10.f),
		1.2f, 0.3f, 0.9f, 0.08f, FVector(0, 0, 0.6f), EWWOParticleStyle::Glow, false))
	{
		B->bOneShot = false;
		B->Direction = FVector::UpVector;
		B->SpreadDegrees = 15.f;
		B->Shape = EWWOEmitShape::Ring;
		B->ShapeRadius = Radius * 0.9f;
		B->Start();
		Motes = B;
	}
}

void AWWOBloomPool::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Motes.IsValid()) Motes->StopAndFade();
	Super::EndPlay(Reason);
}

void AWWOBloomPool::Tick(float DeltaSeconds)
{
	WWO_PROF("BloomPool");
	Super::Tick(DeltaSeconds);
	if (Life.Advance(DeltaSeconds, Duration) == EWWOLifeStep::Over)
	{
		if (!FWWOEffectLife::End(*this)) SetActorHiddenInGame(true); // a joiner's copy hides until the host's goes
		return;
	}
	const float FadeOut = Life.LeftOf(Duration, 0.5f);
	const float Scale = Radius * WWO::M / 50.f * FMath::Lerp(0.2f, 1.f, Life.GrowIn()) * FadeOut;
	Disc->SetRelativeScale3D(FVector(Scale, Scale, 1.f));
	if (Mat) Mat->SetScalarParameterValue("Alpha", FadeOut);
	Haze->SetRelativeScale3D(FVector(Scale * 0.95f, Scale * 0.95f, Scale * 0.22f));
	if (UMaterialInstanceDynamic* HazeMat = WWOFX::MID(Haze)) HazeMat->SetScalarParameterValue("Alpha", 0.22f * FadeOut);
	if (Glow) Glow->SetIntensity(2200.f * FadeOut);
	if (FadeOut < 1.f && Motes.IsValid()) Motes->bEmitting = false;
	if (!HasAuthority()) return;
	TickLeft -= DeltaSeconds;
	if (TickLeft > 0.f) return;
	TickLeft += PoolTickSeconds;
	AWWOCharacter* By = Cast<AWWOCharacter>(Thrower.Get());
	if (!By) return;
	const float Amount = WWODawnLotus::T().BloomPoolHealPerSecond * PoolTickSeconds;
	for (AActor* Mate : MatesAround(*By, GetActorLocation(), Radius)) WWODawnLotus::HealTeammate(*By, Mate, Amount, EWWOHealSource::BloomPool);
}
