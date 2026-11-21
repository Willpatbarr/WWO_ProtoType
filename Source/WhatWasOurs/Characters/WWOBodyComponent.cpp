#include "Characters/WWOBodyComponent.h"
#include "SkinnedAssetCompiler.h"
#include "Net/UnrealNetwork.h"
#include "WhatWasOurs.h"
#include "FX/WWOFX.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "WWOSwitches.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/WWOBodyAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Characters/WWOModelAnimInstance.h"
#include "Retargeter/IKRetargeter.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Systems/WWOConfig.h"
#include "Misc/PackageName.h"
#include "Player/WWOCharacter.h"

namespace
{
	const TCHAR* MannyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	// The baked Godot animations that loop (the UAL clips named *_Loop, as Godot's import loops them).
	const TCHAR* BodyMaterials[] = { TEXT("/Game/WWO/Materials/MI_WWO_Body_01.MI_WWO_Body_01"), TEXT("/Game/WWO/Materials/MI_WWO_Body_02.MI_WWO_Body_02") };
	const FName RegionTints[] = { "Head Tint", "Arms Tint", "Core Tint", "Legs Tint" };
	const FName Looping[] = { "Idle", "Walk", "Idle_Unarmed", "Walk_Unarmed", "Jog_Fwd", "Sprint", "Sprint_Unarmed", "Crouch_Idle", "Crouch_Fwd", "Jump", "Spell_Simple_Idle", "Slide", "Pistol_Aim_Neutral", "Aim_Pistol" };

	// Config/CharacterModels.json, read and parsed once a run (every body that spawns asks): retune with a restart.
	TSharedPtr<FJsonObject> CharacterModels() { return WWOConfig::LoadCached(TEXT("CharacterModels.json")); }

	// "shell_lod_screen_sizes": the screen size at which each of SK_RobotSkeleton's LODs starts, set on the loaded mesh
	// before a robot shows it (the import's own, 1 / 0.5 / 0.25, put LOD 1 at about 3 m). The robot picks its LOD from
	// these; its shell parts follow it, and so do its anchor grafts' detail levels. LOD 0 stays at 1.
	void ApplyShellLodSizes(USkeletalMesh* Bare)
	{
		const TSharedPtr<FJsonObject> Json = CharacterModels();
		const TArray<TSharedPtr<FJsonValue>>* Sizes = nullptr;
		if (!Bare || !Json.IsValid() || !Json->TryGetArrayField(TEXT("shell_lod_screen_sizes"), Sizes)) return;
		for (int32 i = 1; i < FMath::Min(Sizes->Num(), Bare->GetLODNum()); ++i)
		{
			const float Size = (float)(*Sizes)[i]->AsNumber();
			if (FSkeletalMeshLODInfo* Info = Bare->GetLODInfo(i); Info && Size > 0.f && Info->ScreenSize.Default != Size) Info->ScreenSize.Default = Size;
		}
	}

	// One entry of its "models".
	TSharedPtr<FJsonObject> ModelConfig(FName Key)
	{
		const TSharedPtr<FJsonObject> Json = CharacterModels();
		if (Key.IsNone() || !Json.IsValid()) return nullptr;
		const TSharedPtr<FJsonObject>* Models = nullptr;
		const TSharedPtr<FJsonObject>* Model = nullptr;
		if (Json->TryGetObjectField(TEXT("models"), Models) && (*Models)->TryGetObjectField(Key.ToString(), Model)) return *Model;
		return nullptr;
	}
}

