#include "FX/WWOFX.h"
#include "Player/WWOProf.h"
#include "WWOTypes.h"
#include "Systems/WWOTargetable.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"

namespace WWOFX
{
	template <typename T>
	T* LoadCached(const TCHAR* Path)
	{
		static TMap<FString, TWeakObjectPtr<T>> Cache;
		TWeakObjectPtr<T>& Slot = Cache.FindOrAdd(Path);
		if (!Slot.IsValid())
		{
			Slot = LoadObject<T>(nullptr, Path);
		}
		return Slot.Get();
	}

	UStaticMesh* Sphere() { return LoadCached<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere")); }
	UStaticMesh* Cube() { return LoadCached<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube")); }
	UStaticMesh* Cylinder() { return LoadCached<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); }
	UStaticMesh* Plane() { return LoadCached<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane.Plane")); }
	UStaticMesh* Cone() { return LoadCached<UStaticMesh>(TEXT("/Engine/BasicShapes/Cone.Cone")); }

	UMaterialInterface* Material(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/WWO/Materials/%s.%s"), Name, Name);
		UMaterialInterface* Mat = LoadCached<UMaterialInterface>(*Path);
		return Mat ? Mat : LoadCached<UMaterialInterface>(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	UMaterialInstanceDynamic* MID(UStaticMeshComponent* Comp)
	{
		if (!Comp) return nullptr;
		if (UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0))) return Existing;
		return Comp->CreateAndSetMaterialInstanceDynamic(0);
	}

	UStaticMeshComponent* AddShape(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const TCHAR* MaterialName,
		const FLinearColor& Color, float Glow)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetMaterial(0, Material(MaterialName));
		if (Parent)
		{
			Comp->SetupAttachment(Parent);
		}
		else if (!Owner->GetRootComponent())
		{
			Owner->SetRootComponent(Comp);
		}
		else
		{
			Comp->SetupAttachment(Owner->GetRootComponent());
		}
		Comp->RegisterComponent();
		Owner->AddInstanceComponent(Comp);
		if (UMaterialInstanceDynamic* M = MID(Comp))
		{
			M->SetVectorParameterValue("Color", Color);
			M->SetScalarParameterValue("Glow", Glow);
		}
		return Comp;
	}

	UStaticMeshComponent* AddSoftSphere(AActor* Owner, USceneComponent* Parent, const FLinearColor& Color, float Alpha,
		float Edge, bool bAdditive, bool bRim, float Core)
	{
		UStaticMeshComponent* Comp = AddShape(Owner, Parent, Sphere(), bAdditive ? TEXT("M_WWO_SoftAdd") : TEXT("M_WWO_SoftAlpha"), Color);
		Comp->SetCastShadow(false);
		if (UMaterialInstanceDynamic* M = MID(Comp))
		{
			M->SetScalarParameterValue("Alpha", Alpha);
			M->SetScalarParameterValue("Edge", Edge);
			M->SetScalarParameterValue("Rim", bRim ? 1.f : 0.f);
			M->SetScalarParameterValue("Core", Core);
		}
		return Comp;
	}

	UStaticMeshComponent* AddGroundDisc(AActor* Owner, USceneComponent* Parent, const FLinearColor& Color, float RingWidth, float Fill, float Ripple)
	{
		UStaticMeshComponent* Comp = AddShape(Owner, Parent, Plane(), TEXT("M_WWO_Ground"), Color);
		Comp->SetCastShadow(false);
		if (UMaterialInstanceDynamic* M = MID(Comp))
		{
			M->SetScalarParameterValue("Alpha", 1.f);
			M->SetScalarParameterValue("RingWidth", RingWidth);
			M->SetScalarParameterValue("Fill", Fill);
			M->SetScalarParameterValue("Ripple", Ripple);
		}
		return Comp;
	}

	bool GroundBelow(UWorld* World, const FVector& Point, float Reach, FVector& OutGround, const AActor* Ignore)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WWOGround), false, Ignore);
		if (!World->LineTraceSingleByChannel(Hit, Point + FVector(0, 0, 20.f), Point - FVector(0, 0, Reach), ECC_Visibility, Params))
		{
			return false;
		}
		if (Cast<IWWOTargetable>(Hit.GetActor())) return false;
		OutGround = Hit.ImpactPoint;
		return true;
	}
}

// --- AWWOFXActor ---------------------------------------------------------------------------

AWWOFXActor::AWWOFXActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AWWOFXActor* AWWOFXActor::Spawn(UWorld* World, const FVector& At, float InLifetime)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWWOFXActor* FX = World->SpawnActor<AWWOFXActor>(At, FRotator::ZeroRotator, Params);
	if (FX) FX->Lifetime = InLifetime;
	return FX;
}

void AWWOFXActor::ScaleTween(USceneComponent* Comp, const FVector& From, const FVector& To, float Duration, EWWOEase Ease, float Delay)
{
	FWWOTween T;
	T.Comp = Comp;
	T.bScale = true;
	T.ScaleFrom = From;
	T.ScaleTo = To;
	T.Duration = FMath::Max(Duration, 0.001f);
	T.Ease = Ease;
	T.Delay = Delay;
	Comp->SetRelativeScale3D(From);
	Tweens.Add(T);
}

