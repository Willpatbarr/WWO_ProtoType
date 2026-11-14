#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Weapons/WWOPerks.h"
#include "Systems/WWOSound.h"
#include "Player/WWOBotController.h"
#include "Player/WWOHealthComponent.h"
#include "Game/WWOGameState.h"
#include "Characters/WWOBodyComponent.h"
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
#include "Misc/PackageName.h"
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
	constexpr float MuzzleFlashTime = 0.05f;
	using namespace WWOPerk; // the perks' numbers (Weapons/WWOPerks.h, shared with the host's checks)
	const FLinearColor ShotColor(1.f, 0.8f, 0.45f); // trails and the flash's light
	// The muzzle flash's parts on the first-person gun: the core, and the flares built around it.
	const FName FlashCore("MuzzleFlash"), FlashTongue("MuzzleFlashTongue"), FlashCrossA("MuzzleFlashCrossA"), FlashCrossB("MuzzleFlashCrossB");
	bool IsFlashPart(FName Name) { return Name == FlashCore || Name == FlashTongue || Name == FlashCrossA || Name == FlashCrossB; }
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

	// Config/WeaponModels.json: which mesh each weapon uses and where its mod parts attach.
	// Retuned with a restart and no build.
	TSharedPtr<FJsonObject> ModelConfig(FName WeaponId)
	{
		// Read and parsed once a run (every equip, every bot's gun swap and every hand-held gun asks).
		static TSharedPtr<FJsonObject> Json;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("WeaponModels.json")))) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
			// Saved/WWO/WeaponModels.local.json (this machine only, never in git or a build): its
			// "weapons" entries replace the shipped ones, for trying a model that isn't the game's.
			TSharedPtr<FJsonObject> Local;
			const TSharedPtr<FJsonObject>* Ours = nullptr;
			const TSharedPtr<FJsonObject>* Theirs = nullptr;
			if (Json.IsValid() && FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir() / TEXT("WWO/WeaponModels.local.json")))
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Local) && Local.IsValid()
				&& Json->TryGetObjectField(TEXT("weapons"), Ours) && Local->TryGetObjectField(TEXT("weapons"), Theirs))
			{
				for (const auto& Pair : (*Theirs)->Values) (*Ours)->SetField(Pair.Key, Pair.Value);
				UE_LOG(LogWWO, Display, TEXT("Weapon models: %d replaced by Saved/WWO/WeaponModels.local.json"), (*Theirs)->Values.Num());
			}
		}
		if (!Json.IsValid()) return nullptr;
		const TSharedPtr<FJsonObject>* Weapons = nullptr;
		const TSharedPtr<FJsonObject>* Model = nullptr;
		if (Json->TryGetObjectField(TEXT("weapons"), Weapons) && (*Weapons)->TryGetObjectField(WeaponId.ToString(), Model)) return *Model;
		return nullptr;
	}

	// A 3-number array field (or one number for all three), else Default.
	FVector JsonVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, const FVector& Default = FVector::ZeroVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
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
	if (const AWWOBotController* Bot = GetPlayer() ? GetPlayer()->GetBot() : nullptr) return Bot->IsHeld((EWWOAction)Action);
	const AWWOPlayerController* PC = GetPlayer() ? GetPlayer()->GetWWOController() : nullptr;
	return PC && PC->IsHeld((EWWOAction)Action);
}

bool UWWOWeaponComponent::JustPressed(int32 Action) const
{
	if (const AWWOBotController* Bot = GetPlayer() ? GetPlayer()->GetBot() : nullptr) return Bot->WasJustPressed((EWWOAction)Action);
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
	RebindLoadout();
}

void UWWOWeaponComponent::RebindLoadout()
{
	// Follow the loadout this character uses now (a bot gets its own after it spawns).
	if (BoundLoadout.IsValid()) BoundLoadout->OnChanged.Remove(LoadoutHandle);
	BoundLoadout = UWWOLoadoutSubsystem::Get(this);
	if (UWWOLoadoutSubsystem* Loadout = BoundLoadout.Get())
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
					// Other mods on the same gun (your other slot's config of it, or a change in the menu):
					// a full magazine stays full at its new size, a part-used one keeps its rounds, perks
					// keep their state, and it takes a swap like any other change of weapon.
					const bool bWasFull = Ammo >= MagazineSize();
					ModSignature = Signature;
					RefreshStats();
					BuildModel(); // the mods' parts
					Ammo = bWasFull ? MagazineSize() : FMath::Min(Ammo, MagazineSize());
					HitStreak = StaticHits = 0;
					BurstLeft = 0;
					bFullBurst = bBurstBoosted = false;
					CancelReload();
					SwapLength = FMath::Max(M("swap_time", SwapTime), 0.05f);
					SwapLeft = SwapLength;
				}
			}
			UpdateArmColor();
		});
	}
	UpdateArmColor();
}

void UWWOWeaponComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BoundLoadout.IsValid()) BoundLoadout->OnChanged.Remove(LoadoutHandle);
	Super::EndPlay(Reason);
}

UStaticMeshComponent* UWWOWeaponComponent::Part(FName Name) const
{
	const TObjectPtr<UStaticMeshComponent>* Found = Parts.Find(Name);
	return Found ? Found->Get() : nullptr;
}

void UWWOWeaponComponent::SetFirstPersonHidden(bool bHidden)
{
	const bool bWas = bFirstPersonHidden;
	bFirstPersonHidden = bHidden;
	if (bWas != bHidden && Weapon && ModelRoot) BuildModel(); // built only while shown
	if (ModelRoot) ModelRoot->SetVisibility(!bHidden, true);
	for (FWWOFPArm* Arm : { &MeleeArm, &ThrowArm })
	{
		if (Arm->Root) Arm->Root->SetVisibility(!bHidden, true);
	}
}

int32 UWWOWeaponComponent::MagazineSize() const
{
	return Weapon ? FMath::Max(1, (int32)Weapon->MagazineSize) : 1;
}

void UWWOWeaponComponent::Equip(UWWOWeaponData* NewWeapon)
{
	// Swap in the weapon's model and start it with a full magazine.
	if (!NewWeapon) return;
	StoreGunState(); // the gun you're putting away keeps its rounds
	BaseWeapon = NewWeapon;
	Weapon = DuplicateObject<UWWOWeaponData>(NewWeapon, this);
	HitStreak = StaticHits = 0;
	bOverpressure = true;
	AimHeld = 0.f;
	SinceShot = 100.f;
	bMeleeLanded = false;
	RefundRounds = 0;
	BurstLeft = BurstSize = BurstHits = 0;
	bFullBurst = bBurstBoosted = false;
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
	// Back to a gun you carried this life: as you left it (a first pickup is full, perks ready).
	if (const FGunState* Kept = GunStates.Find(NewWeapon->Id))
	{
		Ammo = FMath::Clamp(Kept->Ammo, 0, MagazineSize());
		bOverpressure = Kept->bOverpressure;
		SinceShot = GetWorld() ? float(GetWorld()->GetTimeSeconds() - Kept->LastShot) : 100.f;
	}
	CancelReload();
	AdsProgress = 0.f;
	FireCooldown = 0.f;
	SwapLength = FMath::Max(M("swap_time", SwapTime), 0.05f);
	SwapLeft = SwapLength;
	UWWOSound::PlayFor(GetOwner(), "weapon_swap");
}

void UWWOWeaponComponent::UpdateWorldGun(bool bShow)
{
	AWWOCharacter* P = GetPlayer();
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this); // that player's own loadout, on any machine
	const UWWOWeaponData* W = L ? L->GetWeapon() : nullptr;
	bShow = (bShow || bForceWorldGun) && P && W && !P->Health->bDead && !P->bUlting && P->GetMesh();
	if (!bShow)
	{
		if (WorldGunRoot) WorldGunRoot->SetVisibility(false, true);
		return;
	}
	if (!WorldGunRoot)
	{
		WorldGunRoot = NewObject<USceneComponent>(P);
		WorldGunRoot->SetupAttachment(P->GetRootComponent());
		WorldGunRoot->SetUsingAbsoluteLocation(true);
		WorldGunRoot->SetUsingAbsoluteRotation(true);
		WorldGunRoot->RegisterComponent();
		WorldGun = NewObject<UStaticMeshComponent>(P);
		WorldGun->SetupAttachment(WorldGunRoot);
		WorldGun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WorldGun->SetCanEverAffectNavigation(false);
		WorldGun->RegisterComponent();
	}
	if (WorldGunId != W->Id)
	{
		// The same mesh, scale and turn as the first-person model; "hand" is where the grip is, from the
		// mesh's own origin (cm, x forward, z up), so that point sits in the hand.
		WorldGunId = W->Id;
		const TSharedPtr<FJsonObject> Model = ModelConfig(W->Id);
		FString MeshPath;
		UStaticMesh* Mesh = Model.IsValid() && Model->TryGetStringField(TEXT("mesh"), MeshPath) ? LoadObject<UStaticMesh>(nullptr, *MeshPath) : nullptr;
		WorldGun->SetStaticMesh(Mesh);
		const FVector Rot = JsonVec(Model, TEXT("rot"));
		WorldGun->SetRelativeScale3D(JsonVec(Model, TEXT("scale"), FVector(1.0)));
		WorldGun->SetRelativeRotation(FRotator(Rot.X, Rot.Y, Rot.Z));
		WorldGun->SetRelativeLocation(-JsonVec(Model, TEXT("hand"), FVector(0.0, 0.0, -5.0)));
	}
	WorldGunRoot->SetVisibility(true, true);
	// In the right hand: pointing down and ahead at rest, along the look once the arms are up.
	// The gun is up (pointing where its owner looks) unless they are sprinting or sliding: told from
	// the body's clip, which every machine has. (Not from the upper body's weight any more: crouched,
	// the arms are the crouch clip's own but the gun still aims.)
	const FName BodyClip = P->Body->CurrentAnim();
	const bool bGunDown = BodyClip == "Sprint" || BodyClip == "Slide" || BodyClip == "Slide_Start";
	WorldGunRaise = WWO::MoveToward(WorldGunRaise, bGunDown ? 0.f : 1.f, 7.f * GetWorld()->GetDeltaSeconds());
	const float Raise = WorldGunRaise;
	const float LookPitch = -P->Body->HunchSpineNow() / 0.55f; // AWWOCharacter::UpdateModel bends the spine by 0.55 of the look
	// The hands that show: the character model's when there is one (its arms are not where the mannequin's are).
	const USkeletalMeshComponent* Hands = P->Body->GetModel() ? P->Body->GetModel() : P->GetMesh();
	const FVector Right = Hands->GetSocketLocation("hand_r"), Left = Hands->GetSocketLocation("hand_l");
	// Arms up, the gun points where its owner is looking: at whoever it is aimed at. (Not along the
	// line of the two hands: a model's arms never land exactly where the mannequin's do, and a gun
	// laid hand to hand ends up pointing down across the body.) Lowered for a sprint it is carried:
	// along the hands when they are a gun's length apart, pointing down and ahead otherwise.
	const FVector Along = (Left - Right).GetSafeNormal();
	const float Apart = FVector::Dist(Left, Right);
	const bool bCarried = Apart > 15.f && Apart < 70.f && FVector::DotProduct(Along, P->GetActorForwardVector()) > -0.3f;
	const FQuat Low = bCarried ? FRotationMatrix::MakeFromXZ(Along, FVector::UpVector).ToQuat() : FRotator(-45.f, P->GetActorRotation().Yaw, 0.f).Quaternion();
	const FQuat Aim = FQuat::Slerp(Low, FRotator(LookPitch, P->GetActorRotation().Yaw, 0.f).Quaternion(), Raise);
	// The grip goes in the palm, not at the wrist, on a model that says where its palm is
	// (Config/CharacterModels.json hand_fingers / hand_palm): 8 cm along the hand from the wrist.
	// (The robot's hand is 17 cm long; at the wrist a pistol floated above the fist.)
	FVector Grip = Right;
	if (P->Body->GetModel() && P->Body->bRigidHands)
	{
		const FReferenceSkeleton& Ref = P->Body->GetModel()->GetSkeletalMeshAsset()->GetRefSkeleton();
		FTransform RefHand = FTransform::Identity;
		for (int32 B = Ref.FindBoneIndex("hand_r"); B != INDEX_NONE; B = Ref.GetParentIndex(B)) RefHand = RefHand * Ref.GetRefBonePose()[B];
		const FQuat FromRef = Hands->GetSocketQuaternion("hand_r") * RefHand.GetRotation().Inverse();
		Grip += FromRef.RotateVector(P->Body->HandFingers) * 8.f + FromRef.RotateVector(P->Body->HandPalm) * 2.f;
	}
	WorldGunRoot->SetWorldLocationAndRotation(Grip, Aim);
}

