#pragma once

// The HUD's layout, for code that has to agree with where the HUD draws (UWWOWeaponComponent's
// marker range is in HUD pixels). The HUD paints in Godot's 1600 x 900 layout (FWWOPainter).

#include "CoreMinimal.h"

namespace WWOHUD
{
	// Godot's canvas_items stretch with aspect "expand": the 1600 x 900 layout scales by the
	// tighter of the two ratios, and the extra width or height is added to the canvas.
	// LocalSize is the widget's size in Slate units; the result is Slate units per layout pixel.
	inline float Scale(const FVector2D& LocalSize)
	{
		return FMath::Min(LocalSize.X / 1600.f, LocalSize.Y / 900.f);
	}
}