void AWWOFXActor::MoveTween(USceneComponent* Comp, const FVector& From, const FVector& To, float Duration, EWWOEase Ease, float Delay)
{
	FWWOTween T;
	T.Comp = Comp;
	T.bMove = true;
	T.ScaleFrom = From;
	T.ScaleTo = To;
	T.Duration = FMath::Max(Duration, 0.001f);
	T.Ease = Ease;
	T.Delay = Delay;
	Comp->SetRelativeLocation(From);
	Tweens.Add(T);
}

void AWWOFXActor::ParamTween(UMaterialInstanceDynamic* Mat, FName Param, float From, float To, float Duration, EWWOEase Ease, float Delay)
{
	if (!Mat) return;
	FWWOTween T;
	T.Material = Mat;
	T.Param = Param;
	T.From = From;
	T.To = To;
	T.Duration = FMath::Max(Duration, 0.001f);
	T.Ease = Ease;
	T.Delay = Delay;
	Mat->SetScalarParameterValue(Param, From);
	Tweens.Add(T);
}

void AWWOFXActor::LightTween(UPointLightComponent* Light, float From, float To, float Duration, EWWOEase Ease)
{
	FWWOTween T;
	T.Comp = Light;
	T.bLight = true;
	T.From = From;
	T.To = To;
	T.Duration = FMath::Max(Duration, 0.001f);
	T.Ease = Ease;
	Light->SetIntensity(From);
	Tweens.Add(T);
}

UPointLightComponent* AWWOFXActor::AddLight(const FLinearColor& Color, float Intensity, float RadiusCm)
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
	Light->SetupAttachment(GetRootComponent());
	Light->SetLightColor(Color);
	Light->SetIntensityUnits(ELightUnits::Unitless);
	Light->SetIntensity(Intensity);
	Light->SetAttenuationRadius(RadiusCm);
	Light->SetCastShadows(false);
	Light->RegisterComponent();
	AddInstanceComponent(Light);
	return Light;
}

float AWWOFXActor::EaseValue(EWWOEase Ease, float T)
{
	T = FMath::Clamp(T, 0.f, 1.f);
	switch (Ease)
	{
	case EWWOEase::QuadOut: return 1.f - (1.f - T) * (1.f - T);
	case EWWOEase::QuadIn: return T * T;
	case EWWOEase::ExpoOut: return T >= 1.f ? 1.f : 1.f - FMath::Pow(2.f, -10.f * T);
	case EWWOEase::BackOut: { const float C1 = 1.70158f, C3 = C1 + 1.f; return 1.f + C3 * FMath::Pow(T - 1.f, 3.f) + C1 * FMath::Pow(T - 1.f, 2.f); }
	case EWWOEase::SineInOut: return -(FMath::Cos(UE_PI * T) - 1.f) * 0.5f;
	default: return T;
	}
}

void AWWOFXActor::Tick(float DeltaSeconds)
{
	WWO_PROF("FXActor");
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	for (const FWWOTween& T : Tweens)
	{
		const float Raw = (Age - T.Delay) / T.Duration;
		if (Raw < 0.f) continue;
		const float E = EaseValue(T.Ease, Raw);
		if (T.bMove && T.Comp.IsValid())
		{
			T.Comp->SetRelativeLocation(FMath::Lerp(T.ScaleFrom, T.ScaleTo, FMath::Clamp(E, 0.f, 1.f)));
		}
		else if (T.bScale && T.Comp.IsValid())
		{
			T.Comp->SetRelativeScale3D(FMath::Lerp(T.ScaleFrom, T.ScaleTo, E));
		}
		else if (T.bLight && T.Comp.IsValid())
		{
			Cast<UPointLightComponent>(T.Comp.Get())->SetIntensity(FMath::Lerp(T.From, T.To, E));
		}
		else if (T.Material.IsValid())
		{
			T.Material->SetScalarParameterValue(T.Param, FMath::Lerp(T.From, T.To, E));
		}
	}
	if (Age >= Lifetime) Destroy();
}

void AWWOFXActor::Shockwave(UWorld* World, const FVector& At, const FLinearColor& Color, float RadiusM, float Seconds)
{
	// A ring along the ground that spreads out and fades.
	AWWOFXActor* FX = Spawn(World, At + FVector(0, 0, 4.f), Seconds);
	if (!FX) return;
	UStaticMeshComponent* Ring = WWOFX::AddGroundDisc(FX, FX->GetRootComponent(), Color, 0.18f);
	const float Full = RadiusM * WWO::M / 50.f; // the plane is 100 cm across
	FX->ScaleTween(Ring, FVector(Full * 0.15f, Full * 0.15f, 1.f), FVector(Full, Full, 1.f), Seconds, EWWOEase::ExpoOut);
	FX->ParamTween(WWOFX::MID(Ring), "Alpha", 1.f, 0.f, Seconds, EWWOEase::QuadIn);
}

