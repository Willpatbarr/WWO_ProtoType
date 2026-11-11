#include "Player/WWOBotController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOGrenadeComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Player/WWOUltComponent.h"
#include "NavigationSystem.h"
#include "Data/WWOData.h"
#include "Enemies/WWOEnemy.h" // WWONav
#include "Flora/WWOSporeFog.h"
#include "Game/WWOGameMode.h"
#include "Systems/WWOWorldSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "Engine/World.h"

int32 AWWOBotController::Sightings = 0;
int32 AWWOBotController::PathsFound = 0;
int32 AWWOBotController::PathsFailed = 0;
int32 AWWOBotController::ShotsFired = 0;
int32 AWWOBotController::StuckJumps = 0;
int32 AWWOBotController::Regoals = 0;
int32 AWWOBotController::Melees = 0;
int32 AWWOBotController::Grenades = 0;
int32 AWWOBotController::AbilitiesUsed = 0;
int32 AWWOBotController::Ults = 0;
int32 AWWOBotController::Retreats = 0;
int32 AWWOBotController::NoCover = 0;
int32 AWWOBotController::Swaps = 0;
int32 AWWOBotController::StuckIn[4] = {};
int32 AWWOBotController::Callouts = 0;

namespace
{
	constexpr float NearSense = 4.f;     // m: anything this close is noticed whichever way the bot faces
	constexpr float TurnGain = 14.f;     // 1/s: how hard the view closes on where it wants to look
	constexpr float PathPointReach = 90.f; // cm: a path point this close counts as reached

	// The host's bot settings (Settings -> Bots).
	struct FBotTuning
	{
		float Reaction = 0.35f, TurnSpeed = 260.f, AimError = 1.6f, ViewAngle = 120.f, SightRange = 60.f, Memory = 6.f, Aggression = 0.5f, KitUse = 1.f, StickToPlayer = 0.5f, CalloutRange = 40.f;
	};

	FBotTuning TuningFor(const UObject* Context)
	{
		FBotTuning T;
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		if (const AWWOGameMode* Mode = World ? World->GetAuthGameMode<AWWOGameMode>() : nullptr)
		{
			T.Reaction = FMath::Max(Mode->BotReaction, 0.f);
			T.TurnSpeed = FMath::Max(Mode->BotTurnSpeed, 20.f);
			T.AimError = FMath::Max(Mode->BotAimError, 0.f);
			T.ViewAngle = FMath::Clamp(Mode->BotViewAngle, 10.f, 360.f);
			T.SightRange = FMath::Max(Mode->BotSightRange, 1.f);
			T.Memory = FMath::Max(Mode->BotMemory, 0.f);
			T.Aggression = FMath::Clamp(Mode->BotAggression, 0.f, 1.f);
			T.KitUse = FMath::Clamp(Mode->BotKitUse, 0.f, 1.f);
			T.StickToPlayer = FMath::Clamp(Mode->BotStickToPlayer, 0.f, 1.f);
			T.CalloutRange = FMath::Max(Mode->BotCalloutRange, 0.f);
		}
		return T;
	}

	FVector Flat(FVector V)
	{
		V.Z = 0.f;
		return V;
	}

	FVector ChestOf(const AWWOCharacter* P)
	{
		const TArray<FWWOAimPart> Parts = P->GetAimParts();
		return Parts.Num() > 0 ? Parts.Last().Position : P->GetActorLocation();
	}
}

AWWOBotController::AWWOBotController()
{
	bWantsPlayerState = true; // a team and a score like any player
	PrimaryActorTick.bCanEverTick = true;
	// Bots don't all think on the same frame.
	SenseLeft = FMath::FRandRange(0.f, 0.15f);
	PathAge = FMath::FRandRange(99.f, 100.f);
	StrafeDir = FMath::RandBool() ? 1.f : -1.f;
}

void AWWOBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	AWWOCharacter* Me = Cast<AWWOCharacter>(InPawn);
	if (Me && HurtBound.Get() != InPawn)
	{
		HurtBound = InPawn;
		Me->Health->OnDamaged.AddWeakLambda(this, [this](float Amount) { OnHurt(Amount); });
	}
	if (Me) GiveKit(Me);
}

// --- Its kit -------------------------------------------------------------------------------------

void AWWOBotController::GiveKit(AWWOCharacter* Me)
{
	// Its own loadout, kept on the host like a joiner's (UWWOLoadoutSubsystem::Get finds it for
	// anything this character owns): a random flora, one of its grenades, two of its abilities and
	// two guns. Picked once; a respawn keeps it.
	if (!Me->HasAuthority()) return;
	const UWWOLoadoutSubsystem* Base = UWWOLoadoutSubsystem::Get(GetWorld());
	if (!bHasKit && Base)
	{
		auto Shuffled = [](TArray<FName> Ids)
		{
			for (int32 i = Ids.Num() - 1; i > 0; --i) Ids.Swap(i, FMath::RandRange(0, i));
			return Ids;
		};
		TArray<FName> Floras, Nades, Powers, Weapons;
		for (const UWWOFloraData* F : Base->Floras) if (F && F->bUnlocked) Floras.Add(F->Id);
		if (Floras.Num() == 0) return;
		Kit.Flora = Floras[FMath::RandRange(0, Floras.Num() - 1)];
		for (const UWWOGrenadeData* G : Base->Grenades) if (G && G->bUnlocked && G->Flora == Kit.Flora) Nades.Add(G->Id);
		for (const UWWOAbilityData* A : Base->Abilities) if (A && A->bUnlocked && A->Flora == Kit.Flora) Powers.Add(A->Id);
		for (const UWWOWeaponData* W : Base->Weapons) if (W && W->bUnlocked) Weapons.Add(W->Id);
		Kit.Grenade = Nades.Num() > 0 ? Nades[FMath::RandRange(0, Nades.Num() - 1)] : NAME_None;
		Powers = Shuffled(Powers);
		Powers.SetNum(FMath::Min(Powers.Num(), UWWOLoadoutSubsystem::AbilitySlots));
		Kit.Abilities = Powers;
		Guns = Shuffled(Weapons);
		Guns.SetNum(FMath::Min(Guns.Num(), 2));
		Kit.Weapon = Guns.Num() > 0 ? Guns[0] : NAME_None;
		bHasKit = true;
	}
	if (!bHasKit) return;
	Me->ServerSetLoadout_Implementation(Kit);
	Me->ServerSetFlora_Implementation(Kit.Flora); // its character model
	if (Me->HasActorBegunPlay()) Me->Weapon->RebindLoadout(); // its gun follows its own loadout from here on
}

