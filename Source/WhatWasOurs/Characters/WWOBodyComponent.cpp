#include "Characters/WWOBodyComponent.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/WWOBodyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const TCHAR* MannyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	// The baked Godot animations that loop (the UAL clips named *_Loop, as Godot's import loops them).
	const TCHAR* BodyMaterials[] = { TEXT("/Game/WWO/Materials/MI_WWO_Body_01.MI_WWO_Body_01"), TEXT("/Game/WWO/Materials/MI_WWO_Body_02.MI_WWO_Body_02") };
	const FName RegionTints[] = { "Head Tint", "Arms Tint", "Core Tint", "Legs Tint" };
	const FName Looping[] = { "Idle", "Walk", "Jog_Fwd", "Sprint", "Crouch_Idle", "Crouch_Fwd", "Jump", "Spell_Simple_Idle", "Slide", "Pistol_Aim_Neutral" };
}

UWWOBodyComponent::UWWOBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UWWOBodyComponent::IsLooping(FName Anim)
{
	for (const FName& L : Looping) if (L == Anim) return true;
	return false;
}

UAnimSequence* UWWOBodyComponent::FindAnim(FName Anim) const
{
	if (Anim.IsNone()) return nullptr;
	if (const TObjectPtr<UAnimSequence>* Found = Anims.Find(Anim)) return *Found;
	const FString Name = Anim.ToString();
	UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/WWO/Anims/UAL/A_%s.A_%s"), *Name, *Name));
	Anims.Add(Anim, Seq);
	return Seq;
}

USkeletalMeshComponent* UWWOBodyComponent::GetMesh() const
{
	if (MeshOverride.IsValid()) return MeshOverride.Get();
	const ACharacter* C = Cast<ACharacter>(GetOwner());
	return C ? C->GetMesh() : GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
}

UWWOBodyAnimInstance* UWWOBodyComponent::GetAnimInstance() const
{
	const USkeletalMeshComponent* Mesh = GetMesh();
	return Mesh ? Cast<UWWOBodyAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
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
	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(UWWOBodyAnimInstance::StaticClass());
	Mesh->SetRelativeScale3D(FVector(ModelScale));
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Materials.Reset();
	for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
	{
		// The mannequin's own two materials, with the paint tint picked per region (M_WWO_Body).
		if (i < UE_ARRAY_COUNT(BodyMaterials))
		{
			if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, BodyMaterials[i])) Mesh->SetMaterial(i, M);
		}
		Materials.Add(Mesh->CreateAndSetMaterialInstanceDynamic(i));
	}
	bSetUp = true;
	ApplyColors();
	ForcePose("Idle"); // Godot: force_pose("Idle") before measuring hitboxes and gear, then play("Idle")
}

void UWWOBodyComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWWOLoadoutSubsystem* Loadout = UWWOLoadoutSubsystem::Get(this)) Loadout->OnChanged.Remove(LoadoutHandle);
	Super::EndPlay(Reason);
}

void UWWOBodyComponent::ApplyColors()
{
	SetTints(FLinearColor::White, 0.f);
}

void UWWOBodyComponent::SetFlash(float Amount)
{
	SetTints(FLinearColor::White, FMath::Clamp(Amount, 0.f, 1.f));
}

void UWWOBodyComponent::SetTints(const FLinearColor& Toward, float Amount)
{
	// Slot 0 is the mannequin's head, limbs and hips (its paint is the body color, Godot's region
	// colors), slot 1 its torso, forearms and trim (the joint color). A region wearing a frame part
	// takes that part's color on slot 0 and a darker shade of it on its trim, as the first-person
	// arm does, so the whole limb reads as the part.
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		UMaterialInstanceDynamic* M = Materials[i];
		if (!M) continue;
		for (int32 r = 0; r < NumRegions; ++r)
		{
			const FLinearColor Color = Region(r);
			const FLinearColor Slot = i == 0 ? Color : (Color.Equals(BodyColor, 0.002f) ? JointColor : Color * 0.55f);
			M->SetVectorParameterValue(RegionTints[r], WWO::Lin(FMath::Lerp(Slot, Toward, Amount)));
		}
		const FLinearColor Base = i == 0 ? BodyColor : JointColor;
		M->SetVectorParameterValue("Paint Tint", WWO::Lin(FMath::Lerp(Base, Toward, Amount))); // the plain mannequin material, if M_WWO_Body is missing
	}
}

// --- Animation ---------------------------------------------------------------------------------

void UWWOBodyComponent::Play(FName Anim, float Speed, float Blend)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance()) A->Play(FindAnim(Anim), IsLooping(Anim), Speed, Blend);
}

void UWWOBodyComponent::Queue(FName Anim)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance()) A->Queue(FindAnim(Anim), IsLooping(Anim));
}

FName UWWOBodyComponent::CurrentAnim() const
{
	const UWWOBodyAnimInstance* A = GetAnimInstance();
	const UAnimSequence* Seq = A ? A->GetCurrent() : nullptr;
	if (!Seq) return NAME_None;
	for (const TPair<FName, TObjectPtr<UAnimSequence>>& Pair : Anims)
	{
		if (Pair.Value == Seq) return Pair.Key;
	}
	return NAME_None;
}

float UWWOBodyComponent::CurrentAnimTime() const
{
	const UWWOBodyAnimInstance* A = GetAnimInstance();
	return A ? A->GetCurrentTime() : 0.f;
}

float UWWOBodyComponent::CurrentAnimLength() const
{
	const UWWOBodyAnimInstance* A = GetAnimInstance();
	return A && A->IsPlaying() ? A->GetCurrentLength() : 0.f;
}