void AWWOFXActor::Pop(UWorld* World, const FVector& At, const FLinearColor& Color, float RadiusM, float Seconds)
{
	AWWOFXActor* FX = Spawn(World, At, Seconds);
	if (!FX) return;
	UStaticMeshComponent* Ball = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), Color, 1.f, 0.8f);
	const float S = RadiusM * WWO::M / 50.f;
	FX->ScaleTween(Ball, FVector(S), FVector(S), Seconds);
	FX->ParamTween(WWOFX::MID(Ball), "Alpha", 1.f, 0.f, Seconds);
}

void AWWOFXActor::Tracer(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Width, float Seconds)
{
	const FVector Mid = (From + To) * 0.5f;
	AWWOFXActor* FX = Spawn(World, Mid, Seconds);
	if (!FX) return;
	UStaticMeshComponent* Line = WWOFX::AddShape(FX, FX->GetRootComponent(), WWOFX::Cube(), TEXT("M_WWO_Unlit"), Color);
	Line->SetCastShadow(false);
	const float Length = FVector::Dist(From, To);
	Line->SetWorldRotation((To - From).Rotation());
	FX->ScaleTween(Line, FVector(Length / 100.f, Width / 100.f, Width / 100.f), FVector(Length / 100.f, Width / 100.f, Width / 100.f), Seconds);
	FX->ParamTween(WWOFX::MID(Line), "Alpha", Color.A, 0.f, Seconds);
}

void AWWOFXActor::BulletTrail(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Size)
{
	constexpr float Speed = 30000.f;   // cm/s: fast, but a few frames in the air so the eye catches it
	constexpr float StreakMax = 450.f; // cm
	constexpr float Linger = 0.2f;     // seconds the wake takes to fade once the streak has landed
	const float Length = FVector::Dist(From, To);
	if (Length < 40.f) return;
	const float Streak = FMath::Min(StreakMax, Length * 0.6f);
	const float Travel = FMath::Max((Length - Streak) / Speed, 0.02f);
	AWWOFXActor* FX = Spawn(World, From, Travel + Linger);
	if (!FX) return;
	FX->SetActorRotation((To - From).Rotation()); // x runs along the shot
	USceneComponent* Root = FX->GetRootComponent();
	const FVector Start(Streak * 0.5f, 0, 0), End(Length - Streak * 0.5f, 0, 0);

	// The streak: a white-hot core inside a soft colored glow, gone when it lands.
	const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.7f);
	UStaticMeshComponent* Core = WWOFX::AddShape(FX, Root, WWOFX::Cube(), TEXT("M_WWO_Unlit"), Hot);
	Core->SetCastShadow(false);
	Core->SetRelativeScale3D(FVector(Streak, 1.4f * Size, 1.4f * Size) / 100.f);
	FX->MoveTween(Core, Start, End, Travel);
	FX->ParamTween(WWOFX::MID(Core), "Alpha", 1.f, 0.f, 0.03f, EWWOEase::Linear, Travel);
	UStaticMeshComponent* Glow = WWOFX::AddSoftSphere(FX, Root, Color, 0.9f, 1.2f, true, false, 0.6f);
	Glow->SetRelativeScale3D(FVector(Streak * 1.2f, 9.f * Size, 9.f * Size) / 100.f);
	FX->MoveTween(Glow, Start, End, Travel);
	FX->ParamTween(WWOFX::MID(Glow), "Alpha", 0.9f, 0.f, 0.03f, EWWOEase::Linear, Travel);

	// The wake: a thin line drawn out behind the streak, fading after it lands.
	FLinearColor Faint = Color;
	Faint.A = 0.45f;
	UStaticMeshComponent* Wake = WWOFX::AddShape(FX, Root, WWOFX::Cube(), TEXT("M_WWO_Unlit"), Faint);
	Wake->SetCastShadow(false);
	const float Thin = 1.2f * Size / 100.f;
	FX->ScaleTween(Wake, FVector(0.01f, Thin, Thin), FVector(Length / 100.f, Thin, Thin), Travel);
	FX->MoveTween(Wake, FVector::ZeroVector, FVector(Length * 0.5f, 0, 0), Travel);
	FX->ParamTween(WWOFX::MID(Wake), "Alpha", 0.45f, 0.f, Linger, EWWOEase::QuadOut, Travel * 0.5f);
}

void AWWOFXActor::Scorch(UWorld* World, const FVector& Ground, float RadiusM, float Seconds)
{
	AWWOFXActor* FX = Spawn(World, Ground + FVector(0, 0, 2.f), Seconds);
	if (!FX) return;
	UStaticMeshComponent* Mark = WWOFX::AddGroundDisc(FX, FX->GetRootComponent(), FLinearColor(0.02f, 0.02f, 0.025f), 0.35f, 0.75f, 0.f);
	const float S = RadiusM * WWO::M / 50.f; // the plane is 100 cm across
	Mark->SetRelativeScale3D(FVector(S, S, 1.f));
	Mark->SetRelativeRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
	FX->ParamTween(WWOFX::MID(Mark), "Alpha", 0.7f, 0.f, Seconds, EWWOEase::QuadIn);
}