void AWWOBotController::PickGun(AWWOCharacter* Me, float Dist)
{
	// Swap to its other gun when that one suits this distance clearly better.
	UWWOLoadoutSubsystem* Loadout = Me->NetLoadout;
	if (!Loadout || Guns.Num() < 2 || Clock < NextSwap || Me->Weapon->IsSwapping() || Me->Weapon->bMeleeing) return;
	auto Fit = [Dist](const UWWOWeaponData* W)
	{
		if (!W) return 0.f;
		if (W->Pellets > 1.5f) return Dist < 9.f ? 2.f : (Dist < 13.f ? 0.5f : 0.f);  // shotgun
		if (W->Zoom >= 4.f) return Dist > 25.f ? 2.f : (Dist > 14.f ? 1.f : 0.2f);     // sniper
		return Dist <= W->FireRange * 0.9f ? 1.f : 0.f;
	};
	UWWOWeaponData* Other = nullptr;
	for (UWWOWeaponData* W : Loadout->Weapons) if (W && Guns.Contains(W->Id) && W != Loadout->GetWeapon()) Other = W;
	if (!Other || Fit(Other) < Fit(Loadout->GetWeapon()) + 0.7f) return;
	NextSwap = Clock + 5.f;
	Loadout->EquipWeapon(Other);
	++Swaps;
}

bool AWWOBotController::ThrowDirection(const AWWOCharacter* Me, const FVector& At, FVector& OutDirection) const
{
	// Where to look so the grenade lands on At. It leaves the hand LaunchAngle above the aim, at
	// the speed that carries it the throw distance on flat ground (UWWOGrenadeComponent::ThrowFrom).
	const FVector Eye = Me->EyeLocation();
	const float Gravity = 9.8f * WWO::M;
	const float Speed2 = Me->Grenades->EffectiveThrowDistance() * WWO::M * Gravity / FMath::Sin(FMath::DegreesToRadians(2.f * UWWOGrenadeComponent::LaunchAngle));
	const float X = FVector::Dist2D(At, Eye), Y = At.Z + 30.f - Eye.Z;
	const float Inside = Speed2 * Speed2 - Gravity * (Gravity * X * X + 2.f * Y * Speed2);
	if (X < 1.f * WWO::M || Inside < 0.f) return false; // too close, or out of reach
	const float Launch = FMath::RadiansToDegrees(FMath::Atan((Speed2 - FMath::Sqrt(Inside)) / (Gravity * X))); // the flat arc
	// Nothing in the way (the arc is nearly a straight line).
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOBotThrow), false, Me);
	if (Foe.IsValid()) Params.AddIgnoredActor(Foe.Get());
	if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, At + FVector(0, 0, 60.f), ECC_Visibility, Params)) return false;
	OutDirection = FRotator(Launch - UWWOGrenadeComponent::LaunchAngle, Flat(At - Eye).Rotation().Yaw, 0.f).Vector();
	return true;
}

