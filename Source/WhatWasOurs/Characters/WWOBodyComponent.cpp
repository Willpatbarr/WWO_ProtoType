#include "Characters/WWOBodyComponent.h"
#include "Net/UnrealNetwork.h"
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
#include "Misc/PackageName.h"
#include "Player/WWOCharacter.h"

namespace
{
	const TCHAR* MannyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	// The baked Godot animations that loop (the UAL clips named *_Loop, as Godot's import loops them).
	const TCHAR* BodyMaterials[] = { TEXT("/Game/WWO/Materials/MI_WWO_Body_01.MI_WWO_Body_01"), TEXT("/Game/WWO/Materials/MI_WWO_Body_02.MI_WWO_Body_02") };
	const FName RegionTints[] = { "Head Tint", "Arms Tint", "Core Tint", "Legs Tint" };
	const FName Looping[] = { "Idle", "Walk", "Idle_Unarmed", "Walk_Unarmed", "Jog_Fwd", "Sprint", "Sprint_Unarmed", "Crouch_Idle", "Crouch_Fwd", "Jump", "Spell_Simple_Idle", "Slide", "Pistol_Aim_Neutral", "Aim_Pistol" };

	// One entry of Config/CharacterModels.json (read each time a model is set: retune with a restart).
	TSharedPtr<FJsonObject> ModelConfig(FName Key)
	{
		// Read and parsed once a run (every body that spawns asks).
		static TSharedPtr<FJsonObject> Json;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("CharacterModels.json")))) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		}
		if (Key.IsNone() || !Json.IsValid()) return nullptr;
		const TSharedPtr<FJsonObject>* Models = nullptr;
		const TSharedPtr<FJsonObject>* Model = nullptr;
		if (Json->TryGetObjectField(TEXT("models"), Models) && (*Models)->TryGetObjectField(Key.ToString(), Model)) return *Model;
		return nullptr;
	}
}

UWWOBodyComponent::UWWOBodyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // online: the host's animation commands (FWWOBodyNet)
}

void UWWOBodyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UWWOBodyComponent, Net, COND_SkipOwner);
}

bool UWWOBodyComponent::Replicating() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority() && Owner->GetNetMode() != NM_Standalone && Owner->GetIsReplicated();
}

namespace
{
	uint8 Q50(float V) { return (uint8)FMath::Clamp(FMath::RoundToInt(V * 50.f), 0, 255); } // 0 to 5.1 in 1/50
	float D50(uint8 V) { return V / 50.f; }
}