void AWWOFXActor::MuzzleFlash(UWorld* World, const FVector& At, const FVector& Dir, const FLinearColor& Color, float Size)
{
	constexpr float Seconds = 0.07f;
	AWWOFXActor* FX = Spawn(World, At, Seconds);
	if (!FX) return;
	// x along the barrel, rolled differently every shot so no two flashes match.
	FRotator Rot = Dir.Rotation();
	Rot.Roll = FMath::FRandRange(0.f, 360.f);
	FX->SetActorRotation(Rot);
	USceneComponent* Root = FX->GetRootComponent();
	const float S = Size * FMath::FRandRange(0.85f, 1.2f) / 100.f;
	auto Flare = [&](const FVector& ScaleCm, const FVector& PosCm, const FLinearColor& C, float Alpha, float Core)
	{
		UStaticMeshComponent* Ball = WWOFX::AddSoftSphere(FX, Root, C, Alpha, 1.3f, true, false, Core);
		Ball->SetRelativeLocation(PosCm * S * 100.f);
		FX->ScaleTween(Ball, ScaleCm * S * 0.6f, ScaleCm * S, Seconds, EWWOEase::ExpoOut);
		FX->ParamTween(WWOFX::MID(Ball), "Alpha", Alpha, 0.f, Seconds, EWWOEase::QuadIn);
	};
	const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.6f);
	Flare(FVector(16.f, 16.f, 16.f), FVector(4.f, 0, 0), Hot, 1.f, 1.f);       // core
	Flare(FVector(46.f, 10.f, 10.f), FVector(22.f, 0, 0), Color, 0.9f, 0.5f);  // tongue, out of the barrel
	Flare(FVector(7.f, 40.f, 7.f), FVector(5.f, 0, 0), Color, 0.7f, 0.3f);     // cross
	Flare(FVector(7.f, 7.f, 32.f), FVector(5.f, 0, 0), Color, 0.7f, 0.3f);
	FX->LightTween(FX->AddLight(Color, 9000.f * Size, 450.f), 9000.f * Size, 0.f, Seconds, EWWOEase::QuadIn);
}

void AWWOFXActor::HitSpark(UWorld* World, const FVector& At, const FVector& Away, bool bCritical)
{
	static const FLinearColor Plain(1.f, 0.72f, 0.35f), Gold(1.f, 0.85f, 0.2f);
	const FLinearColor Color = bCritical ? Gold : Plain;
	const float Big = bCritical ? 1.7f : 1.f;
	const FVector Out = At + Away * 4.f; // just off the surface, so the body doesn't hide it
	if (AWWOFXActor* FX = Spawn(World, Out, 0.12f))
	{
		UStaticMeshComponent* Ball = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), FMath::Lerp(Color, FLinearColor::White, 0.6f), 1.f, 1.3f, true, false, 1.f);
		const float S = 0.22f * Big;
		FX->ScaleTween(Ball, FVector(S * 0.4f), FVector(S), 0.12f, EWWOEase::ExpoOut);
		FX->ParamTween(WWOFX::MID(Ball), "Alpha", 1.f, 0.f, 0.12f, EWWOEase::QuadIn);
		if (bCritical)
		{
			// A flat flare across the hit, so a headshot reads at a glance.
			UStaticMeshComponent* Streak = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), Color, 0.9f, 1.3f, true, false, 0.5f);
			Streak->SetWorldRotation(Away.Rotation());
			FX->ScaleTween(Streak, FVector(0.05f, 0.25f, 0.05f), FVector(0.05f, 0.75f, 0.05f), 0.12f, EWWOEase::ExpoOut);
			FX->ParamTween(WWOFX::MID(Streak), "Alpha", 0.9f, 0.f, 0.12f, EWWOEase::QuadIn);
		}
	}
	if (AWWOParticles* Sparks = AWWOParticles::Burst(World, Out, FMath::Lerp(Color, FLinearColor::White, 0.4f), bCritical ? 12 : 6, bCritical ? 0.4f : 0.28f,
		2.f, bCritical ? 7.f : 5.f, bCritical ? 0.045f : 0.035f, FVector(0, 0, -12.f), EWWOParticleStyle::Glow, false))
	{
		Sparks->Direction = (Away + FVector(0, 0, 0.3f)).GetSafeNormal();
		Sparks->SpreadDegrees = 55.f;
		Sparks->Start();
	}
}

void AWWOFXActor::DeathBurst(UWorld* World, const FVector& At, const FLinearColor& Color, float SizeM)
{
	const float K = SizeM / 1.8f;
	const FLinearColor Hot = FMath::Lerp(Color, FLinearColor::White, 0.5f);
	if (AWWOFXActor* FX = Spawn(World, At, 0.35f))
	{
		UStaticMeshComponent* Ball = WWOFX::AddSoftSphere(FX, FX->GetRootComponent(), Hot, 0.9f, 1.2f, true, false, 0.8f);
		FX->ScaleTween(Ball, FVector(0.5f * K), FVector(2.4f * K), 0.25f, EWWOEase::ExpoOut);
		FX->ParamTween(WWOFX::MID(Ball), "Alpha", 0.9f, 0.f, 0.25f, EWWOEase::QuadIn);
		FX->LightTween(FX->AddLight(Color, 12000.f * K, 600.f * K), 12000.f * K, 0.f, 0.3f, EWWOEase::QuadOut);
	}
	FVector Ground;
	if (WWOFX::GroundBelow(World, At, 300.f, Ground)) Shockwave(World, Ground, Color, 2.2f * K, 0.4f);
	if (AWWOParticles* Sparks = AWWOParticles::Burst(World, At, Hot, 26, 0.6f, 2.5f, 8.f, 0.05f * K, FVector(0, 0, -10.f), EWWOParticleStyle::Glow, false))
	{
		Sparks->Shape = EWWOEmitShape::Sphere;
		Sparks->ShapeRadius = 0.3f * K;
		Sparks->Start();
	}
	if (AWWOParticles* Chunks = AWWOParticles::Burst(World, At, FLinearColor(0.12f, 0.12f, 0.14f), 10, 0.9f, 2.f, 5.5f, 0.09f * K, FVector(0, 0, -14.f), EWWOParticleStyle::Chunk, false))
	{
		Chunks->Direction = FVector::UpVector;
		Chunks->SpreadDegrees = 75.f;
		Chunks->Shape = EWWOEmitShape::Sphere;
		Chunks->ShapeRadius = 0.3f * K;
		Chunks->Start();
	}
}

