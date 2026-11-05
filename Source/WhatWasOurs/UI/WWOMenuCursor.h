#pragma once

// A Destiny-style menu cursor shared by the settings and character menus (Godot: MenuCursor).
// - Left stick moves the real mouse cursor (squared response: fine near center, fast at full tilt).
// - Cross clicks (and drags sliders, since the click is held while the button is).
// - Right stick scrolls whatever scroll box is under the cursor.
// - The mouse keeps working as usual.
// Registered with Slate as an input pre-processor while a menu is open.

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class FWWOMenuCursor : public IInputProcessor
{
public:
	static constexpr float Speed = 1400.f;       // pixels per second at full stick
	static constexpr float ScrollSpeed = 1100.f; // pixels per second at full right stick
	static constexpr float Deadzone = 0.15f;

	void CenterCursor();

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("WWOMenuCursor"); }

private:
	FVector2D LeftStick = FVector2D::ZeroVector;
	float RightStickY = 0.f;
	float ScrollCarry = 0.f;
	bool bPendingCenter = false;
	void Click(FSlateApplication& SlateApp, bool bDown);
};
