// The ability table (see Player/WWOAbilities.h).

#include "Player/WWOAbilities.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOCharacter.h"
#include "Weapons/WWOGrenade.h"
#include "Flora/WWOSporeFog.h"
#include "Flora/WWODawnLotus.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOIds.h"
#include "WWOTypes.h"

namespace
{
	void SporeDash(UWWOAbilityComponent& K, const UWWOAbilityData& A)
	{
		// Launch backwards (and a little up, to keep the speed through the air) and leave a
		// thick fog where you were standing.
		AWWOCharacter* P = Cast<AWWOCharacter>(K.GetOwner());
		const FVector At = P->GetActorLocation();
		if (P->HasAuthority()) AWWOSporeFog::SpawnFog(K.GetWorld(), At, A.DashFogRadius, A.DashFogTime, true, K.GetOwner()); // online it replicates
		AWWOParticles::Burst(K.GetWorld(), At, WWO::WithAlpha(WWO::Lightened(AWWOSporeFog::Color(), 0.15f), 0.5f), 18, 1.f, 1.5f, 4.f, 0.9f,
			FVector(0, 0, 0.3f), EWWOParticleStyle::Smoke);
		AWWOParticles::Burst(K.GetWorld(), At, WWO::Lightened(AWWOSporeFog::Color(), 0.4f), 24, 0.7f, 2.f, 5.f, 0.06f, FVector(0, 0, -2.f));
		if (!P->IsLocallyControlled()) return; // the launch is the dashing player's own move
		FVector Back = -P->GetActorForwardVector();
		Back.Z = 0.f;
		P->Launch((Back.GetSafeNormal() * A.DashSpeed + FVector(0, 0, A.DashUp)) * WWO::M);
	}

	void RootBombs(UWWOAbilityComponent& K, const UWWOAbilityData& A)
	{
		// Two big bombs, thrown a little left and right of where you're looking, that blow up the
		// moment they hit something.
		AWWOCharacter* P = Cast<AWWOCharacter>(K.GetOwner());
		if (!P->HasAuthority()) return; // the host throws a joiner's bombs (they replicate)
		const float Bigger = 1.f + UWWOLoadoutSubsystem::Bonus(&K, "blast_bigger");
		// Where the thrower is looking: its own aim here, or for a joiner (whose copy here has no camera
		// pitch) the direction it sent with the ability.
		const FVector Aim = P->IsLocallyControlled() ? P->AimForward() : P->NetAim;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Aim).GetSafeNormal(UE_SMALL_NUMBER, P->GetActorRightVector());
		TArray<AWWOGrenade*> Thrown;
		for (const float Side : { -1.f, 1.f })
		{
			FWWOGrenadeSpec Spec;
			Spec.Color = A.Color;
			Spec.Damage = A.BombDamage;
			Spec.BlastRadius = A.BombRadius * Bigger;
			Spec.Fuse = 6.f; // backup, in case it never hits anything
			Spec.SizeScale = 2.f;
			Spec.bExplodeOnImpact = true;
			Spec.Flavor = A.Flora;
			Spec.ThrownFrom = P->GetActorLocation();
			const FVector At = P->EyeLocation() + Aim * 60.f + Right * 35.f * Side - FVector(0, 0, 20.f);
			FVector Dir = Aim.RotateAngleAxis(1.5f * Side, FVector::UpVector);
			Dir = Dir.RotateAngleAxis(-2.f, Right); // a little upward
			AWWOGrenade* Bomb = AWWOGrenade::Throw(K.GetWorld(), Spec, At, Dir.GetSafeNormal() * A.BombSpeed * WWO::M, P);
			if (!Bomb) continue;
			for (AWWOGrenade* Other : Thrown) Bomb->IgnoreActor(Other); // the pair never hit each other
			Thrown.Add(Bomb);
		}
	}
}

