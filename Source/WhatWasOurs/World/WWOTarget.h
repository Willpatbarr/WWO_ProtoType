#pragma once

// A shootable dummy (Godot: target.gd). The head box is the crit spot. Flashes when hit and
// pops a damage number. Can strafe side to side to test tracking and aim assist. At zero
// health it shrinks away until revived (Settings -> Targets, or the auto respawn timer).
// Online the host owns it: its health, death and strafing replicate. A joiner's hit flashes it and
// shows the number there, but only the host's damage counts (phase 2 sends hits to the host).

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Systems/WWOTargetable.h"
#include "WWOTarget.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class WHATWASOURS_API AWWOTarget : public AActor, public IWWOTargetable
{
	GENERATED_BODY()

public:
	AWWOTarget();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, Category = "Target") float StrafeDistance = 0.f; // meters of side-to-side travel
	UPROPERTY(EditAnywhere, Category = "Target") float StrafeSpeed = 3.f;    // top speed, m/s

	virtual TArray<FWWOAimPart> GetAimParts() const override;
	virtual bool IsCriticalComponent(const UPrimitiveComponent* Component) const override;
	virtual bool TakeHit(float Damage, bool bCritical, const FVector& Point, FLinearColor Color = FLinearColor(0, 0, 0, 0)) override;
	virtual bool IsDead() const override { return bDead; }
	virtual float GetHealth() const override { return Health; }
	virtual float GetMaxHealth() const override { return MaxHealth; }
	virtual FVector FeetLocation() const override { return GetActorLocation(); }

	void Revive(float NewMaxHealth);

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> BodyBox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> HeadBox;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> HeadMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMat;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HeadMat;
	FVector Start;
	float Time = 0.f;
	UPROPERTY(Replicated) float Health = 200.f;
	UPROPERTY(Replicated) float MaxHealth = 200.f;
	UPROPERTY(ReplicatedUsing = OnRep_Dead) bool bDead = false;
	UFUNCTION() void OnRep_Dead();
	void StartDeath();
	void ShowAlive();
	float BodyFlash = 0.f, HeadFlash = 0.f;
	float DeathAge = -1.f;
	float RespawnLeft = -1.f;
	void SetCollision(bool bOn);
};
