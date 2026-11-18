#include "UI/WWOHUDWidget.h"
#include "Player/WWOProf.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "FX/WWOFX.h"
#include "Flora/WWOSporeFog.h"
#include "Flora/WWOFloraEffects.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "UI/WWOGunPreviewActor.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "WWOIds.h"

// The HUD's state: binding to the player, easing its animated values each frame, the radar's
// maths. The painting is in WWOHUDPaint.cpp.

AWWOCharacter* UWWOHUDWidget::Player() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC ? Cast<AWWOCharacter>(PC->GetPawn()) : nullptr;
}

void UWWOHUDWidget::Bind(AWWOCharacter* P)
{
	BoundPlayer = P;
	P->Weapon->OnHitConfirmed.AddWeakLambda(this, [this](bool bCritical, bool bKilled)
	{
		if (!bKilled && bHitKilled && HitTime > 0.f) return; // a kill marker outlasts the hits around it
		HitTime = bKilled ? KillMarkerTime : HitMarkerTime;
		bHitCritical = bCritical;
		bHitKilled = bKilled;
	});
	P->Health->OnDamaged.AddWeakLambda(this, [this](float) { DamageFlash = 0.3f; });
}

void UWWOHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	WWO_PROF("HUDWidget.NativeTick");
	Super::NativeTick(MyGeometry, InDeltaTime);
	AWWOCharacter* P = Player();
	if (P && BoundPlayer.Get() != P) Bind(P);
	HitTime -= GetWorld() ? GetWorld()->GetDeltaSeconds() : InDeltaTime; // game time, like Godot's _process (slows in a hit-stop)
	DamageFlash = WWO::MoveToward(DamageFlash, 0.f, 1.2f * InDeltaTime);
	{
		const UWWOAbilityData* Static = P ? P->Abilities->Equipped(WWOIds::Ability::StaticCharge) : nullptr;
		const bool bCharged = Static && P->Abilities->IsEffectActive(Static) && !P->Health->bDead;
		StaticShown = WWO::MoveToward(StaticShown, bCharged ? 1.f : 0.f, (bCharged ? 2.5f : 5.f) * InDeltaTime); // in over 0.4 s, gone in 0.2
	}
	// How hurt you are, 0 (full health and shield) to 1 (nothing left): the vignette follows it,
	// quickly as damage lands and a little slower as you heal.
	float Hurt = 0.f;
	if (P && P->Health)
	{
		const float Most = P->Health->MaxHealth() + P->Health->MaxShield();
		Hurt = Most > 0.f ? 1.f - FMath::Clamp((P->Health->Health + P->Health->Shield) / Most, 0.f, 1.f) : 0.f;
	}
	HurtShown = WWO::MoveToward(HurtShown, Hurt, (Hurt > HurtShown ? 6.f : 1.5f) * InDeltaTime);
	// What's on you right now (the chips under the vitals), and the longest time each chip has
	// shown: its bar drains against that.
	if (P && P->Health)
	{
		Chips.Reset();
		if (!P->Health->bDead)
		{
			if (P->SlowLeft > 0.f) Chips.Add({ FString::Printf(TEXT("SLOWED %d%%"), FMath::RoundToInt(P->SlowAmount * 100.f)), FLinearColor(0.6f, 0.85f, 1.f), P->SlowLeft, 0.f });
			if (const UWWODamageOverTime* Dot = P->FindComponentByClass<UWWODamageOverTime>()) Chips.Add({ TEXT("POISONED"), UWWODamageOverTime::Color(), Dot->TimeLeft, 0.f });
			if (AWWOSporeFog::Contains(P->GetWorld(), P->GetActorLocation())) Chips.Add({ P->IsInvisible() ? TEXT("VEILED") : TEXT("IN FOG"), FLinearColor(0.8f, 0.65f, 1.f), P->IsInvisible() ? P->VeilLeft : 0.f, 0.f });
			if (P->EntrenchLeft > 0.f) Chips.Add({ TEXT("ENTRENCHED"), FLinearColor(0.85f, 0.65f, 0.4f), P->EntrenchLeft, 0.f });
			if (P->Abilities->GuardTimeLeft() > 0.f) Chips.Add({ TEXT("GUARD"), FLinearColor(0.75f, 0.9f, 0.6f), P->Abilities->GuardTimeLeft(), 0.f });
		}
		TMap<FString, float> Seen;
		for (const FWWOStatusChip& Chip : Chips)
		{
			const FString Key = Chip.Label.Left(6);
			Seen.Add(Key, Chip.Of > 0.f ? Chip.Of : FMath::Max(ChipPeak.FindRef(Key), Chip.Left));
		}
		ChipPeak = MoveTemp(Seen);
	}
	FpsClock += FApp::GetDeltaTime(); // real time, so hit-stops and pauses don't skew it
	++FpsFrames;
	if (FpsClock >= 0.25f)
	{
		FpsShown = FpsFrames / FpsClock;
		FpsClock = 0.f;
		FpsFrames = 0;
	}
	if (!VignetteMat)
	{
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_Vignette"))) VignetteMat = UMaterialInstanceDynamic::Create(M, this);
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_Scope"))) ScopeMat = UMaterialInstanceDynamic::Create(M, this);
	}
	UpdateGunPictures();
}

