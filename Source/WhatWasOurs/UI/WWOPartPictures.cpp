#include "UI/WWOPartPictures.h"
#include "UI/WWOPreviewActor.h"
#include "Characters/WWOBodyComponent.h"
#include "Data/WWOData.h"
#include "FX/WWOFX.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include <atomic>

// A probe of the cut-out on its way back from the GPU: copied after its capture, polled on the render thread each
// frame until it is ready, then measured (ShownRect) there; the game thread reads the rectangle once bDone is set.
struct FWWOPartProbe
{
	FRHIGPUTextureReadback Readback{ TEXT("WWOPartProbe") };
	std::atomic<bool> bDone{ false };
	FBox2D Rect{ ForceInit };
};

namespace
{
	// The stations sit in a row along -Y, this far apart: the key light's shadows (it travels down and toward -Y)
	// fall far below the next one's part.
	constexpr float StationGap = 1500.f;
	// The view: the camera a little above, looking down this far (degrees), with a long lens (horizontal field of
	// view) for little perspective, like a product shot. The probe is framed on the mesh's bounds (BoundsFill of
	// the picture); the picture on what shows in the probe (ShownFill: a small margin all round).
	constexpr float CameraPitch = -14.f, FieldOfView = 20.f, BoundsFill = 0.95f, ShownFill = 0.92f;
	// How far each slot's part is turned from facing the camera, its front toward the key light (the camera's
	// left): three-quarters on. The arms (spread wide in the reference pose) a little more, so the pair comes out
	// about as wide as the picture's 3:2 and fills it.
	constexpr float Turn[UWWOBodyComponent::NumShellSlots] = { 32.f, 30.f, 36.f, 26.f };

	FVector StationAt(int32 Station) { return FVector(0.f, -StationGap * Station, 0.f); }
	FRotator Look() { return FRotator(CameraPitch, 180.f, 0.f); }

	// Both captures' settings: only the part (ShowOnly), nothing from the level (the character preview's flags), no
	// anti-aliasing (the two must cover the same pixels; the targets are drawn at about half their size).
	void SetUpCapture(USceneCaptureComponent2D* Capture, USkeletalMeshComponent* Part, ESceneCaptureSource Source)
	{
		Capture->FOVAngle = FieldOfView;
		Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Capture->ShowOnlyComponents.Add(Part);
		Capture->CaptureSource = Source;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
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
		Show.SetAntiAliasing(false);
		Show.SetTemporalAA(false);
		// The character preview's exposure and bloom (AWWOPreviewActor), so the colour comes out as on the preview robot.
		FPostProcessSettings& PP = Capture->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = AWWOPreviewActor::CharacterExposure;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.f;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.45f;
		PP.bOverride_BloomThreshold = true;
		PP.BloomThreshold = 0.6f;
		PP.bOverride_SceneFringeIntensity = true;
		PP.SceneFringeIntensity = 0.f;
		Capture->PostProcessBlendWeight = 1.f;
	}

	UTextureRenderTarget2D* MakeTarget(UObject* Outer, ETextureRenderTargetFormat Format, FIntPoint Size)
	{
		UTextureRenderTarget2D* T = NewObject<UTextureRenderTarget2D>(Outer);
		T->RenderTargetFormat = Format;
		T->ClearColor = FLinearColor(0.f, 0.f, 0.f, 1.f); // (a cut-out's alpha 1: clear until it's taken)
		T->InitAutoFormat(Size.X, Size.Y);
		return T;
	}
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
		USceneCaptureComponent2D* Color = CreateDefaultSubobject<USceneCaptureComponent2D>(*FString::Printf(TEXT("ColorCapture%d"), i));
		Color->SetupAttachment(GetRootComponent());
		SetUpCapture(Color, Part, ESceneCaptureSource::SCS_FinalColorLDR); // tone mapped, as the preview's
		ColorCaptures.Add(Color);
		USceneCaptureComponent2D* Cut = CreateDefaultSubobject<USceneCaptureComponent2D>(*FString::Printf(TEXT("CutCapture%d"), i));
		Cut->SetupAttachment(GetRootComponent());
		SetUpCapture(Cut, Part, ESceneCaptureSource::SCS_SceneColorHDR); // alpha: 0 where the part is
		CutCaptures.Add(Cut);
	}
}