void UWWOWeaponComponent::StoreGunState()
{
	if (!BaseWeapon || !GetWorld()) return;
	FGunState& State = GunStates.FindOrAdd(BaseWeapon->Id);
	State.Ammo = Ammo;
	State.bOverpressure = bOverpressure;
	State.LastShot = GetWorld()->GetTimeSeconds() - SinceShot;
}

void UWWOWeaponComponent::Interrupt()
{
	BurstLeft = 0;
	bBurstBoosted = false;
	bReadyShotQueued = false;
	CancelReload();
	bMeleeing = false;
	MeleeProgress = 0.f;
	bMeleeLanded = false;
	MeleeTarget = nullptr;
}

void UWWOWeaponComponent::ResetForRespawn()
{
	Interrupt();
	GunStates.Reset();
	HitStreak = StaticHits = 0;
	AdrenalLeft = HammerLeft = 0.f;
	bFullBurst = false;
	bOverpressure = true;
	SinceShot = 100.f;
	AimHeld = 0.f;
	FireCooldown = 0.f;
	if (Weapon) Ammo = MagazineSize();
}

void UWWOWeaponComponent::BuildModel()
{
	for (auto& Pair : Parts)
	{
		if (Pair.Value) Pair.Value->DestroyComponent();
	}
	Parts.Reset();
	AdsHiddenParts.Reset();
	FlareSizes.Reset();
	TopBelowSight = 0.f;
	if (bFirstPersonHidden) return; // someone else's character or a bot: nobody sees a first-person gun on it (SetFirstPersonHidden builds it if that changes)
	// One part. Pos and Rot are in the model's space: cm, x forward, y right, z up, origin at the sight.
	auto AddPart = [this](FName Name, UStaticMesh* Mesh, const FVector& Scale, const FVector& Pos, const FRotator& Rot,
		const FLinearColor* Color, float Glow) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(GetOwner());
		Comp->SetupAttachment(ModelRoot);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetVisibility(!bFirstPersonHidden); // rebuilt after equips: stays hidden on someone else's character
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
	// The flash is more than the core's blink: a tongue of flame out of the barrel and a cross of
	// flares around it (soft additive shapes, sized off the core; Shoot rolls and resizes them).
	if (UStaticMeshComponent* Core = Part(FlashCore))
	{
		const FVector At = Core->GetRelativeLocation();
		const float Across = FMath::Min(Core->GetRelativeScale3D().X, 0.07f); // the core's width (1 = a meter); big-bore guns don't get a flash that fills the view
		// The core itself glows soft and white-hot instead of showing as a solid ball.
		Core->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_SoftAdd")));
		Core->SetRelativeScale3D(FVector(Across * 1.3f));
		if (UMaterialInstanceDynamic* Mat = Core->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mat->SetVectorParameterValue("Color", FMath::Lerp(Flash, FLinearColor::White, 0.5f));
			Mat->SetScalarParameterValue("Alpha", 1.f);
			Mat->SetScalarParameterValue("Edge", 1.3f);
			Mat->SetScalarParameterValue("Rim", 0.f);
			Mat->SetScalarParameterValue("Core", 1.f);
		}
		auto AddFlare = [&](FName Name, const FVector& Scale, float Forward, float Alpha)
		{
			UStaticMeshComponent* Flare = AddPart(Name, WWOFX::Sphere(), Scale * Across, At + FVector(Forward * Across * 100.f, 0, 0), FRotator::ZeroRotator, nullptr, 0.f);
			Flare->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_SoftAdd")));
			if (UMaterialInstanceDynamic* Mat = Flare->CreateAndSetMaterialInstanceDynamic(0))
			{
				Mat->SetVectorParameterValue("Color", Flash);
				Mat->SetScalarParameterValue("Alpha", Alpha);
				Mat->SetScalarParameterValue("Edge", 1.3f);
				Mat->SetScalarParameterValue("Rim", 0.f);
				Mat->SetScalarParameterValue("Core", 0.6f);
			}
			Flare->SetVisibility(false);
		};
		AddFlare(FlashTongue, FVector(4.f, 1.1f, 1.1f), 1.8f, 0.8f);
		AddFlare(FlashCrossA, FVector(0.7f, 2.8f, 0.7f), 0.4f, 0.6f);
		AddFlare(FlashCrossB, FVector(0.7f, 0.7f, 2.2f), 0.4f, 0.6f);
	}
	TMap<FString, FVector> Anchors;
	TopBelowSight = 0.f;
	if (BodyMesh)
	{
		// How far the gun's top sits under the sight line (AdsLevel tips the gun by it when aimed).
		const FBoxSphereBounds MeshBounds = BodyMesh->GetBounds();
		const float MeshTop = (MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z) * JsonVec(Model, TEXT("scale"), FVector(1.0)).Z;
		TopBelowSight = FMath::Max(-(JsonVec(Model, TEXT("offset")).Z + MeshTop) + Weapon->SightHeight, 0.f);
		const FVector Rot = JsonVec(Model, TEXT("rot"));
		AddPart("Body", BodyMesh, JsonVec(Model, TEXT("scale"), FVector(1.0)), JsonVec(Model, TEXT("offset")), FRotator(Rot.X, Rot.Y, Rot.Z), nullptr, 0.f);
		const TArray<TSharedPtr<FJsonValue>>* Extras = nullptr;
		if (Model->TryGetArrayField(TEXT("parts"), Extras))
		{
			for (int32 i = 0; i < Extras->Num(); ++i)
			{
				if ((*Extras)[i]->AsObject().IsValid()) AddJsonPart(FName(*FString::Printf(TEXT("Extra%d"), i)), (*Extras)[i]->AsObject(), JsonVec(Model, TEXT("offset")));
			}
		}
		const TSharedPtr<FJsonObject>* AnchorObj = nullptr;
		if (Model->TryGetObjectField(TEXT("anchors"), AnchorObj))
		{
			for (const auto& Pair : (*AnchorObj)->Values)
			{
				const FString Key(Pair.Key);
				Anchors.Add(Key, JsonVec(Model, TEXT("offset")) + JsonVec(*AnchorObj, *Key)); // anchors move with the mesh
			}
		}
	}

	// The equipped mods' parts, each at its slot's anchor (or the part's own "at"). A model can say
	// "no_mods": true to show none (a model that has its own sights and barrel).
	bool bNoMods = false;
	if (Model.IsValid()) Model->TryGetBoolField(TEXT("no_mods"), bNoMods);
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this); L && !bNoMods)
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
	// The same clip the body plays (Tools/bake_body_anims.py, which also writes WWOFPArmPaths.h from
	// it); the Godot bake if that one is missing.
	const TCHAR* AnimName = bLeft ? TEXT("A_OverhandThrow") : TEXT("A_Melee_Hook");
	const FString BodyPath = FString::Printf(TEXT("/Game/WWO/Anims/Body/%s"), AnimName);
	Arm.Anim = FPackageName::DoesPackageExist(BodyPath) ? LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s.%s"), *BodyPath, AnimName)) : nullptr;
	if (!Arm.Anim) Arm.Anim = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/WWO/Anims/UAL/%s.%s"), AnimName, AnimName));
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

FVector UWWOWeaponComponent::TrailStart() const
{
	// Where a trail has to start in the world to look like it leaves the first-person gun's muzzle:
	// the gun draws with its own field of view, so its muzzle isn't where it appears to be.
	const AWWOCharacter* P = GetPlayer();
	const UCameraComponent* Camera = P->Camera;
	const FTransform View = Camera->GetComponentTransform();
	constexpr float Depth = 120.f; // cm in front of the eye
	const UStaticMeshComponent* Core = Part(FlashCore);
	const FVector Seen = Core ? View.InverseTransformPositionNoScale(Core->GetComponentLocation()) : FVector::ZeroVector;
	if (Seen.X < 1.f) return View.TransformPositionNoScale(FVector(Depth, 12.f, -10.f));
	const float Ratio = FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView) * 0.5f)
		/ FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Camera->FirstPersonFieldOfView, 1.f, 170.f)) * 0.5f);
	return View.TransformPositionNoScale(FVector(Depth, Seen.Y / Seen.X * Ratio * Depth, Seen.Z / Seen.X * Ratio * Depth));
}

bool UWWOWeaponComponent::SightOnScreen(FVector2D& OutAt, float& OutRoll) const
{
	const AWWOCharacter* P = GetPlayer();
	if (!P || !P->Camera || !ModelRoot || bFirstPersonHidden) return false;
	// The gun draws with its own field of view (TrailStart has the same sum the other way round).
	const FTransform View = P->Camera->GetComponentTransform();
	const float HalfTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(P->Camera->FirstPersonFieldOfView, 1.f, 170.f)) * 0.5f);
	auto Seen = [&](const FVector& World, FVector2D& Out) -> bool
	{
		const FVector V = View.InverseTransformPositionNoScale(World);
		if (V.X < 0.1f) return false;
		Out = FVector2D(V.Y, -V.Z) / (V.X * HalfTan);
		return true;
	};
	const FVector Sight = ModelRoot->GetComponentLocation();
	FVector2D Above;
	if (!Seen(Sight, OutAt) || !Seen(Sight + ModelRoot->GetUpVector() * 2.f, Above)) return false;
	const FVector2D Up = Above - OutAt;
	OutRoll = FMath::Atan2(Up.X, -Up.Y);
	return true;
}