float UWWOBodyComponent::AnimLength(FName Anim) const
{
	const UAnimSequence* Seq = FindAnim(Anim);
	return Seq ? Seq->GetPlayLength() : 0.f;
}

void UWWOBodyComponent::ForcePose(FName Anim, float At)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	UWWOBodyAnimInstance* A = GetAnimInstance();
	if (!Mesh || !A) return;
	A->Stop();
	A->Play(FindAnim(Anim), IsLooping(Anim), 1.f, 0.f);
	Mesh->TickAnimation(At, false);
	Mesh->RefreshBoneTransforms();
}

void UWWOBodyComponent::SetUpperBody(FName Anim, float Weight)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance()) A->SetUpperBody(FindAnim(Anim), Weight);
}

void UWWOBodyComponent::PlayUpperShot(FName Anim, float FadeIn, float FadeOut)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance()) A->PlayUpperShot(FindAnim(Anim), FadeIn, FadeOut);
}

void UWWOBodyComponent::SetMix(FName A, FName B, float Alpha, float SpeedB)
{
	if (UWWOBodyAnimInstance* I = GetAnimInstance()) I->SetMix(FindAnim(A), FindAnim(B), Alpha, SpeedB);
}

void UWWOBodyComponent::ClearLayers()
{
	if (UWWOBodyAnimInstance* I = GetAnimInstance()) I->ClearLayers();
}

void UWWOBodyComponent::SetHunch(float SpineDegrees, float HeadDegrees)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance())
	{
		A->HunchSpine = SpineDegrees;
		A->HunchHead = HeadDegrees;
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

void UWWOBodyComponent::SetShadowOnly(bool bOn)
{
	bShadowOnly = bOn;
	if (USkeletalMeshComponent* Mesh = GetMesh())
	{
		Mesh->SetOwnerNoSee(bOn);
		Mesh->FirstPersonPrimitiveType = bOn ? EFirstPersonPrimitiveType::WorldSpaceRepresentation : EFirstPersonPrimitiveType::None;
		Mesh->MarkRenderStateDirty();
	}
	for (const TArray<TObjectPtr<UPrimitiveComponent>>* List : { &Grafts, &Pinned })
	{
		for (UPrimitiveComponent* C : *List)
		{
			if (!C) continue;
			C->SetOwnerNoSee(bOn);
			C->bCastHiddenShadow = bOn;
			C->MarkRenderStateDirty();
		}
	}
}

FTransform UWWOBodyComponent::IdleBone(FName Bone) const
{
	// Where a bone is in the standing pose (Idle's first frame), in world space. Godot places the
	// grafts from that pose whatever is playing (show_flora plays Idle, seeks 0, then goes back).
	const USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return GetOwner()->GetActorTransform();
	const USkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
	const UAnimSequence* Idle = FindAnim("Idle");
	if (IdlePose.Num() == 0 && Asset && Asset->GetSkeleton() && Idle)
	{
		const FReferenceSkeleton& Ref = Asset->GetSkeleton()->GetReferenceSkeleton();
		TArray<FTransform> CS;
		CS.SetNum(Ref.GetNum());
		for (int32 i = 0; i < Ref.GetNum(); ++i)
		{
			FTransform Local;
			Idle->GetBoneTransform(Local, FSkeletonPoseBoneIndex(i), FAnimExtractContext(0.0), false);
			const int32 Parent = Ref.GetParentIndex(i);
			CS[i] = Parent == INDEX_NONE ? Local : Local * CS[Parent];
			IdlePose.Add(Ref.GetBoneName(i), CS[i]);
		}
	}
	if (const FTransform* Found = IdlePose.Find(Bone)) return *Found * Mesh->GetComponentTransform();
	return Mesh->GetSocketTransform(Bone);
}

FVector UWWOBodyComponent::BonePoint(FName Bone) const
{
	return IdleBone(Bone).GetLocation();
}

void UWWOBodyComponent::Pin(UPrimitiveComponent* C, FName Bone, const FTransform& World, bool bTrack)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->LightingChannels = Mesh->LightingChannels; // lit like the body (the menu preview has its own lights)
	C->SetupAttachment(Mesh, Bone);
	// Placed against the bone in the standing pose, then it follows the bone.
	C->SetRelativeTransform(World.GetRelativeTransform(IdleBone(Bone)));
	C->RegisterComponent();
	GetOwner()->AddInstanceComponent(C);
	if (bShadowOnly)
	{
		C->SetOwnerNoSee(true);
		C->bCastHiddenShadow = true;
	}
	(bTrack ? Grafts : Pinned).Add(C);
}

UStaticMeshComponent* UWWOBodyComponent::AddShape(FName Bone, const FVector& World, UStaticMesh* MeshAsset, const FVector& ScaleM,
	const FLinearColor& Color, float Glow, bool bTrack)
{
	if (!GetMesh()) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner());
	C->SetStaticMesh(MeshAsset);
	C->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Solid")));
	// Lined up with the body (not the bone), meters -> the 1 m engine shapes.
	Pin(C, Bone, FTransform(GetOwner()->GetActorRotation(), World, ScaleM), bTrack);
	if (UMaterialInstanceDynamic* M = C->CreateAndSetMaterialInstanceDynamic(0))
	{
		M->SetVectorParameterValue("Color", Color);
		M->SetScalarParameterValue("Glow", Glow);
	}
	return C;
}

UStaticMeshComponent* UWWOBodyComponent::AttachBox(FName Bone, const FVector& WorldCenter, const FVector& SizeM, const FLinearColor& Color, float Glow)
{
	// Godot attach_box: not one of the flora parts, so the loadout's rebuilds keep it.
	return AddShape(Bone, WorldCenter, WWOFX::Cube(), SizeM, Color, Glow, false);
}
