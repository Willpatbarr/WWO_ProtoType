#pragma once

// Gray-box visual effects (Godot: flora/fx.gd, lightning_bolt.gd, thornfist_discharge.gd and
// the tween-driven blasts in grenade.gd). Everything is built from engine basic shapes and a
// few materials made by Tools/create_assets.py:
//   M_WWO_Solid       opaque lit: Color, Glow, Flash
//   M_WWO_SoftAdd     translucent additive soft sphere: Color, Alpha, Edge, Rim, Core
//   M_WWO_SoftAlpha   the same, alpha blended
//   M_WWO_Ground      translucent ground disc with a bright ring: Color, Alpha, RingWidth, Fill, Ripple
//   M_WWO_Particle    per-instance colored soft dots (instanced static mesh custom data RGBA)
//   M_WWO_Chunk       per-instance colored opaque chunks
//   M_WWO_Unlit       translucent unlit: Color, Alpha (tracers, lightning)

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOFX.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UPointLightComponent;

namespace WWOFX
{
	UStaticMesh* Sphere();
	UStaticMesh* Cube();
	UStaticMesh* Cylinder();
	UStaticMesh* Plane();
	UStaticMesh* Cone();
	UMaterialInterface* Material(const TCHAR* Name);

	// A component showing a basic shape with its own material instance.
	UStaticMeshComponent* AddShape(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const TCHAR* MaterialName,
		const FLinearColor& Color, float Glow = 0.f);
	UMaterialInstanceDynamic* MID(UStaticMeshComponent* Comp);

	// Soft glowing ball (M_WWO_Soft*).
	UStaticMeshComponent* AddSoftSphere(AActor* Owner, USceneComponent* Parent, const FLinearColor& Color, float Alpha,
		float Edge = 1.5f, bool bAdditive = true, bool bRim = false, float Core = 0.f);
	// Flat disc lying on the ground (M_WWO_Ground). Radius is in cm.
	UStaticMeshComponent* AddGroundDisc(AActor* Owner, USceneComponent* Parent, const FLinearColor& Color,
		float RingWidth = 0.12f, float Fill = 0.f, float Ripple = 0.f);

	// The floor point under a spot, if any (within Reach cm).
	bool GroundBelow(UWorld* World, const FVector& Point, float Reach, FVector& OutGround, const AActor* Ignore = nullptr);
}

UENUM()
enum class EWWOEase : uint8 { Linear, QuadOut, QuadIn, ExpoOut, BackOut, SineInOut };

// One property animated over time.
struct FWWOTween
{
	TWeakObjectPtr<USceneComponent> Comp;
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	FName Param;           // material scalar (when Material is set)
	bool bScale = false;   // animate the component's relative scale
	bool bLight = false;   // animate a light's intensity
	bool bMove = false;    // animate the component's relative location (ScaleFrom/ScaleTo hold the two spots)
	FVector ScaleFrom = FVector::OneVector, ScaleTo = FVector::OneVector;
	float From = 0.f, To = 0.f;
	float Delay = 0.f, Duration = 0.1f;
	EWWOEase Ease = EWWOEase::Linear;
};

// A short-lived effect made of shapes and tweens. Destroys itself after Lifetime.
UCLASS()
class WHATWASOURS_API AWWOFXActor : public AActor
{
	GENERATED_BODY()

public:
	AWWOFXActor();
	virtual void Tick(float DeltaSeconds) override;
	// Effects never shape the navmesh. (Without this, Unreal adds each new effect's shapes to the
	// navigation octree, and the runtime navmesh rebuilds a hole wherever a shot or tracer lands.)
	virtual bool IsComponentRelevantForNavigation(UActorComponent* Component) const override { return false; }

	static AWWOFXActor* Spawn(UWorld* World, const FVector& At, float Lifetime);

	void ScaleTween(USceneComponent* Comp, const FVector& From, const FVector& To, float Duration, EWWOEase Ease = EWWOEase::Linear, float Delay = 0.f);
	void MoveTween(USceneComponent* Comp, const FVector& From, const FVector& To, float Duration, EWWOEase Ease = EWWOEase::Linear, float Delay = 0.f);
	void ParamTween(UMaterialInstanceDynamic* Mat, FName Param, float From, float To, float Duration, EWWOEase Ease = EWWOEase::Linear, float Delay = 0.f);
	void LightTween(UPointLightComponent* Light, float From, float To, float Duration, EWWOEase Ease = EWWOEase::Linear);
	UPointLightComponent* AddLight(const FLinearColor& Color, float Intensity, float RadiusCm);

	float Lifetime = 1.f;

