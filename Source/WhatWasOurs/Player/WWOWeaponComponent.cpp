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
#include "Player/WWOFPArmPaths.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	// The punch (Melee_Hook, seconds into it): fist chambered, landed, starts pulling back
	// (fp_arm.gd PUNCH_TIMING), and its length.
	constexpr float PunchChambered = 0.2f, PunchContact = 0.264f, PunchRetract = 0.3f, PunchEnd = WWOArmPaths::MeleeHookLength;
	// The overhand throw (left arm: OverhandThrow, mirrored), seconds into it.
	constexpr float ThrowAnimStart = 0.05f, ThrowAnimCocked = 0.2f, ThrowAnimRelease = 0.363f, ThrowAnimEnd = 0.42f;

	struct FGunPart { const TCHAR* Name; int32 Kind; FVector Size; FVector Pos; FVector RotDeg; FLinearColor Color; float Glow; };
	enum { Box, Cyl, Sphere };
	const FLinearColor Dark(0.12f, 0.12f, 0.13f), Steel(0.22f, 0.24f, 0.28f), Orange(0.95f, 0.5f, 0.1f), Wood(0.38f, 0.22f, 0.12f),
		Bronze(0.48f, 0.34f, 0.2f), Gold(0.78f, 0.6f, 0.25f), Olive(0.22f, 0.27f, 0.2f), Flash(1.f, 0.75f, 0.3f), White(0.95f, 0.95f, 0.95f), LensBlue(0.3f, 0.6f, 0.9f);

	// Config/WeaponModels.json: which mesh each weapon uses and where its mod parts attach. Read
	// on every rebuild, so it can be retuned with a restart and no build.
	TSharedPtr<FJsonObject> ModelConfig(FName WeaponId)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("WeaponModels.json")))) return nullptr;
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return nullptr;
		const TSharedPtr<FJsonObject>* Weapons;
		const TSharedPtr<FJsonObject>* Model;
		if (Json->TryGetObjectField(TEXT("weapons"), Weapons) && (*Weapons)->TryGetObjectField(WeaponId.ToString(), Model)) return *Model;
		return nullptr;
	}

	// A 3-number array field (or one number for all three), else Default.
	FVector JsonVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector& Default = FVector::ZeroVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* A;
		if (O.IsValid() && O->TryGetArrayField(Field, A) && A->Num() >= 3) return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
		if (O.IsValid() && O->TryGetArrayField(Field, A) && A->Num() == 2) return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), 0.0);
		double N;
		if (O.IsValid() && O->TryGetNumberField(Field, N)) return FVector(N);
		return Default;
	}

	FLinearColor JsonColor(const TSharedPtr<FJsonObject>& O)
	{
		FString Name;
		if (O->TryGetStringField(TEXT("color"), Name))
		{
			static const TMap<FString, FLinearColor> Named = {
				{ TEXT("dark"), FLinearColor(0.12f, 0.12f, 0.13f) }, { TEXT("steel"), FLinearColor(0.22f, 0.24f, 0.28f) },
				{ TEXT("orange"), FLinearColor(0.95f, 0.5f, 0.1f) }, { TEXT("gold"), FLinearColor(0.78f, 0.6f, 0.25f) },
				{ TEXT("olive"), FLinearColor(0.22f, 0.27f, 0.2f) }, { TEXT("white"), FLinearColor(0.95f, 0.95f, 0.95f) },
				{ TEXT("cyan"), FLinearColor(0.2f, 0.8f, 1.f) }, { TEXT("green"), FLinearColor(0.3f, 1.f, 0.4f) },
				{ TEXT("red"), FLinearColor(1.f, 0.2f, 0.15f) }, { TEXT("yellow"), FLinearColor(1.f, 0.8f, 0.2f) },
				{ TEXT("purple"), FLinearColor(0.6f, 0.3f, 1.f) },
			};
			if (const FLinearColor* C = Named.Find(Name.ToLower())) return *C;
		}
		const FVector V = JsonVec(O, TEXT("color"), FVector(0.22, 0.24, 0.28));
		return FLinearColor(V.X, V.Y, V.Z);
	}

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

	// One arm joint (0 shoulder, 1 elbow, 2 wrist, 3 knuckles) at Seconds into a baked path,
	// in Godot model space relative to the eyes (see WWOFPArmPaths.h).
	template <int32 N>
	FVector ArmJoint(const FVector (&Path)[N][4], int32 Joint, float Seconds)
	{
		const float F = FMath::Clamp(Seconds * WWOArmPaths::FrameRate, 0.f, float(N - 1));
		const int32 I = FMath::Min(FMath::FloorToInt32(F), N - 2);
		return FMath::Lerp(Path[I][Joint], Path[I + 1][Joint], F - I);
	}

	// fp_arm.gd place() + pose(): the rig turned to face -z (mirrored for the left arm), aimed by
	// Basis.from_euler(pitch, yaw) (yaw applied after pitch), with the eyes at the camera,
	// pushed forward and dropped. Godot camera space out.
	FVector PlaceJoint(FVector P, bool bMirror, float YawDeg, float PitchDeg, float Push, float Drop)
	{
		if (bMirror) P.X = -P.X;
		P = FVector(-P.X, P.Y, -P.Z); // Basis(UP, PI)
		const float X = FMath::DegreesToRadians(PitchDeg), Y = FMath::DegreesToRadians(YawDeg);
		P = FVector(P.X, P.Y * FMath::Cos(X) - P.Z * FMath::Sin(X), P.Y * FMath::Sin(X) + P.Z * FMath::Cos(X));
		P = FVector(P.X * FMath::Cos(Y) + P.Z * FMath::Sin(Y), P.Y, -P.X * FMath::Sin(Y) + P.Z * FMath::Cos(Y));
		return P + FVector(0.f, -Drop, -Push);
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
			if (Loadout->GetWeapon() != BaseWeapon) Equip(Loadout->GetWeapon());
			else
			{
				// New mods: a fresh, full magazine (it may have grown), like re-equipping.
				FString Signature;
				for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
				{
					if (const UWWOModData* Mod = Loadout->GetMod(Slot)) Signature += Mod->Id.ToString() + TEXT(",");
				}
				if (Signature != ModSignature)
				{
					ModSignature = Signature;
					RefreshStats();
					BuildModel(); // the mods' parts
					Ammo = MagazineSize();
					HitStreak = StaticHits = 0;
					bOverpressure = true;
				}
			}
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
	BaseWeapon = NewWeapon;
	Weapon = DuplicateObject<UWWOWeaponData>(NewWeapon, this);
	HitStreak = StaticHits = 0;
	bOverpressure = true;
	AimHeld = 0.f;
	ModSignature.Reset();
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
		{
			if (const UWWOModData* Mod = L->GetMod(Slot)) ModSignature += Mod->Id.ToString() + TEXT(",");
		}
	}
	RefreshStats();
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
	AdsHiddenParts.Reset();
	// One part. Pos and Rot are in the model's space: cm, x forward, y right, z up, origin at the sight.
	auto AddPart = [this](FName Name, UStaticMesh* Mesh, const FVector& Scale, const FVector& Pos, const FRotator& Rot,
		const FLinearColor* Color, float Glow) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(GetOwner());
		Comp->SetupAttachment(ModelRoot);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
		Comp->SetStaticMesh(Mesh);
		if (Color) Comp->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Solid")));
		Comp->SetRelativeScale3D(Scale);
		Comp->SetRelativeLocationAndRotation(Pos, Rot);
		Comp->RegisterComponent();
		if (Color)
		{
			if (UMaterialInstanceDynamic* Mat = Comp->CreateAndSetMaterialInstanceDynamic(0))
			{
				Mat->SetVectorParameterValue("Color", *Color);
				Mat->SetScalarParameterValue("Glow", Glow);
			}
		}
		Parts.Add(Name, Comp);
		return Comp;
	};
	// A shape from JSON: "shape" box (size = length, width, height), cyl (radius, length, along x)
	// or sphere (radius), all in cm; at Anchor + "offset", turned by "rot" (pitch, yaw, roll).
	auto AddJsonPart = [&](FName Name, const TSharedPtr<FJsonObject>& O, const FVector& Anchor)
	{
		FString Shape = TEXT("box");
		O->TryGetStringField(TEXT("shape"), Shape);
		const FVector Size = JsonVec(O, TEXT("size"), FVector(2.0));
		const FVector Rot = JsonVec(O, TEXT("rot"));
		FRotator R(Rot.X, Rot.Y, Rot.Z);
		UStaticMesh* Mesh = WWOFX::Cube();
		FVector Scale = Size / 100.0;
		if (Shape == TEXT("cyl"))
		{
			Mesh = WWOFX::Cylinder(); // stands along z: lay it along x
			Scale = FVector(Size.X * 2.0, Size.X * 2.0, Size.Y) / 100.0;
			R = (R.Quaternion() * FRotator(-90.f, 0.f, 0.f).Quaternion()).Rotator();
		}
		else if (Shape == TEXT("sphere"))
		{
			Mesh = WWOFX::Sphere();
			Scale = FVector(Size.X * 2.0 / 100.0);
		}
		double Glow = 0.0;
		O->TryGetNumberField(TEXT("glow"), Glow);
		const FLinearColor Color = JsonColor(O);
		AddPart(Name, Mesh, Scale, Anchor + JsonVec(O, TEXT("offset")), R, &Color, (float)Glow);
		bool bHideAds = false;
		if (O->TryGetBoolField(TEXT("hide_ads"), bHideAds) && bHideAds) AdsHiddenParts.Add(Name);
	};

	// A mesh model (Config/WeaponModels.json) replaces the box model, keeping its sight dot, muzzle
	// flash and any parts it lists in "keep". Without one, the Godot box model.
	const TSharedPtr<FJsonObject> Model = ModelConfig(Weapon->Id);
	FString MeshPath;
	UStaticMesh* BodyMesh = Model.IsValid() && Model->TryGetStringField(TEXT("mesh"), MeshPath) ? LoadObject<UStaticMesh>(nullptr, *MeshPath) : nullptr;
	TSet<FString> Keep = { TEXT("SightDot"), TEXT("MuzzleFlash") };
	TArray<FString> KeepList;
	if (BodyMesh && Model->TryGetStringArrayField(TEXT("keep"), KeepList)) Keep.Append(KeepList);
	for (const FGunPart& P : ModelParts(Weapon->Id))
	{
		if (BodyMesh && !Keep.Contains(P.Name)) continue;
		FRotator Rot = GRot(Deg(P.RotDeg.X, P.RotDeg.Y, P.RotDeg.Z));
		UStaticMesh* Mesh = WWOFX::Sphere();
		FVector Scale(P.Size.X * 2.f);
		if (P.Kind == Box)
		{
			Mesh = WWOFX::Cube();
			Scale = FVector(P.Size.Z, P.Size.X, P.Size.Y);
		}
		else if (P.Kind == Cyl)
		{
			Mesh = WWOFX::Cylinder();
			Scale = FVector(P.Size.X * 2.f, P.Size.X * 2.f, P.Size.Y);
			Rot = Rot + FRotator(90.f, 0.f, 0.f); // the engine cylinder stands along z: lay it along the barrel
		}
		FVector Pos = G2U(P.Pos);
		if (BodyMesh && FName(P.Name) == "MuzzleFlash") Pos = JsonVec(Model, TEXT("offset")) + JsonVec(Model, TEXT("muzzle"), Pos);
		UStaticMeshComponent* Comp = AddPart(P.Name, Mesh, Scale, Pos, Rot, &P.Color, P.Glow);
		if (FName(P.Name) == "MuzzleFlash") Comp->SetVisibility(false);
		if (FName(P.Name) == "Magazine") MagRest = P.Pos;
	}
	TMap<FString, FVector> Anchors;
	if (BodyMesh)
	{
		const FVector Rot = JsonVec(Model, TEXT("rot"));
		AddPart("Body", BodyMesh, JsonVec(Model, TEXT("scale"), FVector(1.0)), JsonVec(Model, TEXT("offset")), FRotator(Rot.X, Rot.Y, Rot.Z), nullptr, 0.f);
		const TArray<TSharedPtr<FJsonValue>>* Extras;
		if (Model->TryGetArrayField(TEXT("parts"), Extras))
		{
			for (int32 i = 0; i < Extras->Num(); ++i)
			{
				if ((*Extras)[i]->AsObject().IsValid()) AddJsonPart(FName(*FString::Printf(TEXT("Extra%d"), i)), (*Extras)[i]->AsObject(), JsonVec(Model, TEXT("offset")));
			}
		}
		const TSharedPtr<FJsonObject>* AnchorObj;
		if (Model->TryGetObjectField(TEXT("anchors"), AnchorObj))
		{
			for (const auto& Pair : (*AnchorObj)->Values)
			{
				const FString Key(Pair.Key);
				Anchors.Add(Key, JsonVec(Model, TEXT("offset")) + JsonVec(*AnchorObj, *Key)); // anchors move with the mesh
			}
		}
	}

	// The equipped mods' parts, each at its slot's anchor (or the part's own "at").
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
		{
			const UWWOModData* Mod = L->GetMod(Slot);
			if (!Mod) continue;
			for (int32 i = 0; i < Mod->VisualParts.Num(); ++i)
			{
				const TSharedPtr<FJsonObject>& O = Mod->VisualParts[i];
				FString At = Slot.ToString();
				O->TryGetStringField(TEXT("at"), At);
				const FVector* Anchor = Anchors.Find(At);
				AddJsonPart(FName(*FString::Printf(TEXT("Mod_%s_%d"), *Slot.ToString(), i)), O, Anchor ? *Anchor : FVector::ZeroVector);
			}
		}
	}

	// Sight height (Settings slider): everything but the sight dot moves down, so the dot sits
	// higher off the gun when aiming.
	BuiltSightHeight = Weapon->SightHeight;
	ModelShift = FVector(0.f, 0.f, -BuiltSightHeight);
	for (auto& Pair : Parts)
	{
		if (Pair.Key != "SightDot" && Pair.Value) Pair.Value->AddRelativeLocation(ModelShift);
	}
}