void UWWOWeaponComponent::StartReload()
{
	if (bReloading || Ammo >= MagazineSize()) return;
	bReloading = true;
	ReloadProgress = 0.f;
	GetPlayer()->bSprinting = false; // reloading stops a sprint
	UWWOSound::PlayFor(GetOwner(), "reload_start");
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

void UWWOWeaponComponent::Shake(float Strength, float Seconds)
{
	// The view widens and tips for a moment (your aim doesn't move) and the controller thumps.
	if (Strength <= 0.01f) return;
	Strength = FMath::Min(Strength, 2.f);
	StartPunch(3.f * Strength, 2.2f * Strength * (FMath::FRand() < 0.5f ? 1.f : -1.f), Seconds);
	Rumble(0.35f * Strength, 0.8f * Strength, Seconds * 0.8f);
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
	SinceShot += DeltaSeconds;
	RefreshStats();
	if (!FMath::IsNearlyEqual(Weapon->SightHeight, BuiltSightHeight)) BuildModel(); // the slider moved
	UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	// Swapping weapons goes to your other weapon slot's saved config. With nothing in that slot it
	// steps through every weapon, as before there were slots.
	if (Loadout && JustPressed((int32)EWWOAction::WeaponNext)) { if (!Loadout->SwapWeaponSlot()) Loadout->CycleWeapon(1); }
	else if (Loadout && JustPressed((int32)EWWOAction::WeaponPrev)) { if (!Loadout->SwapWeaponSlot()) Loadout->CycleWeapon(-1); }
	ConfigToastLeft = FMath::Max(ConfigToastLeft - DeltaSeconds, 0.f);
	if (Loadout && JustPressed((int32)EWWOAction::NextConfig))
	{
		// You carry two saved configs, one per weapon slot: this goes to the other one and names it
		// (the same swap as the weapon swap key, which says nothing).
		const bool bSwapped = Loadout->SwapWeaponSlot();
		const int32 Index = Loadout->GetSlotConfig(Loadout->GetActiveSlot());
		ConfigToast = bSwapped && Index != INDEX_NONE ? Loadout->GetConfigs()[Index].Name + TEXT(": ") + UWWOLoadoutSubsystem::ConfigSummary(Loadout->GetConfigs()[Index], Loadout)
			: FString(TEXT("Your other weapon slot is empty (pick a config for it in the character menu)"));
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
		bMeleeConnected = false; // (the swing's whoosh is a miss's sound: MeleeStrike plays it when nothing was hit)
		CancelReload(); // meleeing cancels a reload, like Destiny
		P->bSprinting = false;
		AActor* Target = FindLungeTarget();
		MeleeTarget = Target;
		// Online, the punch shows on your full body for everyone else (the host's copy replicates it).
		const ENetMode Mode = GetNetMode();
		if (Mode == NM_Client) P->ServerMeleeSwing();
		if (Mode != NM_Standalone) P->PlaySwingOnBody();
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
	for (AActor* Actor : UWWOWorldSubsystem::Get(this)->LivingTargets(this))
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
	for (AActor* Actor : UWWOWorldSubsystem::Get(this)->LivingTargets(this))
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
	if (!Best)
	{
		UWWOSound::PlayFor(GetOwner(), "melee_swing"); // a miss: the whoosh, as the fist passes the reticle
		return;
	}
	IWWOTargetable* Target = Cast<IWWOTargetable>(Best);
	FWWODamageBy By(P);
	const float Boosted = M("melee_damage", MeleeDamage) * P->Abilities->MeleeMultiplier() * P->VeilMeleeMultiplier();
	bool bKilled;
	if (GetNetMode() == NM_Client)
	{
		// A joiner shows the hit now (flash, number) and predicts the kill from the health the host
		// last sent; the host applies it and the knockback.
		const float Before = Target->GetHealth() + Target->GetShield();
		Target->TakeHit(Boosted, false, BestPoint);
		bKilled = Before > 0.f && Before - Boosted <= 0.f;
		P->ServerMeleeHit(Best, Boosted, Origin, BestPoint);
	}
	else bKilled = Target->TakeHit(Boosted, false, BestPoint); // melee doesn't crit
	OnHitConfirmed.Broadcast(false, bKilled);
	Kick = 1.f;
	KickAge = 0.f;
	AWWOFXActor::HitSpark(GetWorld(), BestPoint, (Origin - BestPoint).GetSafeNormal(), bKilled); // sparks off the hit; the gold flare on a kill
	UWWOSound::Play(this, "melee_hit", BestPoint);
	if (bKilled && P->IsLocallyControlled() && P->IsPlayerControlled()) UWWOSound::PlayFlat(this, "kill");
	MeleeImpact(Best, bKilled, BestPoint);
}

void UWWOWeaponComponent::MeleeImpact(AActor* TargetActor, bool bKilled, const FVector& Point)
{
	bMeleeConnected = true;
	// Everything that makes a landed melee feel heavy. Kills hit harder.
	AWWOCharacter* P = GetPlayer();
	const float Boost = bKilled ? MeleeKillBoost : 1.f;
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFloraData* Flora = Loadout ? Loadout->GetFlora() : nullptr;
	const FLinearColor Color = Flora && Flora->GraftColor.A > 0.f ? Flora->GraftColor : FLinearColor(0.25f, 0.55f, 1.f);
	if (GetNetMode() == NM_Client)
	{
		// A joiner shows its own discharge now; the host runs the rest (ServerMeleeHit).
		if (const UWWOAbilityData* Thornfist = P->Abilities->ThornfistActive())
		{
			if (Thornfist->DischargeArcs >= 1.f)
			{
				AWWODischarge::Spawn(GetWorld(), TargetActor, Point, (Point - P->EyeLocation()).GetSafeNormal(), Color,
					FMath::RoundToInt(Thornfist->DischargeArcs), Thornfist->DischargeRadius, Thornfist->DischargeTime);
			}
		}
		P->Abilities->OnMeleeLanded();
		P->Abilities->ConsumeStaticCharge(); // used up here too (the HUD); the host sends the chain
	}
	else MeleeKitEffects(TargetActor, Point);
	P->AddRecoil(FMath::DegreesToRadians(MeleeJolt * Boost), 0.f, 0.02f, 1.f, 0.12f); // the view snaps up and settles
	StartPunch(MeleePunchFov * Boost, MeleePunchRoll * Boost, MeleePunchTime);
	Rumble(MeleeRumbleWeak * Boost, MeleeRumbleStrong * Boost, MeleeRumbleTime * Boost);
	if (GetNetMode() != NM_Client) MeleeKnockBack(TargetActor, bKilled); // a joiner's is the host's (it moves the target)
	if (AWWOPlayerController* PC = P->GetWWOController()) PC->Hitstop(MeleeHitstop * Boost);
}

void UWWOWeaponComponent::MeleeKitEffects(AActor* TargetActor, const FVector& Point)
{
	AWWOCharacter* P = GetPlayer();
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	const UWWOFloraData* Flora = Loadout ? Loadout->GetFlora() : nullptr;
	const FLinearColor Color = Flora && Flora->GraftColor.A > 0.f ? Flora->GraftColor : FLinearColor(0.25f, 0.55f, 1.f);
	P->Abilities->OnMeleeLanded(); // Melee Guard
	if (const UWWOAbilityData* Thornfist = P->Abilities->ThornfistActive())
	{
		if (Thornfist->DischargeArcs >= 1.f)
		{
			AWWOGameState::Discharge(GetWorld(), TargetActor, Point, (Point - P->EyeLocation()).GetSafeNormal(), Color,
				FMath::RoundToInt(Thornfist->DischargeArcs), Thornfist->DischargeRadius, Thornfist->DischargeTime, P);
		}
	}
	if (const UWWOAbilityData* StaticCharge = P->Abilities->ConsumeStaticCharge())
	{
		AWWOChainLightning::Spawn(GetWorld(), StaticCharge, TargetActor, P, Color);
	}
}

void UWWOWeaponComponent::MeleeKnockBack(AActor* TargetActor, bool bKilled) const
{
	IWWOTargetable* Target = Cast<IWWOTargetable>(TargetActor);
	if (!Target) return;
	FVector Away = TargetActor->GetActorLocation() - GetPlayer()->GetActorLocation();
	Away.Z = 0.f;
	Target->KnockBack(Away.GetSafeNormal() * MeleeKnockback * (bKilled ? MeleeKillKnockback : 1.f) * WWO::M);
}

// --- Reload, ADS, firing -----------------------------------------------------------------------

void UWWOWeaponComponent::UpdateReload(float DeltaSeconds)
{
	Ammo = FMath::Min(Ammo, MagazineSize()); // in case the slider shrank the magazine
	// Square is Revive too: next to a downed teammate it revives instead.
	if (JustPressed((int32)EWWOAction::Reload) && !GetPlayer()->Health->FindReviveTarget()) StartReload();
	if (bReloading && GetPlayer()->bSprinting) CancelReload(); // sprinting cancels a reload, like Destiny
	if (!bReloading) return;
	const float Adrenal = AdrenalLeft > 0.f ? 1.4f : 1.f; // Adrenal: reloads 40% faster after a kill
	ReloadProgress += DeltaSeconds * Adrenal / FMath::Max(M("reload_time", Weapon->ReloadTime), 0.05f);
	if (ReloadProgress >= ReloadSeated && Ammo < MagazineSize())
	{
		Ammo = MagazineSize(); // the new magazine is in
		UWWOSound::PlayFor(GetOwner(), "reload_done");
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
	// The gun model's kick: full at the shot, held, then eased back (UWWOWeaponData::KickHold / KickReturn).
	KickAge += DeltaSeconds;
	{
		const float Back = FMath::Clamp((KickAge - FMath::Max(Weapon->KickHold, 0.f)) / FMath::Max(Weapon->KickReturn, 0.01f), 0.f, 1.f);
		Kick = 1.f - Back * Back * (3.f - 2.f * Back); // slow off the top, slow onto rest
	}
	FlashTime -= DeltaSeconds;
	bFlashVisible = FlashTime > 0.f; // decided before this frame's shot, like Godot (a new flash shows from the next frame)

	FireCooldown -= DeltaSeconds;
	// A semi-auto pull is remembered for a moment: one that comes just before the gun can fire
	// again (or during a burst) fires as soon as it can, instead of being lost.
	if (Weapon->IsSemiAuto() && JustPressed((int32)EWWOAction::Fire)) FireBuffer = FMath::Max(P->FireBufferTime, 0.f);
	else FireBuffer = FMath::Max(FireBuffer - DeltaSeconds, 0.f);
	// A burst under way finishes on its own, trigger held or not (a reload, a punch or a swap cuts
	// it short). FireCooldown keeps counting meanwhile: it times burst start to burst start.
	if (BurstLeft > 0)
	{
		if (bReloading || bMeleeing || IsSwapping())
		{
			EndBurst();
			return;
		}
		BurstCooldown -= DeltaSeconds;
		while (BurstLeft > 0 && BurstCooldown <= 0.f)
		{
			if (Ammo <= 0) break;
			FireRound();
			--BurstLeft;
			BurstCooldown += 60.f / FMath::Max(Weapon->BurstRate, 1.f);
		}
		if (BurstLeft == 0 || Ammo <= 0) EndBurst();
		if (Ammo <= 0) StartReload();
		return;
	}
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
	const bool bWants = WantsToFire() || ((bReadyShotQueued || FireBuffer > 0.f) && Weapon->IsSemiAuto());
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
		const int32 Rounds = Weapon->BurstCount();
		FireBuffer = 0.f; // the remembered pull is spent
		if (Rounds > 1) BeginBurst(Rounds);
		FireRound();
		FireCooldown += 60.f / FMath::Max(Weapon->FireRate, 1.f);
		if (Rounds > 1)
		{
			// The rest of the burst follows at the burst's own rate (see the top of this function).
			BurstLeft = Rounds - 1;
			BurstCooldown = 60.f / FMath::Max(Weapon->BurstRate, 1.f);
			FireCooldown = FMath::Max(FireCooldown, 0.f); // one burst per frame, however long the frame
			break;
		}
		if (Weapon->IsSemiAuto()) break;
	}
	if (Ammo <= 0)
	{
		if (BurstLeft > 0) EndBurst();
		StartReload(); // ran dry: reload automatically
	}
}

void UWWOWeaponComponent::FireRound()
{
	Shoot();
	Ammo -= 1;
	if (RefundRounds > 0) Ammo = FMath::Min(Ammo + RefundRounds, MagazineSize()); // Marksman: a crit's round comes back
	RefundRounds = 0;
}

void UWWOWeaponComponent::BeginBurst(int32 Rounds)
{
	BurstSize = Rounds;
	BurstHits = 0;
	bBurstBoosted = bFullBurst; // Full Burst: earned by the last burst, spent on this one
	bFullBurst = false;
}

void UWWOWeaponComponent::EndBurst()
{
	// Full Burst: every round of a finished burst hit.
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	bFullBurst = BurstLeft == 0 && BurstSize > 1 && BurstHits >= BurstSize && L && L->HasPerk("full_burst");
	BurstLeft = 0;
	bBurstBoosted = false;
}

void UWWOWeaponComponent::Shoot()
{
	AWWOCharacter* P = GetPlayer();
	Kick = 1.f;
	KickAge = 0.f;
	FlashTime = MuzzleFlashTime;
	const int32 PelletCount = FMath::Max(1, FMath::RoundToInt(Weapon->Pellets));
	// Hip-fire spread shrinks to nothing as you aim in; pellets scatter around that aim.
	const FVector Aim = WWO::RandomInCone(P->AimForward(), FMath::DegreesToRadians(Weapon->HipSpread) * (1.f - AdsAmount));
	struct FTotal { float Damage = 0.f; bool bCritical = false; FVector Point; };
	TMap<AActor*, FTotal> Totals; // pellets add up into one hit per target
	TArray<FVector_NetQuantize> Ends; // every pellet's impact, for the other players' screens
	// Perks that power up just this shot.
	float ShotScale = 1.f, CritBonus = 0.f;
	bool bLongShot = false, bPointBlank = false, bExecutioner = false; // perks that depend on what each pellet hits
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		if (L->HasPerk("overpressure") && bOverpressure) ShotScale *= Overpressure;
		if (L->HasPerk("final_round") && Ammo == 1) ShotScale *= FinalRound; // Ammo drops after the shot
		if (L->HasPerk("patience") && AimHeld >= PatienceTime) CritBonus = PatienceCrit;
		if (L->HasPerk("cold_bore") && SinceShot >= ColdBoreTime) ShotScale *= ColdBore;
		if (L->HasPerk("full_burst") && bBurstBoosted) ShotScale *= FullBurst;
		bLongShot = L->HasPerk("longshot");
		bPointBlank = L->HasPerk("point_blank");
		bExecutioner = L->HasPerk("executioner");
	}
	SinceShot = 0.f;
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
		if (!bHit)
		{
			Ends.Add(P->EyeLocation() + Dir * Weapon->FireRange * WWO::M); // a miss still flies through the air
			continue;
		}
		AWWOFXActor::ImpactMark(GetWorld(), Hit.ImpactPoint);
		Ends.Add(Hit.ImpactPoint);
		IWWOTargetable* Target = Cast<IWWOTargetable>(Hit.GetActor());
		if (!Target || Target->IsDead() || !UWWOWorldSubsystem::CanHurt(P, Hit.GetActor())) continue; // PvP: not your team
		const bool bCritical = Target->IsCriticalHit(Hit, P->EyeLocation());
		if (P->IsLocallyControlled() && P->IsPlayerControlled()) AWWOFXActor::HitSpark(GetWorld(), Hit.ImpactPoint, -Dir, bCritical); // a bot's: ShowShotFromBody
		FTotal& Entry = Totals.FindOrAdd(Hit.GetActor());
		if (Entry.Damage == 0.f) Entry.Point = Hit.ImpactPoint;
		// Long Shot: far targets; Point Blank: close ones; Finisher: ones under half their health and shield.
		float TargetScale = 1.f;
		const float HitDist = FVector::Dist(P->EyeLocation(), Hit.ImpactPoint) / WWO::M;
		if (bLongShot && HitDist >= LongShotRange) TargetScale *= LongShot;
		if (bPointBlank && HitDist <= PointBlankRange) TargetScale *= PointBlank;
		if (bExecutioner && Target->GetHealth() + Target->GetShield() < 0.5f * (Target->GetMaxHealth() + Target->GetMaxShield())) TargetScale *= Finisher;
		Entry.Damage += Weapon->Damage * ShotScale * TargetScale * (bCritical ? Weapon->CritMultiplier + CritBonus : 1.f);
		Entry.bCritical |= bCritical;
	}
	// The shot through the air, on this machine. Yours leaves from where your gun's muzzle shows on
	// screen (thinner, so it doesn't cover what you're aiming at) with the flash on the gun itself;
	// a bot's is drawn from its body like any other player's.
	if (P->IsLocallyControlled() && P->IsPlayerControlled())
	{
		UWWOSound::PlayFlat(this, UWWOSound::HasEvent(this, FName(*(TEXT("fire_") + Weapon->Id.ToString()))) ? FName(*(TEXT("fire_") + Weapon->Id.ToString())) : FName("fire"));
		const FVector Muzzle = TrailStart();
		for (const FVector_NetQuantize& End : Ends) AWWOFXActor::BulletTrail(GetWorld(), Muzzle, End, ShotColor, 0.6f);
		// No two flashes alike: the cross rolls and every flare changes size a little.
		const float Roll = FMath::FRandRange(0.f, 360.f);
		for (const FName& Name : { FlashTongue, FlashCrossA, FlashCrossB })
		{
			if (UStaticMeshComponent* Flare = Part(Name))
			{
				if (!FlareSizes.Contains(Name)) FlareSizes.Add(Name, Flare->GetRelativeScale3D());
				Flare->SetRelativeRotation(FRotator(0.f, 0.f, Roll));
				Flare->SetRelativeScale3D(FlareSizes[Name] * FMath::FRandRange(0.75f, 1.25f));
			}
		}
		if (AWWOFXActor* Light = AWWOFXActor::Spawn(GetWorld(), P->EyeLocation() + P->AimForward() * 80.f, 0.07f))
		{
			Light->LightTween(Light->AddLight(ShotColor, 7000.f, 500.f), 7000.f, 0.f, 0.07f, EWWOEase::QuadIn);
		}
	}
	else P->ShowShotFromBody(P->EyeLocation(), Ends, false);

	bool bAnyCritical = false, bAnyKilled = false;
	int32 Kills = 0;
	AActor* FirstHit = nullptr;
	// Online, a joiner's hits are shown here at once (TakeHit only flashes and numbers on a joiner)
	// and sent to the host, which checks and applies them; a kill is predicted from the health the
	// host last sent. The host's own shot applies directly. Either way the others see it.
	const ENetMode Mode = GetWorld()->GetNetMode();
	const bool bJoiner = Mode == NM_Client;
	FWWODamageBy By(P); // the host's own hits are credited to it (PvP)
	FWWOGunShot NetShot;
	NetShot.WeaponId = Weapon->Id;
	if (const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this))
	{
		for (const FName& Slot : UWWOLoadoutSubsystem::ModSlots())
		{
			if (const UWWOModData* Mod = L->GetMod(Slot)) NetShot.Mods.Add(Mod->Id);
		}
	}
	NetShot.Origin = P->EyeLocation();
	NetShot.Ends = Ends;
	for (const auto& Pair : Totals)
	{
		if (IWWOTargetable* Target = Cast<IWWOTargetable>(Pair.Key))
		{
			const float Before = Target->GetHealth() + Target->GetShield();
			bool bKilled = Target->TakeHit(Pair.Value.Damage, Pair.Value.bCritical, Pair.Value.Point);
			if (bJoiner)
			{
				bKilled = Before > 0.f && Before - Pair.Value.Damage <= 0.f;
				NetShot.Hits.Add({ Pair.Key, Pair.Value.Damage, Pair.Value.bCritical, Pair.Value.Point });
			}
			bAnyKilled |= bKilled;
			Kills += bKilled ? 1 : 0;
			bAnyCritical |= Pair.Value.bCritical;
			if (!FirstHit) FirstHit = Pair.Key;
		}
	}
	if (bJoiner) P->ServerGunShot(NetShot);
	else if (Mode == NM_ListenServer) P->MulticastGunShot(NetShot.Origin, NetShot.Ends);
	// A joiner's kill perks wait for the host's word (OnKillsConfirmed); its kill marker doesn't.
	bLastShotPredictedKill = bAnyKilled;
	OnShotResult(FirstHit, Totals.Num(), bJoiner ? 0 : Kills, bAnyCritical);
	if (Totals.Num() > 0) OnHitConfirmed.Broadcast(bAnyCritical, bAnyKilled);
	if (Totals.Num() > 0 && P->IsLocallyControlled() && P->IsPlayerControlled()) UWWOSound::PlayFlat(this, bAnyKilled ? "kill" : (bAnyCritical ? "hit_crit" : "hit"));
	// Recoil kicks the actual view, so you have to pull down to stay on target.
	const bool bSynced = Weapon->RecoilRecoverySync >= 0.5f;
	float RecoveryTime = Weapon->RecoilRecoveryTime;
	if (bSynced) RecoveryTime = 60.f / FMath::Max(Weapon->FireRate, 1.f) - Weapon->RecoilKickTime + Weapon->RecoilRecoveryDelay;
	const bool bShaped = Weapon->RecoilShape >= 0.5f; // the measured rise and fall, over the rise and fall-back times
	if (bShaped) RecoveryTime = Weapon->RecoilRecoveryTime;
	const float Share = BurstLeft > 0 ? FMath::Max(Weapon->BurstRecoilLater, 0.f) : 1.f; // (BurstLeft is only set once a burst's first round is away)
	P->AddRecoil(FMath::DegreesToRadians(Weapon->RecoilUp * Share), FMath::DegreesToRadians(FMath::FRandRange(-Weapon->RecoilSide, Weapon->RecoilSide) * Share),
		Weapon->RecoilKickTime, Weapon->RecoilRecovery, RecoveryTime, bShaped ? -1.f : (bSynced ? Weapon->RecoilRecoveryCurve : 0.f));
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
		// (The dot on the gun is never drawn: the HUD draws the aim mark at the aim point, where the
		// gun's kick and sway can't move it. See UWWOWeaponData::AdsMark.)
		if (Pair.Value) Pair.Value->SetVisibility(!bFirstPersonHidden && bShowModel && !bAdsHidden && Pair.Key != "SightDot" && (!IsFlashPart(Pair.Key) || bFlashVisible));
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
	// The faint tint over the enemy under the reticle (moves with it, off when nothing's there).
	AActor* Now = P->TargetHighlight > 0.f ? ReticleTarget.Get() : nullptr;
	if (Cast<AWWOCharacter>(Now)) Now = nullptr; // players flash an outline when marked instead (AWWOCharacter::UpdateTeamTint)
	if (Now != Highlighted.Get())
	{
		if (UWWOBodyComponent* Old = Highlighted.IsValid() ? Highlighted->FindComponentByClass<UWWOBodyComponent>() : nullptr) Old->SetHighlight(0.f);
		Highlighted = Now;
	}
	if (UWWOBodyComponent* Body = Now ? Now->FindComponentByClass<UWWOBodyComponent>() : nullptr) Body->SetHighlight(P->TargetHighlight);
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
	const float SlideAmount = WWO::SmoothStep(0.f, 1.f, SlidePose) * (1.f - 0.8f * AdsAmount); // the tilt
	const float SlideShift = WWO::SmoothStep(0.f, 1.f, SlidePose) * HipOnly;                   // the move

	// The rule the rest of this follows: aimed in, the gun's sight point (the model's origin) stays
	// on the aim point, where the HUD draws the aim mark. Everything that turns the gun turns it
	// about that point, and everything that moves it bodily (bob, air lag, landing, the slide's
	// shift) fades out as you aim in. Only the strafe's small sideways shift is kept.

	// Air: the gun lags behind vertical motion.
	float AirTarget = 0.f;
	if (!P->IsOnFloor() && !P->IsLunging())
	{
		AirTarget = FMath::Clamp(-P->GetVelocity().Z / WWO::M * AirLagPerSpeed, -AirLagMax, AirLagMax);
	}
	AirLag = FMath::Lerp(AirLag, AirTarget, FMath::Min(8.f * DeltaSeconds, 1.f));
	const FVector AirOffset = FVector(0.f, AirLag + P->LandOffset * 0.6f, 0.f) * HipOnly;

	// Bob: follows the player's step timing. Sprinting swings it much harder.
	const FVector Swing(FMath::Cos(P->BobTime * 0.5f), -FMath::Abs(FMath::Sin(P->BobTime)), 0.f);
	// From the hip the gun only dips (each step pushes it down from rest). Aimed in, the same dip is
	// centred on rest instead, so the sight rides evenly above and below the aim point.
	const FVector AimedSwing = Swing + FVector(0.f, 0.5f * AdsAmount, 0.f);
	FVector Bob = AimedSwing * FVector(0.012f, 0.01f, 0.f) * Weapon->GunBob * FMath::Lerp(1.f, FMath::Clamp(Weapon->AdsGunBob, 0.f, 1.f), AdsAmount); // (the one bodily move kept when aimed, with the strafe shift)
	Bob.X *= HipOnly; // aimed in the bob is up and down only: sideways the gun is where the strafe shift puts it
	Bob += Swing * FVector(0.03f, 0.025f, 0.f) * Weapon->GunBob * SprintAmount;
	Bob *= P->BobWeight;
	const float BobRoll = FMath::Cos(P->BobTime * 0.5f) * 0.08f * Weapon->GunBob * SprintAmount * P->BobWeight;

	FVector Rot(SwayValue.Y + AirLag * 2.f * Settle, SwayValue.X, BobRoll);
	Rot += SprintRotation * SprintAmount + FVector(FMath::DegreesToRadians(Weapon->SlideTilt), 0.f, 0.f) * SlideAmount;
	// Strafing: the gun trails the way you move and rolls with it.
	const float SideSpeed = FVector::DotProduct(P->GetVelocity(), P->GetActorRightVector()) / WWO::M;
	// (Against the speed you can go right now, so a full strafe is the whole of the sliders' amounts
	// aimed in too, where you move slower.)
	const float TopSpeed = FMath::Max(P->M("walk_speed", P->WalkSpeed) * FMath::Lerp(1.f, Weapon->AdsMoveSpeed, AdsAmount), 1.f);
	StrafeLean = FMath::Lerp(StrafeLean, FMath::Clamp(SideSpeed / TopSpeed, -1.f, 1.f), FMath::Min(8.f * DeltaSeconds, 1.f));
	Rot.Z += FMath::DegreesToRadians(Weapon->StrafeRoll) * StrafeLean;
	const float KickNow = Kick * 0.06f * FMath::Lerp(1.f, FMath::Clamp(Weapon->AdsKick, 0.f, 1.f), AdsAmount); // how much is left aimed in is the frame's
	Rot.X += KickNow * FMath::Max(Weapon->VerticalKick, 0.f);
	// The sideways part (the swing and the shift) comes back sooner than the tilt: it follows the
	// kick squared, so half way down the tilt it is a quarter of what it was (as in the footage).
	Rot.Y -= KickNow * Kick * Weapon->HorizontalKick; // Godot yaw: + turns left, so + kick swings the muzzle right
	// The low reticle: the gun (drawn with its own FOV) points at it, and aimed in, its sight point
	// (the model's origin) moves down onto it.
	const float GunDrop = P->ReticleAngleForFov(Weapon->ViewmodelFov);
	Rot.X -= GunDrop;
	FVector Pos = FMath::Lerp(Weapon->HipPosition, Weapon->AdsPosition, AdsAmount) + Bob + AirOffset;
	// Aimed in, level the top: the gun tips down about its sight point until its top's plane passes
	// through your eye, so the top is seen edge-on (the sight stays on the reticle).
	const float EyeToSight = FMath::Max(-Weapon->AdsPosition.Z * 100.f, 5.f); // cm
	const float LevelAngle = FMath::Atan2(TopBelowSight * FMath::Lerp(1.f, FMath::Max(AdsGunScale, 0.05f), AdsAmount), EyeToSight);
	Rot.X -= FMath::Min(LevelAngle, FMath::DegreesToRadians(8.f)) * FMath::Clamp(AdsLevel, 0.f, 1.f) * AdsAmount; // muzzle down: the top's near end comes up to the eye line
	Rot.X -= FMath::DegreesToRadians(Weapon->AdsTilt) * AdsAmount; // the gun's own aimed-in angle, also about the sight point
	Pos.Y -= -Pos.Z * FMath::Tan(GunDrop) * AdsAmount; // Godot: z is forward (negative), y up
	Pos += SprintOffset * SprintAmount + SlideOffset * SlideShift;
	Pos.X -= StrafeLean * Weapon->StrafeShift * 0.01f;
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
	Pos.Z += Kick * Weapon->KickPush * 0.01f; // back toward you at the peak
	Pos.X += Kick * Kick * Weapon->KickSide * 0.01f;
	{
		// The kick's turn is about a point below and ahead of the sight, not the sight itself: the
		// sight point swings down and back around it as the muzzle comes up.
		const float Turn = KickNow * FMath::Max(Weapon->VerticalKick, 0.f);
		const float Down = Weapon->KickPivotDown * 0.01f, Ahead = Weapon->KickPivotAhead * 0.01f;
		Pos.Y += -Ahead * FMath::Sin(Turn) - Down * (1.f - FMath::Cos(Turn));
		Pos.Z += -Ahead * (1.f - FMath::Cos(Turn)) + Down * FMath::Sin(Turn);
	}
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
	// (It stays down until the punching hand is back: HandsActionWeight, from UpdateHands a frame ago.)
	const float DropHeld = FMath::Max(Drop, HandsAction == 1 ? WWO::SmoothStep(0.f, 1.f, HandsActionWeight) : 0.f);
	Pos += MeleeGunDropOffset * DropHeld;
	Rot += MeleeGunDropRotation * DropHeld;

	Pos += DebugGunOffset;
	Rot += DebugGunRotation;
	SetRelativeLocationAndRotation(G2U(Pos), GRot(Rot));
	ModelRoot->SetRelativeScale3D(FVector(FMath::Lerp(1.f, FMath::Max(AdsGunScale, 0.05f), AdsAmount))); // the model's origin is its sight point

	// The punch and the throw are played by the hands (UpdateHands); the old single arms stay hidden.
	ThrowClock += DeltaSeconds;
	HandsDelta = DeltaSeconds;
	UpdateHands();
	const float MeleeT = bMeleeing ? MeleeAnimTime(MeleeProgress) : 0.f;
	PlaceArm(MeleeArm, MeleeT, MeleeArmYaw, MeleeArmPitch, MeleeArmPush, 0.f, false);
	PlaceArm(ThrowArm, 0.f, ThrowArmYaw, ThrowArmPitch, ThrowArmPush, 0.f, false);
	// The grenade sits in the throwing (right) hand until it leaves.
	bool bBall = HandsMesh && HandsMesh->IsVisible() && HandsOwner.Num() > 0 && ThrowClock < ThrowRelease;
	const int32 Thrower = 1; // the left hand
	const FName ThrowBone = bBall ? HandsAsset->GetRefSkeleton().GetBoneName(HandsArms[Thrower].Hand) : NAME_None;
	// (Not while the hand is beside your head: that close the ball would fill the view.)
	if (bBall && HandsMesh->GetBoneTransformByName(ThrowBone, EBoneSpaces::ComponentSpace).GetLocation().X < 25.f) bBall = false;
	ThrowArm.Ball->SetVisibility(bBall);
	if (bBall)
	{
		const FTransform HandWorld = HandsMesh->GetBoneTransformByName(ThrowBone, EBoneSpaces::WorldSpace);
		const FQuat FromRef = HandsMesh->GetBoneTransformByName(ThrowBone, EBoneSpaces::ComponentSpace).GetRotation() * HandsArms[Thrower].HandRef.Inverse();
		const FVector InPalm = bHandsRigid ? FromRef.RotateVector(HandsArms[Thrower].RigidRef.GetAxisX() * 9.f + HandsArms[Thrower].RigidRef.GetAxisZ() * 4.f) : FVector::ZeroVector;
		ThrowArm.Ball->SetWorldLocation(HandWorld.GetLocation() + HandsMesh->GetComponentTransform().TransformVectorNoScale(InPalm));
	}
	if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(ThrowArm.Ball->GetMaterial(0)))
	{
		Mat->SetVectorParameterValue("Color", ThrowColor);
		Mat->SetScalarParameterValue("Glow", 2.f); // Godot: the held ball's glow
	}
}