void AWWOBotController::UseKit(AWWOCharacter* Me, AWWOCharacter* Target, float Dist, float OffDeg, bool bInRange, float HealthLeft)
{
	// In a fight: the punch, abilities, ult and grenade, each when it fits the moment.
	const FBotTuning T = TuningFor(this);
	UWWOWeaponComponent* Gun = Me->Weapon;
	const UWWOLoadoutSubsystem* Loadout = Me->GetLoadout();
	if (!Loadout || bThrowing) return;
	const bool bGunDown = Gun->bReloading || Gun->Ammo <= 0;

	// Punch: up close; from the edge of the lunge when the gun's down or one hit finishes them.
	const float Reach = Gun->LungeRange * 0.9f;
	if (!Gun->bMeleeing && Clock >= NextMelee && Dist <= Reach && OffDeg < 15.f
		&& (Dist < 2.6f || bGunDown || Target->GetHealth() + Target->GetShield() <= Gun->MeleeDamage))
	{
		NextMelee = Clock + FMath::FRandRange(0.9f, 1.3f);
		Press(EWWOAction::Melee);
		++Melees;
		return;
	}

	if (Clock >= NextAbility)
	{
		for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
		{
			const UWWOAbilityData* A = Loadout->GetAbility(Slot);
			if (!A || Me->Abilities->Charge(A) < 1.f || Me->Abilities->IsEffectActive(A)) continue;
			bool bUse = false;
			if (A->Id == "thornfist") bUse = Dist < 6.f;                              // a harder punch is coming
			else if (A->Id == "static_charge") bUse = Dist < 8.f;                     // armed for the next punch
			else if (A->Id == "spore_dash") bUse = HealthLeft < 0.5f && OffDeg < 30.f; // back out of trouble, behind fog
			else if (A->Id == "root_bombs") bUse = Dist > 5.f && Dist < 30.f && OffDeg < 4.f;
			else bUse = A->Kind == EWWOAbilityKind::Active && bInRange;
			if (!bUse || (A->Kind != EWWOAbilityKind::Active && A->Kind != EWWOAbilityKind::Charged)) continue;
			NextAbility = Clock + 1.f;
			if (FMath::FRand() > T.KitUse) break;
			Press(Slot == 0 ? EWWOAction::Ability1 : EWWOAction::Ability2);
			++AbilitiesUsed;
			return;
		}
	}

	if (Clock >= NextUlt && Me->Ult->CanActivate() && Loadout->GetFlora())
	{
		bool bUse = false;
		switch (Loadout->GetFlora()->Ult)
		{
		case EWWOUltKind::Veil: bUse = HealthLeft < 0.5f; break;                 // vanish when it's going badly
		case EWWOUltKind::Entrench: bUse = HealthLeft < 0.5f && bInRange; break; // can't be hurt, can still shoot
		default: bUse = Dist < FMath::Min(Me->Ult->BlastRadius * 0.6f, 8.f); break; // the blast is centered on it
		}
		if (bUse)
		{
			NextUlt = Clock + 2.f;
			if (FMath::FRand() <= T.KitUse)
			{
				Press(EWWOAction::Ult);
				++Ults;
				return;
			}
		}
	}

	// Grenade: at an enemy it has had in its sights a moment. A wall grenade goes between them
	// when it's hurt.
	const UWWOGrenadeData* G = Loadout->GetGrenade();
	if (G && Clock >= NextGrenade && Me->Grenades->IsReady() && !Gun->bMeleeing)
	{
		NextGrenade = Clock + FMath::FRandRange(1.5f, 3.f);
		const bool bWall = G->Kind == EWWOGrenadeKind::Wall;
		const bool bWant = bWall ? HealthLeft < 0.5f && Dist > 6.f : Dist >= 7.f && OnTarget > 0.6f;
		const FVector At = bWall ? Me->FeetLocation() + Flat(FoeFeet - Me->FeetLocation()).GetSafeNormal() * FMath::Min(Dist * 0.5f, 6.f) * WWO::M
			: FoeFeet + Flat(FoeVelocity) * 0.4f;
		FVector Dir;
		if (bWant && FMath::FRand() < 0.6f * T.KitUse && ThrowDirection(Me, At, Dir))
		{
			bThrowing = true;
			bThrown = false;
			ThrowAt = At;
			ThrowLeft = 0.9f;
		}
	}
}

bool AWWOBotController::FindCover(const AWWOCharacter* Me, FVector& OutSpot) const
{
	// The nearest place to stand, not toward the enemy, where they can't see its head or chest.
	const UWorld* World = GetWorld();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav) return false;
	const FVector Feet = Me->FeetLocation();
	const FVector ToFoe = Flat(FoeFeet - Feet).GetSafeNormal();
	const FVector FoeEye = FoeSpot + FVector(0, 0, 30.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOBotCover), false, Me);
	if (Foe.IsValid()) Params.AddIgnoredActor(Foe.Get());
	float Best = TNumericLimits<float>::Max();
	for (int32 i = 0; i < 20; ++i)
	{
		const FVector Dir = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector();
		if (FVector::DotProduct(Dir, ToFoe) > 0.4f) continue;
		const float Away = FMath::FRandRange(3.f, 13.f) * WWO::M;
		FNavLocation Spot;
		if (!Nav->ProjectPointToNavigation(Feet + Dir * Away, Spot, FVector(100.f, 100.f, 250.f))) continue;
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Spot.Location + FVector(0, 0, 165.f), FoeEye, ECC_Visibility, Params)) continue;
		if (!World->LineTraceSingleByChannel(Hit, Spot.Location + FVector(0, 0, 110.f), FoeEye, ECC_Visibility, Params)) continue;
		const float Score = FVector::Dist(Spot.Location, Feet);
		if (Score < Best)
		{
			Best = Score;
			OutSpot = Spot.Location;
		}
	}
	return Best < TNumericLimits<float>::Max();
}

void AWWOBotController::OnHurt(float Amount)
{
	// Shot by someone it can't see: now it knows where they are (after its reaction time).
	AWWOCharacter* Me = Cast<AWWOCharacter>(GetPawn());
	AWWOCharacter* Attacker = Cast<AWWOCharacter>(UWWOWorldSubsystem::CurrentAttacker.Get());
	if (!Me || !Attacker || Attacker == Me || Attacker->Health->bDead || !UWWOWorldSubsystem::CanHurt(Me, Attacker)) return;
	if (Foe.IsValid() && bFoeVisible) return; // busy with the one in front of it
	const FBotTuning T = TuningFor(this);
	if (Foe.Get() != Attacker)
	{
		bAware = false;
		ReactLeft = T.Reaction * FMath::FRandRange(0.8f, 1.3f);
	}
	Foe = Attacker;
	bFoeVisible = false;
	bSpotInSight = true; // look that way; Sense corrects it
	FoeSpot = ChestOf(Attacker);
	FoeFeet = Attacker->FeetLocation();
	FoeVelocity = FVector::ZeroVector;
	FoeSeenAgo = 0.f;
}

// --- Its team ------------------------------------------------------------------------------------