void AWWOFXActor::ImpactMark(UWorld* World, const FVector& At)
{
	// A small dark dot where a shot landed, gone after a couple seconds.
	AWWOFXActor* FX = Spawn(World, At, 2.f);
	if (!FX) return;
	UStaticMeshComponent* Dot = WWOFX::AddShape(FX, FX->GetRootComponent(), WWOFX::Sphere(), TEXT("M_WWO_Unlit"), FLinearColor(0.05f, 0.05f, 0.05f, 1.f));
	Dot->SetCastShadow(false);
	Dot->SetRelativeScale3D(FVector(0.06f));
	if (UMaterialInstanceDynamic* M = WWOFX::MID(Dot)) M->SetScalarParameterValue("Alpha", 1.f);
}

// --- AWWOParticles ------------------------------------------------------------------------

AWWOParticles::AWWOParticles()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Mesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Particles"));
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetUsingAbsoluteLocation(true);
	Mesh->SetUsingAbsoluteRotation(true);
	Mesh->SetUsingAbsoluteScale(true);
	Mesh->NumCustomDataFloats = 4;
}

AWWOParticles* AWWOParticles::Burst(UWorld* World, const FVector& At, const FLinearColor& InColor, int32 InAmount, float LifetimeS,
	float InSpeedMin, float InSpeedMax, float InSize, const FVector& GravityM, EWWOParticleStyle InStyle, bool bStart)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWWOParticles* P = World->SpawnActor<AWWOParticles>(At, FRotator::ZeroRotator, Params);
	if (!P) return nullptr;
	P->Color = InColor;
	P->Amount = FMath::Max(InAmount, 1);
	P->ParticleLife = LifetimeS;
	P->SpeedMin = InSpeedMin;
	P->SpeedMax = InSpeedMax;
	P->Size = InSize;
	P->Gravity = GravityM;
	P->Style = InStyle;
	if (InStyle == EWWOParticleStyle::Smoke)
	{
		P->DampingMin = 3.f;
		P->DampingMax = 6.f;
	}
	else if (InStyle == EWWOParticleStyle::Chunk)
	{
		P->DampingMin = 0.5f;
		P->DampingMax = 1.f;
	}
	if (bStart) P->Start();
	return P;
}

void AWWOParticles::Start()
{
	Mesh->SetStaticMesh(Style == EWWOParticleStyle::Chunk ? WWOFX::Cube() : WWOFX::Sphere());
	Mesh->SetMaterial(0, WWOFX::Material(Style == EWWOParticleStyle::Chunk ? TEXT("M_WWO_Chunk") : TEXT("M_WWO_Particle")));
	bEmitting = true;
	bStarted = true;
	if (bOneShot)
	{
		for (int32 i = 0; i < Amount; ++i) EmitOne();
		bEmitting = false;
	}
}

void AWWOParticles::StopAndFade()
{
	bEmitting = false;
	bOneShot = true; // destroy once empty
}

void AWWOParticles::EmitOne()
{
	FParticle P;
	FVector Offset = FVector::ZeroVector;
	switch (Shape)
	{
	case EWWOEmitShape::Sphere: Offset = FMath::VRand() * FMath::FRand() * ShapeRadius; break;
	case EWWOEmitShape::Box: Offset = FVector(FMath::FRandRange(-1.f, 1.f) * BoxExtent.X, FMath::FRandRange(-1.f, 1.f) * BoxExtent.Y, FMath::FRandRange(-1.f, 1.f) * BoxExtent.Z); break;
	case EWWOEmitShape::Ring: { const float A = FMath::FRand() * UE_TWO_PI; Offset = FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * ShapeRadius * FMath::Sqrt(FMath::FRand()); break; }
	default: break;
	}
	if (Shape == EWWOEmitShape::Box) Offset = GetActorQuat().RotateVector(Offset);
	P.Pos = GetActorLocation() + Offset * WWO::M;
	const FVector Dir = WWO::RandomInCone(Direction.GetSafeNormal(), FMath::DegreesToRadians(FMath::Min(SpreadDegrees, 179.f)));
	const FVector FinalDir = SpreadDegrees >= 180.f ? FMath::VRand() : Dir;
	P.Vel = FinalDir * FMath::FRandRange(SpeedMin, SpeedMax) * WWO::M;
	P.Age = 0.f;
	P.Life = ParticleLife;
	P.Scale = FMath::FRandRange(0.6f, 1.3f);
	P.Damping = FMath::FRandRange(DampingMin, DampingMax) * WWO::M;
	P.Spin = Style == EWWOParticleStyle::Chunk ? FMath::FRandRange(-540.f, 540.f) : 0.f;
	P.Rot = FQuat(FMath::VRand(), FMath::FRand() * UE_TWO_PI);
	Particles.Add(P);
}