void UWWOWeaponComponent::BuildArm(FWWOFPArm& Arm, bool bLeft)
{
	Arm.bLeft = bLeft;
	Arm.Root = NewObject<USceneComponent>(GetOwner());
	Arm.Root->SetupAttachment(GetAttachParent()); // the camera
	Arm.Root->RegisterComponent();
	// The mannequin, posed bone by bone each frame. Mirrored (the left arm) by a flipped scale.
	USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	Arm.Mesh = NewObject<UPoseableMeshComponent>(GetOwner());
	Arm.Mesh->SetupAttachment(Arm.Root);
	Arm.Mesh->SetSkinnedAssetAndUpdate(Manny);
	Arm.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Arm.Mesh->SetCastShadow(false);
	Arm.Mesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	Arm.Mesh->SetBoundsScale(2.f); // the posed arm reaches well outside the reference pose's bounds
	Arm.Mesh->SetRelativeScale3D(FVector(1.f, bLeft ? -1.f : 1.f, 1.f));
	Arm.Mesh->RegisterComponent();
	for (int32 i = 0; i < Arm.Mesh->GetNumMaterials(); ++i)
	{
		Arm.Mesh->SetMaterial(i, WWOFX::Material(TEXT("M_WWO_Solid"))); // fp_arm.gdshader: flat color, roughness 0.6
		Arm.Mesh->CreateAndSetMaterialInstanceDynamic(i);
	}
	const TCHAR* AnimPath = bLeft ? TEXT("/Game/WWO/Anims/UAL/A_OverhandThrow.A_OverhandThrow") : TEXT("/Game/WWO/Anims/UAL/A_Melee_Hook.A_Melee_Hook");
	Arm.Anim = LoadObject<UAnimSequence>(nullptr, AnimPath);
	// Every bone is posed each frame (parents first): the right arm from the animation, the rest
	// of the body shrunk away into the shoulder (fp_arm.gd hides it in its shader instead).
	if (Manny && Manny->GetSkeleton())
	{
		const FReferenceSkeleton& Ref = Manny->GetSkeleton()->GetReferenceSkeleton();
		const int32 Upper = Ref.FindBoneIndex("upperarm_r");
		for (int32 b = 0; b < Ref.GetNum(); ++b)
		{
			const int32 Parent = Ref.GetParentIndex(b);
			Arm.Bones.Add(b);
			Arm.Shown.Add(b == Upper || (Parent != INDEX_NONE && Arm.Shown[Parent]));
		}
	}
	Arm.Ball = WWOFX::AddShape(GetOwner(), Arm.Root, WWOFX::Sphere(), TEXT("M_WWO_Solid"), FLinearColor::White);
	Arm.Ball->SetCastShadow(false);
	Arm.Ball->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	Arm.Ball->MarkRenderStateDirty();
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
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFramePartData* ArmsPart = Loadout ? Loadout->GetFramePart("arms") : nullptr;
	const FLinearColor Skin = ArmsPart ? ArmsPart->BodyColor(Base) : Base;
	for (FWWOFPArm* Arm : { &MeleeArm, &ThrowArm })
	{
		if (!Arm->Root) continue;
		// Godot's arm is the robot's, all in the skin color (its texture shading it). The mannequin's
		// forearm and hand are mostly its trim (slot 1), so the trim takes a darker shade of the skin
		// rather than the joint color, or the arm would read as the joint color.
		for (int32 i = 0; Arm->Mesh && i < Arm->Mesh->GetNumMaterials(); ++i)
		{
			if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Arm->Mesh->GetMaterial(i))) Mat->SetVectorParameterValue("Color", i == 0 ? Skin : Skin * 0.55f);
		}
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
	AdrenalLeft = FMath::Max(AdrenalLeft - DeltaSeconds, 0.f);
	HammerLeft = FMath::Max(HammerLeft - DeltaSeconds, 0.f);
	RefreshStats();
	if (!FMath::IsNearlyEqual(Weapon->SightHeight, BuiltSightHeight)) BuildModel(); // the slider moved
	UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (Loadout && JustPressed((int32)EWWOAction::WeaponNext)) Loadout->CycleWeapon(1);
	else if (Loadout && JustPressed((int32)EWWOAction::WeaponPrev)) Loadout->CycleWeapon(-1);
	ConfigToastLeft = FMath::Max(ConfigToastLeft - DeltaSeconds, 0.f);
	if (Loadout && JustPressed((int32)EWWOAction::NextConfig))
	{
		const int32 Index = Loadout->CycleConfig();
		ConfigToast = Index == INDEX_NONE ? FString(TEXT("No saved configs (save one in the character menu)"))
			: Loadout->GetConfigs()[Index].Name + TEXT(": ") + UWWOLoadoutSubsystem::ConfigSummary(Loadout->GetConfigs()[Index], Loadout);
		ConfigToastLeft = 2.5f;
	}
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
	const float Adrenal = AdrenalLeft > 0.f ? 1.4f : 1.f; // Adrenal: reloads 40% faster after a kill
	ReloadProgress += DeltaSeconds * Adrenal / FMath::Max(M("reload_time", Weapon->ReloadTime), 0.05f);
	if (ReloadProgress >= ReloadSeated && Ammo < MagazineSize())
	{
		Ammo = MagazineSize(); // the new magazine is in
		bOverpressure = true;
	}
	if (ReloadProgress >= 1.f) CancelReload();
}

