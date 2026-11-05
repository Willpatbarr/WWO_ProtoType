#include "Player/WWOWeaponComponent.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOAimAssistComponent.h"
#include "Player/WWOAbilityComponent.h"
#include "Flora/WWOFloraEffects.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Systems/WWOWorldSubsystem.h"
#include "Systems/WWOTargetable.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

namespace
{
	// Godot camera space (meters, x right, y up, z back) -> Unreal (cm, x forward, y right, z up).
	FVector G2U(const FVector& G) { return FVector(-G.Z, G.X, G.Y) * WWO::M; }
	// Godot rotation (radians: pitch about x, yaw about y, roll about z) -> FRotator.
	FRotator GRot(const FVector& R) { return FRotator(FMath::RadiansToDegrees(R.X), -FMath::RadiansToDegrees(R.Y), -FMath::RadiansToDegrees(R.Z)); }
	FVector Deg(float X, float Y, float Z) { return FVector(FMath::DegreesToRadians(X), FMath::DegreesToRadians(Y), FMath::DegreesToRadians(Z)); }

	constexpr float SwayPerTurnSpeed = 0.025f;
	constexpr float SwayMax = 0.08f;
	const FVector SprintOffset(-0.1f, 0.03f, 0.02f);
	const FVector SprintRotation(-0.15f, 0.55f, 0.45f); // muzzle down and left, canted
	const FVector SlideOffset(0.f, 0.07f, 0.f);
	constexpr float AirLagPerSpeed = 0.008f;
	constexpr float AirLagMax = 0.06f;
	const FVector MantleOffset(0.02f, -0.14f, 0.06f);
	const FVector MantleRotation(-0.5f, 0.f, 0.25f);
	const FVector ThrowOffset(0.03f, -0.1f, 0.04f);
	const FVector ThrowRotation(-0.35f, -0.15f, 0.f);
	const FVector SwapOffset(0.f, -0.22f, 0.06f);
	const FVector SwapRotation(-0.7f, 0.f, 0.f);
	constexpr float ReloadTilt = 0.35f;
	constexpr float ReloadMagDrop = 0.2f;
	const FVector ReloadLift(-0.05f, 0.1f, -0.12f);
	constexpr float ReloadRoll = -0.7f;
	constexpr float ReloadSeated = 0.75f; // ammo refills once the new mag is in
	constexpr float MeleeImpactAt = 0.4f;  // the hit lands here (fraction of the swing)
	constexpr float MeleeLungeHold = 0.3f; // the swing waits here (wound up) until the lunge arrives
	constexpr float MeleeAngle = 35.f;     // degrees off center a melee can still connect
	constexpr float MeleeArmDrop = 0.45f;
	const FVector MeleeGunDropOffset(0.03f, -0.3f, 0.12f);
	const FVector MeleeGunDropRotation(-0.9f, 0.f, 0.25f);
	constexpr float MuzzleFlashTime = 0.04f;
	const FLinearColor SightColor(0.95f, 0.95f, 0.95f);
	const FLinearColor SightOnTargetColor(1.f, 0.1f, 0.08f);

	// The punch, as fist positions (Godot camera space) at animation times 0..3:
	// rest (below the view), chambered, contact, then pulled back.
	const FVector PunchKeys[] = { FVector(0.34f, -0.55f, -0.15f), FVector(0.26f, -0.27f, -0.18f), FVector(0.04f, -0.09f, -0.62f), FVector(0.3f, -0.4f, -0.35f) };
	constexpr float PunchChambered = 1.f, PunchContact = 2.f, PunchEnd = 3.f;
	const FVector RightShoulder(0.32f, -0.5f, 0.2f);
	// The overhand throw (left arm) at the Godot animation's times.
	constexpr float ThrowAnimStart = 0.05f, ThrowAnimCocked = 0.2f, ThrowAnimRelease = 0.363f, ThrowAnimEnd = 0.42f;
	const FVector ThrowKeys[] = { FVector(-0.28f, -0.5f, -0.3f), FVector(-0.22f, 0.1f, 0.25f), FVector(-0.06f, 0.f, -0.55f), FVector(-0.18f, -0.6f, -0.45f) };
	const FVector LeftShoulder(-0.32f, -0.5f, 0.2f);

	struct FGunPart { const TCHAR* Name; int32 Kind; FVector Size; FVector Pos; FVector RotDeg; FLinearColor Color; float Glow; };
	enum { Box, Cyl, Sphere };
	const FLinearColor Dark(0.12f, 0.12f, 0.13f), Steel(0.22f, 0.24f, 0.28f), Orange(0.95f, 0.5f, 0.1f), Wood(0.38f, 0.22f, 0.12f),
		Bronze(0.48f, 0.34f, 0.2f), Gold(0.78f, 0.6f, 0.25f), Olive(0.22f, 0.27f, 0.2f), Flash(1.f, 0.75f, 0.3f), White(0.95f, 0.95f, 0.95f), LensBlue(0.3f, 0.6f, 0.9f);