void UWWOBodyComponent::OnRep_Net()
{
	// Play what the host played, once per change.
	UWWOBodyAnimInstance* A = GetAnimInstance();
	if (!A) return;
	if (Net.ClearSerial != Applied.ClearSerial) A->ClearLayers();
	if (Net.Anim != Applied.Anim || Net.Speed != Applied.Speed) A->Play(FindAnim(Net.Anim), IsLooping(Net.Anim), D50(Net.Speed), 0.2f);
	if (Net.Queued != Applied.Queued && !Net.Queued.IsNone()) A->Queue(FindAnim(Net.Queued), IsLooping(Net.Queued));
	if (Net.Upper != Applied.Upper || Net.UpperWeight != Applied.UpperWeight) A->SetUpperBody(FindAnim(Net.Upper), D50(Net.UpperWeight));
	if (Net.MixA != Applied.MixA || Net.MixB != Applied.MixB || Net.MixAlpha != Applied.MixAlpha || Net.MixSpeedB != Applied.MixSpeedB)
	{
		if (!Net.MixA.IsNone()) A->SetMix(FindAnim(Net.MixA), FindAnim(Net.MixB), D50(Net.MixAlpha), D50(Net.MixSpeedB));
	}
	A->HunchSpine = Net.HunchSpine;
	A->HunchHead = Net.HunchHead;
	if (Net.ShotSerial != Applied.ShotSerial) A->PlayUpperShot(FindAnim(Net.Shot), 0.05f, 0.2f);
	Applied = Net;
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
	// The Unreal-native replacement first (Tools/bake_body_anims.py: stock mannequin, Lyra and Fab clips
	// baked as drop-in replacements under the same names); a clip without one keeps its Godot bake.
	const FString BodyPath = FString::Printf(TEXT("/Game/WWO/Anims/Body/A_%s"), *Name);
	UAnimSequence* Seq = FPackageName::DoesPackageExist(BodyPath) ? LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s.A_%s"), *BodyPath, *Name)) : nullptr;
	if (!Seq) Seq = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/WWO/Anims/UAL/A_%s.A_%s"), *Name, *Name));
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
	// Players can wear another mesh built on the mannequin's skeleton (Config/CharacterModels.json,
	// "player_mesh"): it takes the mannequin's place and its animations directly, with its own materials.
	bool bOwnMaterials = false;
	if (GetOwner()->IsA<AWWOCharacter>())
	{
		FString Text, Path;
		TSharedPtr<FJsonObject> Json;
		if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectConfigDir() / TEXT("CharacterModels.json")))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid() && Json->TryGetStringField(TEXT("player_mesh"), Path) && !Path.IsEmpty())
		{
			if (USkeletalMesh* Own = LoadObject<USkeletalMesh>(nullptr, *Path))
			{
				Mesh->SetSkeletalMeshAsset(Own);
				bOwnMaterials = true;
			}
			else UE_LOG(LogWWO, Warning, TEXT("Character models: player_mesh '%s' not found"), *Path);
		}
	}
	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(UWWOBodyAnimInstance::StaticClass());
	Mesh->SetRelativeScale3D(FVector(ModelScale));
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Materials.Reset();
	for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
	{
		// The mannequin's own two materials, with the paint tint picked per region (M_WWO_Body).
		if (!bOwnMaterials && i < UE_ARRAY_COUNT(BodyMaterials))
		{
			if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, BodyMaterials[i])) Mesh->SetMaterial(i, M);
		}
		Materials.Add(Mesh->CreateAndSetMaterialInstanceDynamic(i));
	}
	bSetUp = true;
	ApplyColors();
	ForcePose("Idle_Unarmed"); // (arms down: the pose hitboxes and gear are measured in; whoever owns the body plays its own idle) Godot: force_pose("Idle") before measuring hitboxes and gear, then play("Idle")
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
	if (FpBody) FpBody->DestroyComponent();
	FpBody = nullptr;
	ModelRetargeter = nullptr;
	ActiveModel = Key;
	const TSharedPtr<FJsonObject> Config = ModelConfig(Key);
	FString MeshPath;
	USkeletalMesh* Asset = Config.IsValid() && Config->TryGetStringField(TEXT("mesh"), MeshPath) ? LoadObject<USkeletalMesh>(nullptr, *MeshPath) : nullptr;
	const FString K = Key.ToString();
	// "retargeter": another model's retargeter (several keys sharing one model, as the three floras
	// sharing the robot frame do); its own RTG_Manny_<key> otherwise.
	FString RK = K;
	if (Config.IsValid()) Config->TryGetStringField(TEXT("retargeter"), RK);
	UIKRetargeter* Retargeter = Asset ? LoadObject<UIKRetargeter>(nullptr, *FString::Printf(TEXT("/Game/WWO/Retarget/RTG_Manny_%s.RTG_Manny_%s"), *RK, *RK)) : nullptr;
	if (!Asset || !Retargeter)
	{
		if (Config.IsValid()) UE_LOG(LogWWO, Warning, TEXT("Character model '%s': no mesh or retargeter (Config/CharacterModels.json, Tools/build_retargeters.py)"), *K);
		UpdateModelExtras();
		return;
	}
	double Scale = 1.0;
	Config->TryGetNumberField(TEXT("scale"), Scale);
	bModelGrafts = false;
	bModelPinned = false;
	Config->TryGetBoolField(TEXT("grafts"), bModelGrafts);
	Config->TryGetBoolField(TEXT("pinned"), bModelPinned);
	// Your own model in first person: everything from this bone up is hidden, and the body sits
	// this far behind the camera (cm), so looking down shows your legs and feet and not your chest.
	FString HideBone = TEXT("spine_02");
	Config->TryGetStringField(TEXT("fp_hide_bone"), HideBone); // one bone, or several separated by commas
	FpHideBones.Reset();
	TArray<FString> Names;
	HideBone.ParseIntoArray(Names, TEXT(","));
	for (FString& Name : Names) FpHideBones.Add(FName(*Name.TrimStartAndEnd()));
	FpHideBone = FpHideBones.Num() > 0 ? FpHideBones[0] : NAME_None;
	double Back = 22.0;
	Config->TryGetNumberField(TEXT("fp_body_back"), Back);
	FpBodyBack = float(Back);
	// A model whose hands are one rigid piece (no finger bones): which way its right hand's fingers
	// point and its palm faces in the reference pose (mesh space). The first-person hands lay such
	// a hand flat against the gun instead of copying a gripping pose it cannot make.
	const TArray<TSharedPtr<FJsonValue>>* Fingers = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Palm = nullptr;
	bRigidHands = Config->TryGetArrayField(TEXT("hand_fingers"), Fingers) && Config->TryGetArrayField(TEXT("hand_palm"), Palm) && Fingers->Num() >= 3 && Palm->Num() >= 3;
	if (bRigidHands)
	{
		HandFingers = FVector((*Fingers)[0]->AsNumber(), (*Fingers)[1]->AsNumber(), (*Fingers)[2]->AsNumber()).GetSafeNormal();
		HandPalm = FVector((*Palm)[0]->AsNumber(), (*Palm)[1]->AsNumber(), (*Palm)[2]->AsNumber()).GetSafeNormal();
	}

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
	ModelRetargeter = Retargeter;
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
	ApplyModelView();
	UpdateModelExtras();
	RefreshOverlay(); // a tint moves onto the new body
	// It copies the mannequin's pose now, not at its first tick (the menu preview may never tick it).
	Model->TickAnimation(0.f, false);
	Model->RefreshBoneTransforms();
}