void AWWOParticles::Tick(float DeltaSeconds)
{
	WWO_PROF("Particles");
	Super::Tick(DeltaSeconds);
	if (!bStarted) return;
	if (bEmitting && !bOneShot)
	{
		EmitAccumulator += DeltaSeconds * Amount / FMath::Max(ParticleLife, 0.01f);
		while (EmitAccumulator >= 1.f)
		{
			EmitOne();
			EmitAccumulator -= 1.f;
		}
	}
	for (FParticle& P : Particles)
	{
		P.Age += DeltaSeconds;
		P.Vel += Gravity * WWO::M * DeltaSeconds;
		const float Speed = P.Vel.Size();
		if (Speed > 0.f) P.Vel *= FMath::Max(Speed - P.Damping * DeltaSeconds, 0.f) / Speed;
		P.Pos += P.Vel * DeltaSeconds;
		if (P.Spin != 0.f) P.Rot = FQuat(FVector::UpVector, FMath::DegreesToRadians(P.Spin) * DeltaSeconds) * P.Rot;
	}
	Particles.RemoveAll([](const FParticle& P) { return P.Age >= P.Life; });

	Mesh->ClearInstances();
	TArray<FTransform> Transforms;
	Transforms.Reserve(Particles.Num());
	for (const FParticle& P : Particles)
	{
		const float T = P.Age / P.Life;
		// Glow shrinks to 0.2; smoke grows from 0.5 to 1.4; chunks shrink.
		const float Curve = Style == EWWOParticleStyle::Smoke ? FMath::Lerp(0.5f, 1.4f, T) : FMath::Lerp(1.f, 0.2f, T);
		const float S = Size * WWO::M * P.Scale * Curve / 100.f;
		Transforms.Add(FTransform(P.Rot, P.Pos, FVector(S)));
	}
	Mesh->AddInstances(Transforms, false, true);
	for (int32 i = 0; i < Particles.Num(); ++i)
	{
		const float T = Particles[i].Age / Particles[i].Life;
		const float A = Style == EWWOParticleStyle::Chunk ? 1.f : Color.A * (1.f - T);
		const FLinearColor C = Style == EWWOParticleStyle::Chunk ? FMath::Lerp(Color, WWO::Darkened(Color, 0.2f), T) : Color;
		const float Data[4] = { C.R, C.G, C.B, A };
		Mesh->SetCustomData(i, MakeArrayView(Data, 4), i == Particles.Num() - 1);
	}
	if (!bEmitting && Particles.Num() == 0) Destroy();
}

// --- Lightning ------------------------------------------------------------------------------

namespace
{
	constexpr int32 BoltSegments = 9;
	constexpr int32 BoltBranch = 3;
	constexpr float BoltJag = 0.12f;

	UInstancedStaticMeshComponent* MakeSegments(AActor* Owner, const TCHAR* Name, const FLinearColor& Color, float Alpha)
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Owner, Name);
		ISM->SetupAttachment(Owner->GetRootComponent());
		ISM->SetStaticMesh(WWOFX::Cube());
		ISM->SetMaterial(0, WWOFX::Material(TEXT("M_WWO_Particle")));
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(false);
		ISM->NumCustomDataFloats = 4;
		ISM->SetUsingAbsoluteLocation(true);
		ISM->SetUsingAbsoluteRotation(true);
		ISM->SetUsingAbsoluteScale(true);
		ISM->RegisterComponent();
		Owner->AddInstanceComponent(ISM);
		return ISM;
	}

	void TintSegments(UInstancedStaticMeshComponent* ISM, const FLinearColor& Color, float Alpha)
	{
		const float Data[4] = { Color.R, Color.G, Color.B, Alpha };
		for (int32 i = 0; i < ISM->GetInstanceCount(); ++i)
		{
			ISM->SetCustomData(i, MakeArrayView(Data, 4), i == ISM->GetInstanceCount() - 1);
		}
	}
}

void WWOPlaceSegment(UInstancedStaticMeshComponent* ISM, int32 Index, const FVector& A, const FVector& B, float Width, bool bVisible)
{
	const float Length = FVector::Dist(A, B);
	FTransform T;
	if (!bVisible || Length < 0.1f)
	{
		T = FTransform(FQuat::Identity, A, FVector(0.f));
	}
	else
	{
		T = FTransform((B - A).ToOrientationQuat(), (A + B) * 0.5f, FVector(Length / 100.f, Width / 100.f, Width / 100.f));
	}
	if (Index < ISM->GetInstanceCount())
	{
		ISM->UpdateInstanceTransform(Index, T, true, true, true);
	}
	else
	{
		ISM->AddInstance(T, true);
	}
}

AWWOLightningBolt::AWWOLightningBolt()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