	// The low-poly weapon models (Godot: tools/build_weapon_models.gd). Origin = the sight point.
	// Size: box size, or (radius, length) for a cylinder along -z, or (radius) for a sphere.
	const TArray<FGunPart>& ModelParts(FName Id)
	{
		static const TArray<FGunPart> Rifle = {
			{ TEXT("Receiver"), Box, FVector(0.05, 0.08, 0.28), FVector(0, -0.07, 0.02), FVector::ZeroVector, Steel, 0 },
			{ TEXT("Stripe"), Box, FVector(0.052, 0.015, 0.2), FVector(0, -0.065, 0.02), FVector::ZeroVector, Orange, 0 },
			{ TEXT("Barrel"), Cyl, FVector(0.015, 0.26, 0), FVector(0, -0.06, -0.25), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Grip"), Box, FVector(0.045, 0.12, 0.06), FVector(0, -0.16, 0.11), FVector(-15, 0, 0), Dark, 0 },
			{ TEXT("Magazine"), Box, FVector(0.04, 0.13, 0.06), FVector(0, -0.165, -0.02), FVector::ZeroVector, Orange, 0 },
			{ TEXT("Stock"), Box, FVector(0.042, 0.07, 0.15), FVector(0, -0.08, 0.23), FVector::ZeroVector, Steel, 0 },
			{ TEXT("SightBase"), Box, FVector(0.034, 0.01, 0.05), FVector(0, -0.0225, 0), FVector::ZeroVector, Dark, 0 },
			{ TEXT("SightDot"), Sphere, FVector(0.0005, 0, 0), FVector::ZeroVector, FVector::ZeroVector, White, 2 },
			{ TEXT("MuzzleFlash"), Sphere, FVector(0.03, 0, 0), FVector(0, -0.06, -0.41), FVector::ZeroVector, Flash, 6 },
		};
		static const TArray<FGunPart> HandCannon = {
			{ TEXT("Frame"), Box, FVector(0.045, 0.07, 0.16), FVector(0, -0.065, 0.02), FVector::ZeroVector, Bronze, 0 },
			{ TEXT("Barrel"), Box, FVector(0.036, 0.042, 0.18), FVector(0, -0.046, -0.14), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Rib"), Box, FVector(0.02, 0.008, 0.3), FVector(0, -0.023, -0.07), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Magazine"), Cyl, FVector(0.032, 0.075, 0), FVector(0, -0.066, -0.005), FVector::ZeroVector, Gold, 0 },
			{ TEXT("Grip"), Box, FVector(0.036, 0.12, 0.05), FVector(0, -0.14, 0.09), FVector(-20, 0, 0), Wood, 0 },
			{ TEXT("Hammer"), Box, FVector(0.012, 0.026, 0.02), FVector(0, -0.026, 0.1), FVector::ZeroVector, Dark, 0 },
			{ TEXT("SightDot"), Sphere, FVector(0.0008, 0, 0), FVector(0, 0, -0.12), FVector::ZeroVector, White, 2 },
			{ TEXT("MuzzleFlash"), Sphere, FVector(0.04, 0, 0), FVector(0, -0.046, -0.26), FVector::ZeroVector, Flash, 6 },
		};
		static const TArray<FGunPart> Shotgun = {
			{ TEXT("Receiver"), Box, FVector(0.05, 0.075, 0.22), FVector(0, -0.058, 0.08), FVector::ZeroVector, Olive, 0 },
			{ TEXT("Barrel"), Cyl, FVector(0.018, 0.46, 0), FVector(0, -0.032, -0.25), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Tube"), Cyl, FVector(0.013, 0.38, 0), FVector(0, -0.068, -0.2), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Magazine"), Box, FVector(0.046, 0.046, 0.13), FVector(0, -0.07, -0.17), FVector::ZeroVector, Wood, 0 },
			{ TEXT("Stock"), Box, FVector(0.04, 0.08, 0.22), FVector(0, -0.09, 0.29), FVector(-6, 0, 0), Wood, 0 },
			{ TEXT("Rib"), Box, FVector(0.012, 0.006, 0.46), FVector(0, -0.012, -0.25), FVector::ZeroVector, Dark, 0 },
			{ TEXT("SightDot"), Sphere, FVector(0.004, 0, 0), FVector(0, 0, -0.47), FVector::ZeroVector, White, 4 },
			{ TEXT("MuzzleFlash"), Sphere, FVector(0.05, 0, 0), FVector(0, -0.032, -0.5), FVector::ZeroVector, Flash, 6 },
		};
		static const TArray<FGunPart> Sniper = {
			{ TEXT("Receiver"), Box, FVector(0.05, 0.07, 0.3), FVector(0, -0.085, 0.05), FVector::ZeroVector, Steel, 0 },
			{ TEXT("Barrel"), Cyl, FVector(0.014, 0.52, 0), FVector(0, -0.075, -0.36), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Brake"), Box, FVector(0.036, 0.03, 0.05), FVector(0, -0.075, -0.64), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Scope"), Cyl, FVector(0.022, 0.3, 0), FVector(0, 0, 0), FVector::ZeroVector, Dark, 0 },
			{ TEXT("ScopeFront"), Cyl, FVector(0.031, 0.05, 0), FVector(0, 0, -0.16), FVector::ZeroVector, Dark, 0 },
			{ TEXT("ScopeRear"), Cyl, FVector(0.028, 0.04, 0), FVector(0, 0, 0.15), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Lens"), Cyl, FVector(0.026, 0.004, 0), FVector(0, 0, -0.186), FVector::ZeroVector, LensBlue, 1.5f },
			{ TEXT("MountFront"), Box, FVector(0.02, 0.04, 0.02), FVector(0, -0.04, -0.08), FVector::ZeroVector, Dark, 0 },
			{ TEXT("MountRear"), Box, FVector(0.02, 0.04, 0.02), FVector(0, -0.04, 0.08), FVector::ZeroVector, Dark, 0 },
			{ TEXT("Bolt"), Box, FVector(0.05, 0.012, 0.012), FVector(0.045, -0.07, 0.12), FVector::ZeroVector, Gold, 0 },
			{ TEXT("Magazine"), Box, FVector(0.036, 0.06, 0.07), FVector(0, -0.145, 0.02), FVector::ZeroVector, Orange, 0 },
			{ TEXT("Grip"), Box, FVector(0.04, 0.11, 0.05), FVector(0, -0.16, 0.16), FVector(-15, 0, 0), Dark, 0 },
			{ TEXT("Stock"), Box, FVector(0.045, 0.09, 0.26), FVector(0, -0.1, 0.34), FVector::ZeroVector, Steel, 0 },
			{ TEXT("MuzzleFlash"), Sphere, FVector(0.06, 0, 0), FVector(0, -0.075, -0.68), FVector::ZeroVector, Flash, 6 },
		};
		if (Id == "handcannon") return HandCannon;
		if (Id == "shotgun") return Shotgun;
		if (Id == "sniper") return Sniper;
		return Rifle;
	}

	FVector PathAt(const FVector* Keys, const float* Times, int32 Count, float T)
	{
		if (T <= Times[0]) return Keys[0];
		for (int32 i = 1; i < Count; ++i)
		{
			if (T <= Times[i])
			{
				const float A = (T - Times[i - 1]) / FMath::Max(Times[i] - Times[i - 1], 1e-4f);
				return FMath::Lerp(Keys[i - 1], Keys[i], WWO::SmoothStep(0.f, 1.f, A));
			}
		}
		return Keys[Count - 1];
	}

	// Godot rotations: about y (yaw, + turns left) then about x (pitch, + lifts).
	FVector Place(const FVector& P, float YawDeg, float PitchDeg, float Push)
	{
		const float Y = FMath::DegreesToRadians(YawDeg), X = FMath::DegreesToRadians(PitchDeg);
		const FVector A(P.X * FMath::Cos(Y) + P.Z * FMath::Sin(Y), P.Y, -P.X * FMath::Sin(Y) + P.Z * FMath::Cos(Y));
		const FVector B(A.X, A.Y * FMath::Cos(X) - A.Z * FMath::Sin(X), A.Y * FMath::Sin(X) + A.Z * FMath::Cos(X));
		return B + FVector(0, 0, -Push);
	}
}

