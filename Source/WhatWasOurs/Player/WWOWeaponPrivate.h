#pragma once

// What more than one of UWWOWeaponComponent's files (Player/WWOWeapon*.cpp) uses. A helper or constant used
// by one file stays in that file's anonymous namespace.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "WWOTypes.h"

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
}