// --- Hands on the gun ----------------------------------------------------------------------------

namespace
{
	// The first-person rifle pose the hands copy (xidePunk First Person Rifle Animation, a Fab pack).
	// Its arms mesh hangs off the demo camera by ClipToCamera, and its rifle off hand_r by ClipRifle.
	const TCHAR* HandsPosePath = TEXT("/Game/FirstPersonRifleAnimations/Animations/AS_Rifle_Idle_Pose.AS_Rifle_Idle_Pose");
	const FTransform ClipToCamera(FRotator(0.f, -90.f, 0.f), FVector(7.f, 0.f, -162.614f));
	const FTransform ClipRifle(FRotator(-10.451844f, 167.601919f, 11.114348f), FVector(-8.663722f, 2.838672f, 0.827358f));
	const FTransform MeshToCamera(FRotator(0.f, -90.f, 0.f)); // a mannequin-style mesh (faces +y) turned to face the camera's +x

	// A clip's bone in its skeleton's mesh space at Seconds.
	FTransform ClipBone(const UAnimSequence* Anim, FName Bone, float Seconds)
	{
		const FReferenceSkeleton& Ref = Anim->GetSkeleton()->GetReferenceSkeleton();
		FTransform Out = FTransform::Identity;
		for (int32 B = Ref.FindBoneIndex(Bone); B != INDEX_NONE; B = Ref.GetParentIndex(B))
		{
			FTransform Local;
			Anim->GetBoneTransform(Local, FSkeletonPoseBoneIndex(B), FAnimExtractContext(double(Seconds)), false);
			Out = Out * Local;
		}
		return Out;
	}

