#include "Characters/WWOBodyComponent.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const TCHAR* MannyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* UnarmedABP = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C");
	const TCHAR* PistolABP = TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Pistol.ABP_TP_Pistol_C");
	const TCHAR* RifleABP = TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Rifle.ABP_TP_Rifle_C");
	const TCHAR* Anim(EWWOBodyAction Action)
	{
		switch (Action)
		{
		case EWWOBodyAction::UltCharge: return TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_ChargedAttack.MM_ChargedAttack");
		case EWWOBodyAction::UltCast: return TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_02.MM_Attack_02");
		case EWWOBodyAction::Bite: return TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01.MM_Attack_01");
		case EWWOBodyAction::Slam: return TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_ChargedAttack.MM_ChargedAttack");
		case EWWOBodyAction::Lob: return TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_03.MM_Attack_03");
		case EWWOBodyAction::Leap: return TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump.MM_Jump");
		default: return nullptr;
		}
	}
}

UWWOBodyComponent::UWWOBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UAnimSequenceBase* UWWOBodyComponent::LoadAnim(const TCHAR* Path)
{
	return Path ? LoadObject<UAnimSequenceBase>(nullptr, Path) : nullptr;
}

USkeletalMeshComponent* UWWOBodyComponent::GetMesh() const
{
	if (MeshOverride.IsValid()) return MeshOverride.Get();
	const ACharacter* C = Cast<ACharacter>(GetOwner());
	return C ? C->GetMesh() : GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
}

void UWWOBodyComponent::BeginPlay()
{
	Super::BeginPlay();
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return;
	if (USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr, MannyPath))
	{
		if (!Mesh->GetSkeletalMeshAsset()) Mesh->SetSkeletalMeshAsset(Manny);
	}
	const TCHAR* ABP = AnimSet == EWWOAnimSet::Pistol ? PistolABP : (AnimSet == EWWOAnimSet::Rifle ? RifleABP : UnarmedABP);
	if (UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, ABP))
	{
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(AnimClass);
	}
	Mesh->SetRelativeScale3D(FVector(ModelScale));
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Materials.Reset();
	for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
	{
		Materials.Add(Mesh->CreateAndSetMaterialInstanceDynamic(i));
	}
	bSetUp = true;
	ApplyColors();
}

void UWWOBodyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this)) Loadout->OnChanged.Remove(LoadoutHandle);
	Super::EndPlay(Reason);
}

void UWWOBodyComponent::ApplyColors()
{
	// Slot 0 is the mannequin's main shell, slot 1 the joints and trim.
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		if (Materials[i]) Materials[i]->SetVectorParameterValue("Paint Tint", WWO::Lin(i == 0 ? BodyColor : JointColor));
	}
}

void UWWOBodyComponent::SetFlash(float Amount)
{
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		if (!Materials[i]) continue;
		const FLinearColor Base = i == 0 ? BodyColor : JointColor;
		Materials[i]->SetVectorParameterValue("Paint Tint", WWO::Lin(FMath::Lerp(Base, FLinearColor::White, FMath::Clamp(Amount, 0.f, 1.f))));
	}
}

void UWWOBodyComponent::PlayAction(EWWOBodyAction Action, float Rate)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!AnimInstance) return;
	if (Action == EWWOBodyAction::None)
	{
		AnimInstance->StopSlotAnimation(0.25f, "DefaultSlot");
		return;
	}
	if (UAnimSequenceBase* Seq = LoadAnim(Anim(Action)))
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Seq, "DefaultSlot", 0.08f, 0.2f, Rate);
	}
}

void UWWOBodyComponent::PlayDeath(bool bKnockedBack)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return;
	const TCHAR* Path = bKnockedBack ? TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01.MM_Death_Back_01")
		: TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01");
	if (UAnimSequenceBase* Seq = LoadAnim(Path))
	{
		Mesh->PlayAnimation(Seq, false);
	}
}

void UWWOBodyComponent::Revive()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return;
	const TCHAR* ABP = AnimSet == EWWOAnimSet::Pistol ? PistolABP : (AnimSet == EWWOAnimSet::Rifle ? RifleABP : UnarmedABP);
	if (UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, ABP))
	{
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(AnimClass);
	}
}

void UWWOBodyComponent::FollowLoadout(bool bShadow)
{
	bShadowOnly = bShadow;
	if (UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this))
	{
		Loadout->OnChanged.Remove(LoadoutHandle);
		LoadoutHandle = Loadout->OnChanged.AddWeakLambda(this, [this]() { RebuildGrafts(); });
	}
	RebuildGrafts();
}

FVector UWWOBodyComponent::BonePoint(FName Bone) const
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	return Mesh ? Mesh->GetSocketLocation(Bone) : GetOwner()->GetActorLocation();
}

