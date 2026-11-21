#include "UI/WWOPartPictures.h"
#include "Characters/WWOBodyComponent.h"
#include "Data/WWOData.h"
#include "FX/WWOFX.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// The stations sit in a row along -Y, this far apart: the key light's shadows (it travels down and toward -Y)
	// fall far below the next one's part.
	constexpr float StationGap = 1500.f;
	// The view: the camera a little above, looking down this far (degrees), with a long lens (horizontal field of
	// view) for little perspective, like a product shot; the part fills this much of the picture.
	constexpr float CameraPitch = -14.f, FieldOfView = 20.f, Fill = 0.95f;
	// How far each slot's part is turned from facing the camera, its front toward the key light (the camera's
	// left): three-quarters on. The arms (spread wide in the reference pose) further, so the pair draws in and
	// each arm shows larger, one a little behind the other but clear of it.
	constexpr float Turn[UWWOBodyComponent::NumShellSlots] = { 32.f, 30.f, 50.f, 26.f };
	// M_WWO_HUDGun's brightness for the studio's lights (the HUD's guns are lit about a quarter as brightly).
	constexpr float Gain = 0.85f;

	FVector StationAt(int32 Station) { return FVector(0.f, -StationGap * Station, 0.f); }
}

AWWOPartPictures::AWWOPartPictures()
{
	PrimaryActorTick.bCanEverTick = false; // the menu steps it
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	for (int32 i = 0; i < Stations; ++i)
	{
		USkeletalMeshComponent* Part = CreateDefaultSubobject<USkeletalMeshComponent>(*FString::Printf(TEXT("Part%d"), i));
		Part->SetupAttachment(GetRootComponent());
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->bVisibleInSceneCaptureOnly = true;     // never in the game's own view
		Part->bComponentUseFixedSkelBounds = true;   // the mesh's own bounds, not its physics bodies'
		Part->SetForcedLOD(1);                       // LOD 0, always
		Part->LightingChannels.bChannel0 = false;    // the character preview's studio lights only
		Part->LightingChannels.bChannel2 = true;
		Part->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // (no animation: the reference pose)
		Part->PrimaryComponentTick.bTickEvenWhenPaused = true; // the menu pauses the game
		Parts.Add(Part);

		USceneCaptureComponent2D* Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(*FString::Printf(TEXT("Capture%d"), i));
		Capture->SetupAttachment(GetRootComponent());
		Capture->FOVAngle = FieldOfView;
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->ShowOnlyComponents.Add(Part);
		Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR; // alpha: 0 where the part is (M_WWO_HUDGun cuts it out)
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		// The character preview's studio and nothing from the level (AWWOPreviewActor's flags), fixed exposure.
		FEngineShowFlags& Show = Capture->ShowFlags;
		Show.SetAtmosphere(false);
		Show.SetFog(false);
		Show.SetCloud(false);
		Show.SetGrain(false);
		Show.SetSkyLighting(false);
		Show.SetReflectionEnvironment(false);
		Show.SetScreenSpaceReflections(false);
		Show.SetMotionBlur(false);
		Show.SetLensFlares(false);
		FPostProcessSettings& PP = Capture->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = 1.f; // (the HUD's gun pictures', which M_WWO_HUDGun's Gain is set against)
		Capture->PostProcessBlendWeight = 1.f;
		Captures.Add(Capture);
	}
}

void AWWOPartPictures::BeginPlay()
{
	Super::BeginPlay();
	for (int32 i = 0; i < Stations; ++i) Parts[i]->SetVisibility(false);
}

UMaterialInterface* AWWOPartPictures::Picture(const UWWOFramePartData* Part, int32 Slot, const FColor (&Paint)[3])
{
	USkeletalMesh* Mesh = UWWOBodyComponent::ShellMeshFor(Part, Slot);
	if (!Mesh) return nullptr;
	const FString Key = FString::Printf(TEXT("%s|%d|%s|%s|%s"), *Mesh->GetPathName(), Slot, *Paint[0].ToHex(), *Paint[1].ToHex(), *Paint[2].ToHex());
	if (FWWOPartPicture* Found = Cache.Find(Key))
	{
		Found->Used = ++Clock;
		return Found->Mat;
	}
	UMaterialInterface* Base = WWOFX::Material(TEXT("M_WWO_HUDGun"));
	if (!Base) return nullptr;
	FWWOPartPicture Pic;
	Pic.Mesh = Mesh;
	Pic.Slot = Slot;
	for (int32 Role = 0; Role < 3; ++Role) Pic.Paint[Role] = Paint[Role];
	Pic.Used = ++Clock;
	Pic.Target = NewObject<UTextureRenderTarget2D>(this);
	Pic.Target->RenderTargetFormat = RTF_RGBA16f;
	Pic.Target->ClearColor = FLinearColor(0.f, 0.f, 0.f, 1.f); // clear until it's taken
	Pic.Target->bAutoGenerateMips = true;                      // the Shell tab shows it small
	Pic.Target->InitAutoFormat(Pixels.X, Pixels.Y);
	Pic.Mat = UMaterialInstanceDynamic::Create(Base, this);
	Pic.Mat->SetScalarParameterValue(TEXT("Gain"), Gain);
	// Until it's taken, the same part's latest picture in other colours stands in (a colour change shows no gap).
	const FWWOPartPicture* StandIn = nullptr;
	for (const TPair<FString, FWWOPartPicture>& It : Cache)
	{
		if (It.Value.bTaken && It.Value.Mesh == Mesh && It.Value.Slot == Slot && (!StandIn || It.Value.Used > StandIn->Used)) StandIn = &It.Value;
	}
	Pic.Mat->SetTextureParameterValue(TEXT("Tex"), StandIn ? StandIn->Target.Get() : Pic.Target.Get());
	UMaterialInterface* Out = Pic.Mat;
	Cache.Add(Key, MoveTemp(Pic));
	Queue.Add(Key);
	Trim();
	return Out;
}