void UWWOBodyComponent::Preload(FName Key, TArray<UObject*>& Out)
{
	const TSharedPtr<FJsonObject> Config = ModelConfig(Key);
	FString MeshPath;
	if (!Config.IsValid() || !Config->TryGetStringField(TEXT("mesh"), MeshPath)) return;
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Asset) return;
	Out.Add(Asset);
	FString RK = Key.ToString();
	Config->TryGetStringField(TEXT("retargeter"), RK);
	if (UIKRetargeter* R = LoadObject<UIKRetargeter>(nullptr, *FString::Printf(TEXT("/Game/WWO/Retarget/RTG_Manny_%s.RTG_Manny_%s"), *RK, *RK))) Out.Add(R);
	// A model that wears shell parts: its bare skeleton and every shell's parts, so a part swapped in
	// mid-match (yours, or another player's arriving over the network) never loads from disk in a frame.
	TArray<USkinnedAsset*> Compile = { Asset };
	bool bWearsShells = false;
	if (Config->TryGetBoolField(TEXT("shells"), bWearsShells) && bWearsShells)
	{
		const auto Load = [&](const FString& Name)
		{
			const FString Package = TEXT("/Game/WWO/Characters/RobotFrame/Shells/") + Name;
			if (!FPackageName::DoesPackageExist(Package)) return false;
			if (USkeletalMesh* Part = LoadObject<USkeletalMesh>(nullptr, *(Package + TEXT(".") + Name))) { Out.AddUnique(Part); Compile.AddUnique(Part); }
			return true;
		};
		Load(TEXT("SK_RobotSkeleton"));
		for (int32 Shell = 0; Shell < 64; ++Shell)
		{
			bool bAny = false;
			for (const TCHAR* Slot : { TEXT("Head"), TEXT("Core"), TEXT("Arms"), TEXT("Legs") }) bAny |= Load(FString::Printf(TEXT("SK_Shell%d_%s"), Shell, Slot));
			if (!bAny) break; // shells are numbered from 0 without gaps
		}
	}
#if WITH_EDITOR // the asset compiler exists only in editor builds; a packaged game loads meshes ready-made
	FSkinnedAssetCompilingManager::Get().FinishCompilation(Compile);
#endif
}

void UWWOBodyComponent::PreloadAnims(TConstArrayView<FName> Names, TArray<UObject*>& Out)
{
	for (const FName& Anim : Names)
	{
		// FindAnim's lookup: the baked replacement first, else the Godot bake.
		const FString Name = Anim.ToString();
		const FString BodyPath = FString::Printf(TEXT("/Game/WWO/Anims/Body/A_%s"), *Name);
		UAnimSequence* Seq = FPackageName::DoesPackageExist(BodyPath) ? LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s.A_%s"), *BodyPath, *Name)) : nullptr;
		if (!Seq) Seq = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/WWO/Anims/UAL/A_%s.A_%s"), *Name, *Name));
		if (Seq) Out.Add(Seq);
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
		FString Path;
		const TSharedPtr<FJsonObject> Json = CharacterModels();
		if (Json.IsValid() && Json->TryGetStringField(TEXT("player_mesh"), Path) && !Path.IsEmpty())
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
	ClearAnchorGrafts(); // (pinned to the model about to go; RebuildShell places them on the new one)
	StopRobotLod();
	for (const TArray<TObjectPtr<USkeletalMeshComponent>>* List : { &ShellParts, &FpShellParts })
	{
		for (USkeletalMeshComponent* C : *List) if (C) C->DestroyComponent();
	}
	ShellParts.Reset();
	FpShellParts.Reset();
	ShellPartSlots.Reset();
	FpShellPartSlots.Reset();
	if (Model) Model->DestroyComponent();
	Model = nullptr;
	if (FpBody) FpBody->DestroyComponent();
	FpBody = nullptr;
	ModelRetargeter = nullptr;
	ActiveModel = Key;
	const TSharedPtr<FJsonObject> Config = ModelConfig(Key);
	FString MeshPath;
	USkeletalMesh* Asset = Config.IsValid() && Config->TryGetStringField(TEXT("mesh"), MeshPath) ? LoadObject<USkeletalMesh>(nullptr, *MeshPath) : nullptr;
	// "shells": the model is the robot's bare skeleton, and its armour is four swappable parts (RebuildShell).
	bShells = false;
	if (Asset && Config->TryGetBoolField(TEXT("shells"), bShells) && bShells)
	{
		USkeletalMesh* Bare = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/WWO/Characters/RobotFrame/Shells/SK_RobotSkeleton.SK_RobotSkeleton"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Bare && Bare->GetSkeleton() == Asset->GetSkeleton()) { ApplyShellLodSizes(Bare); Asset = Bare; }
		else
		{
			bShells = false;
			UE_LOG(LogWWO, Warning, TEXT("Character model '%s': shells are on but SK_RobotSkeleton is missing (Tools/import_shell_parts.py); showing the whole mesh"), *Key.ToString());
		}
	}
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
	bModelAnchorGrafts = false;
	Config->TryGetBoolField(TEXT("anchor_grafts"), bModelAnchorGrafts);
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
	// A robot of shell parts picks its own LOD from its own screen size (SK_RobotSkeleton's three, made at import, starting
	// at CharacterModels.json's shell_lod_screen_sizes; the parts follow it as their leader, and so do its anchor grafts'
	// detail levels). Attached to the mannequin it would take the mannequin's, which, hidden, never leaves LOD 0. (The
	// menu's preview, seen only through its capture, keeps that: always in full.)
	if (bShells && !bFullDetailGrafts) Model->bSyncAttachParentLOD = false;
	// (Which LOD it shows is picked by UpdateRobotLod, registered once it's set up below.)
	Model->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Model->SetAnimInstanceClass(UWWOModelAnimInstance::StaticClass());
	Model->AddTickPrerequisiteComponent(Driver); // the mannequin poses first, then the model copies it
	Model->PrimaryComponentTick.bTickEvenWhenPaused = Driver->PrimaryComponentTick.bTickEvenWhenPaused; // the menu preview idles while the game is paused
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
	if (bShells && !bFullDetailGrafts)
	{
		RobotLod = 0;
		RobotLodAt = 0.0;
		RobotLodHandle = Model->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &UWWOBodyComponent::UpdateRobotLod));
	}
	// It copies the mannequin's pose now, not at its first tick (the menu preview may never tick it).
	Model->TickAnimation(0.f, false);
	Model->RefreshBoneTransforms();
}