UWWOWeaponComponent::UWWOWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the character updates it each frame
}

AWWOCharacter* UWWOWeaponComponent::GetPlayer() const
{
	return Cast<AWWOCharacter>(GetOwner());
}

float UWWOWeaponComponent::M(FName Stat, float Base) const
{
	const AWWOCharacter* P = GetPlayer();
	return P ? P->M(Stat, Base) : Base;
}

bool UWWOWeaponComponent::Pressed(int32 Action) const
{
	const AWWOPlayerController* PC = GetPlayer() ? GetPlayer()->GetWWOController() : nullptr;
	return PC && PC->IsHeld((EWWOAction)Action);
}

bool UWWOWeaponComponent::JustPressed(int32 Action) const
{
	const AWWOPlayerController* PC = GetPlayer() ? GetPlayer()->GetWWOController() : nullptr;
	return PC && PC->WasJustPressed((EWWOAction)Action);
}

void UWWOWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	ModelRoot = NewObject<USceneComponent>(GetOwner(), TEXT("WeaponModel"));
	ModelRoot->SetupAttachment(this);
	ModelRoot->RegisterComponent();
	BuildArms();
	if (UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this))
	{
		Equip(Loadout->GetWeapon());
		LoadoutHandle = Loadout->OnChanged.AddWeakLambda(this, [this, Loadout]()
		{
			if (Loadout->GetWeapon() != Weapon) Equip(Loadout->GetWeapon());
			UpdateArmColor();
		});
	}
	UpdateArmColor();
}

void UWWOWeaponComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this)) Loadout->OnChanged.Remove(LoadoutHandle);
	Super::EndPlay(Reason);
}

UStaticMeshComponent* UWWOWeaponComponent::Part(FName Name) const
{
	const TObjectPtr<UStaticMeshComponent>* Found = Parts.Find(Name);
	return Found ? Found->Get() : nullptr;
}

int32 UWWOWeaponComponent::MagazineSize() const
{
	return Weapon ? FMath::Max(1, (int32)Weapon->MagazineSize) : 1;
}

void UWWOWeaponComponent::Equip(UWWOWeaponData* NewWeapon)
{
	// Swap in the weapon's model and start it with a full magazine.
	if (!NewWeapon) return;
	Weapon = NewWeapon;
	BuildModel();
	Ammo = MagazineSize();
	CancelReload();
	AdsProgress = 0.f;
	FireCooldown = 0.f;
	SwapLength = FMath::Max(M("swap_time", SwapTime), 0.05f);
	SwapLeft = SwapLength;
}

void UWWOWeaponComponent::BuildModel()
{
	for (auto& Pair : Parts)
	{
		if (Pair.Value) Pair.Value->DestroyComponent();
	}
	Parts.Reset();
	for (const FGunPart& P : ModelParts(Weapon->Id))
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(GetOwner());
		Comp->SetupAttachment(ModelRoot);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
		Comp->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Solid")));
		FRotator Rot = GRot(Deg(P.RotDeg.X, P.RotDeg.Y, P.RotDeg.Z));
		switch (P.Kind)
		{
		case Box:
			Comp->SetStaticMesh(WWOFX::Cube());
			Comp->SetRelativeScale3D(FVector(P.Size.Z, P.Size.X, P.Size.Y));
			break;
		case Cyl:
			Comp->SetStaticMesh(WWOFX::Cylinder());
			Comp->SetRelativeScale3D(FVector(P.Size.X * 2.f, P.Size.X * 2.f, P.Size.Y));
			Rot = Rot + FRotator(90.f, 0.f, 0.f); // the engine cylinder stands along z: lay it along the barrel
			break;
		default:
			Comp->SetStaticMesh(WWOFX::Sphere());
			Comp->SetRelativeScale3D(FVector(P.Size.X * 2.f));
			break;
		}
		Comp->SetRelativeLocationAndRotation(G2U(P.Pos), Rot);
		Comp->RegisterComponent();
		if (UMaterialInstanceDynamic* Mat = Comp->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mat->SetVectorParameterValue("Color", P.Color);
			Mat->SetScalarParameterValue("Glow", P.Glow);
		}
		if (FName(P.Name) == "MuzzleFlash") Comp->SetVisibility(false);
		Parts.Add(P.Name, Comp);
		if (FName(P.Name) == "Magazine") MagRest = P.Pos;
	}
}

void UWWOWeaponComponent::BuildArm(FWWOFPArm& Arm, bool bLeft)
{
	auto Shape = [this](USceneComponent* Parent, UStaticMesh* Mesh) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner());
		C->SetupAttachment(Parent);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
		C->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Solid")));
		C->SetUsingAbsoluteScale(false);
		C->RegisterComponent();
		C->CreateAndSetMaterialInstanceDynamic(0);
		return C;
	};
	Arm.bLeft = bLeft;
	Arm.Root = NewObject<USceneComponent>(GetOwner());
	Arm.Root->SetupAttachment(GetAttachParent()); // the camera
	Arm.Root->RegisterComponent();
	Arm.Upper = Shape(Arm.Root, WWOFX::Cylinder());
	Arm.Fore = Shape(Arm.Root, WWOFX::Cylinder());
	Arm.Fist = Shape(Arm.Root, WWOFX::Cube());
	Arm.Ball = Shape(Arm.Root, WWOFX::Sphere());
	Arm.Ball->SetRelativeScale3D(FVector(0.12f));
	Arm.Root->SetVisibility(false, true);
}

void UWWOWeaponComponent::BuildArms()
{
	BuildArm(MeleeArm, false);
	BuildArm(ThrowArm, true);
}

void UWWOWeaponComponent::UpdateArmColor()
{
	// Same color as the body's arms: the equipped Arms frame part's group color.
	const FLinearColor Base(0.3f, 0.42f, 0.55f);
	const FLinearColor Joint(0.15f, 0.16f, 0.18f);
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFramePartData* ArmsPart = Loadout ? Loadout->GetFramePart("arms") : nullptr;
	const FLinearColor Skin = ArmsPart ? ArmsPart->BodyColor(Base) : Base;
	for (FWWOFPArm* Arm : { &MeleeArm, &ThrowArm })
	{
		if (!Arm->Root) continue;
		for (UStaticMeshComponent* C : { Arm->Upper.Get(), Arm->Fore.Get() })
		{
			if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0))) Mat->SetVectorParameterValue("Color", Skin);
		}
		if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Arm->Fist->GetMaterial(0))) Mat->SetVectorParameterValue("Color", Joint);
	}
}