TArray<FVector> AWWOLightningBolt::Jagged(const FVector& From, const FVector& To, int32 Segments, float Jag)
{
	TArray<FVector> Points = { From };
	const FVector Along = To - From;
	const float Length = Along.Size();
	if (Length < 0.1f)
	{
		for (int32 s = 0; s < Segments; ++s) Points.Add(To);
		return Points;
	}
	const FVector Dir = Along / Length;
	const FVector Side = FVector::CrossProduct(Along, FMath::Abs(Dir.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	const FVector Up = FVector::CrossProduct(Side, Along).GetSafeNormal();
	for (int32 s = 1; s < Segments; ++s)
	{
		const float T = float(s) / Segments;
		const FVector Kink = (Side * FMath::FRandRange(-1.f, 1.f) + Up * FMath::FRandRange(-1.f, 1.f)) * Length * Jag * FMath::Sin(UE_PI * T);
		Points.Add(From + Along * T + Kink);
	}
	Points.Add(To);
	return Points;
}

FVector AWWOLightningBolt::CenterOf(const AActor* Actor)
{
	if (const IWWOTargetable* T = Cast<IWWOTargetable>(Actor)) return T->BodyCenter();
	return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
}

AWWOLightningBolt* AWWOLightningBolt::Spawn(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& InColor,
	float InLifetime, AActor* InFrom, AActor* InTo)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWWOLightningBolt* Bolt = World->SpawnActor<AWWOLightningBolt>((From + To) * 0.5f, FRotator::ZeroRotator, Params);
	if (!Bolt) return nullptr;
	Bolt->FromPoint = From;
	Bolt->ToPoint = To;
	Bolt->FromActor = InFrom;
	Bolt->ToActor = InTo;
	Bolt->Color = InColor;
	Bolt->Lifetime = InLifetime;
	Bolt->Core = MakeSegments(Bolt, TEXT("Core"), FLinearColor(0.9f, 0.95f, 1.f), 1.f);
	Bolt->Halo = MakeSegments(Bolt, TEXT("Halo"), InColor, 0.45f);
	Bolt->Light = NewObject<UPointLightComponent>(Bolt);
	Bolt->Light->SetupAttachment(Bolt->GetRootComponent());
	Bolt->Light->SetLightColor(FMath::Lerp(InColor, FLinearColor::White, 0.3f));
	Bolt->Light->SetCastShadows(false);
	Bolt->Light->SetIntensityUnits(ELightUnits::Unitless);
	Bolt->Light->SetUsingAbsoluteLocation(true);
	Bolt->Light->RegisterComponent();
	Bolt->Reform();
	return Bolt;
}

void AWWOLightningBolt::Reform()
{
	const FVector A = FromActor.IsValid() ? CenterOf(FromActor.Get()) : FromPoint;
	const FVector B = ToActor.IsValid() ? CenterOf(ToActor.Get()) : ToPoint;
	const TArray<FVector> Points = Jagged(A, B, BoltSegments, BoltJag);
	for (int32 s = 0; s < BoltSegments; ++s)
	{
		WWOPlaceSegment(Core, s, Points[s], Points[s + 1], 1.2f);
		WWOPlaceSegment(Halo, s, Points[s], Points[s + 1], 4.f);
	}
	const FVector Split = Points[FMath::RandRange(2, BoltSegments - 3)];
	const float Length = FVector::Dist(A, B);
	const FVector Off = (B - A).GetSafeNormal() * 0.5f + FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-0.6f, 1.f)).GetSafeNormal();
	const TArray<FVector> Fork = Jagged(Split, Split + Off.GetSafeNormal() * Length * FMath::FRandRange(0.15f, 0.3f), BoltBranch, BoltJag * 1.5f);
	for (int32 s = 0; s < BoltBranch; ++s)
	{
		WWOPlaceSegment(Core, BoltSegments + s, Fork[s], Fork[s + 1], 1.2f);
		WWOPlaceSegment(Halo, BoltSegments + s, Fork[s], Fork[s + 1], 4.f);
	}
	Light->SetWorldLocation((A + B) * 0.5f);
	Light->SetAttenuationRadius(FMath::Clamp(Length * 0.7f, 200.f, 600.f));
}

void AWWOLightningBolt::Tick(float DeltaSeconds)
{
	WWO_PROF("LightningBolt");
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age >= Lifetime)
	{
		Destroy();
		return;
	}
	const float Fade = 1.f - Age / Lifetime;
	TintSegments(Core, FLinearColor(0.9f, 0.95f, 1.f), FMath::Clamp(Fade * 1.5f, 0.f, 1.f));
	TintSegments(Halo, Color, 0.45f * Fade);
	Light->SetIntensity(3.f * Fade * FMath::FRandRange(0.4f, 1.f) * 2000.f);
	NextFlicker -= DeltaSeconds;
	if (NextFlicker <= 0.f)
	{
		NextFlicker = 0.035f;
		Reform();
	}
}

// --- Thornfist discharge -----------------------------------------------------------------

namespace
{
	constexpr int32 ArcSegments = 7;
	constexpr float ArcJag = 0.22f;
}