namespace
{
	// The robot's LOD hysteresis (UpdateRobotLod): a step to a coarser LOD waits until it is this fraction past the switch's
	// screen size, a step back to a finer one until it is this fraction past it the other way; and no step at all within
	// RobotLodHold seconds of the last one. (A client saw a robot flip between LOD 1 and 2 every frame for 0.2 s near a switch.)
	constexpr float RobotLodMargin = 0.1f;
	constexpr double RobotLodHold = 0.5;
}

void UWWOBodyComponent::UpdateRobotLod()
{
	USkeletalMeshComponent* M = Model;
	const USkeletalMesh* Mesh = M ? M->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh) return;
	const int32 Num = Mesh->GetLODNum();
	// What the engine would pick: the largest screen size any view saw the robot at last frame (MaxDistanceFactor is the
	// squared screen radius, r.SkeletalMeshLODRadiusScale applied: FSkeletalMeshObject::UpdateMinDesiredLODLevel) against
	// each LOD's screen size, plus r.SkeletalMeshLODBias. Not yet drawn (0): it stays where it is.
	if (M->MaxDistanceFactor > 0.f && Num > 1)
	{
		const float Size = 2.f * FMath::Sqrt(M->MaxDistanceFactor);
		static const IConsoleVariable* BiasVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMeshLODBias"));
		const int32 Bias = BiasVar ? BiasVar->GetInt() : 0;
		auto LevelAt = [&](float Scale)
		{
			int32 Level = 0;
			for (int32 i = Num - 1; i > 0; --i)
			{
				const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(i);
				if (Info && Size < Info->ScreenSize.GetValue() * Scale) { Level = i; break; }
			}
			return FMath::Clamp(Level + Bias, 0, Num - 1);
		};
		const int32 Coarser = LevelAt(1.f - RobotLodMargin); // past a switch by the margin (smaller on screen)
		const int32 Finer = LevelAt(1.f + RobotLodMargin);   // past it by the margin the other way (larger)
		const int32 Want = Coarser > RobotLod ? Coarser : (Finer < RobotLod ? Finer : RobotLod);
		const double Now = FPlatformTime::Seconds();
		if (Want != RobotLod && Now - RobotLodAt >= RobotLodHold)
		{
			RobotLod = Want;
			RobotLodAt = Now;
		}
	}
	// Its parts follow it (their leader). Only the rendered LOD is forced (through ILODSyncInterface, where it's public):
	// streaming is left alone.
	ILODSyncInterface* Sync = M;
	if (Sync->GetForceRenderedLOD() != RobotLod) Sync->SetForceRenderedLOD(RobotLod);
	UpdateAnchorLevel(); // and its anchor grafts' level follows the LOD it shows
}