void AWWOBotController::PickLeader(const AWWOCharacter* Me)
{
	// Who it sticks with while nothing's going on. The first few bots of a team (how many:
	// StickToPlayer) escort the nearest living human on it; the rest pair up, every second one
	// following the one before it. Leaders (and anyone whose leader is dead) roam by themselves.
	Leader = nullptr;
	const FBotTuning T = TuningFor(this);
	const int32 Team = UWWOWorldSubsystem::TeamOf(Me);
	AWWOCharacter* Human = nullptr;
	bool bAnyHuman = false;
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(this)->GetPlayers())
	{
		if (!P || P->GetBot() || UWWOWorldSubsystem::TeamOf(P) != Team) continue;
		bAnyHuman = true;
		if (P->Health->bDead) continue;
		if (!Human || FVector::DistSquared(P->GetActorLocation(), Me->GetActorLocation()) < FVector::DistSquared(Human->GetActorLocation(), Me->GetActorLocation())) Human = P;
	}
	TArray<AWWOBotController*> Mates; // this team's bots, in a fixed order
	for (TActorIterator<AWWOBotController> It(GetWorld()); It; ++It)
	{
		if (It->GetPawn() && UWWOWorldSubsystem::TeamOf(It->GetPawn()) == Team) Mates.Add(*It);
	}
	Mates.Sort([](const AWWOBotController& A, const AWWOBotController& B) { return A.GetUniqueID() < B.GetUniqueID(); });
	const int32 Index = Mates.IndexOfByKey(this);
	const int32 Escorts = bAnyHuman ? FMath::RoundToInt(Mates.Num() * T.StickToPlayer) : 0;
	FlankSide = Index % 2 == 0 ? 1.f : -1.f;
	if (Index < 0) return;
	if (Index < Escorts) Leader = Human;
	else if ((Index - Escorts) % 2 == 1)
	{
		AWWOCharacter* Ahead = Cast<AWWOCharacter>(Mates[Index - 1]->GetPawn());
		if (Ahead && !Ahead->Health->bDead) Leader = Ahead;
	}
}

void AWWOBotController::CallOut(const AWWOCharacter* Me, AWWOCharacter* Enemy)
{
	// Tell teammates in earshot where the enemy it sees is.
	const float Range = TuningFor(this).CalloutRange * WWO::M;
	const int32 Team = UWWOWorldSubsystem::TeamOf(Me);
	for (TActorIterator<AWWOBotController> It(GetWorld()); It; ++It)
	{
		const AWWOCharacter* Mate = Cast<AWWOCharacter>(It->GetPawn());
		if (*It == this || !Mate || Mate->Health->bDead || UWWOWorldSubsystem::TeamOf(Mate) != Team) continue;
		if (FVector::Dist(Mate->GetActorLocation(), Me->GetActorLocation()) <= Range) It->HearCallout(Me, Enemy);
	}
}

void AWWOBotController::HearCallout(const AWWOCharacter* From, AWWOCharacter* Enemy)
{
	// A teammate has eyes on an enemy. With nothing of its own to deal with, it goes to help
	// (after a moment to take it in); if it's already hunting that enemy, it now knows where.
	if (!Enemy || Enemy->Health->bDead || bRetreating || (Foe.IsValid() && (Foe.Get() != Enemy || bFoeVisible))) return;
	if (!Foe.IsValid())
	{
		bAware = false;
		ReactLeft = TuningFor(this).Reaction * FMath::FRandRange(1.2f, 2.f);
		++Callouts;
	}
	Foe = Enemy;
	bFoeVisible = false;
	FoeSpot = ChestOf(Enemy);
	FoeFeet = Enemy->FeetLocation();
	FoeVelocity = FVector::ZeroVector;
	FoeSeenAgo = 0.f;
}

void AWWOBotController::ListenToHumans(const AWWOCharacter* Me)
{
	// A human teammate nearby who is being shot, or (the host) shooting at someone: same as a callout.
	const float Range = TuningFor(this).CalloutRange * WWO::M;
	const int32 Team = UWWOWorldSubsystem::TeamOf(Me);
	for (AWWOCharacter* P : UWWOWorldSubsystem::Get(this)->GetPlayers())
	{
		if (!P || P->GetBot() || P->Health->bDead || UWWOWorldSubsystem::TeamOf(P) != Team) continue;
		if (FVector::Dist(P->GetActorLocation(), Me->GetActorLocation()) > Range) continue;
		AWWOCharacter* Enemy = P->Health->SinceDamage < 1.f ? Cast<AWWOCharacter>(P->Health->LastAttacker.Get()) : nullptr;
		if (!Enemy && (P->MoveIn.Held & MoveFire)) Enemy = Cast<AWWOCharacter>(P->Weapon->ReticleTarget.Get());
		if (Enemy && UWWOWorldSubsystem::CanHurt(Me, Enemy)) HearCallout(P, Enemy);
	}
}

// --- Seeing ------------------------------------------------------------------------------------

bool AWWOBotController::CanSee(const AWWOCharacter* Me, const AWWOCharacter* Other, float RangeCm, float ConeCos) const
{
	if (Other->IsInvisible()) return false; // Veil
	const FVector Eye = Me->EyeLocation();
	const FVector To = Other->EyeLocation() - Eye;
	const float Dist = To.Size();
	if (Dist > RangeCm) return false;
	if (Dist > NearSense * WWO::M && FVector::DotProduct(Me->AimForward(), To.GetSafeNormal()) < ConeCos) return false;
	const UWorld* World = GetWorld();
	if (AWWOSporeFog::Blocks(World, Eye, Other->EyeLocation())) return false;
	// A clear line to the head or the chest.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOBotSight), false, Me);
	for (const FVector& Point : { Other->EyeLocation(), ChestOf(Other) })
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params) || Hit.GetActor() == Other) return true;
	}
	return false;
}