	// A bone of a reference skeleton in mesh space.
	FTransform RefBone(const FReferenceSkeleton& Ref, int32 Bone)
	{
		FTransform Out = FTransform::Identity;
		for (int32 B = Bone; B != INDEX_NONE; B = Ref.GetParentIndex(B)) Out = Out * Ref.GetRefBonePose()[B];
		return Out;
	}

	// A frame for a forearm: x along it, y toward the body's outside.
	FQuat ForearmFrame(const FVector& Along, bool bLeft)
	{
		return FRotationMatrix::MakeFromXY(Along, FVector(0.f, bLeft ? -1.f : 1.f, 0.f)).ToQuat();
	}
}

void UWWOWeaponComponent::BuildHandsClip(FHandsClip& Clip, const UAnimSequence* Anim, const FTransform& ToCamera, float Start, float Key)
{
	Clip.bValid = false;
	if (!Anim || !Anim->GetSkeleton() || HandsOwner.Num() == 0) return;
	const FReferenceSkeleton& ClipRef = Anim->GetSkeleton()->GetReferenceSkeleton();
	Clip.ToCamera = ToCamera;
	Clip.Start = Start;
	Clip.Key = Key;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bLeft = Side == 1;
		const int32 Lower = ClipRef.FindBoneIndex(bLeft ? "lowerarm_l" : "lowerarm_r"), Hand = ClipRef.FindBoneIndex(bLeft ? "hand_l" : "hand_r");
		if (Lower == INDEX_NONE || Hand == INDEX_NONE) return;
		const FTransform L = RefBone(ClipRef, Lower) * MeshToCamera, H = RefBone(ClipRef, Hand) * MeshToCamera;
		Clip.HandRef[Side] = H.GetRotation();
		Clip.HandAlign[Side] = ForearmFrame((H.GetLocation() - L.GetLocation()).GetSafeNormal(), bLeft) * ForearmFrame(HandsArms[Side].LowerDir, bLeft).Inverse();
	}
	Clip.bValid = true;
}

