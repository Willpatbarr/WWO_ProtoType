#pragma once

// What more than one of AWWOCharacter's files (Player/WWOCharacter*.cpp) uses. A helper or constant used
// by one file stays in that file's anonymous namespace.

#include "CoreMinimal.h"

namespace WWOCharacter
{
	constexpr float StandCameraZ = 60.f;      // eye height above the standing capsule center (cm)
	constexpr float StandHalfHeight = 100.f;
	constexpr float CapsuleRadius = 50.f;
	constexpr float HeadHitUp = 10.f;         // skull center above the head bone, as on the enemies
	constexpr float HeadHitForward = 2.f;     // (the head hitbox, and the test probe in WWOTestTally.cpp)
}
