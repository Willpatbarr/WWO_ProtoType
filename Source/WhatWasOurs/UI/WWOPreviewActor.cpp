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
	// (Now nearer, 2.75 m and 0.98 m up, so the character fills the menu's tall middle column.)
	Capture->SetRelativeLocationAndRotation(FVector(275.f, 0.f, 98.f), FRotator(0.f, 180.f, 0.f));
	Capture->FOVAngle = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(22.5f)) * Size.X / Size.Y));
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = true;
	Capture->PrimaryComponentTick.bTickEvenWhenPaused = true;
	// Godot's preview has its own world and a transparent background (the panel shows through):
	// no sky or fog here, and a flat backdrop in the panel's color behind the character.
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetGrain(false);
	// Fixed exposure, so the backdrop comes out the same shade every time (auto exposure would
	// brighten it toward mid-gray).
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = ExposureBias;
	// Nothing that shades the flat backdrop unevenly: it has to match the page around it.
	Capture->PostProcessSettings.bOverride_VignetteIntensity = true;
	Capture->PostProcessSettings.VignetteIntensity = 0.f;
	Capture->PostProcessSettings.bOverride_BloomIntensity = true;
	Capture->PostProcessSettings.BloomIntensity = 0.f;
	Capture->PostProcessBlendWeight = 1.f;
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(GetRootComponent());
	Backdrop->SetRelativeLocationAndRotation(FVector(-400.f, 0.f, 100.f), FRotator(90.f, 0.f, 0.f));
	Backdrop->SetRelativeScale3D(FVector(30.f));
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->CastShadow = false;
	Body = CreateDefaultSubobject<UWWOBodyComponent>(TEXT("Body"));
	Body->BodyColor = FLinearColor(0.3f, 0.42f, 0.55f);
	Mesh->LightingChannels.bChannel0 = false; // the grafts copy the body's channels
	Mesh->LightingChannels.bChannel2 = true;
	// Godot's light: rotation (-35, 30, 0) degrees, seen from the preview camera (Godot +Z, toward
	// the camera, is +X here; Godot +X, the camera's right, is -Y).
	auto MakeLight = [this](const TCHAR* Name, const FRotator& Rotation, float Lux)
	{
		UDirectionalLightComponent* Light = CreateDefaultSubobject<UDirectionalLightComponent>(Name);
		Light->SetupAttachment(GetRootComponent());
		Light->SetRelativeRotation(Rotation);
		Light->Intensity = Lux;
		Light->CastShadows = false;
		Light->bAtmosphereSunLight = false;
		Light->LightingChannels.bChannel0 = false;
		Light->LightingChannels.bChannel2 = true;
		return Light;
	};
	KeyLight = MakeLight(TEXT("KeyLight"), FRotator(-35.f, 150.f, 0.f), UE_PI);
	FillLight = MakeLight(TEXT("FillLight"), FRotator(-10.f, 180.f, 0.f), UE_PI * 0.5f);
}

void AWWOPreviewActor::BeginPlay()
{
	Body->MeshOverride = Mesh;
	Super::BeginPlay();
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor(0.07f, 0.08f, 0.1f, 1.f);
	Target->InitAutoFormat(FMath::RoundToInt(Size.X * 1.2f), FMath::RoundToInt(Size.Y * 1.2f)); // 1080p pixels
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		Backdrop->SetStaticMesh(Plane);
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_Unlit")))
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(M, this);
			Mat->SetVectorParameterValue("Color", BackdropFor(ExposureBias));
			Backdrop->SetMaterial(0, Mat);
		}
	}
	Capture->TextureTarget = Target;
	Capture->ShowOnlyActors.Add(this);
	Body->FollowLoadout(false); // flora grafts follow the loadout
}
