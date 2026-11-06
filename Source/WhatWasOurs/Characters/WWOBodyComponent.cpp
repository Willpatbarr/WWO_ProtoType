#include "Characters/WWOBodyComponent.h"
#include "WhatWasOurs.h"
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
#include "Characters/WWOModelAnimInstance.h"
#include "Retargeter/IKRetargeter.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* MannyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	// The baked Godot animations that loop (the UAL clips named *_Loop, as Godot's import loops them).
	const TCHAR* BodyMaterials[] = { TEXT("/Game/WWO/Materials/MI_WWO_Body_01.MI_WWO_Body_01"), TEXT("/Game/WWO/Materials/MI_WWO_Body_02.MI_WWO_Body_02") };
	const FName RegionTints[] = { "Head Tint", "Arms Tint", "Core Tint", "Legs Tint" };
	const FName Looping[] = { "Idle", "Walk", "Jog_Fwd", "Sprint", "Crouch_Idle", "Crouch_Fwd", "Jump", "Spell_Simple_Idle", "Slide", "Pistol_Aim_Neutral" };

	// One entry of Config/CharacterModels.json (read each time a model is set: retune with a restart).
	TSharedPtr<FJsonObject> ModelConfig(FName Key)
	{
		FString Text;
		if (Key.IsNone() || !FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("CharacterModels.json")))) return nullptr;
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return nullptr;
		const TSharedPtr<FJsonObject>* Models;
		const TSharedPtr<FJsonObject>* Model;
		if (Json->TryGetObjectField(TEXT("models"), Models) && (*Models)->TryGetObjectField(Key.ToString(), Model)) return *Model;
		return nullptr;
	}
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
	if (!ModelKey.IsNone()) SetModel(ModelKey);
}

// --- Character models ----------------------------------------------------------------------------

void UWWOBodyComponent::SetModel(FName Key)
{
	USkeletalMeshComponent* Driver = GetMesh();
	UE_LOG(LogWWO, Verbose, TEXT("Body %s: model '%s' (driver %s)"), *GetOwner()->GetName(), *Key.ToString(), Driver ? TEXT("yes") : TEXT("none"));
	if (!Driver || (Key == ActiveModel && (Model || Key.IsNone()))) return;
	if (Model) Model->DestroyComponent();
	Model = nullptr;
	ActiveModel = Key;
	const TSharedPtr<FJsonObject> Config = ModelConfig(Key);
	FString MeshPath;
	USkeletalMesh* Asset = Config.IsValid() && Config->TryGetStringField(TEXT("mesh"), MeshPath) ? LoadObject<USkeletalMesh>(nullptr, *MeshPath) : nullptr;
	const FString K = Key.ToString();
	UIKRetargeter* Retargeter = Asset ? LoadObject<UIKRetargeter>(nullptr, *FString::Printf(TEXT("/Game/WWO/Retarget/RTG_Manny_%s.RTG_Manny_%s"), *K, *K)) : nullptr;
	if (!Asset || !Retargeter)
	{
		if (!Key.IsNone()) UE_LOG(LogWWO, Warning, TEXT("Character model '%s': no mesh or retargeter (Config/CharacterModels.json, Tools/build_retargeters.py)"), *K);
		UpdateModelExtras();
		return;
	}
	double Scale = 1.0;
	Config->TryGetNumberField(TEXT("scale"), Scale);
	bModelGrafts = false;
	bModelPinned = false;
	Config->TryGetBoolField(TEXT("grafts"), bModelGrafts);
	Config->TryGetBoolField(TEXT("pinned"), bModelPinned);

	Model = NewObject<USkeletalMeshComponent>(GetOwner());
	Model->SetupAttachment(Driver);
	Model->SetSkeletalMeshAsset(Asset);
	Model->SetRelativeScale3D(FVector(Scale));
	Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Model->LightingChannels = Driver->LightingChannels; // the menu preview has its own lights
	Model->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Model->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Model->SetAnimInstanceClass(UWWOModelAnimInstance::StaticClass());
	Model->AddTickPrerequisiteComponent(Driver); // the mannequin poses first, then the model copies it
	TArray<FString> HideSlots;
	if (Config->TryGetStringArrayField(TEXT("hide_slots"), HideSlots) && HideSlots.Num() > 0) Model->bDisableClothSimulation = true;
	Model->RegisterComponent();
	GetOwner()->AddInstanceComponent(Model);
	if (UWWOModelAnimInstance* Anim = Cast<UWWOModelAnimInstance>(Model->GetAnimInstance())) Anim->Configure(Retargeter, Driver);
	// Hidden material slots (VeilSpore's cape): every section using them, in every LOD.
	if (const FSkeletalMeshRenderData* Render = Asset->GetResourceForRendering())
	{
		for (const FString& SlotName : HideSlots)
		{
			const int32 SlotIndex = Model->GetMaterialIndex(FName(*SlotName));
			if (SlotIndex == INDEX_NONE) continue;
			for (int32 Lod = 0; Lod < Render->LODRenderData.Num(); ++Lod)
			{
				const TArray<FSkelMeshRenderSection>& Sections = Render->LODRenderData[Lod].RenderSections;
				for (int32 s = 0; s < Sections.Num(); ++s)
				{
					if (Sections[s].MaterialIndex == SlotIndex) Model->ShowMaterialSection(SlotIndex, s, false, Lod);
				}
			}
		}
	}
	ApplyShadowOnly(Model);
	UpdateModelExtras();
}

void UWWOBodyComponent::UpdateModelExtras()
{
	// With a model the mannequin only drives the animation (it keeps ticking while hidden).
	if (USkeletalMeshComponent* Driver = GetMesh()) Driver->SetVisibility(Model == nullptr, false);
	for (UPrimitiveComponent* C : Grafts)
	{
		if (C) C->SetVisibility(!Model || bModelGrafts);
	}
	for (UPrimitiveComponent* C : Pinned)
	{
		if (C) C->SetVisibility(!Model || bModelPinned || GlowingPinned.Contains(C));
	}
}

void UWWOBodyComponent::ApplyShadowOnly(UPrimitiveComponent* C) const
{
	if (!C) return;
	C->SetOwnerNoSee(bShadowOnly);
	if (USkeletalMeshComponent* S = Cast<USkeletalMeshComponent>(C))
	{
		S->FirstPersonPrimitiveType = bShadowOnly ? EFirstPersonPrimitiveType::WorldSpaceRepresentation : EFirstPersonPrimitiveType::None;
	}
	C->MarkRenderStateDirty();
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
	bFollowsLoadout = true;
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
	ApplyShadowOnly(Model);
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
	if (Model && bTrack && !bModelGrafts) C->SetVisibility(false); // the model is the flora's look
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
	// Godot attach_box: not one of the flora parts, so the loadout's rebuilds keep it. A glowing box
	// (the Tank's weak spot) stays on a character model; plain ones hide (the model has its own armor).
	UStaticMeshComponent* Box = AddShape(Bone, WorldCenter, WWOFX::Cube(), SizeM, Color, Glow, false);
	if (Box && Glow > 0.f) GlowingPinned.Add(Box);
	UpdateModelExtras();
	return Box;
}