void AWWOPartPictures::BeginPlay()
{
	Super::BeginPlay();
	for (int32 i = 0; i < Stations; ++i)
	{
		Parts[i]->SetVisibility(false);
		Probes.Add(MakeTarget(this, RTF_RGBA16f, ProbePixels));
	}
}

UMaterialInterface* AWWOPartPictures::Picture(const UWWOFramePartData* Part, int32 Slot, const FColor (&Paint)[3], FColor Frame, bool bSmall)
{
	USkeletalMesh* Mesh = UWWOBodyComponent::ShellMeshFor(Part, Slot);
	if (!Mesh) return nullptr;
	const FString Key = FString::Printf(TEXT("%s|%d|%s|%s|%s|%s|%s"), *Mesh->GetPathName(), Slot, *Paint[0].ToHex(), *Paint[1].ToHex(), *Paint[2].ToHex(), *Frame.ToHex(), bSmall ? TEXT("small") : TEXT("large"));
	if (FWWOPartPicture* Found = Cache.Find(Key))
	{
		Found->Used = ++Clock;
		return Found->Mat;
	}
	UMaterialInterface* Base = WWOFX::Material(TEXT("M_WWO_PartPicture"));
	if (!Base) return nullptr;
	FWWOPartPicture Pic;
	Pic.Mesh = Mesh;
	Pic.Slot = Slot;
	Pic.bSmall = bSmall;
	for (int32 Role = 0; Role < 3; ++Role) Pic.Paint[Role] = Paint[Role];
	Pic.Frame = Frame;
	Pic.Used = ++Clock;
	const FIntPoint Size = bSmall ? SmallPixels : Pixels;
	Pic.Color = MakeTarget(this, RTF_RGBA8, Size); // display values, as the preview's (M_WWO_PartPicture decodes them)
	Pic.Cut = MakeTarget(this, RTF_RGBA16f, Size);
	Pic.Mat = UMaterialInstanceDynamic::Create(Base, this);
	// Until it's taken, the same part's latest picture in other colours stands in (a colour change shows no gap).
	const FWWOPartPicture* StandIn = nullptr;
	for (const TPair<FString, FWWOPartPicture>& It : Cache)
	{
		if (It.Value.bTaken && It.Value.Mesh == Mesh && It.Value.Slot == Slot && It.Value.bSmall == bSmall && (!StandIn || It.Value.Used > StandIn->Used)) StandIn = &It.Value;
	}
	Pic.Mat->SetTextureParameterValue(TEXT("Color"), StandIn ? StandIn->Color.Get() : Pic.Color.Get());
	Pic.Mat->SetTextureParameterValue(TEXT("Cut"), StandIn ? StandIn->Cut.Get() : Pic.Cut.Get());
	UMaterialInterface* Out = Pic.Mat;
	Cache.Add(Key, MoveTemp(Pic));
	Queue.Add(Key);
	Trim();
	return Out;
}

void AWWOPartPictures::Aim(int32 Station, const FVector& Middle, float Distance, float Fov)
{
	FStation& Job = Jobs[Station];
	Job.Middle = Middle;
	Job.Distance = Distance;
	const FVector At = Middle - Look().Vector() * Distance;
	for (USceneCaptureComponent2D* Capture : { ColorCaptures[Station].Get(), CutCaptures[Station].Get() })
	{
		Capture->SetRelativeLocationAndRotation(At, Look());
		Capture->FOVAngle = Fov;
	}
}