void UWWOWeaponComponent::UpdateAds(float DeltaSeconds)
{
	const float Target = IsAiming() ? 1.f : 0.f;
	AdsProgress = WWO::MoveToward(AdsProgress, Target, DeltaSeconds / FMath::Max(M("ads_time", Weapon->AdsTime), 0.01f));
	AdsAmount = WWO::SmoothStep(0.f, 1.f, AdsProgress);
	AimHeld = AdsProgress >= 1.f ? AimHeld + DeltaSeconds : 0.f; // Patience
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
	// Perks that power up just this shot.
	float ShotScale = 1.f, CritBonus = 0.f;
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		if (L->HasPerk("overpressure") && bOverpressure) ShotScale *= 1.5f;
		if (L->HasPerk("final_round") && Ammo == 1) ShotScale *= 2.f; // Ammo drops after the shot
		if (L->HasPerk("patience") && AimHeld >= 0.6f) CritBonus = 0.6f;
	}
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
		Entry.Damage += Weapon->Damage * ShotScale * (bCritical ? Weapon->CritMultiplier + CritBonus : 1.f);
		Entry.bCritical |= bCritical;
	}
	bool bAnyCritical = false, bAnyKilled = false;
	int32 Kills = 0;
	AActor* FirstHit = nullptr;
	for (const auto& Pair : Totals)
	{
		if (IWWOTargetable* Target = Cast<IWWOTargetable>(Pair.Key))
		{
			const bool bKilled = Target->TakeHit(Pair.Value.Damage, Pair.Value.bCritical, Pair.Value.Point);
			bAnyKilled |= bKilled;
			Kills += bKilled ? 1 : 0;
			bAnyCritical |= Pair.Value.bCritical;
			if (!FirstHit) FirstHit = Pair.Key;
		}
	}
	OnShotResult(FirstHit, Totals.Num(), Kills, bAnyCritical);
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
	// The gun draws with its own FOV. Both arms draw with the arm FOV in Godot (a per-material
	// projection); Unreal has one first-person FOV, so PlaceArm squeezes the arms toward the view
	// center by ArmFovRatio, which projects them exactly as the arm FOV would.
	Camera->FirstPersonFieldOfView = Weapon->ViewmodelFov;
	ArmFovRatio = FMath::Tan(FMath::DegreesToRadians(Weapon->ViewmodelFov) * 0.5f) / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(MeleeArmFov, 1.f, 170.f)) * 0.5f);
	// Scoped weapons hide the gun once the scope is up (the HUD draws the scope).
	const bool bShowModel = !(Weapon->ScopeOverlay >= 0.5f && AdsAmount > 0.85f);
	for (const auto& Pair : Parts)
	{
		// Mod parts that would block the view through the sight (scopes, holo frames) fade out of the way.
		const bool bAdsHidden = AdsHiddenParts.Contains(Pair.Key) && AdsAmount > 0.6f;
		if (Pair.Value) Pair.Value->SetVisibility(bShowModel && !bAdsHidden && (Pair.Key != "MuzzleFlash" || bFlashVisible));
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
	const float KickNow = Kick * 0.06f * (1.f - 0.6f * AdsAmount); // aiming tames the jolt
	Rot.X += KickNow * FMath::Max(Weapon->VerticalKick, 0.f);
	Rot.Y -= KickNow * Weapon->HorizontalKick; // Godot yaw: + turns left, so + kick swings the muzzle right
	// The low reticle: the gun (drawn with its own FOV) points at it, and aimed in, its sight point
	// (the model's origin) moves down onto it.
	const float GunDrop = P->ReticleAngleForFov(Weapon->ViewmodelFov);
	Rot.X -= GunDrop;
	FVector Pos = FMath::Lerp(Weapon->HipPosition, Weapon->AdsPosition, AdsAmount) + Bob + AirOffset;
	Pos.Y -= -Pos.Z * FMath::Tan(GunDrop) * AdsAmount; // Godot: z is forward (negative), y up
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
	if (UStaticMeshComponent* Mag = Part("Magazine")) Mag->SetRelativeLocation(G2U(MagRest + FVector(0.f, -ReloadMagDrop * MagOut, 0.f)) + ModelShift);
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
		Mat->SetScalarParameterValue("Glow", 2.f); // Godot: the held ball's glow
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
	return FMath::Lerp(PunchRetract, PunchEnd, WWO::SmoothStep(0.f, 1.f, T)); // skips the animation's own long hold
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
	if (!bVisible || !Arm.Mesh || !Arm.Anim) return;
	const USkeletalMesh* Manny = Cast<USkeletalMesh>(Arm.Mesh->GetSkinnedAsset());
	if (!Manny || !Manny->GetSkeleton()) return;
	const FReferenceSkeleton& Ref = Manny->GetSkeleton()->GetReferenceSkeleton();
	// The animation frame, in the mannequin's mesh space (cm; faces +y, left +x, up +z).
	TArray<FTransform> CS;
	CS.SetNum(Ref.GetNum());
	const FAnimExtractContext At(double(FMath::Clamp(AnimT, 0.f, Arm.Anim->GetPlayLength())));
	for (const int32 B : Arm.Bones)
	{
		FTransform Local;
		Arm.Anim->GetBoneTransform(Local, FSkeletonPoseBoneIndex(B), At, false);
		const int32 Parent = Ref.GetParentIndex(B);
		CS[B] = Parent == INDEX_NONE ? Local : Local * CS[Parent];
	}
	// Mesh space -> Godot model space (meters, y up, facing +z), the file the Godot arm plays.
	auto ToGodot = [](const FVector& V) { return FVector(V.X, V.Z, V.Y) / WWO::M; };
	// The shoulder goes exactly where the Godot arm's shoulder is (the baked path, relative to the
	// eyes); the rest of the arm hangs off it at the animation's angles.
	const FVector Shoulder = Arm.bLeft ? ArmJoint(WWOArmPaths::OverhandThrow, 0, AnimT) : ArmJoint(WWOArmPaths::MeleeHook, 0, AnimT);
	const FVector Eye = ToGodot(CS[Ref.FindBoneIndex("upperarm_r")].GetLocation()) - Shoulder;
	// Then fp_arm.gd's placement (PlaceJoint), squeezed toward the view axis by the arm FOV (see
	// UpdateFov; the bones shrink with it, so the arm's thickness does too), into camera space.
	// The left arm's mesh is mirrored by its scale, so its points are flipped back here.
	const float K = ArmFovRatio;
	const FVector Flip(1.f, Arm.bLeft ? -1.f : 1.f, 1.f);
	auto Point = [&](const FVector& MeshPos) { return G2U(PlaceJoint(ToGodot(MeshPos) - Eye, Arm.bLeft, Yaw, PitchDeg, Push, Drop) * FVector(K, K, 1.f)); };
	auto Dir = [&](const FVector& MeshDir) { return (G2U(PlaceJoint(ToGodot(MeshDir), Arm.bLeft, Yaw, PitchDeg, 0.f, 0.f) * FVector(K, K, 1.f)) * Flip).GetSafeNormal(); };
	const FVector Hidden = Point(CS[Ref.FindBoneIndex("upperarm_r")].GetLocation()) * Flip;
	for (int32 i = 0; i < Arm.Bones.Num(); ++i)
	{
		if (!Arm.Shown[i])
		{
			// A bone outside the arm: shrunk to nothing at the shoulder, so the body's vertices
			// vanish and the ones it shares with the arm pull in to close the shoulder.
			Arm.Mesh->SetBoneTransformByName(Ref.GetBoneName(Arm.Bones[i]), FTransform(FQuat::Identity, Hidden, FVector(0.01f)), EBoneSpaces::ComponentSpace);
			continue;
		}
		const FTransform& T = CS[Arm.Bones[i]];
		const FQuat Rot = FRotationMatrix::MakeFromXY(Dir(T.GetUnitAxis(EAxis::X)), Dir(T.GetUnitAxis(EAxis::Y))).ToQuat();
		Arm.Mesh->SetBoneTransformByName(Ref.GetBoneName(Arm.Bones[i]), FTransform(Rot, Point(T.GetLocation()) * Flip, FVector(K)), EBoneSpaces::ComponentSpace);
	}
	// Godot: a 4 cm radius ball held at the base of the middle finger (in the palm).
	if (!Arm.bLeft) Arm.Ball->SetVisibility(false); // only the throwing hand holds a grenade
	Arm.Ball->SetRelativeScale3D(FVector(0.08f * K));
	Arm.Ball->SetRelativeLocation(Point(CS[Ref.FindBoneIndex("middle_01_r")].GetLocation()));
}