void UWWOBodyComponent::StopRobotLod()
{
	if (Model && RobotLodHandle.IsValid()) Model->UnregisterOnBoneTransformsFinalizedDelegate(RobotLodHandle);
	RobotLodHandle.Reset();
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
		for (USkeletalMeshComponent* C : ShellParts) if (C && C->GetOverlayMaterial()) C->SetOverlayMaterial(nullptr);
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
	for (USkeletalMeshComponent* C : ShellParts) if (C && C->GetOverlayMaterial() != HighlightMat) C->SetOverlayMaterial(HighlightMat); // the armour is most of what you see
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
		RebuildShell();
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
	RebuildShell();
}

// --- Shell parts ---------------------------------------------------------------------------------

int32 UWWOBodyComponent::ShellOverride[UWWOBodyComponent::NumShellSlots] = { -1, -1, -1, -1 };

namespace
{
	const TCHAR* const ShellSlotNames[UWWOBodyComponent::NumShellSlots] = { TEXT("Head"), TEXT("Core"), TEXT("Arms"), TEXT("Legs") };

	// Shell Shell's part for Slot (SK_Shell<n>_<Slot>); null if it isn't imported.
	USkeletalMesh* LoadShellPart(int32 Shell, int32 Slot)
	{
		const FString Name = FString::Printf(TEXT("SK_Shell%d_%s"), Shell, ShellSlotNames[Slot]);
		return LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/WWO/Characters/RobotFrame/Shells/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	// "2" (every slot) or "2,1,0,2" / "2 1 0 2" (head, core, arms, legs); "-" or "off" leaves a slot to the body's own pick.
	void ParseShellPicks(const TArray<FString>& Args, int32 Out[UWWOBodyComponent::NumShellSlots])
	{
		for (int32 i = 0; i < UWWOBodyComponent::NumShellSlots; ++i)
		{
			const FString* A = Args.IsValidIndex(i) ? &Args[i] : (Args.Num() == 1 ? &Args[0] : nullptr);
			Out[i] = A && A->IsNumeric() ? FCString::Atoi(**A) : -1;
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ShellCommand(TEXT("wwo.Shell"),
		TEXT("wwo.Shell <n>: every robot wears shell n (0 base, 1 sleek, 2 scout). wwo.Shell <head> <core> <arms> <legs>: a shell per part. wwo.Shell off: back to each body's own."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			int32 Picks[UWWOBodyComponent::NumShellSlots];
			ParseShellPicks(Args, Picks);
			UWWOBodyComponent::SetShellOverride(World, Picks);
		}));

	// -WWOShell=2 or -WWOShell=2,1,0,2: the same from the command line, read once.
	void ReadShellSwitch()
	{
		static bool bRead = false;
		if (bRead) return;
		bRead = true;
		FString Text;
		if (!WWOSwitches::Value(TEXT("WWOShell="), Text, false)) return;
		TArray<FString> Args;
		Text.ParseIntoArray(Args, TEXT(","));
		ParseShellPicks(Args, UWWOBodyComponent::ShellOverride);
	}
}

void UWWOBodyComponent::SetShellOverride(UWorld* World, const int32 Picks[NumShellSlots])
{
	ReadShellSwitch(); // (so a later first read doesn't undo this)
	for (int32 i = 0; i < NumShellSlots; ++i) ShellOverride[i] = Picks[i];
	for (TObjectIterator<UWWOBodyComponent> It; It; ++It)
	{
		if (It->GetWorld() == World && It->HasShells()) It->RebuildShell();
	}
}

bool UWWOBodyComponent::SetShell(int32 Head, int32 Core, int32 Arms, int32 Legs)
{
	const int32 Picks[NumShellSlots] = { Head, Core, Arms, Legs };
	bool bChanged = false;
	for (int32 i = 0; i < NumShellSlots; ++i)
	{
		bChanged |= ShellPick[i] != Picks[i];
		ShellPick[i] = Picks[i];
	}
	if (!bChanged || !HasShells()) return false;
	RebuildShell();
	return true;
}

void UWWOBodyComponent::ShellFromLoadout(const UWWOLoadoutSubsystem* Loadout)
{
	static const FName Slots[NumShellSlots] = { "head", "core", "arms", "legs" };
	int32 Picks[NumShellSlots] = { 0, 0, 0, 0 };
	FColor Paint[NumShellSlots][NumShellRoles] = {};
	for (int32 i = 0; i < NumShellSlots; ++i)
	{
		Picks[i] = ShellLookOf(Loadout ? Loadout->GetFramePart(Slots[i]) : nullptr);
		for (int32 Role = 0; Role < NumShellRoles; ++Role) Paint[i][Role] = Loadout ? Loadout->GetShellColor(i, Role) : FColor(0, 0, 0, 0);
	}
	SetShellPaint(Paint); // (repaints the parts worn now; a rebuild below paints the new ones)
	SetShell(Picks[0], Picks[1], Picks[2], Picks[3]);
}

int32 UWWOBodyComponent::ShellSlotOf(FName FrameSlot)
{
	static const FName Slots[NumShellSlots] = { "head", "core", "arms", "legs" };
	for (int32 i = 0; i < NumShellSlots; ++i) if (Slots[i] == FrameSlot) return i;
	return INDEX_NONE;
}

int32 UWWOBodyComponent::ShellLookOf(const UWWOFramePartData* Part)
{
	int32 Shell = 0;
	const TSharedPtr<FJsonObject> Json = CharacterModels();
	const TSharedPtr<FJsonObject>* Looks = nullptr;
	if (Part && Json.IsValid() && Json->TryGetObjectField(TEXT("shell_looks"), Looks)) (*Looks)->TryGetNumberField(Part->Id.ToString(), Shell);
	return Shell;
}

void UWWOBodyComponent::SetShellPaint(const FColor (&Paint)[NumShellSlots][NumShellRoles])
{
	bool bChanged = false;
	for (int32 Slot = 0; Slot < NumShellSlots; ++Slot)
	{
		for (int32 Role = 0; Role < NumShellRoles; ++Role)
		{
			bChanged |= ShellPaint[Slot][Role] != Paint[Slot][Role];
			ShellPaint[Slot][Role] = Paint[Slot][Role];
		}
	}
	if (!bChanged) return;
	++ShellPaintSerial; // (the first-person hands' arms part repaints on this)
	for (int32 i = 0; i < ShellParts.Num(); ++i) if (ShellPartSlots.IsValidIndex(i)) PaintShellPart(ShellParts[i], ShellPartSlots[i]);
	for (int32 i = 0; i < FpShellParts.Num(); ++i) if (FpShellPartSlots.IsValidIndex(i)) PaintShellPart(FpShellParts[i], FpShellPartSlots[i]);
}

namespace
{
	// Which of a slot's colours a shell part's material takes (FWWOEquipped::EShellRole), from its name: a shell's
	// MI_Shell<N>_<Role> or the base robot's MI_Robot_<Role> (Tools/import_shell_parts.py). INDEX_NONE: it keeps its own.
	int32 ShellMaterialRole(const UMaterialInterface* M, bool& bOutLight)
	{
		bOutLight = false;
		if (!M) return INDEX_NONE;
		const FString Name = M->GetName();
		int32 Cut = INDEX_NONE;
		if (!Name.FindLastChar(TEXT('_'), Cut)) return INDEX_NONE;
		const FString Role = Name.Mid(Cut + 1);
		if (Role == TEXT("PrimaryLight")) { bOutLight = true; return 0; }
		if (Role == TEXT("Primary") || Role == TEXT("Armour")) return 0;
		if (Role == TEXT("Secondary") || Role == TEXT("Trim")) return 1;
		if (Role == TEXT("Glow") || Role == TEXT("Visor")) return 2;
		return INDEX_NONE; // Frame, Joint
	}

	// How much lighter a shell's PrimaryLight is than its Primary (their Color's mean), so a picked primary keeps that step.
	float PrimaryLightRatio(const UMaterialInterface* Light)
	{
		static TMap<FString, float> Known;
		const FString Path = Light->GetPathName();
		if (const float* Found = Known.Find(Path)) return *Found;
		float Ratio = 1.44f; // (0.052 / 0.036, import_shell_parts.py's)
		FLinearColor L, P;
		const UMaterialInterface* Primary = LoadObject<UMaterialInterface>(nullptr, *Path.Replace(TEXT("PrimaryLight"), TEXT("Primary")), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Primary && Light->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), L) && Primary->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), P))
		{
			const float Base = P.R + P.G + P.B;
			if (Base > KINDA_SMALL_NUMBER) Ratio = (L.R + L.G + L.B) / Base;
		}
		Known.Add(Path, Ratio);
		return Ratio;
	}
}

