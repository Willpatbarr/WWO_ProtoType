#pragma once

// Shared types and helpers.
// Every tunable keeps the units the Godot prototype used (meters, seconds, degrees), so the
// settings sliders and saved values carry over unchanged. Code converts meters to Unreal's
// centimeters with WWO::M at the point of use.

#include "CoreMinimal.h"
#include "WWOTypes.generated.h"

namespace WWO
{
	constexpr float M = 100.f; // centimeters per meter

	// Godot's "move_toward".
	inline float MoveToward(float From, float To, float Delta)
	{
		return FMath::Abs(To - From) <= Delta ? To : From + FMath::Sign(To - From) * Delta;
	}

	inline FVector2D MoveToward2D(const FVector2D& From, const FVector2D& To, float Delta)
	{
		const FVector2D D = To - From;
		const float Len = D.Size();
		return (Len <= Delta || Len < 1e-6f) ? To : From + D / Len * Delta;
	}

	inline float SmoothStep(float A, float B, float X)
	{
		if (A == B) return X < A ? 0.f : 1.f;
		const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	// Godot "lightened" / "darkened".
	inline FLinearColor Lightened(const FLinearColor& C, float Amount)
	{
		return FLinearColor(C.R + (1.f - C.R) * Amount, C.G + (1.f - C.G) * Amount, C.B + (1.f - C.B) * Amount, C.A);
	}

	inline FLinearColor Darkened(const FLinearColor& C, float Amount)
	{
		return FLinearColor(C.R * (1.f - Amount), C.G * (1.f - Amount), C.B * (1.f - Amount), C.A);
	}

	// Colors in this project are written as sRGB, like the Godot prototype's. Rendering
	// (materials, Slate, the mannequin tint) wants linear values: convert at that boundary.
	inline FLinearColor Lin(const FLinearColor& C)
	{
		return FLinearColor(FMath::Pow(FMath::Max(C.R, 0.f), 2.2f), FMath::Pow(FMath::Max(C.G, 0.f), 2.2f), FMath::Pow(FMath::Max(C.B, 0.f), 2.2f), C.A);
	}

	inline FLinearColor WithAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, A);
	}

	// A random direction within Cone radians of Direction (evenly over the cone's area).
	inline FVector RandomInCone(const FVector& Direction, float Cone)
	{
		if (Cone <= 0.f) return Direction;
		const FVector Dir = Direction.GetSafeNormal();
		FVector Side, Up;
		Dir.FindBestAxisVectors(Side, Up);
		const float Radius = FMath::Tan(Cone) * FMath::Sqrt(FMath::FRand());
		const float Turn = FMath::FRand() * UE_TWO_PI;
		return (Dir + Side * FMath::Cos(Turn) * Radius + Up * FMath::Sin(Turn) * Radius).GetSafeNormal();
	}
}

// One shootable part of a target (aim assist, melee, grenades and the lunge use these).
USTRUCT(BlueprintType)
struct FWWOAimPart
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FVector Position = FVector::ZeroVector; // world, cm
	UPROPERTY(BlueprintReadOnly) float Radius = 0.f;                     // cm
	UPROPERTY(BlueprintReadOnly) bool bCritical = false;
};

UENUM(BlueprintType)
enum class EWWOStatOp : uint8
{
	Flat,       // amount added
	PercentAdd, // 0.12 = +12%
	Multiply,   // 1.5 = x1.5
};

// One change to one stat, e.g. "sprint_speed +12%".
// Final value = (base + all Flat) * (1 + all PercentAdd) * (all Multiply multiplied together)
USTRUCT(BlueprintType)
struct FWWOStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Stat;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EWWOStatOp Op = EWWOStatOp::PercentAdd;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Value = 0.f;
};

UENUM(BlueprintType)
enum class EWWOGrenadeKind : uint8
{
	Explosive, // blows up on its fuse
	Pool,      // small blast that leaves a damaging pool
	Spike,     // bounces around until it touches an enemy, then poisons it
	Tripmine,  // sticks where it lands, arms, blows up (slowing enemies) when one comes close
	Wall,      // blows up and raises a wall facing you
	Blind,     // blows up and blinds enemies
};

UENUM(BlueprintType)
enum class EWWOAbilityKind : uint8
{
	Passive,   // always on (HUD: half-grayed)
	Active,    // press its button to use, then it recharges
	Triggered, // lights up on its own when something sets it off
	Charged,   // fills up from something happening, then you arm it with its button
};

UENUM(BlueprintType)
enum class EWWOUltKind : uint8
{
	Blast,    // a massive grenade explosion centered on you
	Veil,     // invisible to enemies (and stronger melee) while in spore fog
	Entrench, // rooted, invulnerable, faster grenades
};

// The actions the game polls each frame (see AWWOPlayerController).
UENUM()
enum class EWWOAction : uint8
{
	Jump, Sprint, Crouch, Aim, Fire, Reload,
	Melee, MeleePad, Grenade, GrenadePad, Ult,
	Ability1, Ability2, WeaponNext, WeaponPrev,
	Menu, MenuBack, TabNext, TabPrev, CharacterMenu,
	NextConfig, // cycle saved weapon configs (draft mods)
	Revive,     // hold next to a downed teammate (online)
	Count UMETA(Hidden)
};
