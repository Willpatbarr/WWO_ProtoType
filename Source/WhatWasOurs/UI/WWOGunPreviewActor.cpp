#include "UI/WWOGunPreviewActor.h"
#include "UI/WWOPreviewActor.h"
#include "Player/WWOWeaponPrivate.h"
#include "Data/WWOData.h"
#include "FX/WWOFX.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"

AWWOGunPreviewActor::AWWOGunPreviewActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true; // the menu pauses the game
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Turntable = CreateDefaultSubobject<USceneComponent>(TEXT("Turntable"));
	Turntable->SetupAttachment(GetRootComponent());
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Turntable);

	// The camera looks at the root from the gun's right side (+Y), a little above: the muzzle (+X)
	// points to the right of the picture. Frame() sets how far away it is.
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(GetRootComponent());
	Capture->SetRelativeRotation(FRotator(CameraPitch, -90.f, 0.f));
	Capture->FOVAngle = 30.f; // long lens: little perspective, like a product shot
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->PrimaryComponentTick.bTickEvenWhenPaused = true;
	// The same capture settings as the character preview (AWWOPreviewActor), so its backdrop comes out
	// in the page's shade: no sky, fog or grain, fixed exposure, no vignette or bloom.
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetGrain(false);
	Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
	Capture->PostProcessSettings.AutoExposureBias = AWWOPreviewActor::ExposureBias;
	Capture->PostProcessSettings.bOverride_VignetteIntensity = true;
	Capture->PostProcessSettings.VignetteIntensity = 0.f;
	Capture->PostProcessSettings.bOverride_BloomIntensity = true;
	Capture->PostProcessSettings.BloomIntensity = 0.f;
	Capture->PostProcessBlendWeight = 1.f;

	// A flat backdrop that rides with the camera, far behind the gun.
	Backdrop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Backdrop"));
	Backdrop->SetupAttachment(Capture);
	Backdrop->SetRelativeLocationAndRotation(FVector(900.f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f));
	Backdrop->SetRelativeScale3D(FVector(20.f));
	Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Backdrop->CastShadow = false;

	// Three lights on channel 1 only. Direction = where the light travels (the light's forward).
	auto MakeLight = [this](const TCHAR* Name, const FRotator& Rotation, float Lux, const FLinearColor& Color)
	{
		UDirectionalLightComponent* Light = CreateDefaultSubobject<UDirectionalLightComponent>(Name);
		Light->SetupAttachment(GetRootComponent());
		Light->SetRelativeRotation(Rotation);
		Light->Intensity = Lux;
		Light->LightColor = Color.ToFColor(true);
		Light->CastShadows = false;
		Light->bAtmosphereSunLight = false;
		Light->LightingChannels.bChannel0 = false;
		Light->LightingChannels.bChannel1 = true;
		return Light;
	};
	KeyLight = MakeLight(TEXT("KeyLight"), FRotator(-28.f, -55.f, 0.f), 1.7f, FLinearColor(1.f, 0.96f, 0.9f));    // from above the camera's left
	RimLight = MakeLight(TEXT("RimLight"), FRotator(-24.f, 118.f, 0.f), 4.5f, FLinearColor(0.8f, 0.88f, 1.f));    // from behind and above: a bright edge
	FillLight = MakeLight(TEXT("FillLight"), FRotator(-6.f, -115.f, 0.f), 0.6f, FLinearColor(0.75f, 0.82f, 1.f)); // low from the camera's right
}

void AWWOGunPreviewActor::BeginPlay()
{
	Super::BeginPlay();
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor(0.07f, 0.08f, 0.1f, 1.f);
	Target->InitAutoFormat(FMath::RoundToInt(Size.X * 1.2f * 2.f), FMath::RoundToInt(Size.Y * 1.2f * 2.f));
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		Backdrop->SetStaticMesh(Plane);
		if (UMaterialInterface* M = WWOFX::Material(TEXT("M_WWO_Unlit")))
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(M, this);
			Mat->SetVectorParameterValue("Color", AWWOPreviewActor::BackdropFor(AWWOPreviewActor::ExposureBias));
			Backdrop->SetMaterial(0, Mat);
		}
	}
	Capture->TextureTarget = Target;
	Capture->ShowOnlyActors.Add(this);
	Turntable->SetRelativeRotation(FRotator(0.f, BaseYaw, 0.f));
	SetLive(false);
}