AWWODischarge::AWWODischarge()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AWWODischarge* AWWODischarge::Spawn(UWorld* World, AActor* InTarget, const FVector& Impact, const FVector& InAway,
	const FLinearColor& InColor, int32 InArcs, float RadiusM, float InLifetime)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWWODischarge* D = World->SpawnActor<AWWODischarge>(Impact, FRotator::ZeroRotator, Params);
	if (!D) return nullptr;
	D->Target = InTarget;
	D->Offset = InTarget ? Impact - InTarget->GetActorLocation() : FVector::ZeroVector;
	D->Away = InAway.GetSafeNormal();
	D->Color = InColor;
	D->Arcs = FMath::Max(InArcs, 1);
	D->Radius = RadiusM * WWO::M;
	D->Lifetime = InLifetime;
	D->Core = MakeSegments(D, TEXT("Core"), FLinearColor(0.9f, 0.95f, 1.f), 1.f);
	D->Halo = MakeSegments(D, TEXT("Halo"), InColor, 0.45f);
	D->Light = NewObject<UPointLightComponent>(D);
	D->Light->SetupAttachment(D->GetRootComponent());
	D->Light->SetLightColor(FMath::Lerp(InColor, FLinearColor::White, 0.3f));
	D->Light->SetAttenuationRadius(D->Radius * 3.5f);
	D->Light->SetCastShadows(false);
	D->Light->SetIntensityUnits(ELightUnits::Unitless);
	D->Light->RegisterComponent();

	// Sparks fly away from the puncher, plus a quick glow.
	if (AWWOParticles* Sparks = AWWOParticles::Burst(World, Impact, FMath::Lerp(InColor, FLinearColor::White, 0.6f), 28, InLifetime * 1.3f,
		2.5f, 7.f, 0.05f, FVector(0, 0, -12.f), EWWOParticleStyle::Glow, false))
	{
		Sparks->Direction = (D->Away + FVector(0, 0, 0.4f)).GetSafeNormal();
		Sparks->SpreadDegrees = 70.f;
		Sparks->Start();
	}
	if (AWWOFXActor* Glow = AWWOFXActor::Spawn(World, Impact, InLifetime))
	{
		UStaticMeshComponent* Ball = WWOFX::AddSoftSphere(Glow, Glow->GetRootComponent(), InColor, 0.8f, 2.f, true, false, 0.6f);
		const float S = D->Radius / 50.f;
		Glow->ScaleTween(Ball, FVector(S * 0.25f), FVector(S * 0.5f), InLifetime, EWWOEase::QuadOut);
		Glow->ParamTween(WWOFX::MID(Ball), "Alpha", 0.8f, 0.f, InLifetime);
	}
	D->Reform();
	return D;
}

void AWWODischarge::Reform()
{
	const int32 Shown = FMath::Max(1, FMath::CeilToInt(Arcs * (1.f - Age / Lifetime)));
	const FVector Origin = GetActorLocation();
	const IWWOTargetable* T = Cast<IWWOTargetable>(Target.Get());
	for (int32 a = 0; a < Arcs; ++a)
	{
		FVector Finish;
		if (a % 2 == 0 && T)
		{
			const TArray<FWWOAimPart> Parts = T->GetAimParts();
			const FWWOAimPart& Part = Parts[FMath::RandRange(0, Parts.Num() - 1)];
			const float Spread = Part.Radius * 1.2f;
			Finish = Part.Position + FVector(FMath::FRandRange(-Spread, Spread), FMath::FRandRange(-Spread, Spread), FMath::FRandRange(-60.f, 60.f));
		}
		else
		{
			FVector Dir = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-0.6f, 1.f)).GetSafeNormal();
			if (FVector::DotProduct(Dir, Away) < 0.f) Dir -= 2.f * FVector::DotProduct(Dir, Away) * Away; // never back at the camera
			Finish = Origin + Dir * Radius * FMath::FRandRange(0.5f, 1.f);
		}
		const TArray<FVector> Points = AWWOLightningBolt::Jagged(Origin, Finish, ArcSegments, ArcJag);
		for (int32 s = 0; s < ArcSegments; ++s)
		{
			const int32 i = a * ArcSegments + s;
			WWOPlaceSegment(Core, i, Points[s], Points[s + 1], 0.9f, a < Shown);
			WWOPlaceSegment(Halo, i, Points[s], Points[s + 1], 2.8f, a < Shown);
		}
	}
}

void AWWODischarge::Tick(float DeltaSeconds)
{
	WWO_PROF("Discharge");
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (Age >= Lifetime)
	{
		Destroy();
		return;
	}
	if (Target.IsValid()) SetActorLocation(Target->GetActorLocation() + Offset); // ride along with a knocked-back body
	const float Fade = 1.f - Age / Lifetime;
	TintSegments(Core, FLinearColor(0.9f, 0.95f, 1.f), FMath::Clamp(Fade * 1.5f, 0.f, 1.f));
	TintSegments(Halo, Color, 0.45f * Fade);
	Light->SetIntensity(7.f * Fade * Fade * FMath::FRandRange(0.5f, 1.f) * 2000.f);
	NextFlicker -= DeltaSeconds;
	if (NextFlicker <= 0.f)
	{
		NextFlicker = 0.035f;
		Reform();
	}
}