void UWWOWeaponComponent::BuildHands(USkeletalMesh* Asset)
{
	HandsAsset = Asset;
	if (!HandsPose) HandsPose = LoadObject<UAnimSequence>(nullptr, HandsPosePath);
	if (!HandsMesh)
	{
		HandsMesh = NewObject<UPoseableMeshComponent>(GetOwner());
		HandsMesh->SetupAttachment(GetAttachParent()); // the camera: its component space is camera space
		HandsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HandsMesh->SetCastShadow(false);
		HandsMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
		HandsMesh->SetBoundsScale(4.f);
		HandsMesh->RegisterComponent();
	}
	HandsMesh->SetSkinnedAssetAndUpdate(Asset);
	HandsRefLocal.Reset();
	HandsOwner.Reset();
	if (!Asset || !HandsPose || !HandsPose->GetSkeleton()) return;
	const AWWOCharacter* Player = GetPlayer();
	bHandsRigid = Player && Player->Body && Player->Body->GetModel() && Player->Body->GetModel()->GetSkeletalMeshAsset() == Asset && Player->Body->bRigidHands;
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const FReferenceSkeleton& ClipRef = HandsPose->GetSkeleton()->GetReferenceSkeleton();
	HandsRefLocal = Ref.GetRefBonePose();
	HandsOwner.Init(-1, Ref.GetNum());
	const FTransform ClipHandR = ClipBone(HandsPose, "hand_r", 0.f) * ClipToCamera;
	HandsClipGun = ClipRifle * ClipHandR;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bLeft = Side == 1;
		const TCHAR* S = bLeft ? TEXT("_l") : TEXT("_r");
		const FName UpperName(*FString::Printf(TEXT("upperarm%s"), S)), LowerName(*FString::Printf(TEXT("lowerarm%s"), S)), HandName(*FString::Printf(TEXT("hand%s"), S));
		FHandsArm& A = HandsArms[Side];
		A.Upper = Ref.FindBoneIndex(UpperName);
		A.Lower = Ref.FindBoneIndex(LowerName);
		A.Hand = Ref.FindBoneIndex(HandName);
		if (A.Upper == INDEX_NONE || A.Lower == INDEX_NONE || A.Hand == INDEX_NONE) { HandsOwner.Reset(); return; }
		const FTransform U = RefBone(Ref, A.Upper) * MeshToCamera, L = RefBone(Ref, A.Lower) * MeshToCamera, H = RefBone(Ref, A.Hand) * MeshToCamera;
		A.UpperLength = FVector::Dist(U.GetLocation(), L.GetLocation());
		A.LowerLength = FVector::Dist(L.GetLocation(), H.GetLocation());
		A.UpperRef = U.GetRotation();
		A.LowerRef = L.GetRotation();
		A.HandRef = H.GetRotation();
		A.UpperDir = (L.GetLocation() - U.GetLocation()).GetSafeNormal();
		A.LowerDir = (H.GetLocation() - L.GetLocation()).GetSafeNormal();
		const FTransform ClipL = RefBone(ClipRef, ClipRef.FindBoneIndex(LowerName)) * MeshToCamera, ClipH = RefBone(ClipRef, ClipRef.FindBoneIndex(HandName)) * MeshToCamera;
		A.ClipHandRef = ClipH.GetRotation();
		A.HandAlign = ForearmFrame((ClipH.GetLocation() - ClipL.GetLocation()).GetSafeNormal(), bLeft) * ForearmFrame(A.LowerDir, bLeft).Inverse();
		A.ClipShoulder = ClipBone(HandsPose, UpperName, 0.f) * ClipToCamera;
		A.ClipElbow = ClipBone(HandsPose, LowerName, 0.f) * ClipToCamera;
		A.ClipHand = ClipBone(HandsPose, HandName, 0.f) * ClipToCamera;
		HandsOwner[A.Upper] = HandsOwner[A.Lower] = HandsOwner[A.Hand] = -2;
		if (bHandsRigid)
		{
			const AWWOCharacter* Owner = GetPlayer();
			const FVector Flip(bLeft ? -1.f : 1.f, 1.f, 1.f); // the left hand is the right one mirrored
			A.RigidRef = FRotationMatrix::MakeFromXZ(MeshToCamera.TransformVector(Owner->Body->HandFingers * Flip), MeshToCamera.TransformVector(Owner->Body->HandPalm * Flip)).ToQuat();
		}
	}
	// Whatever hangs off a hand (fingers) goes with it.
	for (int32 B = 0; B < Ref.GetNum(); ++B)
	{
		const int32 Parent = Ref.GetParentIndex(B);
		if (HandsOwner[B] != -1 || Parent == INDEX_NONE) continue;
		if (Parent == HandsArms[0].Hand || HandsOwner[Parent] == 0) HandsOwner[B] = 0;
		else if (Parent == HandsArms[1].Hand || HandsOwner[Parent] == 1) HandsOwner[B] = 1;
	}
	// The action clips (Fab packs: First person brawler animation pack, xidePunk First Person Grenade
	// Animations), each with where its arms hang off its demo camera; the throw's wind-up starts 0.3 s in and the grenade leaves the hand at 0.78 s.
	if (!HandsPunchAnim) HandsPunchAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/FirstPersonBrawler/Character/Animations/FP_Hook_R.FP_Hook_R"));
	if (!HandsThrowAnim) HandsThrowAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/FirstPersonGrenadeAnimation/Animations/AS_Player_Grenade_Throw.AS_Player_Grenade_Throw"));
	// The hook: the fist goes out wide to the right by 0.15 s, comes across the middle (its furthest
	// forward) at about 0.25 s, and carries on to the left until 0.40 s.
	// A swing that hits something stops at the middle instead of carrying through (UpdateHands).
	// It is played as the pack made it: its own reach (the fist 34 cm ahead of the eyes at most),
	// shoulder and wrist turn. (Reach, Lean and bFist can push the fist further out, lean the
	// shoulder in and square the fist to the forearm; all off here.)
	// (Key: the wrist is still 12 cm right of the middle at 0.215 s, which is when the fist itself,
	// turned in ahead of the wrist, is over the reticle.)
	BuildHandsClip(HandsPunch, HandsPunchAnim, FTransform(FRotator(0.f, -90.f, 0.f), FVector(-3.96167f, 0.f, -166.104431f)), 0.f, 0.215f);
	HandsPunch.Reach = 1.f;
	HandsPunch.Lean = 0.f;
	HandsPunch.bFist = false;
	HandsPunch.Offset = FVector(20.f, 0.f, 0.f); // the whole swing, unchanged in shape, 20 cm further from the eyes
	HandsPunch.Roll = 10.f;                      // turned so the elbow rides higher and the fist comes in lower (at 20 the forearm is level with the eyes and fills the view)
	HandsPunch.bOnReticle = true;                // and placed so the fist crosses the reticle itself: where the hit lands
	BuildHandsClip(HandsThrow, HandsThrowAnim, FTransform(FRotator(0.f, -90.f, 0.f), FVector(10.f, 0.f, -162.61403f)), 0.3f, 0.78f);
	HandsThrow.bMirror = true; // thrown with the left hand: the right one stays on the gun
	HandsThrow.Arms = 2;
	HandsPunch.Arms = 1;       // the right hand punches: the left one keeps the gun as it drops out of the way
}