void AWWOGunPreviewActor::ShowGun(const UWWOWeaponData* Weapon, const UWWOLoadoutSubsystem* Loadout)
{
	for (UStaticMeshComponent* Part : Parts) if (Part) Part->DestroyComponent();
	Parts.Reset();
	Shown = Weapon ? Weapon->Id : NAME_None;
	if (!Weapon) return;
	TArray<UStaticMeshComponent*> Built;
	WWOWeapon::BuildShowcase(this, Pivot, Weapon, Loadout, Built);
	for (UStaticMeshComponent* Part : Built)
	{
		Part->LightingChannels.bChannel0 = false;
		Part->LightingChannels.bChannel1 = true;
		Part->MarkRenderStateDirty();
		Part->SetCastShadow(false);
		Parts.Add(Part);
	}
	Frame();
	if (bPicture)
	{
		PictureFrames = 12;
		SetLights(true);
		SetActorTickEnabled(true);
	}
	else if (Capture->bCaptureEveryFrame) Capture->CaptureScene();
}

void AWWOGunPreviewActor::Frame()
{
	// Measured unturned, from each part's mesh bounds and its place under the pivot (the pivot at zero,
	// so that is the turntable's space): the model's box, its middle moved onto the turntable's axis.
	FBox Box(ForceInit);
	for (UStaticMeshComponent* Part : Parts)
	{
		if (!Part || !Part->GetStaticMesh()) continue;
		Box += Part->GetStaticMesh()->GetBoundingBox().TransformBy(Part->GetRelativeTransform());
	}
	if (!Box.IsValid) return;
	const FVector Half = Box.GetExtent();
	Pivot->SetRelativeLocation(-Box.GetCenter());
	// Far enough that the gun's length and its height both fit with a margin all round. The camera
	// looks across the gun, so its length shows most when the sway turns it least toward the camera;
	// the end that comes nearer then shows bigger by the depth it gains.
	const float Aspect = Shape.X / Shape.Y;
	const float TanH = FMath::Tan(FMath::DegreesToRadians(Capture->FOVAngle * 0.5f));
	const float TanV = TanH / Aspect;
	const float Least = FMath::DegreesToRadians(FMath::Max(FMath::Abs(BaseYaw) - SwayYaw, 0.f));
	const float Across = Half.X * FMath::Cos(Least) + Half.Y * FMath::Sin(Least);
	const float Nearer = Half.X * FMath::Sin(FMath::DegreesToRadians(FMath::Abs(BaseYaw) + SwayYaw)); // how much nearer the near end gets at the most turn
	const float Tall = Half.Z + Half.X * FMath::Sin(FMath::DegreesToRadians(-Pitch)) * 0.5f;
	const float Distance = FMath::Max(Across * 1.02f / TanH, Tall * 1.3f / TanV) + Nearer;
	const FVector Forward = Capture->GetRelativeRotation().Vector();
	Capture->SetRelativeLocation(-Forward * Distance);
}

void AWWOGunPreviewActor::SetLive(bool bLive)
{
	Capture->bCaptureEveryFrame = bLive;
	Capture->SetVisibility(bLive);
	SetActorTickEnabled(bLive);
	SetLights(bLive);
}

void AWWOGunPreviewActor::SetLights(bool bOn)
{
	for (UDirectionalLightComponent* Light : { KeyLight.Get(), RimLight.Get(), FillLight.Get() }) if (Light) Light->SetVisibility(bOn);
}

void AWWOGunPreviewActor::MakePicture(const FVector2D& InShape)
{
	bPicture = true;
	Shape = InShape;
	BaseYaw = 0.f;
	SwayYaw = 0.f;
	Pitch = 0.f;
	Turntable->SetRelativeRotation(FRotator::ZeroRotator);
	Capture->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // square on to the gun's right side
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR; // alpha: 0 where the gun is
	Capture->bCaptureEveryFrame = false;
	Capture->SetVisibility(true);
	Backdrop->SetVisibility(false);
	Target = NewObject<UTextureRenderTarget2D>(this);
	Target->RenderTargetFormat = RTF_RGBA16f;
	Target->ClearColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
	Target->InitAutoFormat(FMath::RoundToInt(Shape.X * 3.f), FMath::RoundToInt(Shape.Y * 3.f)); // drawn smaller: clean edges
	Capture->TextureTarget = Target;
	SetLights(false);
	SetActorTickEnabled(false);
}

void AWWOGunPreviewActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPicture)
	{
		if (PictureFrames > 0)
		{
			Capture->CaptureScene();
			if (--PictureFrames == 0)
			{
				SetLights(false);
				SetActorTickEnabled(false);
			}
		}
		return;
	}
	// A slow sway about the three-quarter view (real time: the menu pauses the game).
	Clock += FApp::GetDeltaTime();
	Turntable->SetRelativeRotation(FRotator(0.f, BaseYaw + SwayYaw * FMath::Sin(Clock * SwayRate), 0.f));
}
