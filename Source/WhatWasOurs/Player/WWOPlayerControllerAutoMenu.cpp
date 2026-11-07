// -WWOAutoTest (or -WWOAutoTestMenus for just these steps): both menus driven by controller
// input alone. DualSense buttons and sticks are injected into Slate like real ones, so they go
// through the same path: the menu cursor's pre-processor (left stick, cross, right stick), then
// the game viewport and Enhanced Input (Options, circle, L1 / R1, touchpad). The cursor is
// steered onto each button by the left stick, the way a player would. Esc, Tab and I are checked
// too. Logs `AutoTest menu: ... ok` or `... FAIL`, with screenshots 25_menu_* to 29_menu_*.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOCharacterMenu.h"
#include "UI/WWOMenuCursor.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WhatWasOurs.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Widgets/SViewport.h"

namespace
{
	void InjectKey(const FKey& Key, bool bDown)
	{
		FSlateApplication& App = FSlateApplication::Get();
		const FKeyEvent Event(Key, FModifierKeysState(), IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), false, 0, 0);
		if (bDown) App.ProcessKeyDownEvent(Event);
		else App.ProcessKeyUpEvent(Event);
	}

	void InjectAxis(const FKey& Key, float Value)
	{
		const FAnalogInputEvent Event(Key, FModifierKeysState(), IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), false, 0, 0, Value);
		FSlateApplication::Get().ProcessAnalogInputEvent(Event);
	}

	// Whether a widget is really on screen: visible all the way up, and the page its widget
	// switchers show.
	bool IsShown(const UWidget* W)
	{
		for (const UWidget* Child = W; Child; Child = Child->GetParent())
		{
			if (!Child->IsVisible()) return false;
			if (const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(Child->GetParent()))
			{
				if (Switcher->GetActiveWidget() != Child) return false;
			}
		}
		return W->GetCachedGeometry().GetLocalSize().X > 1.f;
	}

	FString ButtonText(const UButton* B)
	{
		const UTextBlock* T = Cast<UTextBlock>(B->GetChildAt(0));
		return T ? T->GetText().ToString() : FString();
	}

	// The on-screen button whose text starts with Prefix, in absolute (desktop) coordinates.
	bool FindButton(UUserWidget* Menu, const FString& Prefix, FVector2D& OutCenter)
	{
		bool bFound = false;
		Menu->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			const UButton* B = Cast<UButton>(W);
			if (bFound || !B || !ButtonText(B).StartsWith(Prefix) || !IsShown(B)) return;
			OutCenter = B->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
			bFound = true;
		});
		return bFound;
	}

	// The first slider on screen in the Specific pane (the right half of the menu).
	USlider* FirstSlider(UUserWidget* Menu)
	{
		USlider* Found = nullptr;
		const float Mid = Menu->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)).X;
		Menu->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (!Found && Cast<USlider>(W) && IsShown(W) && W->GetCachedGeometry().GetAbsolutePosition().X > Mid) Found = Cast<USlider>(W);
		});
		return Found;
	}
}