UStaticMeshComponent* UWWOBodyComponent::AddGraft(FName Bone, const FVector& World, UStaticMesh* MeshAsset, const FVector& ScaleM,
	const FLinearColor& Color, float Glow, const FRotator& WorldRot)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner());
	C->SetStaticMesh(MeshAsset);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Solid")));
	C->SetupAttachment(Mesh, Bone);
	C->RegisterComponent();
	GetOwner()->AddInstanceComponent(C);
	C->SetWorldLocationAndRotation(World, WorldRot.IsZero() ? GetOwner()->GetActorRotation() : WorldRot);
	C->SetWorldScale3D(ScaleM); // meters -> the 1 m engine shapes
	if (UMaterialInstanceDynamic* M = C->CreateAndSetMaterialInstanceDynamic(0))
	{
		M->SetVectorParameterValue("Color", Color);
		M->SetScalarParameterValue("Glow", Glow);
	}
	if (bShadowOnly)
	{
		C->SetOwnerNoSee(true);
		C->bCastHiddenShadow = true;
	}
	Grafts.Add(C);
	return C;
}

UStaticMeshComponent* UWWOBodyComponent::AttachBox(FName Bone, const FVector& WorldCenter, const FVector& SizeM, const FLinearColor& Color, float Glow)
{
	return AddGraft(Bone, WorldCenter, WWOFX::Cube(), SizeM, Color, Glow);
}

