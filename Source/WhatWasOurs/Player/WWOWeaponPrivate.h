#pragma once

// What more than one of UWWOWeaponComponent's files (Player/WWOWeapon*.cpp) uses. A helper or constant used
// by one file stays in that file's anonymous namespace.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "WWOTypes.h"

class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
class UWWOWeaponData;
class UWWOLoadoutSubsystem;

namespace WWOWeapon
{
	// Godot camera space (meters, x right, y up, z back) -> Unreal (cm, x forward, y right, z up).
	inline FVector G2U(const FVector& G) { return FVector(-G.Z, G.X, G.Y) * WWO::M; }
	// Godot rotation (radians: pitch about x, yaw about y, roll about z) -> FRotator.
	inline FRotator GRot(const FVector& R) { return FRotator(FMath::RadiansToDegrees(R.X), -FMath::RadiansToDegrees(R.Y), -FMath::RadiansToDegrees(R.Z)); }

	constexpr float ReloadMagDrop = 0.2f;
	constexpr float ReloadSeated = 0.75f; // ammo refills once the new mag is in
	constexpr float MeleeImpactAt = 0.4f;  // the hit lands here (fraction of the swing)

	// The muzzle flash's parts on the first-person gun: the core, and the flares built around it.
	const FName FlashCore("MuzzleFlash"), FlashTongue("MuzzleFlashTongue"), FlashCrossA("MuzzleFlashCrossA"), FlashCrossB("MuzzleFlashCrossB");
	inline bool IsFlashPart(FName Name) { return Name == FlashCore || Name == FlashTongue || Name == FlashCrossA || Name == FlashCrossB; }

	// Config/WeaponModels.json's entry for a weapon (WWOWeaponModel.cpp: read and merged once a run).
	TSharedPtr<FJsonObject> ModelConfig(FName WeaponId);

	// A shape from JSON (a model's "parts", a mod's visual parts): "shape" box (size = length, width,
	// height), cyl (radius, length, along x) or sphere (radius), all in cm; "offset", "rot" (pitch,
	// yaw, roll), "color", "glow". Offset is from the part's anchor.
	struct FJsonPartShape
	{
		UStaticMesh* Mesh = nullptr;
		FVector Scale = FVector::OneVector;
		FRotator Rot = FRotator::ZeroRotator;
		FVector Offset = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		float Glow = 0.f;
	};
	FJsonPartShape JsonPartShape(const TSharedPtr<FJsonObject>& O);

	// The weapon as a still model for show (the character menu's Weapon tab): the same mesh, kept box
	// parts, extra parts and (for the loadout's own weapon) its mods' parts as the first-person gun,
	// without the sight dot or muzzle flash. Each part is a new component of Owner under Parent, added to Out.
	void BuildShowcase(UObject* Owner, USceneComponent* Parent, const UWWOWeaponData* Weapon, const UWWOLoadoutSubsystem* Loadout,
		TArray<UStaticMeshComponent*>& Out);
}