// --- Queries ---------------------------------------------------------------------------------

bool UWWOWeaponComponent::IsSwapping() const
{
	return SwapLeft > SwapLength * 0.4f; // can fire once the gun is mostly back up
}

bool UWWOWeaponComponent::IsAiming() const
{
	return Pressed((int32)EWWOAction::Aim) && !bReloading && !bMeleeing && !IsSwapping();
}

float UWWOWeaponComponent::LookMultiplier() const
{
	return Weapon ? FMath::Lerp(1.f, Weapon->AdsSensitivity / Weapon->Zoom, AdsAmount) : 1.f;
}

float UWWOWeaponComponent::MoveMultiplier() const
{
	return Weapon ? FMath::Lerp(1.f, Weapon->AdsMoveSpeed, AdsAmount) : 1.f;
}

bool UWWOWeaponComponent::CastRayTo(const FVector& To, FHitResult& OutHit) const
{
	const AWWOCharacter* P = GetPlayer();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOShot), false, P);
	return GetWorld()->LineTraceSingleByChannel(OutHit, P->EyeLocation(), To, ECC_Visibility, Params);
}

void UWWOWeaponComponent::StartReload()
{
	if (bReloading || Ammo >= MagazineSize()) return;
	bReloading = true;
	ReloadProgress = 0.f;
	GetPlayer()->bSprinting = false; // reloading stops a sprint
}

void UWWOWeaponComponent::CancelReload()
{
	bReloading = false;
	ReloadProgress = 0.f;
}

void UWWOWeaponComponent::StartThrowPose(const FLinearColor& GrenadeColor)
{
	ThrowTime = ThrowPoseLength();
	ThrowClock = 0.f;
	ThrowColor = GrenadeColor;
}

void UWWOWeaponComponent::StartPunch(float FovDegrees, float RollDegrees, float Seconds)
{
	// A quick camera jolt: the FOV and roll snap to these, then ease back over Seconds.
	PunchAge = 0.f;
	PunchFovValue = FovDegrees;
	PunchRollValue = RollDegrees;
	PunchLength = FMath::Max(Seconds, 0.01f);
}

void UWWOWeaponComponent::Rumble(float Weak, float Strong, float Seconds)
{
	if (AWWOPlayerController* PC = GetPlayer()->GetWWOController()) PC->Rumble(Weak, Strong, Seconds);
}

float UWWOWeaponComponent::PunchAmount() const
{
	// Snaps up over a few ms, then eases back to 0.
	if (PunchAge >= PunchLength) return 0.f;
	const float Attack = FMath::Min(0.015f, PunchLength * 0.25f);
	if (PunchAge < Attack) return PunchAge / Attack;
	const float T = (PunchAge - Attack) / (PunchLength - Attack);
	return (1.f - T) * (1.f - T);
}

float UWWOWeaponComponent::CameraRoll() const
{
	return -PunchRollValue * PunchAmount() * PunchScale; // Godot +roll = tilt left
}

// --- Each frame ------------------------------------------------------------------------------

void UWWOWeaponComponent::UpdateWeapon(float DeltaSeconds)
{
	if (!Weapon) return;
	UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (Loadout && JustPressed((int32)EWWOAction::WeaponNext)) Loadout->CycleWeapon(1);
	else if (Loadout && JustPressed((int32)EWWOAction::WeaponPrev)) Loadout->CycleWeapon(-1);
	UpdateMelee(DeltaSeconds);
	UpdateReload(DeltaSeconds);
	UpdateAds(DeltaSeconds);
	UpdateFiring(DeltaSeconds);
	PunchAge += DeltaSeconds;
	UpdateFov();
	UpdateReticleTarget();
	UpdatePose(DeltaSeconds);
}

// --- Melee -------------------------------------------------------------------------------------

void UWWOWeaponComponent::UpdateMelee(float DeltaSeconds)
{
	AWWOCharacter* P = GetPlayer();
	AWWOPlayerController* PC = P->GetWWOController();
	const bool bMeleePressed = JustPressed((int32)EWWOAction::Melee) || (PC && PC->ConsumeChord(EWWOChord::Melee)); // V, or R1 alone
	if (bMeleePressed && !bMeleeing)
	{
		bMeleeing = true;
		MeleeProgress = 0.f;
		bMeleeLanded = false;
		CancelReload(); // meleeing cancels a reload, like Destiny
		P->bSprinting = false;
		AActor* Target = FindLungeTarget();
		MeleeTarget = Target;
		if (Target && P->EntrenchLeft <= 0.f) P->StartLunge(Target); // rooted by Entrench: punch in place
	}
	if (!bMeleeing) return;
	// The swing plays alongside the lunge; if the lunge is still travelling when the swing
	// reaches the hit, it waits just short of it and lands the moment you arrive.
	MeleeProgress += DeltaSeconds / FMath::Max(M("melee_time", MeleeTime), 0.05f);
	if (P->IsLunging()) MeleeProgress = FMath::Min(MeleeProgress, MeleeImpactAt - 0.01f);
	if (MeleeProgress >= MeleeImpactAt && !bMeleeLanded)
	{
		bMeleeLanded = true;
		MeleeStrike();
	}
	if (MeleeProgress >= 1.f)
	{
		bMeleeing = false;
		MeleeProgress = 0.f;
	}
}

AActor* UWWOWeaponComponent::FindLungeTarget() const
{
	// The target nearest your crosshair inside the lunge range and angle, or one the swing
	// would hit anyway.
	const AWWOCharacter* P = GetPlayer();
	const FVector Origin = P->EyeLocation();
	const FVector Forward = P->AimForward();
	AActor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (AActor* Actor : UWWOWorldSubsystem::Get(this)->LivingTargets())
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
		for (const FWWOAimPart& Part : Target->GetAimParts())
		{
			const FVector To = Part.Position - Origin;
			const float Distance = (To.Size() - Part.Radius) / WWO::M;
			if (Distance >= BestScore) continue;
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, To.GetSafeNormal()), -1.f, 1.f)));
			const bool bInCone = Distance <= LungeRange && Angle <= LungeAngle;
			const bool bWillHit = Distance <= MeleeReach && Angle <= MeleeAngle;
			if (!(bInCone || bWillHit)) continue;
			const float Score = Angle + Distance * 2.f;
			if (Score >= BestScore) continue;
			FHitResult Check;
			if (!CastRayTo(Part.Position, Check) || Check.GetActor() != Actor) continue;
			Best = Actor;
			BestScore = Score;
		}
	}
	return Best;
}

