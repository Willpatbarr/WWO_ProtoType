#include "UI/WWOMenuCursor.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ICursor.h"
#include "Widgets/SWindow.h"

void FWWOMenuCursor::CenterCursor()
{
	bPendingCenter = true; // start centered (done on the next tick, when the window is known)
}

void FWWOMenuCursor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	if (bPendingCenter)
	{
		bPendingCenter = false;
		if (TSharedPtr<SWindow> Window = SlateApp.GetActiveTopLevelWindow())
		{
			const FGeometry Geometry = Window->GetWindowGeometryInScreen();
			SlateApp.SetCursorPos(Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
		}
	}
	const float Dt = FMath::Min(DeltaTime, 0.1f);
	const float Length = LeftStick.Size();
	if (Length > Deadzone)
	{
		const float Strength = (Length - Deadzone) / (1.f - Deadzone);
		const FVector2D Move = LeftStick / Length * Strength * Strength * Speed * Dt;
		const FVector2D OldPos = SlateApp.GetCursorPos();
		const FVector2D NewPos = OldPos + FVector2D(Move.X, -Move.Y); // stick up = screen up
		SlateApp.SetCursorPos(NewPos);
		const FPointerEvent MouseEvent(SlateApp.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, NewPos, OldPos,
			SlateApp.GetPressedMouseButtons(), EKeys::Invalid, 0, SlateApp.GetPlatformApplication()->GetModifierKeys());
		SlateApp.ProcessMouseMoveEvent(MouseEvent);
	}
	if (FMath::Abs(RightStickY) > Deadzone)
	{
		// The wheel scrolls about 100 px per notch in a scroll box; send fractional notches.
		ScrollCarry += RightStickY * ScrollSpeed * Dt / 100.f;
		if (FMath::Abs(ScrollCarry) >= 0.1f)
		{
			const FVector2D Pos = SlateApp.GetCursorPos();
			const FPointerEvent Wheel(SlateApp.GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Pos, Pos,
				SlateApp.GetPressedMouseButtons(), EKeys::MouseWheelAxis, ScrollCarry, SlateApp.GetPlatformApplication()->GetModifierKeys());
			SlateApp.ProcessMouseWheelOrGestureEvent(Wheel, nullptr);
			ScrollCarry = 0.f;
		}
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
		|| Key == EKeys::Gamepad_LeftStick_Right || Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down;
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
