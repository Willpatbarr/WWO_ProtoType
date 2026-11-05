#include "UI/WWOMenuCursor.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ICursor.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Types/SlateConstants.h"
#include "Widgets/SWindow.h"
#include "WWOTypes.h"

#if PLATFORM_MAC
#include "Mac/MacSystemIncludes.h" // Apple headers, with Unreal's FVector workaround
#pragma push_macro("FVector")
#define FVector FVectorWorkaround
#import <GameController/GameController.h>
#pragma pop_macro("FVector")
#endif

bool FWWOMenuCursor::ViewportRect(FVector2D& OutMin, FVector2D& OutSize) const
{
	// The game viewport in Slate's absolute (desktop) space, the space the cursor lives in.
	if (const TSharedPtr<SWidget> W = Viewport.Pin())
	{
		const FGeometry& G = W->GetTickSpaceGeometry();
		OutMin = G.GetAbsolutePosition();
		OutSize = G.GetAbsoluteSize();
		if (OutSize.X > 1.f && OutSize.Y > 1.f) return true;
	}
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow())
	{
		const FGeometry G = Window->GetWindowGeometryInScreen();
		OutMin = G.GetAbsolutePosition();
		OutSize = G.GetAbsoluteSize();
		return OutSize.X > 1.f && OutSize.Y > 1.f;
	}
	return false;
}

void FWWOMenuCursor::CenterCursor()
{
	bPendingCenter = true; // start centered (done on the next tick, when the viewport is laid out)
}

void FWWOMenuCursor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	FVector2D Min, Size;
	if (!ViewportRect(Min, Size)) return;
	if (bPendingCenter)
	{
		bPendingCenter = false;
		SlateApp.SetCursorPos(Min + Size * 0.5f);
	}
	// Godot moves the cursor in its 1600 x 900 layout, which the window scales up by this much.
	const float Scale = FMath::Min(Size.X / 1600.f, Size.Y / 900.f);
	const float Dt = FMath::Min(DeltaTime, 0.1f);
	const float Length = LeftStick.Size();
	if (Length > Deadzone)
	{
		const float Strength = (Length - Deadzone) / (1.f - Deadzone);
		const FVector2D Move = LeftStick / Length * Strength * Strength * Speed * Scale * Dt;
		const FVector2D OldPos = SlateApp.GetCursorPos();
		FVector2D NewPos = OldPos + FVector2D(Move.X, -Move.Y); // stick up = screen up
		NewPos.X = FMath::Clamp(NewPos.X, Min.X, Min.X + Size.X - 1.f);
		NewPos.Y = FMath::Clamp(NewPos.Y, Min.Y, Min.Y + Size.Y - 1.f);
		SlateApp.SetCursorPos(NewPos);
		// Tell the UI the "mouse" moved so hover highlights (and slider drags) follow.
		const FPointerEvent MouseEvent(SlateApp.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, NewPos, OldPos,
			SlateApp.GetPressedMouseButtons(), EKeys::Invalid, 0, SlateApp.GetPlatformApplication()->GetModifierKeys());
		SlateApp.ProcessMouseMoveEvent(MouseEvent);
	}
	if (FMath::Abs(RightStickY) > Deadzone)
	{
		// A scroll box moves GlobalScrollAmount Slate units per wheel notch; Slate units here are
		// the layout's (1.2 per Godot pixel at the 1920 x 1080 design size). Stick up (+) is a wheel
		// turn up (+), scrolling toward the top, like Godot.
		const float Notches = RightStickY * ScrollSpeed * 1.2f * Dt / GetGlobalScrollAmount();
		const FVector2D Pos = SlateApp.GetCursorPos();
		const FPointerEvent Wheel(SlateApp.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Pos, Pos,
			SlateApp.GetPressedMouseButtons(), EKeys::MouseWheelAxis, Notches, SlateApp.GetPlatformApplication()->GetModifierKeys());
		SlateApp.ProcessMouseWheelOrGestureEvent(Wheel, nullptr);
	}
}

void FWWOMenuCursor::Click(FSlateApplication& SlateApp, bool bDown)
{
	const FVector2D Pos = SlateApp.GetCursorPos();
	TSet<FKey> Pressed = SlateApp.GetPressedMouseButtons();
	if (bDown) Pressed.Add(EKeys::LeftMouseButton);
	else Pressed.Remove(EKeys::LeftMouseButton);
	const FPointerEvent Event(SlateApp.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Pos, Pos, Pressed,
		EKeys::LeftMouseButton, 0, SlateApp.GetPlatformApplication()->GetModifierKeys());
	TSharedPtr<FGenericWindow> Window = SlateApp.GetPlatformApplication()->GetWindowUnderCursor();
	if (bDown) SlateApp.ProcessMouseButtonDownEvent(Window, Event);
	else SlateApp.ProcessMouseButtonUpEvent(Event);
}

bool FWWOMenuCursor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Bottom)
	{
		if (!InKeyEvent.IsRepeat()) Click(SlateApp, true);
		return true;
	}
	// Stick and d-pad navigation would move keyboard focus around; the cursor drives everything.
	const FKey Key = InKeyEvent.GetKey();
	return Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down || Key == EKeys::Gamepad_LeftStick_Left
		|| Key == EKeys::Gamepad_LeftStick_Right || Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down
		|| Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right;
}

bool FWWOMenuCursor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Bottom)
	{
		Click(SlateApp, false);
		return true;
	}
	return false;
}

bool FWWOMenuCursor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	const float Value = InAnalogInputEvent.GetAnalogValue();
	if (Key == EKeys::Gamepad_LeftX) { LeftStick.X = Value; return true; }
	if (Key == EKeys::Gamepad_LeftY) { LeftStick.Y = Value; return true; }
	if (Key == EKeys::Gamepad_RightY) { RightStickY = Value; return true; }
	return false;
}

void FWWOMenuCursor::PaintRing(const FGeometry& Geometry, const FGeometry& TickGeometry, FSlateWindowElementList& Out, int32 Layer, float Scale)
{
	const FVector2f At = FVector2f(TickGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()));
	const FLinearColor Color = WWO::Lin(FLinearColor(1, 1, 1, 0.9f));
	auto Ring = [&](float Radius, float Width)
	{
		TArray<FVector2f> Points;
		constexpr int32 Segments = 32;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float A = UE_TWO_PI * i / Segments;
			Points.Add(At + FVector2f(FMath::Cos(A), FMath::Sin(A)) * Radius * Scale);
		}
		FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Width * Scale);
	};
	Ring(14.f, 2.5f);
	for (float R = 2.25f; R > 0.f; R -= 0.75f) Ring(R, 1.5f); // the filled 3 px dot
}

// --- FWWOMenuKeys ------------------------------------------------------------------------------

bool FWWOMenuKeys::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (!OnEscape || InKeyEvent.GetKey() != EKeys::Escape) return false;
	const TSharedPtr<SWidget> W = Viewport.Pin();
	if (!W.IsValid() || !(W->HasKeyboardFocus() || W->HasFocusedDescendants())) return false; // typing in an editor panel
	if (!InKeyEvent.IsRepeat()) OnEscape();
	return true;
}

bool FWWOMenuKeys::IsCreateButtonDown()
{
#if PLATFORM_MAC
	if (@available(macOS 11.3, *))
	{
		for (GCController* Controller in [GCController controllers])
		{
			GCExtendedGamepad* Pad = Controller.extendedGamepad;
			if ([Pad isKindOfClass:[GCDualSenseGamepad class]] && Pad.buttonOptions.pressed) return true;
		}
	}
#endif
	return false;
}