void UWWOBodyComponent::RebuildGrafts()
{
	// Gray-box markers for the equipped flora's grenade and abilities, placed from the
	// current pose and then following their bones.
	for (UStaticMeshComponent* C : Grafts)
	{
		if (C) C->DestroyComponent();
	}
	Grafts.Reset();
	const UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this);
	if (!Loadout || !GetMesh()) return;
	TSet<FName> Ids;
	for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
	{
		if (const UWWOAbilityData* A = Loadout->GetAbility(Slot)) Ids.Add(A->Id);
	}
	const UWWOFloraData* Flora = Loadout->GetFlora();
	const UWWOGrenadeData* Grenade = Loadout->GetGrenade();
	const FName FloraId = Flora ? Flora->Id : NAME_None;
	const FLinearColor Graft = Flora && Flora->GraftColor.A > 0.f ? Flora->GraftColor : (Grenade ? Grenade->Color : FLinearColor::White);
	const FVector Right = GetOwner()->GetActorRightVector();
	const FVector Forward = GetOwner()->GetActorForwardVector();
	const FVector Up = FVector::UpVector;
	const float S = ModelScale;
	const FVector Hips = BonePoint("pelvis");
	auto Pod = [&](FName Bone, const FVector& At, float LengthM, float WidthM, const FLinearColor& Color)
	{
		AddGraft(Bone, At, WWOFX::Sphere(), FVector(WidthM, WidthM, LengthM) * S, Color, 2.f);
	};
	auto Cap = [&](FName Bone, const FVector& At, float WidthM, const FLinearColor& Color)
	{
		AddGraft(Bone, At, WWOFX::Sphere(), FVector(WidthM, WidthM, WidthM * 0.4f) * S, Color, 1.5f);
	};
	auto Plate = [&](FName Bone, const FVector& At, const FVector& SizeM)
	{
		AddGraft(Bone, At, WWOFX::Cube(), SizeM * S, FLinearColor(0.32f, 0.21f, 0.12f));
		AddGraft(Bone, At + FVector(0, 0, 0.f), WWOFX::Cube(), FVector(SizeM.X * 0.15f, SizeM.Y * 1.05f, SizeM.Z * 1.05f) * S, FLinearColor(0.95f, 0.5f, 0.15f), 2.f);
	};
	const FLinearColor Bark(0.45f, 0.31f, 0.19f);
	const FLinearColor SporeCap(0.35f, 0.18f, 0.5f);

	// Grenade on the right hip.
	if (Grenade)
	{
		if (FloraId == "voltvine")
		{
			const int32 Count = Grenade->Id == "shock_pool" ? 2 : (Grenade->Id == "snare_mine" ? 3 : 1);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Size = Count == 1 ? 1.f : (Count == 2 ? 0.72f : 0.55f);
				Pod("pelvis", Hips + Right * 19.f * S + Forward * (i - (Count - 1) * 0.5f) * 5.f * S - Up * 4.f * S, 0.12f * Size, 0.07f * Size, Graft);
			}
		}
		else if (FloraId == "veilspore")
		{
			Cap("pelvis", Hips + Right * 22.f * S - Up * 4.f * S, 0.17f, SporeCap);
		}
		else if (FloraId == "ironbark")
		{
			for (int32 k = 0; k < 2; ++k) Pod("pelvis", Hips + Forward * 17.f * S - Right * (3.f + k * 5.f) * S - Up * 6.f * S, 0.11f - k * 0.015f, 0.08f, FLinearColor(0.95f, 0.5f, 0.15f));
		}
		else
		{
			AddGraft("pelvis", Hips + Right * 17.f * S + Forward * 4.f * S, WWOFX::Sphere(), FVector(0.13f) * S, Grenade->Color, 2.f);
		}
	}
	if (Ids.Contains("seedburst"))
	{
		for (int32 i = 0; i < 3; ++i)
		{
			AddGraft("pelvis", Hips - Right * 16.5f * S + Forward * (7.f - 7.f * i) * S, WWOFX::Sphere(), FVector(0.07f) * S, Graft, 2.f);
		}
	}
	for (const float Side : { -1.f, 1.f })
	{
		const TCHAR* L = Side > 0.f ? TEXT("r") : TEXT("l");
		const FVector Out = Right * Side;
		const FName Calf(*FString::Printf(TEXT("calf_%s"), L)), Foot(*FString::Printf(TEXT("foot_%s"), L));
		const FName Lower(*FString::Printf(TEXT("lowerarm_%s"), L)), Hand(*FString::Printf(TEXT("hand_%s"), L));
		const FName Upper(*FString::Printf(TEXT("upperarm_%s"), L));
		const FVector Knee = BonePoint(Calf), Ankle = BonePoint(Foot), Elbow = BonePoint(Lower), Wrist = BonePoint(Hand), Shoulder = BonePoint(Upper);
		if (Ids.Contains("overgrowth"))
		{
			for (const float T : { 0.3f, 0.65f }) Pod(Calf, FMath::Lerp(Knee, Ankle, T) + Forward * 7.f * S, 0.1f, 0.06f, Graft);
		}
		if (Ids.Contains("thornfist"))
		{
			for (const float T : { 0.3f, 0.68f }) Pod(Lower, FMath::Lerp(Elbow, Wrist, T) + Out * 6.5f * S, 0.085f, 0.05f, Graft);
		}
		if (Ids.Contains("static_charge"))
		{
			for (const float T : { 0.4f, 0.72f }) Pod(Upper, FMath::Lerp(Shoulder, Elbow, T) + Out * 7.5f * S, 0.09f, 0.055f, Graft);
		}
		if (Ids.Contains("melee_guard"))
		{
			for (int32 i = 0; i < 3; ++i) AddGraft(Hand, Wrist + Out * 3.f * S + Forward * (i - 1) * 2.4f * S, WWOFX::Sphere(), FVector(0.025f) * S, Graft, 2.f);
		}
		if (Ids.Contains("root_bombs"))
		{
			Plate(Lower, FMath::Lerp(Elbow, Wrist, 0.5f) + Out * 1.f * S, FVector(0.2f, 0.08f, 0.22f));
			Plate(Calf, FMath::Lerp(Knee, Ankle, 0.42f) + Forward * 1.f * S, FVector(0.08f, 0.22f, 0.3f));
		}
		if (Ids.Contains("shield_share"))
		{
			Plate(Upper, Shoulder + Up * 6.f * S + Out * 4.f * S, FVector(0.2f, 0.18f, 0.08f));
		}
	}
	if (Ids.Contains("shield_share"))
	{
		Plate("spine_04", BonePoint("spine_04") + Forward * 14.f * S, FVector(0.06f, 0.36f, 0.32f));
	}
	if (Ids.Contains("extra_grenade") && FloraId == "ironbark")
	{
		for (int32 k = 0; k < 2; ++k) Pod("pelvis", Hips + Forward * 17.f * S + Right * (3.f + k * 5.f) * S - Up * 6.f * S, 0.11f - k * 0.015f, 0.08f, FLinearColor(0.95f, 0.5f, 0.15f));
	}
	if (Ids.Contains("spore_fog_damage"))
	{
		Cap("upperarm_r", BonePoint("upperarm_r") + Up * 10.f * S + Right * 2.f * S, 0.32f, SporeCap);
		Cap("head", BonePoint("head") + Up * 15.f * S + Right * 9.f * S, 0.3f, SporeCap);
	}
	if (Ids.Contains("spore_dash"))
	{
		const FVector Hip = BonePoint("thigh_l"), Knee = BonePoint("calf_l");
		for (int32 k = 0; k < 3; ++k) Cap("thigh_l", FMath::Lerp(Hip, Knee, 0.22f + k * 0.2f) - Right * 9.5f * S, 0.22f - k * 0.03f, SporeCap);
	}
	if (Ids.Contains("spore_patches"))
	{
		for (int32 i = 0; i < 3; ++i)
		{
			AddGraft("pelvis", Hips - Forward * 14.f * S + Right * (i - 1) * 10.f * S + Up * 5.f * S, WWOFX::Sphere(), FVector(0.09f, 0.09f, 0.05f) * S, SporeCap, 1.5f);
		}
	}
}