// --- Mods and perks (draft) -------------------------------------------------------------------

void UWWOWeaponComponent::RefreshStats()
{
	// Copy the asset (Settings sliders edit it live), then apply the mods' percentages to the
	// stats read straight off the weapon. Reload, aim-in, swap and ready times go through
	// M() instead, where the loadout adds the mods' percentages itself.
	if (!BaseWeapon || !Weapon) return;
	for (TFieldIterator<FProperty> It(UWWOWeaponData::StaticClass()); It; ++It)
	{
		It->CopyCompleteValue_InContainer(Weapon, BaseWeapon);
	}
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	auto Scale = [L](float& Value, FName Stat) { Value *= FMath::Max(1.f + L->ModPercent(Stat), 0.f); };
	Scale(Weapon->FireRange, "range");
	Scale(Weapon->RecoilSide, "recoil_side");
	Scale(Weapon->RecoilUp, "recoil_up");
	Scale(Weapon->MagazineSize, "magazine_size");
	Scale(Weapon->AdsMoveSpeed, "ads_move_speed");
	Scale(Weapon->Sway, "sway");
	Scale(Weapon->Damage, "damage");
	Scale(Weapon->FireRate, "fire_rate");
	Scale(Weapon->CritMultiplier, "crit_multiplier");
	// Zoom is a magnification: +40% means 40% more of the zoom past 1x.
	Weapon->Zoom = 1.f + (Weapon->Zoom - 1.f) * FMath::Max(1.f + L->ModPercent("zoom"), 0.f);
	Weapon->MagazineSize = FMath::Max(1.f, FMath::RoundToFloat(Weapon->MagazineSize));

	// Perks that change stats while they're active.
	const AWWOCharacter* Pl = GetPlayer();
	if (L->HasPerk("focus") && HitStreak >= 5) Weapon->Damage *= 1.15f;
	if (L->HasPerk("brace") && Pl && (Pl->bCrouching || Pl->bSliding))
	{
		Weapon->RecoilUp *= 0.6f;
		Weapon->RecoilSide *= 0.6f;
	}
	if (L->HasPerk("hunter")) Weapon->CritMultiplier += 0.3f * AdsAmount;
	if (L->HasPerk("anchor") && IsAnchored())
	{
		Weapon->RecoilUp *= 0.5f;
		Weapon->RecoilSide *= 0.5f;
	}
	if (L->HasPerk("hammer") && HammerLeft > 0.f) Weapon->FireRate *= 1.25f;
}