void AWWOBotController::Sense(AWWOCharacter* Me)
{
	const FBotTuning T = TuningFor(this);
	const float Range = T.SightRange * WWO::M;
	const float ConeCos = FMath::Cos(FMath::DegreesToRadians(T.ViewAngle * 0.5f));
	AWWOCharacter* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	// Who its teammates are on (focus fire).
	const int32 Team = UWWOWorldSubsystem::TeamOf(Me);
	TMap<const AWWOCharacter*, int32> Focused;
	for (TActorIterator<AWWOBotController> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->GetFoe() && It->GetPawn() && UWWOWorldSubsystem::TeamOf(It->GetPawn()) == Team) ++Focused.FindOrAdd(It->GetFoe());
	}
	for (AActor* A : UWWOWorldSubsystem::Get(this)->LivingTargets(Me))
	{
		AWWOCharacter* Other = Cast<AWWOCharacter>(A);
		if (!Other || !CanSee(Me, Other, Range, ConeCos)) continue;
		// The nearest, leaning toward the one its teammates are shooting and the weaker one; and it
		// doesn't drop the one it's on for someone only a little better.
		const float Left = (Other->GetHealth() + Other->GetShield()) / FMath::Max(Other->GetMaxHealth() + Other->GetMaxShield(), 1.f);
		const float Score = FVector::Dist(Other->GetActorLocation(), Me->GetActorLocation()) * (Other == Foe.Get() ? 0.7f : 1.f)
			* (1.f - 0.15f * FMath::Min(Focused.FindRef(Other), 2)) * (0.7f + 0.3f * Left);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}
	if (Best)
	{
		if (Best != Foe.Get())
		{
			// Someone new: it takes a moment to register.
			bAware = false;
			ReactLeft = T.Reaction * FMath::FRandRange(0.8f, 1.3f);
			++Sightings;
		}
		else if (!bFoeVisible)
		{
			// The one it was hunting came back into view: it was expecting them.
			ReactLeft = FMath::Max(ReactLeft, T.Reaction * FMath::FRandRange(0.3f, 0.5f));
			++Sightings;
		}
		const bool bNews = Best != Foe.Get() || !bFoeVisible;
		Foe = Best;
		bFoeVisible = true;
		// Call it out: at first sight, then every second or so while it's still in view.
		if (bNews || Clock >= CalloutLeft)
		{
			CalloutLeft = Clock + FMath::FRandRange(0.8f, 1.2f);
			CallOut(Me, Best);
		}
	}
	else
	{
		ListenToHumans(Me);
		bFoeVisible = false;
		bSpotInSight = false;
		if (Foe.IsValid())
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOBotSpot), false, Me);
			Params.AddIgnoredActor(Foe.Get());
			bSpotInSight = !GetWorld()->LineTraceSingleByChannel(Hit, Me->EyeLocation(), FoeSpot, ECC_Visibility, Params);
		}
	}
	// Strafing: not into a wall or off a ledge.
	const FVector Right = FRotationMatrix(FRotator(0.f, Me->GetActorRotation().Yaw, 0.f)).GetUnitAxis(EAxis::Y);
	if (State == EState::Fight && !SideIsClear(Me, Right * StrafeDir)) StrafeDir = -StrafeDir;
}

// --- Looking and moving --------------------------------------------------------------------------

void AWWOBotController::TurnView(AWWOCharacter* Me, const FVector& Direction, float MaxDegPerSecond, float DeltaSeconds, FVector2D OffsetDeg)
{
	// Turn toward a direction like a hand on a stick: fast when far off, easing in, never faster
	// than MaxDegPerSecond. Recoil kicks the view; this pulls it back at the same limited speed.
	const FVector D = Direction.GetSafeNormal();
	if (D.IsNearlyZero()) return;
	const float WantYaw = D.Rotation().Yaw + OffsetDeg.X;
	const float WantPitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.Z, -1.f, 1.f))) + OffsetDeg.Y;
	const float Yaw = Me->GetActorRotation().Yaw;
	const float Pitch = FMath::RadiansToDegrees(Me->GetPitch());
	const float DYaw = FMath::FindDeltaAngleDegrees(Yaw, WantYaw), DPitch = WantPitch - Pitch;
	const float Angle = FMath::Sqrt(DYaw * DYaw + DPitch * DPitch);
	if (Angle < 0.001f) return;
	const float Step = FMath::Min(Angle, FMath::Min(MaxDegPerSecond, Angle * TurnGain) * DeltaSeconds) / Angle;
	Me->SetViewRotation(FMath::DegreesToRadians(Pitch + DPitch * Step), Yaw + DYaw * Step);
}

FVector AWWOBotController::PathDirection(AWWOCharacter* Me, const FVector& To, float DeltaSeconds)
{
	// Which way to run to reach To, around walls, on the level's navmesh (a straight line where
	// there's none).
	PathAge += DeltaSeconds;
	if (PathAge > 0.4f || FVector::DistSquared(To, PathGoal) > FMath::Square(150.f))
	{
		Path = WWONav::FindPath(Me, Me->FeetLocation(), To, bPathPartial);
		PathAge = 0.f;
		PathGoal = To;
		++(Path.Num() > 0 ? PathsFound : PathsFailed);
	}
	const FVector Here = Me->GetActorLocation();
	while (Path.Num() > 1 && FVector::Dist2D(Path[0], Here) < PathPointReach) Path.RemoveAt(0);
	const FVector Straight = Flat(To - Here);
	if (Path.Num() == 0) return Straight.GetSafeNormal();
	const FVector Step = Flat(Path[0] - Here);
	return (Step.Size() > 1.f ? Step : Straight).GetSafeNormal();
}

void AWWOBotController::PickGoal(const AWWOCharacter* Me)
{
	// A random spawn spot to roam to, not one it's already standing near.
	TArray<FVector> Spots;
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It) if (It->ActorHasTag("EnemySpawn")) Spots.Add(It->GetActorLocation());
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) Spots.Add(It->GetActorLocation());
	TArray<FVector> Far = Spots.FilterByPredicate([Me](const FVector& S) { return FVector::Dist2D(S, Me->GetActorLocation()) > 12.f * WWO::M; });
	const TArray<FVector>& From = Far.Num() > 0 ? Far : Spots;
	Goal = From.Num() > 0 ? From[FMath::RandRange(0, From.Num() - 1)] : FVector::ZeroVector;
	PathAge = 99.f;
}

bool AWWOBotController::SideIsClear(const AWWOCharacter* Me, const FVector& Direction) const
{
	const UWorld* World = GetWorld();
	const FVector Here = Me->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOBotSide), false, Me);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Here, Here + Direction * 130.f, ECC_Visibility, Params)) return false; // a wall
	const FVector Over = Here + Direction * 150.f;
	return World->LineTraceSingleByChannel(Hit, Over, Over - FVector(0, 0, 350.f), ECC_Visibility, Params); // floor to stand on
}

