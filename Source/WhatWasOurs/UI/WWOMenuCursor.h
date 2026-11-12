#pragma once

// A Destiny-style menu cursor shared by the settings and character menus (Godot: MenuCursor).
// - Left stick moves the real mouse cursor (squared response: fine near center, fast at full tilt),
//   at Godot's speed in its 1600 x 900 layout, kept inside the game viewport.
// - Cross clicks (and drags sliders, since the click is held while the button is).
// - Right stick scrolls whatever scroll box is under the cursor.
// - The mouse drives the same cursor. The OS pointer is hidden over the menus, which draw
//   Godot's ring-and-dot cursor instead (PaintRing).
// Registered with Slate as an input pre-processor while a menu is open.
//
// FWWOMenuKeys: menu keys Slate or the editor would otherwise take.
// - Play-in-Editor: Esc stops PIE before the game sees it; while the game viewport has focus,
//   Esc goes to the game's menu toggle instead (stop PIE with the toolbar's Stop button).
// - The DualSense Create button: Unreal's Apple controller code maps the touchpad click to
//   Gamepad_Special_Left and doesn't read Create at all, so it's polled here (Mac only).

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class SWidget;
class FSlateWindowElementList;
struct FGeometry;

class FWWOMenuCursor : public IInputProcessor
{
public:
	static constexpr float Speed = 1400.f;       // Godot pixels per second at full stick
	static constexpr float ScrollSpeed = 1100.f; // Godot pixels per second at full right stick
	static constexpr float Deadzone = 0.15f;
	// The right stick's sideways tilt (its up and down scrolls): the character menu turns the
	// character with it.
	static inline float RightStickX = 0.f;

	// The game viewport: the cursor centers in it, stays in it and moves in its scale.
	void SetViewport(const TSharedPtr<SWidget>& InViewport) { Viewport = InViewport; }
	void CenterCursor();

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override;
	// Set while a key binding waits for its key (the Controls page): the next key, mouse button,
	// wheel step or controller button goes to it instead of the menu. Returns true once it's done
	// (taken or cancelled); false keeps waiting.
	TFunction<bool(const FKey&)> Capture;
	virtual const TCHAR* GetDebugName() const override { return TEXT("WWOMenuCursor"); }

	// Godot's cursor: a 14 px ring (2.5 px line) with a 3 px dot, white at 90%. Drawn by an
	// open menu at the mouse position; Scale is the menu's units per Godot pixel.
	// Geometry is the paint geometry (window space); TickGeometry the same widget's desktop-space
	// geometry, which the cursor position is in.
	static void PaintRing(const FGeometry& Geometry, const FGeometry& TickGeometry, FSlateWindowElementList& Out, int32 Layer, float Scale);

private:
	TWeakPtr<SWidget> Viewport;
	FVector2D LeftStick = FVector2D::ZeroVector;
	float RightStickY = 0.f;
	bool bPendingCenter = false;
	bool ViewportRect(FVector2D& OutMin, FVector2D& OutSize) const;
	void Click(FSlateApplication& SlateApp, bool bDown);
	void TakeCapture(const FKey& Key);
};

class FWWOMenuKeys : public IInputProcessor
{
public:
	TFunction<void()> OnEscape;  // set in Play-in-Editor only
	TWeakPtr<SWidget> Viewport;  // Esc counts only while this has keyboard focus

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("WWOMenuKeys"); }

	// True while a DualSense's Create button is held (always false off Mac).
	static bool IsCreateButtonDown();
};