void UWWOWeaponComponent::UpdateHands()
{
	AWWOCharacter* P = GetPlayer();
	if (bFirstPersonHidden) // someone else's character or a bot: nobody sees first-person hands on it
	{
		if (HandsMesh) HandsMesh->SetVisibility(false);
		return;
	}
	USkeletalMesh* Want = nullptr;
	if (P && P->Body && P->Body->GetModel()) Want = P->Body->GetModel()->GetSkeletalMeshAsset();
	if (!Want) Want = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (Want != HandsAsset || !HandsMesh) BuildHands(Want);
	const bool bShow = !bFirstPersonHidden && HandsOwner.Num() > 0 && IsVisible();
	HandsMesh->SetVisibility(bShow);
	if (!bShow) return;
	const FReferenceSkeleton& Ref = HandsAsset->GetRefSkeleton();
	{
		// This gun's grip and fore-end (Config/WeaponModels.json: hand, and the grip mod's anchor).
		const TSharedPtr<FJsonObject> Model = Weapon ? ModelConfig(Weapon->Id) : nullptr;
		const TSharedPtr<FJsonObject>* Anchors = nullptr;
		const FVector Offset = JsonVec(Model, TEXT("offset")) + ModelShift;
		HandsGrip = Offset + JsonVec(Model, TEXT("hand"), FVector(-3.f, 0.f, -4.f));
		HandsFore = Offset + (Model.IsValid() && Model->TryGetObjectField(TEXT("anchors"), Anchors) ? JsonVec(*Anchors, TEXT("grip"), FVector(22.f, 0.f, -1.f)) : FVector(22.f, 0.f, -1.f));
		HandsMag = Offset + (Model.IsValid() && Model->TryGetObjectField(TEXT("anchors"), Anchors) ? JsonVec(*Anchors, TEXT("magazine"), FVector(8.f, 0.f, -6.f)) : FVector(8.f, 0.f, -6.f));
		bHandsPistol = Weapon && Weapon->Id == "handcannon";
	}
	// The clip's rifle onto this gun: anything held against the clip's rifle moves to the same place on ours.
	const FTransform GunOurs = FTransform(HandsGunOffset) * GetRelativeTransform();
	const FTransform ClipToOurs = HandsClipGun.Inverse() * GunOurs;
	TArray<FTransform> CS;
	CS.SetNum(Ref.GetNum());
	FVector Hidden = FVector(-40.f, 0.f, -40.f); // behind and below the camera
	// The punch and the throw: the arms leave the gun (which drops out of the way by itself) and
	// play a first-person clip at its own speed, placed in time so its key moment (the fist landing,
	// the grenade leaving the hand) falls on the game's (the melee hit, the throw's release).
	{
		const float MeleeSeconds = FMath::Max(M("melee_time", MeleeTime), 0.05f);
		const float ThrowEnd = HandsThrow.bValid ? ThrowRelease + (HandsThrowAnim->GetPlayLength() - HandsThrow.Key) : 0.f;
		int32 Playing = 0;
		float Time = 0.f;
		HandsHitTime = bMeleeing && bMeleeConnected ? HandsHitTime + HandsDelta : 0.f;
		if (bMeleeing && HandsPunch.bValid && HandsHitTime <= HandsHitHold) // (after a hit's short hold the hand goes straight back to the gun)
		{
			// The swing fills the melee: up to the hit it plays from its start to its key moment, after
			// it the rest, so the melee time setting speeds the whole swing up or slows it down.
			Playing = 1;
			const float End = HandsPunchAnim->GetPlayLength() - 0.1f;
			Time = MeleeProgress < MeleeImpactAt
				? FMath::Lerp(HandsPunch.Start, HandsPunch.Key, MeleeProgress / MeleeImpactAt)
				: FMath::Lerp(HandsPunch.Key, End, (MeleeProgress - MeleeImpactAt) / (1.f - MeleeImpactAt));
			if (bMeleeConnected) Time = FMath::Min(Time, HandsPunch.Key); // it hit: the fist stops there, on the reticle
		}
		else if (HandsAction == 1 && HandsPunch.bValid && !bMeleeConnected && HandsActionTime < HandsPunchAnim->GetPlayLength() - 0.12f) { Playing = 1; Time = HandsActionTime + HandsDelta; } // a miss swings through and comes back
		else if (HandsThrow.bValid && ThrowClock < ThrowEnd - 0.15f) { Playing = 2; Time = HandsThrow.Key + (ThrowClock - ThrowRelease); }
		if (Playing != 0)
		{
			const FHandsClip& Clip = Playing == 1 ? HandsPunch : HandsThrow;
			HandsAction = Playing;
			HandsActionTime = FMath::Max(Time, Clip.Start);
		}
		HandsActionWeight = WWO::MoveToward(HandsActionWeight, Playing != 0 ? 1.f : 0.f, HandsDelta / (Playing != 0 ? 0.08f : 0.18f));
		if (HandsActionWeight <= 0.f) HandsAction = 0;
	}
	const FHandsClip* Action = HandsAction == 1 ? &HandsPunch : HandsAction == 2 ? &HandsThrow : nullptr;
	const UAnimSequence* ActionAnim = HandsAction == 1 ? HandsPunchAnim : HandsThrowAnim;
	const float ActionWeight = Action ? WWO::SmoothStep(0.f, 1.f, HandsActionWeight) : 0.f;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FHandsArm& A = HandsArms[Side];
		FTransform Hand = A.ClipHand * ClipToOurs;
		FQuat HandRotation = Hand.GetRotation() * A.ClipHandRef.Inverse() * A.HandAlign * A.HandRef; // turns as the clip's does from its reference pose
		if (bHandsRigid)
		{
			// A rigid hand lies flat against the gun: the right one along the right of the grip, the
			// left one under the fore-end (a pistol: along the left of the grip). Touch is where the
			// palm meets the gun, Fingers and Palm the way the hand points and faces, in gun space.
			FVector Touch, Fingers, Palm;
			// (The knuckles sit 2 cm short of Touch along the fingers, which then close toward the palm:
			// the right hand's around the front of the grip, the left's up around the far side of the fore-end.)
			if (Side == 0) { Touch = HandsGrip + FVector(4.f, 1.5f, -2.f); Fingers = FVector(1.f, 0.f, 0.25f); Palm = FVector(0.f, -1.f, 0.f); }
			else if (bHandsPistol) { Touch = HandsGrip + FVector(4.5f, -1.5f, -3.f); Fingers = FVector(1.f, 0.f, 0.2f); Palm = FVector(0.f, 1.f, 0.f); }
			else { Touch = HandsFore + FVector(0.f, 2.f, -1.f); Fingers = FVector(0.35f, 1.f, 0.1f); Palm = FVector(0.f, 0.f, 1.f); }
			const FTransform Gun = GetRelativeTransform();
			// The hand's middle is 11 cm from the wrist along the fingers and 2 cm behind the wrist's line; the palm's surface 3.5 cm in front of its middle.
			auto Lay = [&](FVector At, FVector Along, FVector Facing)
			{
				Along.Normalize();
				Facing = (Facing - Along * FVector::DotProduct(Facing, Along)).GetSafeNormal();
				return FTransform(Gun.GetRotation() * FRotationMatrix::MakeFromXZ(Along, Facing).ToQuat() * A.RigidRef.Inverse() * A.HandRef,
					Gun.TransformPositionNoScale(At - Facing * 1.5f - Along * 11.f));
			};
			Hand = Lay(Touch, Fingers, Palm);
			if (Side == 1 && bReloading)
			{
				// Reloading: the left hand goes to the magazine, takes it down and out as the gun's own
				// magazine drops (UpdatePose), brings it back up, and returns to its hold.
				const float RP = ReloadProgress;
				const float AtMag = WWO::SmoothStep(0.06f, 0.2f, RP) * (1.f - WWO::SmoothStep(FMath::Max(ReloadSeated, 0.6f), FMath::Min(FMath::Max(ReloadSeated, 0.6f) + 0.2f, 1.f), RP));
				const float MagOut = WWO::SmoothStep(0.2f, 0.4f, RP) * (1.f - WWO::SmoothStep(0.5f, ReloadSeated, RP));
				const FTransform OnMag = Lay(HandsMag + FVector(0.f, -1.5f, -3.f - ReloadMagDrop * 100.f * MagOut), FVector(0.3f, 0.f, 1.f), FVector(0.f, 1.f, 0.f));
				Hand.Blend(Hand, OnMag, AtMag);
			}
			HandRotation = Hand.GetRotation();
		}
		FVector Shoulder = A.ClipShoulder.GetLocation();
		FVector ElbowHint = A.ClipElbow.GetLocation() + (Hand.GetLocation() - A.ClipHand.GetLocation()) * 0.5f;
		if (Action && (Action->Arms & (1 << Side)))
		{
			// (Mirrored: this arm plays the clip's other arm, flipped left for right across the view.)
			const int32 From = Action->bMirror ? 1 - Side : Side;
			const bool bLeft = From == 1;
			auto Flip = [&](const FVector& V) { return Action->bMirror ? FVector(V.X, -V.Y, V.Z) : V; };
			auto FlipQ = [&](const FQuat& Q) { return Action->bMirror ? FQuat(-Q.X, Q.Y, -Q.Z, Q.W) : Q; };
			const FTransform ClipHandRaw = ClipBone(ActionAnim, bLeft ? "hand_l" : "hand_r", HandsActionTime) * Action->ToCamera;
			FTransform ClipHand(FlipQ(ClipHandRaw.GetRotation()), Flip(ClipHandRaw.GetLocation()));
			FVector ClipShoulder = Flip((ClipBone(ActionAnim, bLeft ? "upperarm_l" : "upperarm_r", HandsActionTime) * Action->ToCamera).GetLocation());
			FVector ClipElbow = Flip((ClipBone(ActionAnim, bLeft ? "lowerarm_l" : "lowerarm_r", HandsActionTime) * Action->ToCamera).GetLocation());
			// The arm turned as one piece about the view's forward axis (nothing in the swing itself changes).
			const float RollRadians = FMath::DegreesToRadians(Action->Roll);
			const FQuat Turn = FQuat::FindBetweenNormals(FVector(0.f, 1.f, 0.f), FVector(0.f, FMath::Cos(RollRadians), FMath::Sin(RollRadians)));
			ClipHand = FTransform(Turn * ClipHand.GetRotation(), Turn.RotateVector(ClipHand.GetLocation()));
			ClipShoulder = Turn.RotateVector(ClipShoulder);
			ClipElbow = Turn.RotateVector(ClipElbow);
			FVector Move = Action->Offset;
			if (Action->bOnReticle && P && Weapon)
			{
				// Where the knuckles are at the key moment, against the reticle (which sits below the
				// middle of the view: the same angle the gun points down by, UpdatePose).
				const FVector Knuckles = Turn.RotateVector(Flip((ClipBone(ActionAnim, bLeft ? "middle_01_l" : "middle_01_r", Action->Key) * Action->ToCamera).GetLocation())) + Action->Offset;
				const float Drop = P->ReticleAngleForFov(Weapon->ViewmodelFov);
				Move += FVector(0.f, -Knuckles.Y, -Knuckles.X * FMath::Tan(Drop) - Knuckles.Z);
			}
			ClipHand.AddToTranslation(Move);
			ClipShoulder += Move;
			ClipElbow += Move;
			if (Action->Reach != 1.f)
			{
				const float Rest = (ClipBone(ActionAnim, bLeft ? "hand_l" : "hand_r", 0.f) * Action->ToCamera).GetLocation().X;
				FVector At = ClipHand.GetLocation();
				const float Was = At.X;
				At.X = FMath::Max(Rest + (At.X - Rest) * Action->Reach, 6.f); // (never pulled back into your face)
				ClipHand.SetLocation(At);
				const float Leaning = FMath::Max(At.X - Was, 0.f) * Action->Lean; // forward only: the shoulder doesn't pull back with the wind-up
				ClipShoulder.X += Leaning;
				ClipElbow.X += Leaning;
			}
			const FQuat ClipRotation = ClipHand.GetRotation() * FlipQ(Action->HandRef[From]).Inverse() * Action->HandAlign[Side] * A.HandRef;
			Hand.SetLocation(FMath::Lerp(Hand.GetLocation(), ClipHand.GetLocation(), ActionWeight));
			HandRotation = FQuat::Slerp(HandRotation, ClipRotation, ActionWeight);
			Shoulder = FMath::Lerp(Shoulder, ClipShoulder, ActionWeight);
			ElbowHint = FMath::Lerp(ElbowHint, ClipElbow, ActionWeight);
		}
		// Two-bone reach: the elbow goes where the clip's does, as near as the model's own arm lengths allow.
		FVector To = Hand.GetLocation() - Shoulder;
		const float Reach = FMath::Clamp(To.Size(), FMath::Abs(A.UpperLength - A.LowerLength) + 1.f, A.UpperLength + A.LowerLength - 0.5f);
		const FVector Axis = To.GetSafeNormal();
		const FVector Wrist = Shoulder + Axis * Reach;
		const float Along = (A.UpperLength * A.UpperLength - A.LowerLength * A.LowerLength + Reach * Reach) / (2.f * Reach);
		const float Out = FMath::Sqrt(FMath::Max(A.UpperLength * A.UpperLength - Along * Along, 0.f));
		const FVector Hint = ElbowHint - Shoulder;
		FVector Pole = (Hint - Axis * FVector::DotProduct(Hint, Axis)).GetSafeNormal();
		if (Pole.IsNearlyZero()) Pole = FVector(0.f, Side == 1 ? -1.f : 1.f, -1.f).GetSafeNormal();
		const FVector Elbow = Shoulder + Axis * Along + Pole * Out;
		CS[A.Upper] = FTransform(FQuat::FindBetweenNormals(A.UpperDir, (Elbow - Shoulder).GetSafeNormal()) * A.UpperRef, Shoulder);
		CS[A.Lower] = FTransform(FQuat::FindBetweenNormals(A.LowerDir, (Wrist - Elbow).GetSafeNormal()) * A.LowerRef, Elbow);
		if (Action && Action->bFist && bHandsRigid && (Action->Arms & (1 << Side)))
		{
			// The fist in line with the forearm, the back of the hand up and a little outward.
			const FVector Forearm = (Wrist - Elbow).GetSafeNormal();
			FVector PalmWay = FVector(0.f, Side == 1 ? 0.35f : -0.35f, -1.f);
			PalmWay = (PalmWay - Forearm * FVector::DotProduct(PalmWay, Forearm)).GetSafeNormal();
			const FQuat Fist = FRotationMatrix::MakeFromXZ(Forearm, PalmWay).ToQuat() * A.RigidRef.Inverse() * A.HandRef;
			HandRotation = FQuat::Slerp(HandRotation, Fist, ActionWeight);
		}
		CS[A.Hand] = FTransform(HandRotation, Wrist);
	}
	Hidden = CS[HandsArms[0].Upper].GetLocation();
	// Fingers: closed around the gun, a fist for the punch, around the grenade and then open as it leaves.
	FQuat Curl[2] = { FQuat::Identity, FQuat::Identity }, ThumbCurl[2] = { FQuat::Identity, FQuat::Identity };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bActing = Action && (Action->Arms & (1 << Side));
		float Hold = Side == 0 ? 0.55f : 0.45f;
		float Acting = HandsAction == 1 ? 1.f : (ThrowClock < ThrowRelease ? 0.5f : 0.f);
		const float Target = bActing ? FMath::Lerp(Hold, Acting, ActionWeight) : Hold;
		HandsCurl[Side] = WWO::MoveToward(HandsCurl[Side], Target, HandsDelta * 8.f);
		if (!bHandsRigid) continue;
		const FQuat FromRef = CS[HandsArms[Side].Hand].GetRotation() * HandsArms[Side].HandRef.Inverse();
		const FVector Along = FromRef.RotateVector(HandsArms[Side].RigidRef.GetAxisX()), Facing = FromRef.RotateVector(HandsArms[Side].RigidRef.GetAxisZ());
		auto Toward = [&](float Degrees) { const float R = FMath::DegreesToRadians(Degrees); return FQuat::FindBetweenNormals(Along, (Along * FMath::Cos(R) + Facing * FMath::Sin(R)).GetSafeNormal()); };
		Curl[Side] = Toward(40.f * HandsCurl[Side]);      // per joint
		ThumbCurl[Side] = Toward(18.f * HandsCurl[Side]);
	}
	for (int32 B = 0; B < Ref.GetNum(); ++B) // parents first
	{
		if (HandsOwner[B] == -1) CS[B] = FTransform(FQuat::Identity, Hidden, FVector(0.001f));
		else if (HandsOwner[B] >= 0)
		{
			CS[B] = HandsRefLocal[B] * CS[Ref.GetParentIndex(B)];
			const bool bThumb = Ref.GetBoneName(B).ToString().StartsWith(TEXT("thumb"));
			CS[B].SetRotation((bThumb ? ThumbCurl : Curl)[HandsOwner[B]] * CS[B].GetRotation());
		}
		HandsMesh->SetBoneTransformByName(Ref.GetBoneName(B), CS[B], EBoneSpaces::ComponentSpace);
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
	bVisible = bVisible && !bFirstPersonHidden;
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
	Scale(Weapon->PelletSpread, "pellet_spread");
	Scale(Weapon->HipSpread, "hip_spread");
	// Zoom is a magnification: +40% means 40% more of the zoom past 1x.
	Weapon->Zoom = 1.f + (Weapon->Zoom - 1.f) * FMath::Max(1.f + L->ModPercent("zoom"), 0.f);
	Weapon->MagazineSize = FMath::Max(1.f, FMath::RoundToFloat(Weapon->MagazineSize));

	// Perks that change stats while they're active.
	const AWWOCharacter* Pl = GetPlayer();
	if (L->HasPerk("focus") && HitStreak >= 5) Weapon->Damage *= Focus;
	if (L->HasPerk("brace") && Pl && (Pl->bCrouching || Pl->bSliding))
	{
		Weapon->RecoilUp *= 0.6f;
		Weapon->RecoilSide *= 0.6f;
	}
	if (L->HasPerk("hunter")) Weapon->CritMultiplier += HunterCrit * AdsAmount;
	if (L->HasPerk("anchor") && IsAnchored())
	{
		Weapon->RecoilUp *= 0.5f;
		Weapon->RecoilSide *= 0.5f;
	}
	if (L->HasPerk("hammer") && HammerLeft > 0.f) Weapon->FireRate *= HammerRate;
	if (L->HasPerk("breath") && AimHeld >= BreathTime)
	{
		Weapon->Sway *= 0.1f;
		Weapon->RecoilUp *= 0.7f;
		Weapon->RecoilSide *= 0.7f;
	}
	if (L->HasPerk("slide_shot") && Pl && Pl->bSliding)
	{
		Weapon->PelletSpread *= 0.5f;
		Weapon->Damage *= SlideShot;
	}
	if (L->HasPerk("tighten")) Weapon->PelletSpread *= 1.f - 0.3f * AdsAmount;
	if (L->HasPerk("fourth_round") && Weapon->BurstCount() > 1) Weapon->Burst = Weapon->BurstCount() + 1.f;
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
	AimHeld = 0.f;         // Patience and Held Breath start over
	if (HitCount > 0) ++BurstHits; // Full Burst
	if (bCritical && L->HasPerk("refund")) ++RefundRounds; // Marksman
	const bool bJoiner = GetNetMode() == NM_Client; // a joiner's arcs are the host's (ServerGunShot)
	if (!bJoiner && bCritical && FirstHit && L->HasPerk("ricochet"))
	{
		// One bounce from the target that was hit, for 60% of the gun's damage.
		AWWOChainLightning::SpawnFrom(GetWorld(), FirstHit, GetOwner(), FLinearColor(1.f, 0.62f, 0.2f),
			Weapon->Damage * 0.6f, 8.f, 1.f, 1, 0.04f);
	}
	// Focus: a shot that hits nothing ends the streak.
	HitStreak = HitCount > 0 ? HitStreak + 1 : 0;
	ApplyKillPerks(Kills);
	if (HitCount > 0 && L->HasPerk("static"))
	{
		StaticHits += 1;
		if (StaticHits >= 10 && (FirstHit || bJoiner))
		{
			StaticHits = 0;
			// Two jumps from the target that was hit, for twice the gun's damage each.
			if (!bJoiner) AWWOChainLightning::SpawnFrom(GetWorld(), FirstHit, GetOwner(), FLinearColor(0.45f, 0.75f, 1.f),
				Weapon->Damage * 2.f, 6.f, 0.85f, 2, 0.07f);
		}
	}
}