void AWWOPartPictures::SetUp(int32 Station, const FWWOPartPicture& Pic)
{
	USkeletalMeshComponent* Part = Parts[Station];
	USceneCaptureComponent2D* Capture = Captures[Station];
	Part->SetSkeletalMeshAsset(Pic.Mesh);
	Part->SetForcedLOD(1);
	UWWOBodyComponent::PaintShellPartWith(Part, Pic.Paint);
	// Facing the camera (the preview robot's -90), turned three-quarters on.
	const FRotator Facing(0.f, -90.f + Turn[FMath::Clamp(Pic.Slot, 0, UWWOBodyComponent::NumShellSlots - 1)], 0.f);
	Part->SetRelativeLocationAndRotation(StationAt(Station), Facing);
	Part->SetVisibility(true);
	// Framed on the part's bounds (its reference pose) as the camera sees them: centred, and far enough back
	// that every corner of the box is inside Fill of the picture.
	const FBox Local = Pic.Mesh->GetImportedBounds().GetBox();
	const FTransform ToActor(Facing, StationAt(Station));
	FVector Corners[8];
	for (int32 i = 0; i < 8; ++i)
	{
		Corners[i] = ToActor.TransformPosition(FVector(i & 1 ? Local.Max.X : Local.Min.X, i & 2 ? Local.Max.Y : Local.Min.Y, i & 4 ? Local.Max.Z : Local.Min.Z));
	}
	const FRotator Look(CameraPitch, 180.f, 0.f);
	const FRotationMatrix Axes(Look);
	const FVector Forward = Axes.GetUnitAxis(EAxis::X), Right = Axes.GetUnitAxis(EAxis::Y), Up = Axes.GetUnitAxis(EAxis::Z);
	FVector Middle = ToActor.TransformPosition(Local.GetCenter());
	float MinX = MAX_flt, MaxX = -MAX_flt, MinY = MAX_flt, MaxY = -MAX_flt;
	for (const FVector& C : Corners)
	{
		MinX = FMath::Min(MinX, (C - Middle) | Right);
		MaxX = FMath::Max(MaxX, (C - Middle) | Right);
		MinY = FMath::Min(MinY, (C - Middle) | Up);
		MaxY = FMath::Max(MaxY, (C - Middle) | Up);
	}
	Middle += Right * (MinX + MaxX) * 0.5f + Up * (MinY + MaxY) * 0.5f;
	const float TanH = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f)) * Fill;
	const float TanV = TanH / (Shape.X / Shape.Y);
	float Distance = 10.f;
	for (const FVector& C : Corners)
	{
		const FVector D = C - Middle;
		const float Depth = D | Forward; // beyond the middle (from the camera)
		Distance = FMath::Max(Distance, FMath::Max(FMath::Abs(D | Right) / TanH, FMath::Abs(D | Up) / TanV) - Depth);
	}
	Capture->SetRelativeLocationAndRotation(Middle - Forward * Distance, Look);
	Capture->TextureTarget = Pic.Target;
}

void AWWOPartPictures::Step()
{
	for (int32 s = 0; s < Stations; ++s)
	{
		if (!Busy[s].IsEmpty())
		{
			FWWOPartPicture* Pic = Cache.Find(Busy[s]);
			if (!Pic)
			{
				Busy[s].Reset();
				continue;
			}
			++Frames[s];
			if (Frames[s] > SettleFrames) Captures[s]->CaptureScene();
			if (Frames[s] >= SettleFrames + Shots)
			{
				Pic->bTaken = true;
				Pic->Mat->SetTextureParameterValue(TEXT("Tex"), Pic->Target);
				Busy[s].Reset();
				if (Queue.IsEmpty()) Parts[s]->SetVisibility(false);
			}
			continue;
		}
		while (!Queue.IsEmpty() && Busy[s].IsEmpty())
		{
			const FString Key = Queue[0];
			Queue.RemoveAt(0);
			if (const FWWOPartPicture* Pic = Cache.Find(Key))
			{
				SetUp(s, *Pic);
				Busy[s] = Key;
				Frames[s] = 0;
			}
		}
	}
}

void AWWOPartPictures::Trim()
{
	if (Cache.Num() <= MostKept) return;
	// The least recently asked for go (never one waiting or being taken), down to a few under the most.
	TArray<TPair<uint64, FString>> Old;
	for (const TPair<FString, FWWOPartPicture>& It : Cache) Old.Add({ It.Value.Used, It.Key });
	Old.Sort([](const TPair<uint64, FString>& A, const TPair<uint64, FString>& B) { return A.Key < B.Key; });
	for (const TPair<uint64, FString>& It : Old)
	{
		if (Cache.Num() <= MostKept - 8) break;
		bool bBusy = Queue.Contains(It.Value);
		for (const FString& B : Busy) bBusy |= B == It.Value;
		if (!bBusy) Cache.Remove(It.Value);
	}
}
