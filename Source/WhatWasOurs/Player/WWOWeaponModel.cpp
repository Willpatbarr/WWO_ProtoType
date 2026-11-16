// UWWOWeaponComponent: the gun models: the first-person model (Config/WeaponModels.json or the box
// model, its mod parts and muzzle flash), the grenade in the throwing hand, and the gun other players
// see in this character's hand (UpdateWorldGun).

#include "Player/WWOWeaponComponent.h"
#include "WhatWasOurs.h"
#include "Player/WWOWeaponPrivate.h"
#include "Player/WWOCharacter.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Engine/World.h"
#include "Systems/WWOConfig.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Characters/WWOBodyComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"

using namespace WWOWeapon; // the helpers these files share (WWOWeaponPrivate.h)

namespace
{
	FVector Deg(float X, float Y, float Z) { return FVector(FMath::DegreesToRadians(X), FMath::DegreesToRadians(Y), FMath::DegreesToRadians(Z)); }

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
}

namespace WWOWeapon
{
	// Config/WeaponModels.json: which mesh each weapon uses and where its mod parts attach.
	// Retuned with a restart and no build.
	TSharedPtr<FJsonObject> ModelConfig(FName WeaponId)
	{
		// Read and parsed once a run (every equip, every bot's gun swap and every hand-held gun asks).
		const TSharedPtr<FJsonObject> Json = WWOConfig::LoadCached(TEXT("WeaponModels.json"));
		static bool bMerged = false;
		if (!bMerged)
		{
			bMerged = true;
			// Saved/WWO/WeaponModels.local.json (this machine only, never in git or a build): its
			// "weapons" entries replace the shipped ones, for trying a model that isn't the game's.
			const TSharedPtr<FJsonObject> Local = Json.IsValid() ? WWOConfig::Load(WWOConfig::SavedPath(TEXT("WeaponModels.local.json"))) : nullptr;
			const TSharedPtr<FJsonObject>* Ours = nullptr;
			const TSharedPtr<FJsonObject>* Theirs = nullptr;
			if (Local.IsValid() && Json->TryGetObjectField(TEXT("weapons"), Ours) && Local->TryGetObjectField(TEXT("weapons"), Theirs))
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
}

UStaticMeshComponent* UWWOWeaponComponent::Part(FName Name) const
{
	const TObjectPtr<UStaticMeshComponent>* Found = Parts.Find(Name);
	return Found ? Found->Get() : nullptr;
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
		const FVector Rot = WWOConfig::Vec(Model, TEXT("rot"));
		WorldGun->SetRelativeScale3D(WWOConfig::Vec(Model, TEXT("scale"), FVector(1.0)));
		WorldGun->SetRelativeRotation(FRotator(Rot.X, Rot.Y, Rot.Z));
		WorldGun->SetRelativeLocation(-WWOConfig::Vec(Model, TEXT("hand"), FVector(0.0, 0.0, -5.0)));
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
		const FVector Size = WWOConfig::Vec(O, TEXT("size"), FVector(2.0));
		const FVector Rot = WWOConfig::Vec(O, TEXT("rot"));
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
		const FLinearColor Color = WWOConfig::Color(O);
		AddPart(Name, Mesh, Scale, Anchor + WWOConfig::Vec(O, TEXT("offset")), R, &Color, (float)Glow);
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
		if (BodyMesh && FName(P.Name) == "MuzzleFlash") Pos = WWOConfig::Vec(Model, TEXT("offset")) + WWOConfig::Vec(Model, TEXT("muzzle"), Pos);
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
		const float MeshTop = (MeshBounds.Origin.Z + MeshBounds.BoxExtent.Z) * WWOConfig::Vec(Model, TEXT("scale"), FVector(1.0)).Z;
		TopBelowSight = FMath::Max(-(WWOConfig::Vec(Model, TEXT("offset")).Z + MeshTop) + Weapon->SightHeight, 0.f);
		const FVector Rot = WWOConfig::Vec(Model, TEXT("rot"));
		AddPart("Body", BodyMesh, WWOConfig::Vec(Model, TEXT("scale"), FVector(1.0)), WWOConfig::Vec(Model, TEXT("offset")), FRotator(Rot.X, Rot.Y, Rot.Z), nullptr, 0.f);
		const TArray<TSharedPtr<FJsonValue>>* Extras = nullptr;
		if (Model->TryGetArrayField(TEXT("parts"), Extras))
		{
			for (int32 i = 0; i < Extras->Num(); ++i)
			{
				if ((*Extras)[i]->AsObject().IsValid()) AddJsonPart(FName(*FString::Printf(TEXT("Extra%d"), i)), (*Extras)[i]->AsObject(), WWOConfig::Vec(Model, TEXT("offset")));
			}
		}
		const TSharedPtr<FJsonObject>* AnchorObj = nullptr;
		if (Model->TryGetObjectField(TEXT("anchors"), AnchorObj))
		{
			for (const auto& Pair : (*AnchorObj)->Values)
			{
				const FString Key(Pair.Key);
				Anchors.Add(Key, WWOConfig::Vec(Model, TEXT("offset")) + WWOConfig::Vec(*AnchorObj, *Key)); // anchors move with the mesh
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

void UWWOWeaponComponent::BuildThrowBall()
{
	// The grenade shown in the throwing hand (UpdatePose places it), on the camera.
	ThrowBall = WWOFX::AddShape(GetOwner(), GetAttachParent(), WWOFX::Sphere(), TEXT("M_WWO_Solid"), FLinearColor::White);
	ThrowBall->SetCastShadow(false);
	ThrowBall->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	ThrowBall->MarkRenderStateDirty();
	ThrowBall->SetVisibility(false);
}