void UWWOBodyComponent::PaintShellPart(USkeletalMeshComponent* Part, int32 Slot) const
{
	if (Slot >= 0 && Slot < NumShellSlots) PaintShellPartWith(Part, ShellPaint[Slot]);
}

void UWWOBodyComponent::PaintShellPartWith(USkeletalMeshComponent* Part, const FColor (&Paint)[NumShellRoles])
{
	const USkeletalMesh* Asset = Part ? Part->GetSkeletalMeshAsset() : nullptr;
	if (!Asset) return;
	const TArray<FSkeletalMaterial>& Slots = Asset->GetMaterials();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		UMaterialInterface* Own = Slots[i].MaterialInterface;
		bool bLight = false;
		const int32 Role = ShellMaterialRole(Own, bLight);
		const FColor Pick = Role == INDEX_NONE ? FColor(0, 0, 0, 0) : Paint[Role];
		if (Pick.A == 0)
		{
			if (Part->GetMaterial(i) != Own) Part->SetMaterial(i, Own); // default: the part's own
			continue;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(i));
		if (!Mid || Mid->Parent != Own)
		{
			Mid = UMaterialInstanceDynamic::Create(Own, Part);
			Part->SetMaterial(i, Mid);
		}
		const FLinearColor Color(Pick); // sRGB -> linear, as the materials' Color is
		if (Role == 2)
		{
			// The glow: its base colour and its emission, at the strength it had (EmissiveColor's brightest channel; Glow,
			// the multiplier, stays the shell's own).
			FLinearColor Emissive = FLinearColor::White;
			Own->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("EmissiveColor")), Emissive);
			Mid->SetVectorParameterValue(TEXT("Color"), Color);
			Mid->SetVectorParameterValue(TEXT("EmissiveColor"), Color * Emissive.GetMax());
		}
		else
		{
			const float Ratio = bLight ? PrimaryLightRatio(Own) : 1.f;
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(FMath::Min(Color.R * Ratio, 1.f), FMath::Min(Color.G * Ratio, 1.f), FMath::Min(Color.B * Ratio, 1.f), 1.f));
		}
	}
}