void UWWOWeaponComponent::MeleeStrike()
{
	// Hit the closest target part within reach and inside the melee angle.
	AWWOCharacter* P = GetPlayer();
	const FVector Origin = P->EyeLocation();
	const FVector Forward = P->AimForward();
	AActor* Best = nullptr;
	FVector BestPoint = FVector::ZeroVector;
	float BestDistance = TNumericLimits<float>::Max();
	const IWWOTargetable* Locked = Cast<IWWOTargetable>(MeleeTarget.Get());
	for (AActor* Actor : UWWOWorldSubsystem::Get(this)->LivingTargets())
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Actor);
		for (const FWWOAimPart& Part : Target->GetAimParts())
		{
			const FVector To = Part.Position - Origin;
			const float Distance = (To.Size() - Part.Radius) / WWO::M;
			if (Distance > MeleeReach || Distance >= BestDistance) continue;
			if (Locked && !Locked->IsDead() && Actor != MeleeTarget.Get()) continue; // the swing lands on the enemy you lunged at
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, To.GetSafeNormal()), -1.f, 1.f)));
			if (Angle > MeleeAngle) continue;
			FHitResult Check;
			if (!CastRayTo(Part.Position, Check) || Check.GetActor() != Actor) continue;
			Best = Actor;
			BestPoint = Check.ImpactPoint;
			BestDistance = Distance;
		}
	}
	if (!Best && Locked && !Locked->IsDead())
	{
		// The lunge got you there: hit it even if it shifted a little off center.
		const FWWOAimPart Near = Locked->GetAimParts().Last();
		if ((FVector::Dist(Origin, Near.Position) - Near.Radius) / WWO::M <= MeleeReach + 0.5f)
		{
			Best = MeleeTarget.Get();
			BestPoint = Near.Position;
		}
	}
	if (!Best) return;
	IWWOTargetable* Target = Cast<IWWOTargetable>(Best);
	const float Boosted = M("melee_damage", MeleeDamage) * P->Abilities->MeleeMultiplier() * P->VeilMeleeMultiplier();
	const bool bKilled = Target->TakeHit(Boosted, false, BestPoint); // melee doesn't crit
	OnHitConfirmed.Broadcast(false, bKilled);
	P->Abilities->OnMeleeLanded(); // Melee Guard
	Kick = 1.f;
	MeleeImpact(Best, bKilled, BestPoint);
}

void UWWOWeaponComponent::MeleeImpact(AActor* TargetActor, bool bKilled, const FVector& Point)
{
	// Everything that makes a landed melee feel heavy. Kills hit harder.
	AWWOCharacter* P = GetPlayer();
	const float Boost = bKilled ? MeleeKillBoost : 1.f;
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFloraData* Flora = Loadout ? Loadout->GetFlora() : nullptr;
	const FLinearColor Color = Flora && Flora->GraftColor.A > 0.f ? Flora->GraftColor : FLinearColor(0.25f, 0.55f, 1.f);
	if (const UWWOAbilityData* Thornfist = P->Abilities->ThornfistActive())
	{
		if (Thornfist->DischargeArcs >= 1.f)
		{
			AWWODischarge::Spawn(GetWorld(), TargetActor, Point, (Point - P->EyeLocation()).GetSafeNormal(), Color,
				FMath::RoundToInt(Thornfist->DischargeArcs), Thornfist->DischargeRadius, Thornfist->DischargeTime);
		}
	}
	if (const UWWOAbilityData* StaticCharge = P->Abilities->ConsumeStaticCharge())
	{
		AWWOChainLightning::Spawn(GetWorld(), StaticCharge, TargetActor, P, Color);
	}
	P->AddRecoil(FMath::DegreesToRadians(MeleeJolt * Boost), 0.f, 0.02f, 1.f, 0.12f); // the view snaps up and settles
	StartPunch(MeleePunchFov * Boost, MeleePunchRoll * Boost, MeleePunchTime);
	Rumble(MeleeRumbleWeak * Boost, MeleeRumbleStrong * Boost, MeleeRumbleTime * Boost);
	if (IWWOTargetable* Target = Cast<IWWOTargetable>(TargetActor))
	{
		FVector Away = TargetActor->GetActorLocation() - P->GetActorLocation();
		Away.Z = 0.f;
		Target->KnockBack(Away.GetSafeNormal() * MeleeKnockback * (bKilled ? MeleeKillKnockback : 1.f) * WWO::M);
	}
	if (AWWOPlayerController* PC = P->GetWWOController()) PC->Hitstop(MeleeHitstop * Boost);
}

// --- Reload, ADS, firing -----------------------------------------------------------------------

void UWWOWeaponComponent::UpdateReload(float DeltaSeconds)
{
	Ammo = FMath::Min(Ammo, MagazineSize()); // in case the slider shrank the magazine
	if (JustPressed((int32)EWWOAction::Reload)) StartReload();
	if (bReloading && GetPlayer()->bSprinting) CancelReload(); // sprinting cancels a reload, like Destiny
	if (!bReloading) return;
	ReloadProgress += DeltaSeconds / FMath::Max(M("reload_time", Weapon->ReloadTime), 0.05f);
	if (ReloadProgress >= ReloadSeated && Ammo < MagazineSize()) Ammo = MagazineSize(); // the new magazine is in
	if (ReloadProgress >= 1.f) CancelReload();
}

void UWWOWeaponComponent::UpdateAds(float DeltaSeconds)
{
	const float Target = IsAiming() ? 1.f : 0.f;
	AdsProgress = WWO::MoveToward(AdsProgress, Target, DeltaSeconds / FMath::Max(M("ads_time", Weapon->AdsTime), 0.01f));
	AdsAmount = WWO::SmoothStep(0.f, 1.f, AdsProgress);
}

bool UWWOWeaponComponent::WantsToFire() const
{
	// Semi-auto weapons need a fresh trigger pull for every shot.
	return Weapon->IsSemiAuto() ? JustPressed((int32)EWWOAction::Fire) : Pressed((int32)EWWOAction::Fire);
}