void UWWOBodyComponent::SetHighlight(float Alpha)
{
	HighlightAlpha = FMath::Max(Alpha, 0.f);
	RefreshOverlay();
}

void UWWOBodyComponent::SetHandGlow(const FLinearColor& Color, float Alpha)
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh) return;
	if (HandGlows.Num() == 0 && Alpha > 0.f)
	{
		for (const FName Hand : { FName("hand_l"), FName("hand_r") })
		{
			UStaticMeshComponent* Glow = WWOFX::AddSoftSphere(GetOwner(), Mesh, Color, Alpha, 1.2f, true, false, 0.7f);
			Glow->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Hand);
			Glow->SetWorldScale3D(FVector(0.34f)); // a 34 cm ball around the fist
			Glow->SetOwnerNoSee(true);             // not in your own first-person view
			HandGlows.Add(Glow);
		}
	}
	for (UStaticMeshComponent* Glow : HandGlows)
	{
		if (!Glow) continue;
		Glow->SetVisibility(Alpha > 0.f);
		if (UMaterialInstanceDynamic* M = WWOFX::MID(Glow))
		{
			M->SetVectorParameterValue("Color", Color);
			M->SetScalarParameterValue("Alpha", Alpha);
		}
	}
}

void UWWOBodyComponent::SetTeamTint(const FLinearColor& Color, float Fill, float Rim)
{
	Fill = FMath::Max(Fill, 0.f);
	Rim = FMath::Max(Rim, 0.f);
	if (FMath::IsNearlyEqual(Fill, TeamTintAlpha) && FMath::IsNearlyEqual(Rim, TeamTintRim) && Color.Equals(TeamTint)) return;
	TeamTint = Color;
	TeamTintAlpha = Fill;
	TeamTintRim = Rim;
	RefreshOverlay();
}