void AWWOPartPictures::SetUp(int32 Station, const FWWOPartPicture& Pic)
{
	USkeletalMeshComponent* Part = Parts[Station];
	Part->SetSkeletalMeshAsset(Pic.Mesh);
	Part->SetForcedLOD(1);
	UWWOBodyComponent::PaintShellPartWith(Part, Pic.Paint, Pic.Frame);
	// Facing the camera (the preview robot's -90), turned three-quarters on.
	const FRotator Facing(0.f, -90.f + Turn[FMath::Clamp(Pic.Slot, 0, UWWOBodyComponent::NumShellSlots - 1)], 0.f);
	Part->SetRelativeLocationAndRotation(StationAt(Station), Facing);
	Part->SetVisibility(true);
	// The probe's framing, on the part's bounds (its reference pose) as the camera sees them: centred, and far enough
	// back that every corner of the box is inside BoundsFill of the picture.
	const FBox Local = Pic.Mesh->GetImportedBounds().GetBox();
	const FTransform ToActor(Facing, StationAt(Station));
	FVector Corners[8];
	for (int32 i = 0; i < 8; ++i)
	{
		Corners[i] = ToActor.TransformPosition(FVector(i & 1 ? Local.Max.X : Local.Min.X, i & 2 ? Local.Max.Y : Local.Min.Y, i & 4 ? Local.Max.Z : Local.Min.Z));
	}
	const FRotationMatrix Axes(Look());
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
	const float TanH = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f)) * BoundsFill;
	const float TanV = TanH / (Shape.X / Shape.Y);
	float Distance = 10.f;
	for (const FVector& C : Corners)
	{
		const FVector D = C - Middle;
		const float Depth = D | Forward; // beyond the middle (from the camera)
		Distance = FMath::Max(Distance, FMath::Max(FMath::Abs(D | Right) / TanH, FMath::Abs(D | Up) / TanV) - Depth);
	}
	Aim(Station, Middle, Distance, FieldOfView);
	CutCaptures[Station]->TextureTarget = Probes[Station];
	ColorCaptures[Station]->TextureTarget = nullptr;
}

void AWWOPartPictures::Reframe(int32 Station, const FBox2D& Rect)
{
	// The probe's rectangle onto the picture: the camera slides sideways and up to centre it (its size at the
	// middle's depth: the lens is long, so the part's depth hardly changes it) and its field of view narrows until
	// the rectangle fills ShownFill of the picture.
	const FStation& Job = Jobs[Station];
	const FVector2D Size = Rect.GetSize();
	if (Size.X < 2.f || Size.Y < 2.f) return;
	const float W = ProbePixels.X, H = ProbePixels.Y;
	const float TanH = FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f));
	const float Pixel = 2.f * Job.Distance * TanH / W; // cm across a probe pixel at the middle's depth
	const FVector2D Centre = Rect.GetCenter();
	const FRotationMatrix Axes(Look());
	const FVector Middle = Job.Middle + Axes.GetUnitAxis(EAxis::Y) * (Centre.X - W * 0.5f) * Pixel - Axes.GetUnitAxis(EAxis::Z) * (Centre.Y - H * 0.5f) * Pixel;
	const float Zoom = FMath::Max(Size.X / W, Size.Y / H) / ShownFill;
	Aim(Station, Middle, Job.Distance, FMath::RadiansToDegrees(2.f * FMath::Atan(TanH * Zoom)));
}

FBox2D AWWOPartPictures::ShownRect(TFunctionRef<bool(int32 X, int32 Y)> Covered, FIntPoint Size, float MinShare)
{
	// Rows first (a thin upright like the antenna has a pixel or two in its rows against the part's dozens), then
	// columns counted only over the rows kept (so the antenna's pixels don't count toward its columns).
	TArray<int32> Rows, Cols;
	Rows.SetNumZeroed(Size.Y);
	Cols.SetNumZeroed(Size.X);
	for (int32 Y = 0; Y < Size.Y; ++Y) for (int32 X = 0; X < Size.X; ++X) if (Covered(X, Y)) ++Rows[Y];
	const int32 MostRow = FMath::Max(Rows);
	if (MostRow == 0) return FBox2D(ForceInit);
	int32 Top = Size.Y, Bottom = -1;
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		if (Rows[Y] >= FMath::Max(1.f, MinShare * MostRow)) { Top = FMath::Min(Top, Y); Bottom = Y; }
	}
	for (int32 Y = Top; Y <= Bottom; ++Y) for (int32 X = 0; X < Size.X; ++X) if (Covered(X, Y)) ++Cols[X];
	const int32 MostCol = FMath::Max(Cols);
	int32 Left = Size.X, RightEdge = -1;
	for (int32 X = 0; X < Size.X; ++X)
	{
		if (MostCol > 0 && Cols[X] >= FMath::Max(1.f, MinShare * MostCol)) { Left = FMath::Min(Left, X); RightEdge = X; }
	}
	if (RightEdge < 0) return FBox2D(ForceInit);
	return FBox2D(FVector2D(Left, Top), FVector2D(RightEdge + 1, Bottom + 1));
}