void UWWOWeaponComponent::UpdateFiring(float DeltaSeconds)
{
	AWWOCharacter* P = GetPlayer();
	Kick = WWO::MoveToward(Kick, 0.f, 12.f * DeltaSeconds);
	FlashTime -= DeltaSeconds;
	bFlashVisible = FlashTime > 0.f; // decided before this frame's shot, like Godot (a new flash shows from the next frame)

	FireCooldown -= DeltaSeconds;
	// Handling: the gun needs a moment to come up after a sprint or mantle. A trigger pull
	// meanwhile fires as soon as it's up.
	if (P->bSprinting || P->IsMantling()) ReadyLeft = M("ready_time", ReadyTime);
	if (ReadyLeft > 0.f)
	{
		if (!(P->bSprinting || P->IsMantling())) ReadyLeft -= DeltaSeconds;
		if (Weapon->IsSemiAuto() && JustPressed((int32)EWWOAction::Fire)) bReadyShotQueued = true;
		FireCooldown = FMath::Max(FireCooldown, 0.f); // don't bank shots while waiting
		return;
	}
	const bool bWants = WantsToFire() || (bReadyShotQueued && Weapon->IsSemiAuto());
	bReadyShotQueued = false;
	if (!bWants || bReloading || bMeleeing || IsSwapping())
	{
		FireCooldown = FMath::Max(FireCooldown, 0.f);
		return;
	}
	if (Weapon->IsSemiAuto() && FireCooldown > 0.f) return; // pulled the trigger before the gun was ready
	while (FireCooldown <= 0.f)
	{
		if (Ammo <= 0)
		{
			StartReload(); // pulling the trigger on empty reloads
			return;
		}
		Shoot();
		Ammo -= 1;
		FireCooldown += 60.f / FMath::Max(Weapon->FireRate, 1.f);
		if (Weapon->IsSemiAuto()) break;
	}
	if (Ammo <= 0) StartReload(); // ran dry: reload automatically
}

void UWWOWeaponComponent::Shoot()
{
	AWWOCharacter* P = GetPlayer();
	Kick = 1.f;
	FlashTime = MuzzleFlashTime;
	const int32 PelletCount = FMath::Max(1, FMath::RoundToInt(Weapon->Pellets));
	// Hip-fire spread shrinks to nothing as you aim in; pellets scatter around that aim.
	const FVector Aim = WWO::RandomInCone(P->AimForward(), FMath::DegreesToRadians(Weapon->HipSpread) * (1.f - AdsAmount));
	struct FTotal { float Damage = 0.f; bool bCritical = false; FVector Point; };
	TMap<AActor*, FTotal> Totals; // pellets add up into one hit per target
	for (int32 i = 0; i < PelletCount; ++i)
	{
		const FVector Dir = PelletCount > 1 ? WWO::RandomInCone(Aim, FMath::DegreesToRadians(Weapon->PelletSpread)) : Aim;
		FHitResult Hit;
		bool bHit = CastRayTo(P->EyeLocation() + Dir * Weapon->FireRange * WWO::M, Hit);
		if (PelletCount == 1 && (!bHit || !Cast<IWWOTargetable>(Hit.GetActor())))
		{
			FHitResult Magnetized;
			if (P->AimAssist->Magnetize(Magnetized)) // near-miss? bend it onto the target
			{
				Hit = Magnetized;
				bHit = true;
			}
		}
		if (!bHit) continue;
		AWWOFXActor::ImpactMark(GetWorld(), Hit.ImpactPoint);
		IWWOTargetable* Target = Cast<IWWOTargetable>(Hit.GetActor());
		if (!Target || Target->IsDead()) continue;
		const bool bCritical = Target->IsCriticalComponent(Hit.GetComponent());
		FTotal& Entry = Totals.FindOrAdd(Hit.GetActor());
		if (Entry.Damage == 0.f) Entry.Point = Hit.ImpactPoint;
		Entry.Damage += Weapon->Damage * (bCritical ? Weapon->CritMultiplier : 1.f);
		Entry.bCritical |= bCritical;
	}
	bool bAnyCritical = false, bAnyKilled = false;
	for (const auto& Pair : Totals)
	{
		if (IWWOTargetable* Target = Cast<IWWOTargetable>(Pair.Key))
		{
			bAnyKilled = Target->TakeHit(Pair.Value.Damage, Pair.Value.bCritical, Pair.Value.Point) || bAnyKilled;
			bAnyCritical |= Pair.Value.bCritical;
		}
	}
	if (Totals.Num() > 0) OnHitConfirmed.Broadcast(bAnyCritical, bAnyKilled);
	// Recoil kicks the actual view, so you have to pull down to stay on target.
	const bool bSynced = Weapon->RecoilRecoverySync >= 0.5f;
	float RecoveryTime = Weapon->RecoilRecoveryTime;
	if (bSynced) RecoveryTime = 60.f / FMath::Max(Weapon->FireRate, 1.f) - Weapon->RecoilKickTime + Weapon->RecoilRecoveryDelay;
	P->AddRecoil(FMath::DegreesToRadians(Weapon->RecoilUp), FMath::DegreesToRadians(FMath::FRandRange(-Weapon->RecoilSide, Weapon->RecoilSide)),
		Weapon->RecoilKickTime, Weapon->RecoilRecovery, RecoveryTime, bSynced ? Weapon->RecoilRecoveryCurve : 0.f);
	Rumble(Weapon->RumbleWeak, Weapon->RumbleStrong, Weapon->RumbleTime);
	StartPunch(Weapon->PunchFov, Weapon->PunchRoll * (FMath::FRand() < 0.5f ? 1.f : -1.f), Weapon->PunchTime);
}

// --- View --------------------------------------------------------------------------------------

void UWWOWeaponComponent::UpdateFov()
{
	UCameraComponent* Camera = GetPlayer()->Camera;
	const float Punch = PunchAmount() * PunchScale;
	// The punch widens the hip FOV, so aiming in (which divides by zoom) scales it down too.
	const float Fov = FMath::Clamp(HipFov + PunchFovValue * Punch, 1.f, 170.f);
	const float Half = FMath::Tan(FMath::DegreesToRadians(Fov) * 0.5f) / FMath::Lerp(1.f, Weapon->Zoom, AdsAmount);
	Camera->SetFieldOfView(FMath::RadiansToDegrees(FMath::Atan(Half) * 2.f));
	const bool bArmOut = MeleeArm.Root && MeleeArm.Root->IsVisible();
	Camera->FirstPersonFieldOfView = bArmOut ? MeleeArmFov : Weapon->ViewmodelFov;
	// Scoped weapons hide the gun once the scope is up (the HUD draws the scope).
	const bool bShowModel = !(Weapon->ScopeOverlay >= 0.5f && AdsAmount > 0.85f);
	for (const auto& Pair : Parts)
	{
		if (Pair.Value) Pair.Value->SetVisibility(bShowModel && (Pair.Key != "MuzzleFlash" || bFlashVisible));
	}
}