void UWWOBodyComponent::RefreshOverlay()
{
	// One overlay over the visible body (the model, else the mannequin): the PvP marker tint (a faint
	// fill, a strong outline), else the reticle highlight in the material's own red (half fill, half
	// outline, as it always looked).
	USkeletalMeshComponent* Driver = GetMesh();
	USkeletalMeshComponent* Mesh = Model ? Model.Get() : Driver;
	if (Model && Driver && Driver->GetOverlayMaterial()) Driver->SetOverlayMaterial(nullptr); // the model took over
	if (!Mesh) return;
	const bool bTeam = TeamTintAlpha > 0.f || TeamTintRim > 0.f;
	const float Alpha = bTeam ? TeamTintAlpha : HighlightAlpha * 0.5f;
	const float Rim = bTeam ? TeamTintRim : HighlightAlpha * 0.5f;
	if (Alpha <= 0.f && Rim <= 0.f)
	{
		if (Mesh->GetOverlayMaterial()) Mesh->SetOverlayMaterial(nullptr);
		return;
	}
	if (!HighlightMat)
	{
		UMaterialInterface* Base = WWOFX::Material(TEXT("M_WWO_Highlight"));
		if (!Base) return;
		HighlightMat = UMaterialInstanceDynamic::Create(Base, this);
	}
	HighlightMat->SetScalarParameterValue("Alpha", Alpha);
	HighlightMat->SetScalarParameterValue("Rim", Rim);
	HighlightMat->SetVectorParameterValue("Color", bTeam ? TeamTint : FLinearColor(1.f, 0.15f, 0.1f)); // the material's default red
	if (Mesh->GetOverlayMaterial() != HighlightMat) Mesh->SetOverlayMaterial(HighlightMat);
}

void UWWOBodyComponent::UpdateModelExtras()
{
	// With a model the mannequin only drives the animation (it keeps ticking while hidden).
	if (USkeletalMeshComponent* Driver = GetMesh())
	{
		Driver->SetVisibility(Model == nullptr, false);
		// Hidden behind a model it casts no shadow of its own (the model's is the body's shadow; two
		// skinned shadows per character cost a few ms of shadow-map drawing in a full PvP match).
		// Your own first-person body is the exception: its model is owner-hidden, so the mannequin's is its shadow.
		const bool bCast = Model == nullptr; // (your own model casts your shadow itself: ApplyModelView)
		if (Driver->bCastHiddenShadow != bCast)
		{
			Driver->bCastHiddenShadow = bCast;
			Driver->MarkRenderStateDirty();
		}
	}
	for (UPrimitiveComponent* C : Grafts)
	{
		if (C) C->SetVisibility(!Model || bModelGrafts);
	}
	for (UPrimitiveComponent* C : Pinned)
	{
		if (C) C->SetVisibility(!Model || bModelPinned || GlowingPinned.Contains(C));
	}
}

void UWWOBodyComponent::ApplyModelView()
{
	// The model on your own character (shadow only): the whole model is hidden from you and casts
	// your shadow, and a second copy of it, shown only to you from the hide bone down and set back
	// behind the camera, is the legs and feet you see when you look down or slide. Everyone else
	// just sees the one whole model.
	if (!Model) return;
	USkeletalMeshComponent* Driver = GetMesh();
	const bool bLegs = bShadowOnly && Driver && ModelRetargeter && !FpHideBone.IsNone() && Model->GetBoneIndex(FpHideBone) != INDEX_NONE;
	ApplyShadowOnly(Model);
	Model->bCastHiddenShadow = bShadowOnly;
	Model->MarkRenderStateDirty();
	if (!bLegs)
	{
		if (FpBody) FpBody->DestroyComponent();
		FpBody = nullptr;
		return;
	}
	if (!FpBody)
	{
		FpBody = NewObject<USkeletalMeshComponent>(GetOwner());
		FpBody->SetupAttachment(Driver);
		FpBody->SetSkeletalMeshAsset(Model->GetSkeletalMeshAsset());
		FpBody->SetRelativeScale3D(Model->GetRelativeScale3D());
		FpBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FpBody->SetCastShadow(false);
		FpBody->SetOnlyOwnerSee(true);
		FpBody->LightingChannels = Driver->LightingChannels;
		FpBody->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		FpBody->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		FpBody->SetAnimInstanceClass(UWWOModelAnimInstance::StaticClass());
		FpBody->AddTickPrerequisiteComponent(Driver);
		FpBody->RegisterComponent();
		if (UWWOModelAnimInstance* Anim = Cast<UWWOModelAnimInstance>(FpBody->GetAnimInstance())) Anim->Configure(ModelRetargeter, Driver);
		for (const FName Bone : FpHideBones) FpBody->HideBoneByName(Bone, PBO_None);
	}
	// The model hangs off the mannequin, whose mesh space faces +y.
	FpBody->SetRelativeLocation(FVector(0.f, -FpBodyBack, 0.f));
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
	if (Replicating())
	{
		if (Net.Anim != Anim) Net.Queued = NAME_None;
		Net.Anim = Anim;
		Net.Speed = Q50(Speed);
	}
}