void UWWOHUDWidget::UpdateGunPictures()
{
	// The weapon in hand (with its mods) and the one in the other slot (as it comes: the loadout only
	// knows the mods of the weapon in hand), each photographed when it changes.
	UWorld* World = GetWorld();
	const UWWOLoadoutSubsystem* L = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UWWOLoadoutSubsystem>() : nullptr;
	if (!L) return;
	const UWWOWeaponData* Guns[2] = { L->GetWeapon(), nullptr };
	const int32 Other = L->GetSlotConfig(1 - L->GetActiveSlot());
	if (Other != INDEX_NONE)
	{
		const FName Id = L->GetConfigs()[Other].Weapon;
		for (const UWWOWeaponData* W : L->Catalog.Weapons) if (W && W->Id == Id) Guns[1] = W;
	}
	for (int32 i = 0; i < 2; ++i)
	{
		FString Key = Guns[i] ? Guns[i]->Id.ToString() : FString();
		if (i == 0 && Guns[i])
		{
			for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
			{
				const UWWOModData* Mod = L->GetMod(Slot);
				Key += TEXT("|") + (Mod ? Mod->Id.ToString() : FString());
			}
		}
		if (!GunPictures[i])
		{
			if (!Guns[i]) continue;
			GunPictures[i] = World->SpawnActor<AWWOGunPreviewActor>(FVector(3000.f * (i + 1), 12000.f, -50000.f), FRotator::ZeroRotator, WWO::AlwaysSpawn());
			if (!GunPictures[i]) continue;
			GunPictures[i]->MakePicture(GunPictureShape);
			if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_HUDGun")))
			{
				GunMats[i] = UMaterialInstanceDynamic::Create(M, this);
				GunMats[i]->SetTextureParameterValue("Tex", GunPictures[i]->Target);
			}
			GunShown[i].Reset();
		}
		if (Key != GunShown[i])
		{
			GunShown[i] = Key;
			GunPictures[i]->ShowGun(Guns[i], L);
		}
	}
}

int32 UWWOHUDWidget::RadarBands(float Distance, float Range, float FullRange, int32 Steps)
{
	Steps = FMath::Max(Steps, 1);
	if (Distance > Range) return 0;
	if (Distance <= FullRange || Steps == 1 || Range <= FullRange) return Steps;
	// The stretch from the range's edge in to the full range, cut into Steps - 1 equal parts.
	const float In = (Range - Distance) / (Range - FullRange); // 0 at the edge, 1 at the full range
	return FMath::Clamp(1 + FMath::FloorToInt(In * (Steps - 1)), 1, Steps - 1);
}

void UWWOHUDWidget::RadarMates(const AWWOCharacter* Viewer, TArray<FVector2D>& Out)
{
	Out.Reset();
	const UWWOWorldSubsystem* WS = Viewer ? UWWOWorldSubsystem::Get(Viewer) : nullptr;
	if (!WS || !Viewer->Camera) return;
	const float Yaw = Viewer->Camera->GetComponentRotation().Yaw;
	for (const AWWOCharacter* Mate : WS->GetPlayers())
	{
		if (!Mate || Mate == Viewer || Mate->Health->bDead || UWWOWorldSubsystem::CanHurt(Viewer, Mate)) continue;
		const FVector To = Mate->FeetLocation() - Viewer->FeetLocation();
		Out.Add(FVector2D(FMath::FindDeltaAngleDegrees(Yaw, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X))),
			FVector2D(To.X, To.Y).Size() / WWO::M / FMath::Max(Viewer->RadarRange, 1.f)));
	}
}

int32 UWWOHUDWidget::Radar(const AWWOCharacter* Viewer, TArray<float>& Out)
{
	const int32 Steps = Viewer ? FMath::Clamp(FMath::RoundToInt(Viewer->RadarSteps), 1, 12) : 1;
	Out.Init(0.f, 8 * Steps);
	const UWWOWorldSubsystem* WS = Viewer ? UWWOWorldSubsystem::Get(Viewer) : nullptr;
	if (!WS || !Viewer->Camera) return Steps;
	const float Yaw = Viewer->Camera->GetComponentRotation().Yaw;
	const FVector From = Viewer->FeetLocation();
	for (const AActor* Actor : WS->LivingTargets(Viewer)) // enemies, and in PvP the other team
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
		if (!Target || Actor == Viewer) continue;
		const FVector To = Target->FeetLocation() - From;
		const int32 Bands = RadarBands(FVector2D(To.X, To.Y).Size() / WWO::M, Viewer->RadarRange, Viewer->RadarFullRange, Steps);
		if (Bands <= 0) continue;
		// Which way, from the way you look: 0 ahead, 90 to the right. (Right on top of you: ahead.)
		const float Bearing = FMath::FindDeltaAngleDegrees(Yaw, FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X)));
		const int32 Section = ((FMath::RoundToInt(Bearing / 45.f) % 8) + 8) % 8;
		const float Level = FMath::Clamp(FMath::Abs(To.Z) / WWO::M / FMath::Max(Viewer->RadarHeightFade, 0.1f), 0.f, 1.f);
		const float Alpha = FMath::Lerp(1.f, FMath::Clamp(Viewer->RadarMinAlpha, 0.f, 1.f), Level);
		for (int32 Band = 0; Band < Bands; ++Band) Out[Section * Steps + Band] = FMath::Max(Out[Section * Steps + Band], Alpha);
	}
	return Steps;
}