void UWWOWeaponComponent::UpdateReticleTarget()
{
	const AWWOCharacter* P = GetPlayer();
	FHitResult Hit;
	ReticleTarget = nullptr;
	if (CastRayTo(P->EyeLocation() + P->AimForward() * Weapon->FireRange * WWO::M, Hit))
	{
		const IWWOTargetable* Target = Cast<IWWOTargetable>(Hit.GetActor());
		if (Target && !Target->IsDead()) ReticleTarget = Hit.GetActor();
	}
	bTargetUnderReticle = ReticleTarget.IsValid();
	if (UStaticMeshComponent* Dot = Part("SightDot"))
	{
		if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Dot->GetMaterial(0)))
		{
			Mat->SetVectorParameterValue("Color", bTargetUnderReticle ? SightOnTargetColor : SightColor);
		}
	}
}

void UWWOWeaponComponent::UpdatePose(float DeltaSeconds)
{
	AWWOCharacter* P = GetPlayer();
	const float Settle = 1.f - 0.75f * AdsAmount; // aiming steadies the gun

	// Sway: the gun lags behind your turning, then catches up.
	const FVector2D TurnSpeed = LookDelta / FMath::Max(DeltaSeconds, 0.001f);
	LookDelta = FVector2D::ZeroVector;
	// Look amounts are + right / + up; the Godot pose convention has + down for pitch.
	FVector2D Target = FVector2D(TurnSpeed.X, -TurnSpeed.Y) * SwayPerTurnSpeed;
	Target = Target.GetClampedToMaxSize(SwayMax) * Weapon->Sway * Settle;
	SwayValue = FMath::Lerp(SwayValue, Target, FMath::Min(10.f * DeltaSeconds, 1.f));

	// Action poses fade in and out; aiming mostly cancels them.
	SprintPose = WWO::MoveToward(SprintPose, P->bSprinting ? 1.f : 0.f, 5.f * DeltaSeconds);
	SlidePose = WWO::MoveToward(SlidePose, P->bSliding ? 1.f : 0.f, 6.f * DeltaSeconds);
	const float HipOnly = 1.f - AdsAmount;
	const float SprintAmount = WWO::SmoothStep(0.f, 1.f, SprintPose) * HipOnly;
	const float SlideAmount = WWO::SmoothStep(0.f, 1.f, SlidePose) * (1.f - 0.8f * AdsAmount);

	// Air: the gun lags behind vertical motion.
	float AirTarget = 0.f;
	if (!P->IsOnFloor() && !P->IsLunging())
	{
		AirTarget = FMath::Clamp(-P->GetVelocity().Z / WWO::M * AirLagPerSpeed, -AirLagMax, AirLagMax);
	}
	AirLag = FMath::Lerp(AirLag, AirTarget, FMath::Min(8.f * DeltaSeconds, 1.f));
	const FVector AirOffset = FVector(0.f, AirLag + P->LandOffset * 0.6f, 0.f) * Settle;

	// Bob: follows the player's step timing. Sprinting swings it much harder.
	const FVector Swing(FMath::Cos(P->BobTime * 0.5f), -FMath::Abs(FMath::Sin(P->BobTime)), 0.f);
	FVector Bob = Swing * FVector(0.012f, 0.01f, 0.f) * Weapon->GunBob * (1.f - 0.85f * AdsAmount);
	Bob += Swing * FVector(0.03f, 0.025f, 0.f) * Weapon->GunBob * SprintAmount;
	Bob *= P->BobWeight;
	const float BobRoll = FMath::Cos(P->BobTime * 0.5f) * 0.08f * Weapon->GunBob * SprintAmount * P->BobWeight;

	FVector Rot(SwayValue.Y + AirLag * 2.f * Settle, SwayValue.X, BobRoll);
	Rot += SprintRotation * SprintAmount + FVector(FMath::DegreesToRadians(Weapon->SlideTilt), 0.f, 0.f) * SlideAmount;
	const float KickNow = Kick * Weapon->GunKick * (1.f - 0.6f * AdsAmount); // aiming tames the jolt
	Rot.X += 0.06f * KickNow;
	FVector Pos = FMath::Lerp(Weapon->HipPosition, Weapon->AdsPosition, AdsAmount) + Bob + AirOffset;
	Pos += SprintOffset * SprintAmount + SlideOffset * SlideAmount;
	// Mantling: hands are busy, so the gun dips out of the way.
	MantlePose = WWO::MoveToward(MantlePose, P->IsMantling() ? 1.f : 0.f, 8.f * DeltaSeconds);
	const float MantleAmount = WWO::SmoothStep(0.f, 1.f, MantlePose);
	Pos += MantleOffset * MantleAmount;
	Rot += MantleRotation * MantleAmount;
	// Grenade throw: quick dip down, then back up.
	ThrowTime = FMath::Max(ThrowTime - DeltaSeconds, 0.f);
	const float ThrowAmount = ThrowTime > 0.f ? FMath::Sin(UE_PI * ThrowTime / ThrowPoseLength()) : 0.f;
	Pos += ThrowOffset * ThrowAmount;
	Rot += ThrowRotation * ThrowAmount;
	Pos.Z += 0.035f * KickNow;
	// Weapon swap: dip down and back up.
	SwapLeft = FMath::Max(SwapLeft - DeltaSeconds, 0.f);
	const float SwapAmount = SwapLeft > 0.f ? FMath::Sin(UE_PI * SwapLeft / SwapLength) : 0.f;
	Pos += SwapOffset * SwapAmount;
	Rot += SwapRotation * SwapAmount;
	// Reload: tip up and roll so the magazine well faces you; the magazine drops out and back.
	const float RP = bReloading ? ReloadProgress : 0.f;
	const float Tilt = WWO::SmoothStep(0.f, 0.2f, RP) * (1.f - WWO::SmoothStep(0.8f, 1.f, RP));
	Rot.X += ReloadTilt * Tilt;
	Rot.Z += ReloadRoll * Tilt;
	Pos += ReloadLift * Tilt;
	const float MagOut = WWO::SmoothStep(0.2f, 0.4f, RP) * (1.f - WWO::SmoothStep(0.5f, ReloadSeated, RP));
	if (UStaticMeshComponent* Mag = Part("Magazine")) Mag->SetRelativeLocation(G2U(MagRest + FVector(0.f, -ReloadMagDrop * MagOut, 0.f)));
	// Melee: the gun drops out of the way while the arm swings.
	const float MP = bMeleeing ? MeleeProgress : 0.f;
	const float Drop = WWO::SmoothStep(0.f, 0.12f, MP) * (1.f - WWO::SmoothStep(0.72f, 0.97f, MP));
	Pos += MeleeGunDropOffset * Drop;
	Rot += MeleeGunDropRotation * Drop;

	SetRelativeLocationAndRotation(G2U(Pos), GRot(Rot));

	// The punching arm.
	const float MeleeT = bMeleeing ? MeleeAnimTime(MeleeProgress) : 0.f;
	PlaceArm(MeleeArm, MeleeT, MeleeArmYaw, MeleeArmPitch, MeleeArmPush, MeleeArmDrop * MeleeRetracting(MeleeProgress),
		bMeleeing && MeleeT < PunchEnd);
	// The throwing arm.
	ThrowClock += DeltaSeconds;
	const float ThrowT = ThrowAnimTime(ThrowClock);
	PlaceArm(ThrowArm, ThrowT, ThrowArmYaw, ThrowArmPitch, ThrowArmPush, 0.f, ThrowT < ThrowAnimEnd);
	ThrowArm.Ball->SetVisibility(ThrowArm.Root->IsVisible() && ThrowClock < ThrowRelease);
	if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(ThrowArm.Ball->GetMaterial(0)))
	{
		Mat->SetVectorParameterValue("Color", ThrowColor);
		Mat->SetScalarParameterValue("Glow", 3.f);
	}
}