USkeletalMesh* UWWOBodyComponent::ShellMesh(int32 Slot) const
{
	if (!HasShells() || Slot < 0 || Slot >= NumShellSlots) return nullptr;
	ReadShellSwitch();
	const USkeleton* Skeleton = Model->GetSkeletalMeshAsset() ? Model->GetSkeletalMeshAsset()->GetSkeleton() : nullptr;
	// The picked shell's part; a shell that has none for this slot (or isn't imported) shows the base robot's.
	for (const int32 Shell : { ShellOverride[Slot] >= 0 ? ShellOverride[Slot] : ShellPick[Slot], 0 })
	{
		USkeletalMesh* Asset = LoadShellPart(Shell, Slot);
		if (Asset && Asset->GetSkeleton() == Skeleton) return Asset;
	}
	return nullptr;
}

USkeletalMesh* UWWOBodyComponent::ShellMeshFor(const UWWOFramePartData* Part, int32 Slot)
{
	if (Slot < 0 || Slot >= NumShellSlots) return nullptr;
	ReadShellSwitch();
	for (const int32 Shell : { ShellOverride[Slot] >= 0 ? ShellOverride[Slot] : ShellLookOf(Part), 0 })
	{
		if (USkeletalMesh* Asset = LoadShellPart(Shell, Slot)) return Asset;
	}
	return nullptr;
}

