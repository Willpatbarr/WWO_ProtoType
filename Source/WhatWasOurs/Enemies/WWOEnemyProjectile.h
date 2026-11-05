#pragma once

// A slow glowing shot fired by enemies (Godot: enemies/enemy_projectile.gd). Moves each
// step with a sweep so it can't skip through thin things; hurts the player or a teammate on
// contact and pops on anything else. Enemies don't hit each other. With Gravity it arcs;
// with SplashRadius it explodes where it lands (full damage at the center, half at the edge).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WWOEnemyProjectile.generated.h"

UCLASS()
class WHATWASOURS_API AWWOEnemyProjectile : public AActor
{
	GENERATED_BODY()

public:
	AWWOEnemyProjectile();
	virtual void Tick(float DeltaSeconds) override;

	static AWWOEnemyProjectile* Fire(UWorld* World, const FVector& From, const FVector& VelocityCm, float Damage, float Life = 3.f,
		float GravityCm = 0.f, float SplashRadiusM = 0.f, const FLinearColor& Color = FLinearColor(1.f, 0.35f, 0.1f), float SizeM = 0.09f);

	FVector Velocity = FVector::ZeroVector; // cm/s
	float Damage = 8.f;
	float Life = 3.f;
	float Gravity = 0.f;      // cm/s^2
	float SplashRadius = 0.f; // meters
	FLinearColor Color = FLinearColor(1.f, 0.35f, 0.1f);
	float Size = 0.09f;       // meters

private:
	void Splash(const FVector& Point);
	void Build();
};