void UWWOWeaponComponent::ApplyKillPerks(int32 Kills)
{
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L || Kills <= 0) return;
	if (L->HasPerk("hammer")) HammerLeft = 3.f;
	if (L->HasPerk("scavenger"))
	{
		Ammo = FMath::Min(Ammo + FMath::Max(1, FMath::RoundToInt(MagazineSize() * 0.2f)) * Kills, MagazineSize());
	}
	if (L->HasPerk("adrenal")) AdrenalLeft = 4.f;
}

void UWWOWeaponComponent::OnKillsConfirmed(int32 Kills, bool bMelee)
{
	if (Kills <= 0) return;
	if (!bMelee)
	{
		ApplyKillPerks(Kills);
		if (!bLastShotPredictedKill) OnHitConfirmed.Broadcast(false, true); // a kill this machine didn't see coming
		bLastShotPredictedKill = false;
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
	if (L->HasPerk("patience") && AdsAmount > 0.5f) Active.Add(AimHeld >= PatienceTime ? FString(TEXT("Patience READY")) : FString(TEXT("Patience")));
	if (L->HasPerk("hammer") && HammerLeft > 0.f) Active.Add(FString::Printf(TEXT("Hammer %.1fs"), HammerLeft));
	if (L->HasPerk("cold_bore") && SinceShot >= ColdBoreTime) Active.Add(TEXT("Cold Bore READY"));
	if (L->HasPerk("tighten") && AdsAmount > 0.5f) Active.Add(TEXT("Tightened"));
	if (L->HasPerk("refund")) Active.Add(TEXT("Marksman"));
	if (L->HasPerk("fourth_round")) Active.Add(TEXT("Fourth Round"));
	// The ones that depend on what you're aiming at: lit while the target under the reticle qualifies.
	if (const IWWOTargetable* Aimed = Cast<IWWOTargetable>(ReticleTarget.Get()); Aimed && Pl)
	{
		const float Dist = FVector::Dist(Pl->EyeLocation(), Aimed->BodyCenter()) / WWO::M;
		if (L->HasPerk("longshot") && Dist >= LongShotRange) Active.Add(TEXT("Long Shot"));
		if (L->HasPerk("point_blank") && Dist <= PointBlankRange) Active.Add(TEXT("Point Blank"));
		if (L->HasPerk("executioner") && Aimed->GetHealth() + Aimed->GetShield() < 0.5f * (Aimed->GetMaxHealth() + Aimed->GetMaxShield())) Active.Add(TEXT("Finisher"));
	}
	if (L->HasPerk("breath") && AdsAmount > 0.5f) Active.Add(AimHeld >= BreathTime ? FString(TEXT("Breath HELD")) : FString(TEXT("Breath")));
	if (L->HasPerk("slide_shot") && Pl && Pl->bSliding) Active.Add(TEXT("Slideshot"));
	if (L->HasPerk("full_burst") && (bFullBurst || bBurstBoosted)) Active.Add(TEXT("Full Burst READY"));
	return FString::Join(Active, TEXT("  ·  "));
}