TConstArrayView<FWWOAbility> WWOAbility::Table()
{
	// Built on first use (FNames). VoltVine, VeilSpore, IronBark, then Seedburst (no flora's).
	// Thornfist comes before Static Charge: the fists show Thornfist's colour when both are on.
	static const FWWOAbility Abilities[] = {
		{ // Overgrowth (passive): faster, and an extra jump.
			.Id = WWOIds::Ability::Overgrowth,
			.MoveSpeed = [](const UWWOAbilityData& A) { return A.SpeedMult; },
			.ExtraJumps = [](const UWWOAbilityData& A) { return FMath::RoundToInt(A.ExtraJumps); },
		},
		{ // Thornfist (active): harder punches for a while, with an electric discharge on each (WWOWeaponMelee.cpp).
			.Id = WWOIds::Ability::Thornfist,
			.bFistsGlow = true,
			.MeleeDealt = [](const UWWOAbilityData& A, const FWWOAbilityState& S) { return S.ActiveLeft > 0.f ? A.MeleeDamageMult : 1.f; },
		},
		{ // Melee Guard (triggered): landing a melee cuts the melee damage you take for a while.
			.Id = WWOIds::Ability::MeleeGuard,
			.Tick = [](FWWOAbilityState& S, float DeltaSeconds) { S.LitLeft = FMath::Max(S.LitLeft - DeltaSeconds, 0.f); },
			.Lit = [](const FWWOAbilityState& S) { return S.LitLeft > 0.f; },
			.MeleeTaken = [](const UWWOAbilityData& A, const FWWOAbilityState& S) { return S.LitLeft > 0.f ? 1.f - A.GuardReduction : 1.f; },
			.OnMeleeLanded = [](const UWWOAbilityData& A, FWWOAbilityState& S) { S.LitLeft = A.Duration; },
		},
		{ // Static Charge (charged): filled by damage taken; armed, the next melee hit chains lightning
		  // (UWWOAbilityComponent::ConsumeStaticCharge, WWOWeaponMelee.cpp).
			.Id = WWOIds::Ability::StaticCharge,
			.bFistsGlow = true,
			.OnDamageTaken = [](const UWWOAbilityData& A, FWWOAbilityState& S, float Amount)
			{
				if (S.bArmed) return; // not while it's armed and waiting
				S.Charge = FMath::Min(ChargeOf(A, &S) + Amount / FMath::Max(A.ChargeDamage, 1.f), 1.f);
			},
		},
		{ // Spore Fog Damage (passive): spore fog hurts the other side (AWWOSporeFog::Tick).
			.Id = WWOIds::Ability::SporeFogDamage,
		},
		{ // Spore Dash (active): launch backwards, leaving a thick fog that blocks sight (the host lets the
		  // launch through: UWWOAbilityComponent::CanLaunchNow).
			.Id = WWOIds::Ability::SporeDash,
			.OnActivate = SporeDash,
		},
		{ // Spore Patches (triggered): your grenade sprouts fog patches where it touches down (AWWOGrenade::OnBounce).
			.Id = WWOIds::Ability::SporePatches,
			.Lit = [](const FWWOAbilityState& S) { return S.Live > 0; },
		},
		{ // Root Bombs (active): two bombs that go off on impact.
			.Id = WWOIds::Ability::RootBombs,
			.OnActivate = RootBombs,
		},
		{ // Shield Share (passive): your blasts refill your teammates' shields (AWWOGrenade::ShareShields).
			.Id = WWOIds::Ability::ShieldShare,
		},
		{ // Extra Grenade (passive): another grenade charge.
			.Id = WWOIds::Ability::ExtraGrenade,
			.GrenadeCharges = [](const UWWOAbilityData& A) { return A.ExtraCharges; },
		},
		{ // Seedburst (triggered): your grenade bursts into seeds (AWWOGrenade::Burst).
			.Id = WWOIds::Ability::Seedburst,
			.Lit = [](const FWWOAbilityState& S) { return S.Live > 0; },
		},
		{ // Pollination (triggered): healing a teammate gives grenade and Full Bloom energy (UWWODawnLotusComponent::OnHealed).
			.Id = WWOIds::Ability::Pollination,
			.Tick = [](FWWOAbilityState& S, float DeltaSeconds) { S.LitLeft = FMath::Max(S.LitLeft - DeltaSeconds, 0.f); },
			.Lit = [](const FWWOAbilityState& S) { return S.LitLeft > 0.f; },
		},
		{ // Full Bloom (active): a fixed pool of healing split among teammates in a circle (the host's).
			.Id = WWOIds::Ability::FullBloom,
			.OnActivate = [](UWWOAbilityComponent& K, const UWWOAbilityData&)
			{
				AWWOCharacter* P = Cast<AWWOCharacter>(K.GetOwner());
				if (P && P->HasAuthority()) WWODawnLotus::FullBloom(*P);
			},
		},
		{ // Petalfall (triggered): dealing enough damage releases a healing burst (UWWODawnLotusComponent::OnDamageDealt).
			.Id = WWOIds::Ability::Petalfall,
			.Tick = [](FWWOAbilityState& S, float DeltaSeconds) { S.LitLeft = FMath::Max(S.LitLeft - DeltaSeconds, 0.f); },
			.Lit = [](const FWWOAbilityState& S) { return S.LitLeft > 0.f; },
		},
	};
	return Abilities;
}

const FWWOAbility* WWOAbility::Find(FName Id)
{
	for (const FWWOAbility& E : Table()) if (E.Id == Id) return &E;
	return nullptr;
}

float WWOAbility::StartCharge(const UWWOAbilityData& A)
{
	return A.Kind == EWWOAbilityKind::Charged ? 0.f : 1.f;
}

float WWOAbility::ChargeOf(const UWWOAbilityData& A, const FWWOAbilityState* S)
{
	return S && S->Charge.IsSet() ? S->Charge.GetValue() : StartCharge(A);
}