float UWWOWeaponComponent::MeleeAnimTime(float Progress) const
{
	// Chamber the fist, throw it so it's fully out exactly when the hit lands, then recover.
	if (Progress < MeleeLungeHold) return FMath::Lerp(0.f, PunchChambered, WWO::SmoothStep(0.f, 1.f, Progress / MeleeLungeHold));
	if (Progress < MeleeImpactAt)
	{
		const float T = (Progress - MeleeLungeHold) / (MeleeImpactAt - MeleeLungeHold);
		return FMath::Lerp(PunchChambered, PunchContact, T * T); // the strike snaps in
	}
	const float SinceHit = (Progress - MeleeImpactAt) * M("melee_time", MeleeTime);
	if (SinceHit < MeleeArmHold) return PunchContact;
	const float T = FMath::Clamp((SinceHit - MeleeArmHold) / FMath::Max(MeleeArmRetract, 0.01f), 0.f, 1.f);
	return FMath::Lerp(PunchContact, PunchEnd + 0.01f, WWO::SmoothStep(0.f, 1.f, T));
}

float UWWOWeaponComponent::MeleeRetracting(float Progress) const
{
	const float SinceHit = (Progress - MeleeImpactAt) * M("melee_time", MeleeTime);
	return FMath::Clamp((SinceHit - MeleeArmHold) / FMath::Max(MeleeArmRetract, 0.01f), 0.f, 1.f);
}

float UWWOWeaponComponent::ThrowAnimTime(float Clock) const
{
	// Wind up (most of the time), whip over the top so the release lands at ThrowRelease,
	// then follow through and drop out of view.
	const float Release = FMath::Max(ThrowRelease, 0.01f);
	if (Clock < Release)
	{
		const float Windup = Release * 0.7f;
		if (Clock < Windup) return FMath::Lerp(ThrowAnimStart, ThrowAnimCocked, WWO::SmoothStep(0.f, 1.f, Clock / Windup));
		return FMath::Lerp(ThrowAnimCocked, ThrowAnimRelease, (Clock - Windup) / (Release - Windup));
	}
	const float T = (Clock - Release) / FMath::Max(ThrowFollow, 0.01f);
	if (T >= 1.f) return ThrowAnimEnd;
	return FMath::Lerp(ThrowAnimRelease, ThrowAnimEnd, 1.f - (1.f - T) * (1.f - T));
}

void UWWOWeaponComponent::PlaceArm(FWWOFPArm& Arm, float AnimT, float Yaw, float PitchDeg, float Push, float Drop, bool bVisible)
{
	if (!Arm.Root) return;
	Arm.Root->SetVisibility(bVisible, true);
	if (!bVisible) return;
	FVector Fist, Shoulder;
	if (Arm.bLeft)
	{
		static const float Times[] = { ThrowAnimStart, ThrowAnimCocked, ThrowAnimRelease, ThrowAnimEnd };
		Fist = PathAt(ThrowKeys, Times, 4, AnimT);
		Shoulder = LeftShoulder;
	}
	else
	{
		static const float Times[] = { 0.f, PunchChambered, PunchContact, PunchEnd };
		Fist = PathAt(PunchKeys, Times, 4, AnimT);
		Shoulder = RightShoulder;
	}
	Fist = Place(Fist, Yaw, PitchDeg, Push) + FVector(0.f, -Drop, 0.f);
	Shoulder = Place(Shoulder, Yaw, PitchDeg, Push);
	const FVector Elbow = FMath::Lerp(Shoulder, Fist, 0.5f) + FVector(Arm.bLeft ? -0.08f : 0.08f, -0.1f, 0.f);
	auto Limb = [](UStaticMeshComponent* C, const FVector& A, const FVector& B, float Thick)
	{
		const FVector UA = G2U(A), UB = G2U(B);
		const float Length = FVector::Dist(UA, UB);
		const FRotator Rot = FRotationMatrix::MakeFromZ((UB - UA).GetSafeNormal()).Rotator(); // cylinder runs along z
		C->SetRelativeLocationAndRotation((UA + UB) * 0.5f, Rot);
		C->SetRelativeScale3D(FVector(Thick, Thick, Length / 100.f));
	};
	Limb(Arm.Upper, Shoulder, Elbow, 0.11f);
	Limb(Arm.Fore, Elbow, Fist, 0.09f);
	const FVector UFist = G2U(Fist);
	Arm.Fist->SetRelativeLocationAndRotation(UFist, FRotationMatrix::MakeFromX((UFist - G2U(Elbow)).GetSafeNormal()).Rotator());
	Arm.Fist->SetRelativeScale3D(FVector(0.11f, 0.1f, 0.1f));
	if (!Arm.bLeft) Arm.Ball->SetVisibility(false); // only the throwing hand holds a grenade
	Arm.Ball->SetRelativeLocation(UFist + FVector(4.f, 0.f, 4.f));
}