bool UWWOWeaponComponent::IsAnchored() const
{
	// Anchor: aimed in and (nearly) standing still.
	const AWWOCharacter* Pl = GetPlayer();
	return Pl && AdsAmount > 0.5f && Pl->GetVelocity().Size2D() < 0.3f * WWO::M;
}

void UWWOWeaponComponent::OnShotResult(AActor* FirstHit, int32 HitCount, int32 Kills, bool bCritical)
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	bOverpressure = false; // any shot uses it up, hit or miss
	AimHeld = 0.f;         // Patience starts over
	if (Kills > 0 && L->HasPerk("hammer")) HammerLeft = 3.f;
	if (bCritical && FirstHit && L->HasPerk("ricochet"))
	{
		// One bounce from the target that was hit, for 60% of the gun's damage.
		AWWOChainLightning::SpawnFrom(GetWorld(), FirstHit, GetOwner(), FLinearColor(1.f, 0.62f, 0.2f),
			Weapon->Damage * 0.6f, 8.f, 1.f, 1, 0.04f);
	}
	// Focus: a shot that hits nothing ends the streak.
	HitStreak = HitCount > 0 ? HitStreak + 1 : 0;
	if (Kills > 0 && L->HasPerk("scavenger"))
	{
		Ammo = FMath::Min(Ammo + FMath::Max(1, FMath::RoundToInt(MagazineSize() * 0.2f)) * Kills, MagazineSize());
	}
	if (Kills > 0 && L->HasPerk("adrenal")) AdrenalLeft = 4.f;
	if (HitCount > 0 && L->HasPerk("static"))
	{
		StaticHits += 1;
		if (StaticHits >= 10 && FirstHit)
		{
			StaticHits = 0;
			// Two jumps from the target that was hit, for twice the gun's damage each.
			AWWOChainLightning::SpawnFrom(GetWorld(), FirstHit, GetOwner(), FLinearColor(0.45f, 0.75f, 1.f),
				Weapon->Damage * 2.f, 6.f, 0.85f, 2, 0.07f);
		}
	}
}