void AWWOPartPictures::Finish(int32 Station)
{
	FStation& Job = Jobs[Station];
	if (FWWOPartPicture* Pic = Cache.Find(Job.Key))
	{
		Pic->bTaken = true;
		Pic->Mat->SetTextureParameterValue(TEXT("Color"), Pic->Color);
		Pic->Mat->SetTextureParameterValue(TEXT("Cut"), Pic->Cut);
	}
	Job.Key.Reset();
	Job.Stage = EStage::Idle;
	Job.Probe.Reset();
	if (Queue.IsEmpty()) Parts[Station]->SetVisibility(false);
}

void AWWOPartPictures::Step()
{
	for (int32 s = 0; s < Stations; ++s)
	{
		FStation& Job = Jobs[s];
		if (Job.Stage != EStage::Idle && !Cache.Contains(Job.Key))
		{
			Job.Key.Reset();
			Job.Stage = EStage::Idle;
			Job.Probe.Reset();
		}
		switch (Job.Stage)
		{
		case EStage::Idle:
			while (!Queue.IsEmpty() && Job.Stage == EStage::Idle)
			{
				const FString Key = Queue[0];
				Queue.RemoveAt(0);
				if (const FWWOPartPicture* Pic = Cache.Find(Key))
				{
					SetUp(s, *Pic);
					Job.Key = Key;
					Job.Stage = EStage::Settle;
					Job.Frames = 0;
				}
			}
			break;
		case EStage::Settle:
			// The probe: the cut-out framed on the bounds, copied back without waiting for it.
			if (++Job.Frames <= SettleFrames) break;
			CutCaptures[s]->CaptureScene();
			if (FTextureRenderTargetResource* Res = Probes[s]->GameThread_GetRenderTargetResource())
			{
				Job.Probe = MakeShared<FWWOPartProbe, ESPMode::ThreadSafe>();
				TSharedPtr<FWWOPartProbe, ESPMode::ThreadSafe> Probe = Job.Probe;
				ENQUEUE_RENDER_COMMAND(WWOPartProbeCopy)([Probe, Res](FRHICommandListImmediate& RHICmdList)
				{
					Probe->Readback.EnqueueCopy(RHICmdList, Res->GetRenderTargetTexture());
				});
			}
			Job.Stage = EStage::Probe;
			Job.Frames = 0;
			break;
		case EStage::Probe:
		{
			// Polled on the render thread (where the readback lives) until it's ready, measured there.
			TSharedPtr<FWWOPartProbe, ESPMode::ThreadSafe> Probe = Job.Probe;
			const bool bDone = Probe && Probe->bDone.load();
			if (bDone || !Probe || ++Job.Frames > ProbeWait)
			{
				if (bDone && Probe->Rect.bIsValid) Reframe(s, Probe->Rect); // else: the bounds' framing stands
				if (const FWWOPartPicture* Pic = Cache.Find(Job.Key))
				{
					CutCaptures[s]->TextureTarget = Pic->Cut;
					ColorCaptures[s]->TextureTarget = Pic->Color;
				}
				Job.Stage = EStage::Shoot;
				Job.Frames = 0;
				break;
			}
			ENQUEUE_RENDER_COMMAND(WWOPartProbeRead)([Probe](FRHICommandListImmediate&)
			{
				if (Probe->bDone.load() || !Probe->Readback.IsReady()) return;
				int32 Pitch = 0, Rows = 0;
				const FFloat16Color* Px = static_cast<const FFloat16Color*>(Probe->Readback.Lock(Pitch, &Rows));
				if (Px && Pitch >= ProbePixels.X)
				{
					Probe->Rect = ShownRect([Px, Pitch](int32 X, int32 Y) { return Px[Y * Pitch + X].A.GetFloat() < 0.5f; }, ProbePixels);
				}
				Probe->Readback.Unlock();
				Probe->bDone.store(true);
			});
			break;
		}
		case EStage::Shoot:
			CutCaptures[s]->CaptureScene();
			ColorCaptures[s]->CaptureScene();
			if (++Job.Frames >= Shots) Finish(s);
			break;
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
		for (const FStation& Job : Jobs) bBusy |= Job.Key == It.Value;
		if (!bBusy) Cache.Remove(It.Value);
	}
}