void AWWOBotController::SetMove(const AWWOCharacter* Me, const FVector& WorldDirection)
{
	// The stick is read against the way the body faces (x = right, y = forward).
	const FVector Dir = Flat(WorldDirection).GetSafeNormal();
	const FRotationMatrix Facing(FRotator(0.f, Me->GetActorRotation().Yaw, 0.f));
	Move = FVector2D(FVector::DotProduct(Dir, Facing.GetUnitAxis(EAxis::Y)), FVector::DotProduct(Dir, Facing.GetUnitAxis(EAxis::X)));
}

FString AWWOBotController::DebugState() const
{
	static const TCHAR* Names[] = { TEXT("roam"), TEXT("hunt"), TEXT("fight"), TEXT("retreat") };
	return FString::Printf(TEXT("%s foe=%s%s path=%d%s"), Names[(int32)State], *GetNameSafe(Foe.Get()), bFoeVisible ? TEXT(" (seen)") : TEXT(""),
		Path.Num(), bPathPartial ? TEXT(" partial") : TEXT(""));
}

// --- Each frame ----------------------------------------------------------------------------------

void AWWOBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Buttons are decided fresh every frame; a press lasts one frame (the character reads it once).
	for (bool& J : Just) J = false;
	for (bool& H : Held) H = false;
	Move = FVector2D::ZeroVector;
	AWWOCharacter* Me = Cast<AWWOCharacter>(GetPawn());
	if (!Me || Me->Health->bDead)
	{
		// Dead: it comes back knowing nothing.
		Foe = nullptr;
		bFoeVisible = bAware = bWasFiring = bThrowing = bRetreating = false;
		Path.Reset();
		PathAge = 99.f;
		Goal = FVector::ZeroVector;
		State = EState::Roam;
		Stuck = 0.f;
		return;
	}
	if (Me->bUlting) return; // the ult plays out by itself
	Clock += DeltaSeconds;
	const FBotTuning T = TuningFor(this);
	const FVector Eye = Me->EyeLocation();
	const FVector Here = Me->GetActorLocation();
	if ((SenseLeft -= DeltaSeconds) <= 0.f)
	{
		SenseLeft = FMath::FRandRange(0.1f, 0.16f);
		Sense(Me);
	}
	if ((TeamLeft -= DeltaSeconds) <= 0.f)
	{
		TeamLeft = FMath::FRandRange(0.8f, 1.2f);
		PickLeader(Me);
	}

	// What it knows about its foe.
	AWWOCharacter* Target = Foe.Get();
	if (Target && Target->Health->bDead) Target = nullptr;
	if (Target && bFoeVisible)
	{
		FoeSpot = ChestOf(Target);
		FoeFeet = Target->FeetLocation();
		FoeVelocity = Target->GetVelocity();
		FoeSeenAgo = 0.f;
	}
	else if (Target && (FoeSeenAgo += DeltaSeconds) > T.Memory) Target = nullptr;
	if (!Target)
	{
		Foe = nullptr;
		bFoeVisible = bAware = false;
	}
	ReactLeft = FMath::Max(ReactLeft - DeltaSeconds, 0.f);
	if (Target && ReactLeft <= 0.f) bAware = true;
	// Badly hurt (how badly: Aggression) and just hit: back off to cover, if there's any, until it
	// has recovered, the enemy is gone, or they come round the corner.
	UWWOHealthComponent* Body = Me->Health;
	const float HealthLeft = (Body->Health + Body->Shield) / FMath::Max(Body->MaxHealth() + Body->MaxShield(), 1.f);
	const float CoverAway = FVector::Dist2D(CoverSpot, Here) / WWO::M;
	if (bRetreating && ((RetreatLeft -= DeltaSeconds) <= 0.f || !Target || HealthLeft >= 0.75f
		|| (bFoeVisible && (CoverAway < 1.5f || FVector::Dist(FoeSpot, Eye) < 5.f * WWO::M))))
	{
		bRetreating = false;
		NextRetreat = Clock + (HealthLeft >= 0.75f ? 2.f : 6.f);
	}
	if (!bRetreating && Target && bAware && Clock >= NextRetreat && Body->SinceDamage < 1.5f && HealthLeft < FMath::Lerp(0.6f, 0.f, T.Aggression))
	{
		if (FindCover(Me, CoverSpot))
		{
			bRetreating = true;
			RetreatLeft = 10.f;
			PathAge = 99.f;
			++Retreats;
		}
		else
		{
			NextRetreat = Clock + 3.f; // nowhere to hide: fight on
			++NoCover;
		}
	}
	const EState Was = State;
	State = bRetreating ? EState::Retreat : (!Target || !bAware ? EState::Roam : (bFoeVisible && ReactLeft <= 0.f ? EState::Fight : EState::Hunt));
	if (State != Was)
	{
		Stuck = 0.f;
		if (State == EState::Fight)
		{
			OnTarget = 0.f;
			AimErrorLeft = 0.f;
			bWasFiring = false;
			PauseLeft = 0.f;
			NextShot = FMath::Max(NextShot, Clock);
		}
	}

	UWWOWeaponComponent* Gun = Me->Weapon;
	const UWWOWeaponData* W = Gun->GetWeaponData();
	const int32 Magazine = FMath::Max(Gun->MagazineSize(), 1);
	const bool bShotgun = W && W->Pellets > 1.5f;
	const bool bSniper = W && W->Zoom >= 4.f;
	bool bWantSprint = false, bMoving = false;

	// Mid-throw: its eyes are on the arc until the grenade has left its hand.
	if (bThrowing)
	{
		FVector Dir;
		ThrowLeft -= DeltaSeconds;
		const bool bArc = ThrowDirection(Me, ThrowAt, Dir);
		if (ThrowLeft <= 0.f || (!bThrown && (!Target || !bArc))) bThrowing = false; // done, or no throw to make any more
		else if (bArc)
		{
			TurnView(Me, Dir, T.TurnSpeed * 1.2f, DeltaSeconds);
			const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Me->AimForward(), Dir), -1.f, 1.f)));
			if (!bThrown && Off < 3.f)
			{
				if (Me->Grenades->ThrowGrenade())
				{
					bThrown = true;
					ThrowLeft = Gun->ThrowRelease + 0.08f;
					++Grenades;
				}
				else bThrowing = false;
			}
		}
	}

	if (State == EState::Retreat)
	{
		// Run for cover; there, watch where they were and reload.
		if (CoverAway > 1.2f)
		{
			const FVector Dir = PathDirection(Me, CoverSpot, DeltaSeconds);
			SetMove(Me, Dir);
			bMoving = true;
			bWantSprint = true;
			if (!bThrowing) TurnView(Me, Dir, T.TurnSpeed, DeltaSeconds);
		}
		else
		{
			if (!bThrowing) TurnView(Me, FoeSpot - Eye, T.TurnSpeed * 0.8f, DeltaSeconds);
			if (Gun->Ammo < Magazine && !Gun->bReloading) Press(EWWOAction::Reload);
		}
	}
	else if (State == EState::Fight)
	{
		OnTarget += DeltaSeconds;
		const FVector To = FoeSpot - Eye;
		const float DistCm = FMath::Max(To.Size(), 1.f), Dist = DistCm / WWO::M;
		const FVector ToFlat = Flat(To).GetSafeNormal();

		// Aim: at the chest, a touch ahead of where it's moving (to make up for the turn lag), off
		// by an error that wanders, bigger at first sight and against a target crossing fast.
		if ((AimErrorLeft -= DeltaSeconds) <= 0.f)
		{
			AimErrorLeft = FMath::FRandRange(0.2f, 0.5f);
			const FVector Across = FoeVelocity - To.GetSafeNormal() * FVector::DotProduct(FoeVelocity, To.GetSafeNormal());
			const float CrossDeg = FMath::RadiansToDegrees(Across.Size() / DistCm); // degrees per second across the view
			const float Scale = 1.f + 2.f * FMath::Exp(-OnTarget / 0.5f) + CrossDeg / 45.f;
			AimErrorGoal = FMath::RandPointInCircle(1.f) * T.AimError * Scale;
		}
		AimError = FMath::Lerp(AimError, AimErrorGoal, 1.f - FMath::Exp(-6.f * DeltaSeconds));
		if (!bThrowing) TurnView(Me, FoeSpot + FoeVelocity * (0.8f / TurnGain) - Eye, T.TurnSpeed, DeltaSeconds, AimError);

		// Fire only with the aim on them (a body's width at that distance) and in the gun's range.
		const float OffDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Me->AimForward(), To.GetSafeNormal()), -1.f, 1.f)));
		const float Tolerance = FMath::Max(FMath::RadiansToDegrees(FMath::Atan(40.f / DistCm)), 0.5f) * (bShotgun ? 2.5f : 1.f) * (bWasFiring ? 1.6f : 1.f);
		const bool bInRange = !W || Dist <= W->FireRange * (bShotgun ? 0.75f : 1.f);
		const bool bCanShoot = W && OffDeg <= Tolerance && bInRange && !Gun->bReloading && !bThrowing && !Gun->bMeleeing;
		PickGun(Me, Dist);
		UseKit(Me, Target, Dist, OffDeg, bInRange, HealthLeft);
		bool bFiring = false;
		if (Gun->Ammo <= 0 && !Gun->bReloading) Press(EWWOAction::Reload);
		else if (W && W->IsSemiAuto())
		{
			// Taps, a little slower and less even than the gun allows.
			if (bCanShoot && Clock >= NextShot)
			{
				NextShot = Clock + FMath::Max(60.f / FMath::Max(W->FireRate, 1.f) * 1.05f, 0.2f) * FMath::FRandRange(1.f, 1.3f);
				Press(EWWOAction::Fire);
				++ShotsFired;
			}
			bFiring = bCanShoot;
		}
		else if (PauseLeft > 0.f) PauseLeft -= DeltaSeconds;
		else if (bCanShoot)
		{
			// Bursts: long up close, short at range.
			if (!bWasFiring)
			{
				BurstLeft = Dist < 10.f ? FMath::FRandRange(0.9f, 1.8f) : FMath::FRandRange(0.3f, 0.7f);
				++ShotsFired;
			}
			Hold(EWWOAction::Fire);
			bFiring = true;
			if ((BurstLeft -= DeltaSeconds) <= 0.f)
			{
				PauseLeft = FMath::FRandRange(0.15f, 0.35f);
				bFiring = false;
			}
		}
		bWasFiring = bFiring;
		if (bInRange && (bSniper || (!bShotgun && Dist > 6.f))) Hold(EWWOAction::Aim);

		// Move: strafe, and keep the distance the gun likes. Out of range, run in.
		const float MinRange = bShotgun ? 3.f : (bSniper ? 20.f : 7.f), MaxRange = bShotgun ? 8.f : (bSniper ? 60.f : 22.f);
		if ((StrafeLeft -= DeltaSeconds) <= 0.f)
		{
			StrafeLeft = FMath::FRandRange(0.5f, 1.6f);
			if (FMath::RandBool()) StrafeDir = -StrafeDir;
		}
		const FVector Right = FVector::CrossProduct(FVector::UpVector, ToFlat);
		FVector Dir = Right * StrafeDir;
		if (Dist > MaxRange || !bInRange)
		{
			const FVector In = PathDirection(Me, FoeFeet, DeltaSeconds);
			Dir = (In.IsNearlyZero() ? ToFlat : In) + Right * StrafeDir * (bInRange ? 0.6f : 0.25f);
			bWantSprint = !bInRange;
		}
		else if ((Gun->bReloading || Gun->Ammo <= 0) && Dist < 6.f) Dir = ToFlat + Right * StrafeDir * 0.3f; // gun's down and they're close: go for the punch
		else if (Dist < MinRange && SideIsClear(Me, -ToFlat)) Dir -= ToFlat;
		SetMove(Me, Dir);
		if (Clock >= NextJump)
		{
			NextJump = Clock + FMath::FRandRange(4.f, 10.f);
			if (FMath::RandBool() && !bSniper) Press(EWWOAction::Jump);
		}
	}
	else if (State == EState::Hunt)
	{
		// Run to where it last saw them (a little along the way they were going), watching that
		// spot when it's in view and the way ahead when it isn't. Nothing there: give up soon.
		const FVector Lead = Flat(FoeVelocity);
		const FVector To = FoeFeet + Lead * 0.5f;
		const float Away = FVector::Dist2D(To, Here) / WWO::M;
		const FVector Dir = PathDirection(Me, To, DeltaSeconds);
		const bool bArrived = Away < 2.5f;
		if (bArrived) FoeSeenAgo = FMath::Max(FoeSeenAgo, T.Memory - 1.5f);
		else
		{
			SetMove(Me, Dir);
			bMoving = true;
		}
		const FVector Watch = FoeSpot + Lead * 1.f;
		if (!bThrowing) TurnView(Me, bSpotInSight || bArrived || Away < 4.f ? Watch - Eye : Dir, T.TurnSpeed * 0.8f, DeltaSeconds);
		// They ducked behind something near a spot it can still see: a grenade flushes them out.
		const UWWOLoadoutSubsystem* Loadout = Me->GetLoadout();
		const UWWOGrenadeData* G = Loadout ? Loadout->GetGrenade() : nullptr;
		if (G && G->Kind != EWWOGrenadeKind::Wall && !bThrowing && Clock >= NextGrenade && Me->Grenades->IsReady() && FoeSeenAgo < 3.f && bSpotInSight && Away >= 7.f)
		{
			NextGrenade = Clock + FMath::FRandRange(1.5f, 3.f);
			FVector Throw;
			if (FMath::FRand() < 0.6f * T.KitUse && ThrowDirection(Me, FoeFeet, Throw))
			{
				bThrowing = true;
				bThrown = false;
				ThrowAt = FoeFeet;
				ThrowLeft = 0.9f;
			}
		}
		if (Gun->Ammo < Magazine * 0.4f && FoeSeenAgo > 1.f && !Gun->bReloading) Press(EWWOAction::Reload);
		bWantSprint = Away > 6.f && !Gun->bReloading;
	}
	else
	{
		if (const AWWOCharacter* Lead = Leader.Get(); Lead && !Lead->Health->bDead)
		{
			// Stick with its leader: catch up when it falls behind, then stand a few meters off
			// watching the leader's flank.
			const float Apart = FVector::Dist2D(Lead->GetActorLocation(), Here) / WWO::M;
			bClosingUp = Apart > (bClosingUp ? 3.5f : 5.5f);
			if (bClosingUp)
			{
				const FVector Dir = PathDirection(Me, Lead->FeetLocation(), DeltaSeconds);
				SetMove(Me, Dir);
				bMoving = true;
				TurnView(Me, Dir, T.TurnSpeed * 0.6f, DeltaSeconds);
				bWantSprint = Apart > 8.f && !Gun->bReloading;
			}
			else TurnView(Me, FRotator(0.f, Lead->GetActorRotation().Yaw + 55.f * FlankSide, 0.f).Vector(), T.TurnSpeed * 0.4f, DeltaSeconds);
			Goal = FVector::ZeroVector;
		}
		else
		{
			// Roam: run to a spawn spot, then another.
			if (Goal.IsZero() || FVector::Dist2D(Goal, Here) < 3.f * WWO::M) PickGoal(Me);
			const FVector Dir = PathDirection(Me, Goal, DeltaSeconds);
			SetMove(Me, Dir);
			bMoving = true;
			TurnView(Me, Dir, T.TurnSpeed * 0.6f, DeltaSeconds);
			bWantSprint = !Gun->bReloading;
		}
		if (Gun->Ammo < Magazine * 0.6f && !Gun->bReloading) Press(EWWOAction::Reload); // reload while it's quiet
	}

	// Sprint latches: one press starts it (sprinting would cancel a reload).
	if (bWantSprint && !Me->bSprinting && !Me->bSliding && !Gun->bReloading) Press(EWWOAction::Sprint);

	// Stuck on something: hop (that also mantles a ledge), then give up on this way.
	const float Speed = DeltaSeconds > 0.f ? FVector::Dist2D(LastSpot, Here) / DeltaSeconds : 0.f;
	LastSpot = Here;
	Stuck = bMoving && Speed < 0.5f * WWO::M && Me->EntrenchLeft <= 0.f && !Gun->bMeleeing ? Stuck + DeltaSeconds : 0.f; // (rooted by Entrench isn't stuck)
	if (Stuck > 0.7f && Clock >= NextStuckJump)
	{
		NextStuckJump = Clock + 0.8f;
		Press(EWWOAction::Jump);
		++StuckJumps;
		++StuckIn[(int32)State];
	}
	if (Stuck > 2.5f)
	{
		Stuck = 0.f;
		++Regoals;
		if (State == EState::Hunt) FoeSeenAgo = T.Memory; // can't get there
		if (bRetreating)
		{
			bRetreating = false; // that cover can't be reached
			NextRetreat = Clock + 4.f;
		}
		PickGoal(Me);
	}
}