FString UWWOWeaponComponent::PerkStatus() const
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return FString();
	TArray<FString> Active;
	if (L->HasPerk("focus")) Active.Add(HitStreak >= 5 ? FString(TEXT("Focus ACTIVE")) : FString::Printf(TEXT("Focus %d/5"), HitStreak));
	if (L->HasPerk("static")) Active.Add(FString::Printf(TEXT("Static %d/10"), StaticHits));
	if (L->HasPerk("adrenal") && AdrenalLeft > 0.f) Active.Add(FString::Printf(TEXT("Adrenal %.1fs"), AdrenalLeft));
	const AWWOCharacter* Pl = GetPlayer();
	if (L->HasPerk("brace") && Pl && (Pl->bCrouching || Pl->bSliding)) Active.Add(TEXT("Braced"));
	if (L->HasPerk("hunter") && AdsAmount > 0.5f) Active.Add(TEXT("Hunter"));
	if (L->HasPerk("overpressure") && bOverpressure) Active.Add(TEXT("Overpressure READY"));
	if (L->HasPerk("final_round") && Ammo == 1) Active.Add(TEXT("Final Round"));
	if (L->HasPerk("anchor") && IsAnchored()) Active.Add(TEXT("Anchored"));
	if (L->HasPerk("patience") && AdsAmount > 0.5f) Active.Add(AimHeld >= 0.6f ? FString(TEXT("Patience READY")) : FString(TEXT("Patience")));
	if (L->HasPerk("hammer") && HammerLeft > 0.f) Active.Add(FString::Printf(TEXT("Hammer %.1fs"), HammerLeft));
	return FString::Join(Active, TEXT("  ·  "));
}