bool AWWOPlayerController::UpdateAutoMenuTest(AWWOCharacter* P, float DeltaTime)
{
	FSlateApplication& App = FSlateApplication::Get();
	AutoMenuClock += DeltaTime;
	auto Next = [this] { ++AutoMenuStep; AutoMenuClock = 0.f; };
	auto Check = [this](bool bOk, const FString& What)
	{
		UE_LOG(LogWWO, Display, TEXT("AutoTest menu: %s %s"), *What, bOk ? TEXT("ok") : TEXT("FAIL"));
		if (!bOk) ++AutoMenuFails;
	};
	// Steers the cursor with the left stick onto Target; true once it's there.
	auto Steer = [&](const FVector2D& Target) -> bool
	{
		const FVector2D Delta = Target - App.GetCursorPos();
		const float Dist = Delta.Size();
		if (Dist < 4.f)
		{
			InjectAxis(EKeys::Gamepad_LeftX, 0.f);
			InjectAxis(EKeys::Gamepad_LeftY, 0.f);
			return true;
		}
		// Ask for about a third of the way this frame, through the cursor's squared response.
		const TSharedPtr<SViewport> Viewport = GetWorld()->GetGameViewport()->GetGameViewportWidget();
		const FVector2D Size = Viewport.IsValid() ? Viewport->GetTickSpaceGeometry().GetAbsoluteSize() : FVector2D(1600, 900);
		const float FullSpeed = FWWOMenuCursor::Speed * FMath::Min(Size.X / 1600.f, Size.Y / 900.f) * FMath::Max(DeltaTime, 1e-3f);
		const float Strength = FMath::Sqrt(FMath::Min(FMath::Max(Dist * 0.35f, 1.5f) / FullSpeed, 1.f));
		const float Tilt = FWWOMenuCursor::Deadzone + Strength * (1.f - FWWOMenuCursor::Deadzone);
		const FVector2D Dir = Delta / Dist;
		InjectAxis(EKeys::Gamepad_LeftX, Dir.X * Tilt);
		InjectAxis(EKeys::Gamepad_LeftY, -Dir.Y * Tilt); // stick up = screen up
		return false;
	};
	auto Timeout = [&](float Seconds, const TCHAR* What)
	{
		if (AutoMenuClock < Seconds) return false;
		Check(false, FString::Printf(TEXT("%s (timed out)"), What));
		InjectAxis(EKeys::Gamepad_LeftX, 0.f);
		InjectAxis(EKeys::Gamepad_LeftY, 0.f);
		return true;
	};
	// Steer to a button by its text and click it with cross (down, then up a step later).
	auto ClickButton = [&](UUserWidget* Menu, const FString& Prefix) -> bool
	{
		FVector2D At;
		if (!FindButton(Menu, Prefix, At)) return Timeout(1.f, *FString::Printf(TEXT("find button '%s'"), *Prefix));
		if (Steer(At) || Timeout(4.f, *FString::Printf(TEXT("steer to '%s'"), *Prefix)))
		{
			InjectKey(EKeys::Gamepad_FaceButton_Bottom, true);
			return true;
		}
		return false;
	};
	auto Release = [&](const FKey& Key) { InjectKey(Key, false); };
	const FString Grenade = UWWOLoadoutSubsystem::Get(this) && UWWOLoadoutSubsystem::Get(this)->GetGrenade()
		? UWWOLoadoutSubsystem::Get(this)->GetGrenade()->DisplayName : TEXT("?");
	UWWOSettingsMenu* Settings = SettingsMenu;
	UWWOCharacterMenu* Character = CharacterMenu;
	const float Wait = 0.15f; // between presses (real seconds), so each lands on its own frame or two

	switch (AutoMenuStep)
	{
	case 0:
		if (IsMenuOpen()) CloseMenus();
		AutoMenuFails = 0;
		UE_LOG(LogWWO, Display, TEXT("AutoTest menu: start (viewport %s)"), *GetWorld()->GetGameViewport()->GetGameViewportWidget()->GetTickSpaceGeometry().GetAbsoluteSize().ToString());
		InjectKey(EKeys::Gamepad_Special_Right, true); // Options
		Next();
		break;
	case 1: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_Special_Right); Check(Settings->IsOpen(), TEXT("Options opens settings")); Next(); } break;
	// Cursor starts at the viewport's center.
	case 2:
		if (AutoMenuClock > Wait)
		{
			const TSharedPtr<SViewport> V = GetWorld()->GetGameViewport()->GetGameViewportWidget();
			const FVector2D Mid = V->GetTickSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
			Check(FVector2D::Distance(Mid, App.GetCursorPos()) < 3.f, FString::Printf(TEXT("cursor starts centered (%s vs %s)"), *App.GetCursorPos().ToString(), *Mid.ToString()));
			InjectKey(EKeys::Gamepad_RightShoulder, true);
			Next();
		}
		break;
	// R1, R1, L1: Global tabs 0 -> 1 -> 2 -> 1.
	case 3: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_RightShoulder); Next(); } break;
	case 4: if (AutoMenuClock > Wait) { Check(Settings->GetGlobalTab() == 1, TEXT("R1 next Global tab")); InjectKey(EKeys::Gamepad_RightShoulder, true); Next(); } break;
	case 5: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_RightShoulder); Next(); } break;
	case 6: if (AutoMenuClock > Wait) { Check(Settings->GetGlobalTab() == 2, TEXT("R1 again")); InjectKey(EKeys::Gamepad_LeftShoulder, true); Next(); } break;
	case 7: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_LeftShoulder); Next(); } break;
	case 8:
		if (AutoMenuClock > Wait)
		{
			Check(Settings->GetGlobalTab() == 1 && Settings->IsOpen(), TEXT("L1 previous Global tab (menu still open, no grenade / melee)"));
			Shot(TEXT("25_menu_settings_tabs"));
			Next();
		}
		break;
	// Left stick + cross: the Specific pane's Flora tab, then the equipped grenade in its list.
	case 9: if (ClickButton(Settings, TEXT("Flora"))) Next(); break;
	case 10: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
	case 11: if (ClickButton(Settings, Grenade + TEXT("\n"))) Next(); break;
	case 12: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
	case 13:
		if (AutoMenuClock > 0.3f)
		{
			Check(Settings->OpenItemKey().StartsWith(TEXT("grenade/")), FString::Printf(TEXT("stick + cross open a grenade's sliders (%s)"), *Settings->OpenItemKey()));
			Shot(TEXT("26_menu_settings_item"));
			Next();
		}
		break;
	// Drag the first slider: cross held at its left end, stick right, release.
	case 14:
	{
		USlider* S = FirstSlider(Settings);
		if (!S) { if (Timeout(1.f, TEXT("find a slider"))) Next(); break; }
		// Grab the handle where it is (the settings sliders' handle is 16 x 1.2 Slate units).
		AutoMenuValue = S->GetValue();
		const FGeometry& G = S->GetCachedGeometry();
		const float Thumb = 16.f * UWWOSettingsMenu::K;
		const float Pct = FMath::GetRangePct(S->GetMinValue(), S->GetMaxValue(), AutoMenuValue);
		const FVector2D At = G.LocalToAbsolute(FVector2D(Thumb * 0.5f + Pct * (G.GetLocalSize().X - Thumb), G.GetLocalSize().Y * 0.5f));
		if (Steer(At) || Timeout(4.f, TEXT("steer to a slider"))) { InjectKey(EKeys::Gamepad_FaceButton_Bottom, true); Next(); }
		break;
	}
	case 15:
		InjectAxis(EKeys::Gamepad_LeftX, 0.6f);
		if (AutoMenuClock > 0.4f)
		{
			InjectAxis(EKeys::Gamepad_LeftX, 0.f);
			Release(EKeys::Gamepad_FaceButton_Bottom);
			Next();
		}
		break;
	case 16:
		if (AutoMenuClock > Wait)
		{
			USlider* S = FirstSlider(Settings);
			const float Now = S ? S->GetValue() : AutoMenuValue;
			Check(S && Now > AutoMenuValue, FString::Printf(TEXT("cross-drag a slider (%.2f -> %.2f)"), AutoMenuValue, Now));
			Next();
		}
		break;
	case 17: if (ClickButton(Settings, TEXT("Reset item"))) Next(); break;
	case 18: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
	case 19:
		if (AutoMenuClock > Wait)
		{
			USlider* S = FirstSlider(Settings);
			Check(S && FMath::IsNearlyEqual(S->GetValue(), AutoMenuValue, 0.001f), FString::Printf(TEXT("Reset item puts it back (%.2f)"), S ? S->GetValue() : -1.f));
			Next();
		}
		break;
	// Right stick scrolls the area under the cursor: the Global pane's Melee tab.
	case 20: if (ClickButton(Settings, TEXT("Melee"))) Next(); break;
	case 21: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
	case 22:
	{
		FVector2D At;
		if (!FindButton(Settings, TEXT("Reset this tab"), At)) { if (Timeout(1.f, TEXT("find Reset this tab"))) Next(); break; }
		if (Steer(At - FVector2D(0.f, 200.f)) || Timeout(4.f, TEXT("steer over the Global rows"))) Next();
		break;
	}
	case 23:
		InjectAxis(EKeys::Gamepad_RightY, -1.f); // pushed down
		if (AutoMenuClock > 0.5f)
		{
			InjectAxis(EKeys::Gamepad_RightY, 0.f);
			// (Which tab index Melee is moves as tabs are added; clicking it by name above is the check.)
			Check(Settings->GlobalScrollOffset() > 100.f,
				FString::Printf(TEXT("right stick scrolls the Melee tab (tab %d, offset %.0f)"), Settings->GetGlobalTab(), Settings->GlobalScrollOffset()));
			Shot(TEXT("27_menu_settings_scrolled"));
			Next();
		}
		break;
	// Circle backs out of the open item, then closes.
	case 24: InjectKey(EKeys::Gamepad_FaceButton_Right, true); Next(); break;
	case 25: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Right); Next(); } break;
	case 26:
		if (AutoMenuClock > Wait)
		{
			Check(Settings->IsOpen() && Settings->OpenItemKey().IsEmpty(), TEXT("circle backs out of the item"));
			InjectKey(EKeys::Gamepad_FaceButton_Right, true);
			Next();
		}
		break;
	case 27: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Right); Next(); } break;
	case 28:
		if (AutoMenuClock > Wait)
		{
			Check(!Settings->IsOpen() && !IsPaused(), TEXT("circle closes settings and unpauses"));
			Check(P && !P->bIsCrouched, TEXT("closing circle doesn't crouch"));
			InjectKey(EKeys::Tab, true);
			Next();
		}
		break;
	// Keyboard: Tab opens, Esc closes; Esc opens, Esc closes (one action per press).
	// (Each key is released a step before the next goes down: Tab and Esc are one action, which
	// only starts again after it has been let go.)
	case 29: if (AutoMenuClock > Wait) { Release(EKeys::Tab); Check(Settings->IsOpen(), TEXT("Tab opens settings")); Next(); } break;
	case 30: if (AutoMenuClock > Wait) { InjectKey(EKeys::Escape, true); Next(); } break;
	case 31: if (AutoMenuClock > Wait) { Release(EKeys::Escape); Check(!Settings->IsOpen(), TEXT("Esc closes settings")); Next(); } break;
	case 32: if (AutoMenuClock > Wait) { InjectKey(EKeys::Escape, true); Next(); } break;
	case 33: if (AutoMenuClock > Wait) { Release(EKeys::Escape); Check(Settings->IsOpen(), TEXT("Esc opens settings")); Next(); } break;
	case 34: if (AutoMenuClock > Wait) { InjectKey(EKeys::Tab, true); Next(); } break;
	case 35: if (AutoMenuClock > Wait) { Release(EKeys::Tab); Check(!Settings->IsOpen(), TEXT("Tab closes settings")); Next(); } break;
	case 36: if (AutoMenuClock > Wait) { InjectKey(EKeys::Gamepad_Special_Left, true); Next(); } break;
	// Character menu: touchpad opens; stick + cross through the Flora tile and the grenade picker.
	case 37: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_Special_Left); Check(Character->IsOpen() && IsPaused(), TEXT("touchpad opens the character menu")); Next(); } break;
	case 38: if (AutoMenuClock > 0.5f) { Shot(TEXT("28_menu_character")); Next(); } break;
	case 39: if (ClickButton(Character, TEXT("Flora"))) Next(); break;
	case 40: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
	case 41: if (ClickButton(Character, TEXT("Grenade\n"))) Next(); break;
	case 42: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
	case 43:
		if (AutoMenuClock > 0.3f)
		{
			Check(Character->DebugState() == TEXT("expanded 'Flora' picker 'grenade'"), TEXT("stick + cross open the grenade picker (") + Character->DebugState() + TEXT(")"));
			Shot(TEXT("29_menu_character_picker"));
			InjectKey(EKeys::Gamepad_FaceButton_Right, true);
			Next();
		}
		break;
	case 44: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Right); Next(); } break;
	case 45:
		if (AutoMenuClock > Wait)
		{
			Check(Character->DebugState() == TEXT("expanded 'Flora' picker ''"), TEXT("circle closes the picker"));
			InjectKey(EKeys::Gamepad_FaceButton_Right, true);
			Next();
		}
		break;
	case 46: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Right); Next(); } break;
	case 47: if (AutoMenuClock > Wait) { Check(Character->DebugState() == TEXT("expanded '' picker ''") && Character->IsOpen(), TEXT("circle collapses the tile")); Next(); } break;
	case 48: if (ClickButton(Character, TEXT("Weapons"))) Next(); break;
	case 49: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_FaceButton_Bottom); Next(); } break;
	case 50:
		if (AutoMenuClock > 0.3f)
		{
			Check(Character->DebugState().StartsWith(TEXT("expanded 'Weapons'")), TEXT("stick + cross open Weapons"));
			Shot(TEXT("29_menu_character_weapons"));
			InjectKey(EKeys::Gamepad_Special_Left, true);
			Next();
		}
		break;
	case 51: if (AutoMenuClock > Wait) { Release(EKeys::Gamepad_Special_Left); Check(!Character->IsOpen() && !IsPaused(), TEXT("touchpad closes the character menu")); Next(); } break;
	case 52: if (AutoMenuClock > Wait) { InjectKey(EKeys::I, true); Next(); } break;
	case 53: if (AutoMenuClock > Wait) { Release(EKeys::I); Check(Character->IsOpen(), TEXT("I opens the character menu")); InjectKey(EKeys::Escape, true); Next(); } break;
	case 54: if (AutoMenuClock > Wait) { Release(EKeys::Escape); Check(!Character->IsOpen(), TEXT("Esc closes it")); Next(); } break;
	default:
		UE_LOG(LogWWO, Display, TEXT("AutoTest menu: done, %d failed"), AutoMenuFails);
		return true;
	}
	return false;
}