USkeletalMeshComponent* UWWOBodyComponent::AddShellPart(USkeletalMeshComponent* Leader, USkeletalMesh* Asset)
{
	// A part skinned to the robot's skeleton, posed by the mesh it sits on (its leader).
	USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(GetOwner());
	C->SetSkeletalMeshAsset(Asset);
	C->SetupAttachment(Leader);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->LightingChannels = Leader->LightingChannels; // lit like the body (the menu preview has its own lights)
	C->SetLeaderPoseComponent(Leader);
	C->RegisterComponent();
	GetOwner()->AddInstanceComponent(C);
	return C;
}

void UWWOBodyComponent::RebuildShell()
{
	for (const TArray<TObjectPtr<USkeletalMeshComponent>>* List : { &ShellParts, &FpShellParts })
	{
		for (USkeletalMeshComponent* C : *List) if (C) C->DestroyComponent();
	}
	ShellParts.Reset();
	FpShellParts.Reset();
	ShellPartSlots.Reset();
	FpShellPartSlots.Reset();
	for (FName& Part : WornParts) Part = NAME_None;
	if (!HasShells())
	{
		ClearAnchorGrafts();
		return;
	}
	const double Started = FPlatformTime::Seconds();
	ON_SCOPE_EXIT { UE_LOG(LogWWO, Verbose, TEXT("Body %s: shell rebuilt in %.2f ms"), *GetOwner()->GetName(), (FPlatformTime::Seconds() - Started) * 1000.0); };
	for (int32 Slot = 0; Slot < NumShellSlots; ++Slot)
	{
		USkeletalMesh* Asset = ShellMesh(Slot);
		if (!Asset) continue;
		WornParts[Slot] = Asset->GetFName();
		USkeletalMeshComponent* C = AddShellPart(Model, Asset);
		ApplyShadowOnly(C); // your own: hidden from you, casting your shadow, as the model is
		C->bCastHiddenShadow = bShadowOnly;
		C->SetOverlayMaterial(Model->GetOverlayMaterial());
		PaintShellPart(C, Slot);
		ShellParts.Add(C);
		ShellPartSlots.Add(Slot);
		// Your own legs in first person (FpBody: the skeleton from the hide bone down) wear their parts too.
		if (FpBody && (Slot == ShellLegs || Slot == ShellCore))
		{
			USkeletalMeshComponent* F = AddShellPart(FpBody, Asset);
			F->SetCastShadow(false);
			F->SetOnlyOwnerSee(true);
			for (const FName Bone : FpHideBones) F->HideBoneByName(Bone, PBO_None);
			PaintShellPart(F, Slot);
			FpShellParts.Add(F);
			FpShellPartSlots.Add(Slot);
		}
	}
	if (bAnchorGraftsLive) BuildAnchorGrafts(); // the new parts' anchors are elsewhere
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
	ClearAnchorGrafts(); // (and a build on its way is not given to it)
	StopRobotLod();
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