void UWWOBodyComponent::Queue(FName Anim)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance()) A->Queue(FindAnim(Anim), IsLooping(Anim));
	if (Replicating()) Net.Queued = Anim;
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
	if (Model)
	{
		Model->TickAnimation(0.f, false);
		Model->RefreshBoneTransforms();
	}
}

void UWWOBodyComponent::SetUpperBody(FName Anim, float Weight)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance()) A->SetUpperBody(FindAnim(Anim), Weight);
	if (Replicating())
	{
		Net.Upper = Anim;
		Net.UpperWeight = Q50(Weight);
	}
}

void UWWOBodyComponent::PlayUpperShot(FName Anim, float FadeIn, float FadeOut)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance()) A->PlayUpperShot(FindAnim(Anim), FadeIn, FadeOut);
	if (Replicating())
	{
		Net.Shot = Anim;
		++Net.ShotSerial;
	}
}

void UWWOBodyComponent::SetMix(FName A, FName B, float Alpha, float SpeedB)
{
	if (UWWOBodyAnimInstance* I = GetAnimInstance()) I->SetMix(FindAnim(A), FindAnim(B), Alpha, SpeedB);
	if (Replicating())
	{
		Net.MixA = A;
		Net.MixB = B;
		Net.MixAlpha = Q50(Alpha);
		Net.MixSpeedB = Q50(SpeedB);
	}
}

void UWWOBodyComponent::ClearLayers()
{
	if (UWWOBodyAnimInstance* I = GetAnimInstance()) I->ClearLayers();
	if (Replicating())
	{
		Net.Upper = NAME_None;
		Net.UpperWeight = 0;
		Net.MixA = Net.MixB = NAME_None;
		++Net.ClearSerial;
	}
}

float UWWOBodyComponent::UpperWeightNow() const
{
	const UWWOBodyAnimInstance* A = GetAnimInstance();
	return A ? A->GetUpperWeight() : 0.f;
}

float UWWOBodyComponent::HunchSpineNow() const
{
	const UWWOBodyAnimInstance* A = GetAnimInstance();
	return A ? A->HunchSpine : 0.f;
}

void UWWOBodyComponent::SetHunch(float SpineDegrees, float HeadDegrees)
{
	if (UWWOBodyAnimInstance* A = GetAnimInstance())
	{
		A->HunchSpine = SpineDegrees;
		A->HunchHead = HeadDegrees;
	}
	if (Replicating())
	{
		Net.HunchSpine = (int8)FMath::Clamp(FMath::RoundToInt(SpineDegrees), -127, 127);
		Net.HunchHead = (int8)FMath::Clamp(FMath::RoundToInt(HeadDegrees), -127, 127);
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
		Mesh->bCastHiddenShadow = Model == nullptr; // see UpdateModelExtras
		Mesh->MarkRenderStateDirty();
	}
	ApplyModelView();
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
