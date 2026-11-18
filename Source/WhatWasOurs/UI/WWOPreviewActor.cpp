#include "UI/WWOPreviewActor.h"
#include "Characters/WWOBodyComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "FX/WWOFX.h"

AWWOPreviewActor::AWWOPreviewActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true; // keeps turning while the menu has the game paused
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Turntable = CreateDefaultSubobject<USceneComponent>(TEXT("Turntable"));
	Turntable->SetupAttachment(GetRootComponent());
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Turntable);
	Mesh->SetRelativeRotation(FRotator(0, -90.f, 0));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->PrimaryComponentTick.bTickEvenWhenPaused = true;
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(GetRootComponent());
	// Godot's preview camera: 4.4 m in front, 1.1 m above the feet, level, 45 degrees tall (Godot
	// FOVs are vertical; Unreal's are horizontal, so convert for the preview's shape).
	// (Now nearer, CameraAt, so the character fills the menu's tall middle column.)
	Capture->SetRelativeLocationAndRotation(CameraAt, FRotator(0.f, 180.f, 0.f));
	Capture->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(22.5f)) * Size.X / Size.Y));
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR; // tone mapped (filmic), like the game's view
	Capture->bCaptureEveryFrame = true;
	Capture->bAlwaysPersistRenderingState = true; // keeps temporal AA history between frames: clean edges on the turntable
	Capture->PrimaryComponentTick.bTickEvenWhenPaused = true;
	// Its own studio, nothing from the level: no sky, fog, sky light or level reflections (those
	// differ per map and are calibrated for the arenas' exposure); only the studio lights below.
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
	// Fixed exposure, so the backdrop comes out the same shade every time (auto exposure would
	// brighten it toward mid-gray) and glow keeps its colour.
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = CharacterExposure;
	// No vignette (the backdrop's edge must meet the page); a soft bloom so glows read as light.
	PP.bOverride_VignetteIntensity = true;
	PP.VignetteIntensity = 0.f;
	PP.bOverride_BloomIntensity = true;
	PP.BloomIntensity = 0.45f;
	PP.bOverride_BloomThreshold = true;
	PP.BloomThreshold = 0.6f;
	PP.bOverride_SceneFringeIntensity = true;
	PP.SceneFringeIntensity = 0.f;
	Capture->PostProcessBlendWeight = 1.f;
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(GetRootComponent());
	Backdrop->SetRelativeLocationAndRotation(FVector(-400.f, 0.f, 100.f), FRotator(90.f, 0.f, 0.f));
	Backdrop->SetRelativeScale3D(FVector(30.f));
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->CastShadow = false;
	Body = CreateDefaultSubobject<UWWOBodyComponent>(TEXT("Body"));
	Body->BodyColor = FLinearColor(0.3f, 0.42f, 0.55f);
	Body->bFullDetailGrafts = true; // seen close, through a capture: always in full
	Mesh->LightingChannels.bChannel0 = false; // the grafts copy the body's channels
	Mesh->LightingChannels.bChannel2 = true;
	// The lights travel along their forward vector. The camera looks down -X from +X; its left is +Y.
	auto MakeLight = [this](const TCHAR* Name, float Pitch, float Yaw, float Lux, const FLinearColor& Color)
	{
		UDirectionalLightComponent* Light = CreateDefaultSubobject<UDirectionalLightComponent>(Name);
		Light->SetupAttachment(GetRootComponent());
		Light->SetRelativeRotation(FRotator(Pitch, Yaw, 0.f));
		Light->Intensity = Lux;
		Light->LightColor = Color.ToFColor(true);
		Light->CastShadows = false;
		Light->bAtmosphereSunLight = false;
		Light->bAffectsWorld = true;
		Light->LightingChannels.bChannel0 = false;
		Light->LightingChannels.bChannel2 = true;
		return Light;
	};
	KeyLight = MakeLight(TEXT("KeyLight"), -26.f, -145.f, 6.5f, FLinearColor(1.f, 0.97f, 0.93f));        // front, upper left
	FillLight = MakeLight(TEXT("FillLight"), -8.f, 145.f, 2.4f, FLinearColor(0.82f, 0.88f, 1.f));        // front right, low, cool
	RimLeft = MakeLight(TEXT("RimLeft"), -28.f, -35.f, 5.5f, FLinearColor(0.9f, 0.94f, 1.f));             // behind, left
	RimRight = MakeLight(TEXT("RimRight"), -22.f, 38.f, 6.5f, FLinearColor(0.85f, 0.9f, 1.f));           // behind, right
	TopLight = MakeLight(TEXT("TopLight"), -82.f, 180.f, 0.3f, FLinearColor(1.f, 1.f, 1.f));             // overhead (faint: the feet's flat tops caught it most)
	BounceLight = MakeLight(TEXT("BounceLight"), 55.f, 180.f, 0.5f, FLinearColor(1.f, 0.95f, 0.9f));     // from the floor
	// Only the key casts shadows: soft (a wide source), with short contact shadows for the creases.
	KeyLight->CastShadows = true;
	KeyLight->LightSourceAngle = 4.f;
	KeyLight->ContactShadowLength = 0.04f;
	FloorShadow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorShadow"));
	FloorShadow->SetupAttachment(GetRootComponent());
	FloorShadow->SetRelativeLocation(FVector(0.f, 0.f, 0.5f));
	FloorShadow->SetRelativeScale3D(FVector(1.3f, 1.5f, 1.f)); // the plane is 1 m: a 1.3 by 1.5 m pool
	FloorShadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FloorShadow->CastShadow = false;
}

void AWWOPreviewActor::BeginPlay()
{
	Body->MeshOverride = Mesh;
	Super::BeginPlay();
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor(0.07f, 0.08f, 0.1f, 1.f);
	Target->InitAutoFormat(FMath::RoundToInt(Size.X * 1.2f * Supersample), FMath::RoundToInt(Size.Y * 1.2f * Supersample)); // 1080p pixels, supersampled
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		Backdrop->SetStaticMesh(Plane);
		// The page's shade at the edges, a soft pool of light behind the character.
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_PreviewBackdrop")))
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(M, this);
			Mat->SetVectorParameterValue("Color", BackdropFor(CharacterExposure) * BackdropTrim); // its edges = the page
			Mat->SetScalarParameterValue("Aspect", Size.X / Size.Y);
			Mat->SetScalarParameterValue("Glow", 1.3f);
			Mat->SetScalarParameterValue("Radius", 0.58f);
			Backdrop->SetMaterial(0, Mat);
		}
		FloorShadow->SetStaticMesh(Plane);
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_PreviewShadow")))
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(M, this);
			Mat->SetScalarParameterValue("Alpha", 0.85f);
			Mat->SetScalarParameterValue("Falloff", 1.3f);
			FloorShadow->SetMaterial(0, Mat);
		}
	}
	Capture->TextureTarget = Target;
	Capture->ShowOnlyActors.Add(this);
	Body->FollowLoadout(false); // flora grafts follow the loadout
	Body->Play(IdleAnim, 1.f, 0.f); // stands idling (the game's own idle loop), whatever the flora
}

void AWWOPreviewActor::SetLive(bool bLive)
{
	Capture->bCaptureEveryFrame = bLive;
	Capture->SetVisibility(bLive);
	for (UDirectionalLightComponent* Light : { KeyLight.Get(), FillLight.Get(), RimLeft.Get(), RimRight.Get(), TopLight.Get(), BounceLight.Get() })
	{
		if (Light) Light->SetVisibility(bLive);
	}
	SetActorTickEnabled(bLive);
}