	// Common one-shot effects.
	static void Shockwave(UWorld* World, const FVector& At, const FLinearColor& Color, float RadiusM, float Seconds = 0.35f);
	static void Pop(UWorld* World, const FVector& At, const FLinearColor& Color, float RadiusM, float Seconds = 0.2f);
	static void Tracer(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Width = 2.f, float Seconds = 0.08f);
	static void ImpactMark(UWorld* World, const FVector& At);
	// A gunshot through the air: a bright streak that flies from From to To with a thin wake that
	// lingers a moment behind it. Size scales the widths (your own shots use a smaller one).
	static void BulletTrail(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Size = 1.f);
	// The burst at a gun's muzzle: a hot core, a tongue of flame along Dir, a cross of flares at a
	// random roll, and a flash of light. For guns seen in the world (someone else's, a bot's).
	// A shot landing on a body: a quick flash and sparks thrown back along Away (toward the shooter).
	// A critical hit is bigger, brighter and gold.
	static void HitSpark(UWorld* World, const FVector& At, const FVector& Away, bool bCritical);
	// Something dying: a flash, a ring along the ground, sparks, and chunks thrown up and out.
	// SizeM is about the body's height.
	static void DeathBurst(UWorld* World, const FVector& At, const FLinearColor& Color, float SizeM = 1.8f);
	// A dark mark left on the ground by a blast, fading over Seconds.
	static void Scorch(UWorld* World, const FVector& Ground, float RadiusM, float Seconds = 5.f);
	static void MuzzleFlash(UWorld* World, const FVector& At, const FVector& Dir, const FLinearColor& Color, float Size = 1.f);

private:
	TArray<FWWOTween> Tweens;
	float Age = 0.f;
	static float EaseValue(EWWOEase Ease, float T);
};

UENUM()
enum class EWWOParticleStyle : uint8 { Glow, Smoke, Chunk };

UENUM()
enum class EWWOEmitShape : uint8 { Point, Sphere, Box, Ring };

// CPU particles drawn with one instanced mesh (Godot: CPUParticles3D via FX.burst).
// Units: meters and seconds, as in Godot's FX.burst, converted internally.
UCLASS()
class WHATWASOURS_API AWWOParticles : public AActor
{
	GENERATED_BODY()

public:
	AWWOParticles();
	virtual void Tick(float DeltaSeconds) override;

	// Godot FX.burst(color, amount, lifetime, speed_min, speed_max, size, gravity, style).
	static AWWOParticles* Burst(UWorld* World, const FVector& At, const FLinearColor& Color, int32 Amount, float LifetimeS,
		float SpeedMin, float SpeedMax, float Size, const FVector& GravityM = FVector(0, 0, -9.8f),
		EWWOParticleStyle Style = EWWOParticleStyle::Glow, bool bStart = true);

	FLinearColor Color = FLinearColor::White;
	int32 Amount = 10;
	float ParticleLife = 1.f;
	float SpeedMin = 1.f, SpeedMax = 2.f;
	float Size = 0.1f; // meters
	FVector Gravity = FVector(0, 0, -9.8f); // m/s^2, Unreal axes
	FVector Direction = FVector::UpVector;
	float SpreadDegrees = 180.f;
	float DampingMin = 1.f, DampingMax = 3.f;
	EWWOParticleStyle Style = EWWOParticleStyle::Glow;
	EWWOEmitShape Shape = EWWOEmitShape::Point;
	float ShapeRadius = 0.f;   // meters (sphere / ring)
	FVector BoxExtent = FVector::ZeroVector; // meters (box)
	bool bOneShot = true;      // false: keeps emitting Amount per lifetime while bEmitting
	bool bEmitting = false;
	bool bLocalSpace = false;  // particles move with the actor

	void Start();
	void StopAndFade(); // stop emitting; destroy once the last particle is gone

private:
	struct FParticle { FVector Pos; FVector Vel; float Age; float Life; float Scale; float Damping; float Spin; FQuat Rot; };
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Mesh;
	TArray<FParticle> Particles;
	float EmitAccumulator = 0.f;
	bool bStarted = false;
	void EmitOne();
};

// A jagged flickering bolt between two points or actors (Godot: LightningBolt).
UCLASS()
class WHATWASOURS_API AWWOLightningBolt : public AActor
{
	GENERATED_BODY()

public:
	AWWOLightningBolt();
	virtual void Tick(float DeltaSeconds) override;

	static AWWOLightningBolt* Spawn(UWorld* World, const FVector& From, const FVector& To, const FLinearColor& Color, float Lifetime,
		AActor* FromActor = nullptr, AActor* ToActor = nullptr);

	// Shared helpers (the Thornfist discharge uses them too).
	static TArray<FVector> Jagged(const FVector& From, const FVector& To, int32 Segments, float Jag);
	static FVector CenterOf(const AActor* Actor);

private:
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Core;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Halo;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	TWeakObjectPtr<AActor> FromActor, ToActor;
	FVector FromPoint, ToPoint;
	FLinearColor Color;
	float Lifetime = 0.35f, Age = 0.f, NextFlicker = 0.f;
	void Reform();
	friend class AWWODischarge;
};

// Thornfist's electric burst where a boosted punch lands (also Static Charge's chain hits).
UCLASS()
class WHATWASOURS_API AWWODischarge : public AActor
{
	GENERATED_BODY()

public:
	AWWODischarge();
	virtual void Tick(float DeltaSeconds) override;

	static AWWODischarge* Spawn(UWorld* World, AActor* Target, const FVector& Impact, const FVector& Away,
		const FLinearColor& Color, int32 Arcs, float RadiusM, float Lifetime);

private:
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Core;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Halo;
	UPROPERTY() TObjectPtr<UPointLightComponent> Light;
	TWeakObjectPtr<AActor> Target;
	FVector Offset = FVector::ZeroVector;
	FVector Away = FVector::ForwardVector;
	FLinearColor Color;
	int32 Arcs = 6;
	float Radius = 120.f; // cm
	float Lifetime = 0.35f, Age = 0.f, NextFlicker = 0.f;
	void Reform();
};

// Places an instance of a unit cube (100 cm) stretched between two points.
void WWOPlaceSegment(UInstancedStaticMeshComponent* ISM, int32 Index, const FVector& A, const FVector& B, float Width, bool bVisible = true);
