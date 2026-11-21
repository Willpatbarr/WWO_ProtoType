// -WWOAutoTestHover: everything in the character menu that hovering shows stays on screen and can be
// clicked, with a lot of saved weapon configs and long names. In memory only: the configs it makes
// never reach Loadout.sav (UWWOLoadoutSubsystem::AutoTestSetConfigs; WWOTest's guard as well).
// For 3, 12 and 40 saved configs (every third with a long name; -WWOHoverCounts=150 or any list for others),
// on the Loadout tab:
//   - each hover tile (flora, grenade, abilities, traits, both weapon slots, the four shell parts):
//     the mouse moves off, then onto it; its row of choices opens, the row and every choice lie
//     inside the window, no two choices overlap (shots 90_hover_<n>_<tile>: weapon slots always,
//     every tile at 40; past 40 the weapon rows scroll);
//   - weapon slot 1 (and slot 2 from 40 on): the mouse walks from the slot to the first choice, wheels the
//     row down if it scrolls, walks to the last choice (the row must stay open all the way) and
//     clicks it; the slot then holds that config (90_hover_<n>_weapon0_last).
// Then the Weapon tab: it fits, the config list lies inside the window, each mod slot's list opens
// inside it when clicked (91_hover_<n>_weapon_tab, 91_hover_mods_<slot>); at 40 the config list is wheeled to its
// end (91_hover_<n>_configs_end). Lines `AutoTest hover: ... ok|FAIL`.

#include "Player/WWOAutoTest.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOTestRun.h"
#include "WWOSwitches.h"
#include "Engine/Engine.h"
#include "UI/WWOCharacterMenu.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WhatWasOurs.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"

namespace
{
	int32 HoverStage = 0;
	float HoverClock = 0.f;
	int32 HoverCount = 0;   // index into Counts
	int32 HoverTile = 0;    // index into the tile names
	int32 HoverWalk = 0;    // frames into a walk
	int32 HoverWalkSlot = 0;
	int32 HoverWheels = 0;
	FVector2D HoverFrom = FVector2D::ZeroVector;
	TArray<FString> HoverTiles;
	// -WWOHoverCounts=a,b,... for others: 150 is more than rows as wide as the window hold, so the row scrolls.
	TArray<int32> Counts = { 3, 12, 40 };
	constexpr int32 AllTilesAt = 40; // every tile's shot at this count (the weapon slots' at every count)

	FSlateApplication& App() { return FSlateApplication::Get(); }

	void MoveMouse(const FVector2D& To)
	{
		const FVector2D From = App().GetCursorPos();
		App().SetCursorPos(To);
		const FPointerEvent Event(App().GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, To, From, App().GetPressedMouseButtons(),
			EKeys::Invalid, 0, App().GetPlatformApplication()->GetModifierKeys());
		App().ProcessMouseMoveEvent(Event);
	}

	void Click(bool bDown)
	{
		const FVector2D Pos = App().GetCursorPos();
		TSet<FKey> Pressed = App().GetPressedMouseButtons();
		if (bDown) Pressed.Add(EKeys::LeftMouseButton);
		else Pressed.Remove(EKeys::LeftMouseButton);
		const FPointerEvent Event(App().GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Pos, Pos, Pressed,
			EKeys::LeftMouseButton, 0, App().GetPlatformApplication()->GetModifierKeys());
		if (bDown) App().ProcessMouseButtonDownEvent(App().GetPlatformApplication()->GetWindowUnderCursor(), Event);
		else App().ProcessMouseButtonUpEvent(Event);
	}

	void Wheel(float Notches)
	{
		const FVector2D Pos = App().GetCursorPos();
		const FPointerEvent Event(App().GetUserIndexForMouse(), FSlateApplication::CursorPointerIndex, Pos, Pos, App().GetPressedMouseButtons(),
			EKeys::MouseWheelAxis, Notches, App().GetPlatformApplication()->GetModifierKeys());
		App().ProcessMouseWheelOrGestureEvent(Event, nullptr);
	}

	bool Inside(const FSlateRect& R, const FSlateRect& In) { return R.Left >= In.Left - 0.5f && R.Top >= In.Top - 0.5f && R.Right <= In.Right + 0.5f && R.Bottom <= In.Bottom + 0.5f; }
	bool Overlap(const FSlateRect& A, const FSlateRect& B) { return A.Left < B.Right - 0.5f && B.Left < A.Right - 0.5f && A.Top < B.Bottom - 0.5f && B.Top < A.Bottom - 0.5f; }
	FString RectText(const FSlateRect& R) { return FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), R.Left, R.Top, R.Right, R.Bottom); }

	// N configs across the guns, every third with a long name.
	TArray<FWWOModConfig> MakeConfigs(const UWWOLoadoutSubsystem& L, int32 N)
	{
		TArray<FWWOModConfig> Out;
		TArray<const UWWOWeaponData*> Guns;
		for (const UWWOWeaponData* W : L.Catalog.Weapons) if (W && W->bUnlocked) Guns.Add(W);
		for (int32 i = 0; i < N && Guns.Num() > 0; ++i)
		{
			FWWOModConfig C;
			C.Weapon = Guns[i % Guns.Num()]->Id;
			C.Name = i % 3 == 2 ? FString::Printf(TEXT("Config %d long range marksman build for the north bridge"), i + 1) : FString::Printf(TEXT("Config %d"), i + 1);
			// The first mod of each slot for this frame on every other one, so the summaries vary.
			if (i % 2 == 1)
			{
				for (const UWWOModData* Mod : L.Catalog.WeaponMods)
				{
					if (Mod && Mod->Frame == C.Weapon && !C.Mods.Contains(Mod->Slot)) C.Mods.Add(Mod->Slot, Mod->Id);
				}
			}
			Out.Add(C);
		}
		return Out;
	}
}

void FWWOAutoTest::UpdateAutoHoverTest(float DeltaTime)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(PC);
	UWWOCharacterMenu* Menu = PC->CharacterMenu;
	if (!L || !PC->GetPawn() || !Menu || HoverStage < 0) return;
	HoverClock += DeltaTime;
	auto Next = [](int32 Stage) { HoverStage = Stage; HoverClock = 0.f; };
	const TSharedPtr<SViewport> ViewportWidget = PC->GetWorld()->GetGameViewport()->GetGameViewportWidget();
	if (!ViewportWidget.IsValid()) return;
	const FGeometry& VG = ViewportWidget->GetTickSpaceGeometry();
	const FSlateRect View = FSlateRect::FromPointAndExtent(VG.GetAbsolutePosition(), VG.GetAbsoluteSize());
	const FVector2D OffEverything = FVector2D(View.Left + 4.f, View.Top + 4.f); // the window's corner: no tile there
	const int32 N = Counts[FMath::Min(HoverCount, Counts.Num() - 1)];
	auto Check = [](bool bOk, const FString& What) { return WWOTest::Check(TEXT("hover"), bOk, What); };
	// The open row and each choice in it: on screen, apart. (A choice scrolled out of a scrolling row is
	// only checked to lie across the row's width.)
	auto CheckRow = [&](const FString& Tile)
	{
		FSlateRect Panel;
		TArray<FSlateRect> Choices;
		if (!Check(Menu->AutoTestHoverRects(Panel, Choices) && Menu->AutoTestHoverRow() == Tile, FString::Printf(TEXT("%d configs: hovering %s opens its row (open: '%s')"), N, *Tile, *Menu->AutoTestHoverRow()))) return;
		Check(Inside(Panel, View), FString::Printf(TEXT("%d configs: %s's row %s lies inside the window %s"), N, *Tile, *RectText(Panel), *RectText(View)));
		int32 Off = 0, Apart = 0;
		FString First;
		for (int32 i = 0; i < Choices.Num(); ++i)
		{
			// In a scrolling row, a choice never scrolled into view has not been laid out yet (no size):
			// the walk below wheels the row to its last choice and clicks it.
			const bool bShown = Inside(Choices[i], Panel);
			const bool bLaidOut = Choices[i].GetSize().X > 0.5f;
			const bool bOk = bShown ? Inside(Choices[i], View)
				: Menu->AutoTestHoverScrolls() && (!bLaidOut || (Choices[i].Left >= Panel.Left - 0.5f && Choices[i].Right <= Panel.Right + 0.5f));
			if (!bOk) { ++Off; if (First.IsEmpty()) First = FString::Printf(TEXT(" (choice %d at %s)"), i + 1, *RectText(Choices[i])); }
			for (int32 j = i + 1; j < Choices.Num(); ++j) if (bShown && Inside(Choices[j], Panel) && Overlap(Choices[i], Choices[j])) ++Apart;
		}
		Check(Off == 0, FString::Printf(TEXT("%d configs: all %d of %s's choices on screen or in its scrolling row, %d off%s"), N, Choices.Num(), *Tile, Off, *First));
		Check(Apart == 0, FString::Printf(TEXT("%d configs: %s's choices don't overlap (%d pairs do)"), N, *Tile, Apart));
		// Rows alone hold up to 40 configs; only past that does a weapon slot's row scroll.
		if (Tile.StartsWith(TEXT("weapon"))) Check(Menu->AutoTestHoverScrolls() == (N > AllTilesAt), FString::Printf(TEXT("%d configs: %s's row %s"), N, *Tile, Menu->AutoTestHoverScrolls() ? TEXT("scrolls") : TEXT("fits without scrolling")));
	};

	switch (HoverStage)
	{
	case 0:
		if (HoverClock < 8.f) break;
		{
			FString Asked;
			TArray<FString> Parts;
			if (WWOSwitches::Value(TEXT("WWOHoverCounts="), Asked, false) && Asked.ParseIntoArray(Parts, TEXT(",")) > 0)
			{
				Counts.Reset();
				for (const FString& Part : Parts) Counts.Add(FMath::Max(1, FCString::Atoi(*Part)));
			}
		}
		// Something in every tile: a weapon with mods, both abilities, four traits.
		L->AutoTestSetConfigs(TArray<FWWOModConfig>()); // from here on nothing is written to the saved file
		for (UWWOWeaponData* W : L->Catalog.Weapons) if (W->Id == "rifle") L->EquipWeapon(W);
		for (int32 Slot = 0; Slot < UWWOLoadoutSubsystem::AbilitySlots; ++Slot)
		{
			if (L->GetAbility(Slot)) continue;
			for (UWWOAbilityData* A : L->AbilitiesForFlora()) if (A->bUnlocked && L->AbilitySlotOf(A) == INDEX_NONE && !L->GetAbility(Slot)) L->EquipAbility(Slot, A);
		}
		for (UWWOTraitData* T : L->TraitsForFlora()) if (T->bUnlocked && !L->IsTraitEquipped(T) && L->EquippedTraits().Num() < UWWOLoadoutSubsystem::TraitSlots) L->ToggleTrait(T);
		Next(1);
		break;
	// A count of configs: put them in, open the Loadout tab.
	case 1:
		L->AutoTestSetConfigs(MakeConfigs(*L, N));
		if (!Menu->IsOpen()) PC->ToggleCharacterMenu();
		Menu->AutoTestShow(TEXT(""), FString()); // the Loadout tab, the cursor off every tile
		UE_LOG(LogWWO, Display, TEXT("AutoTest hover: %d saved configs, window %s"), N, *RectText(View));
		HoverTile = 0;
		Next(2);
		break;
	case 2:
		if (HoverClock < 1.f) break;
		HoverTiles = Menu->AutoTestHoverTiles();
		Check(HoverTiles.Num() >= 11, FString::Printf(TEXT("%d configs: the Loadout tab has its hover tiles (%d)"), N, HoverTiles.Num()));
		if (N == Counts[0]) Shot(TEXT("90_hover_loadout"));
		Next(3);
		break;
	// Each tile: the mouse off everything (the last row closes), then onto the tile.
	case 3:
		if (HoverClock < 0.15f) break;
		if (!HoverTiles.IsValidIndex(HoverTile)) { HoverWalkSlot = 0; Next(10); break; }
		MoveMouse(OffEverything);
		Next(4);
		break;
	case 4:
		if (HoverClock < 0.5f) break;
		Check(Menu->AutoTestHoverRow().IsEmpty(), FString::Printf(TEXT("%d configs: the row closes with the mouse off it (open: '%s')"), N, *Menu->AutoTestHoverRow()));
		{
			FSlateRect Tile;
			if (Menu->AutoTestTileRect(HoverTiles[HoverTile], Tile)) MoveMouse(Tile.GetCenter());
		}
		Next(5);
		break;
	case 5:
		if (HoverClock < 0.6f) break;
		CheckRow(HoverTiles[HoverTile]);
		if (HoverTiles[HoverTile].StartsWith(TEXT("weapon")) || N == AllTilesAt) Shot(*FString::Printf(TEXT("90_hover_%d_%s"), N, *HoverTiles[HoverTile]));
		++HoverTile;
		Next(3);
		break;
	// A weapon slot: walk from it to the first choice, wheel to the end if the row scrolls, walk to the
	// last choice, click it.
	case 10:
		MoveMouse(OffEverything);
		Next(11);
		break;
	case 11:
		if (HoverClock < 0.5f) break;
		{
			FSlateRect Tile;
			if (Menu->AutoTestTileRect(FString::Printf(TEXT("weapon%d"), HoverWalkSlot), Tile)) MoveMouse(Tile.GetCenter());
		}
		Next(12);
		break;
	case 12:
		if (HoverClock < 0.6f) break;
		// -WWOHoverGC: a full garbage collection here, after the last count's click rebuilt the gun models.
		// It reproduced a crash this test found (2026-11-18): the Metal RHI's "Resource is being deleted" in
		// the base pass, when a GC destroyed a material parameter collection instance the world had made
		// again after a body model was unloaded and loaded back (UWWOLoadoutSubsystem::KeepBodyModelsLoaded).
		// -WWOHoverCounts=3,3,3 -WWOHoverGC crashed in most runs before that fix.
		if (WWOSwitches::Has(TEXT("WWOHoverGC"))) { UE_LOG(LogWWO, Display, TEXT("AutoTest hover: forcing a garbage collection")); GEngine->ForceGarbageCollection(true); }
		HoverFrom = App().GetCursorPos();
		HoverWalk = 0;
		HoverWheels = 0;
		Next(13);
		break;
	case 13:
	case 15:
	{
		// 24 frames from where the mouse is to the first (13) or last (15) choice; the row must stay open.
		const FString Want = FString::Printf(TEXT("weapon%d"), HoverWalkSlot);
		FSlateRect Panel;
		TArray<FSlateRect> Choices;
		if (!Menu->AutoTestHoverRects(Panel, Choices) || Choices.Num() == 0 || Menu->AutoTestHoverRow() != Want)
		{
			Check(false, FString::Printf(TEXT("%d configs: %s's row stays open while the mouse goes to its %s choice (closed, %d frames in; open: '%s')"),
				N, *Want, HoverStage == 13 ? TEXT("first") : TEXT("last"), HoverWalk, *Menu->AutoTestHoverRow()));
			Next(18);
			break;
		}
		const FVector2D To = (HoverStage == 13 ? Choices[0] : Choices.Last()).GetCenter();
		++HoverWalk;
		MoveMouse(FMath::Lerp(HoverFrom, To, FMath::Min(HoverWalk / 24.f, 1.f)));
		if (HoverWalk < 26) break;
		if (HoverStage == 15)
		{
			Check(true, FString::Printf(TEXT("%d configs: %s's row stays open from the slot to its last choice"), N, *Want));
			Next(16);
			break;
		}
		HoverFrom = App().GetCursorPos();
		HoverWalk = 0;
		Next(14);
		break;
	}
	case 14:
	{
		// On the row: wheel down until the last choice shows (a row that fits has nothing to wheel).
		FSlateRect Panel;
		TArray<FSlateRect> Choices;
		if (!Menu->AutoTestHoverRects(Panel, Choices) || Choices.Num() == 0) { Next(18); break; }
		if (Inside(Choices.Last(), Panel) || HoverWheels >= 400)
		{
			Check(Inside(Choices.Last(), Panel) && Inside(Choices.Last(), View), FString::Printf(TEXT("%d configs: the last choice can be brought into view (%d wheel notches, at %s)"), N, HoverWheels, *RectText(Choices.Last())));
			HoverFrom = App().GetCursorPos();
			HoverWalk = 0;
			Next(15);
			break;
		}
		Wheel(-1.f);
		++HoverWheels;
		break;
	}
	case 16:
		if (HoverClock < 0.8f) break; // long enough for the choice's tip to show
		Shot(*FString::Printf(TEXT("90_hover_%d_weapon%d_last"), N, HoverWalkSlot));
		Next(17);
		break;
	case 17:
		if (HoverClock < 0.3f) break;
		if (HoverClock < 0.4f) { Click(true); HoverClock = 0.4f; break; }
		Click(false);
		Next(18);
		break;
	case 18:
		if (HoverClock < 0.4f) break;
		Check(L->GetSlotConfig(HoverWalkSlot) == L->GetConfigs().Num() - 1, FString::Printf(TEXT("%d configs: clicking the last choice puts config %d in weapon slot %d (it holds %d)"),
			N, L->GetConfigs().Num(), HoverWalkSlot + 1, L->GetSlotConfig(HoverWalkSlot) + 1));
		if (HoverWalkSlot == 0 && N >= AllTilesAt) { HoverWalkSlot = 1; Next(10); break; }
		Next(20);
		break;
	// The Weapon tab with these configs.
	case 20:
		MoveMouse(OffEverything);
		Menu->AutoTestShow(TEXT("Weapons"), FString());
		Next(21);
		break;
	case 21:
		if (HoverClock < 1.2f) break;
		{
			Check(Menu->ScrollOverflow() < 1.f, FString::Printf(TEXT("%d configs: the Weapon tab fits on one screen (%.0f to scroll)"), N, Menu->ScrollOverflow()));
			FSlateRect List;
			Check(Menu->AutoTestConfigListRect(List) && Inside(List, View), FString::Printf(TEXT("%d configs: the config list %s lies inside the window"), N, *RectText(List)));
			Shot(*FString::Printf(TEXT("91_hover_%d_weapon_tab"), N));
			if (N >= AllTilesAt) MoveMouse(List.GetCenter());
		}
		HoverTile = 0;
		HoverWheels = 0;
		Next(N >= AllTilesAt ? 22 : 24);
		break;
	case 22:
		// The mouse wheel takes the long list to its end.
		if (HoverWheels < 400) { Wheel(-3.f); ++HoverWheels; break; }
		Next(23);
		break;
	case 23:
		if (HoverClock < 0.4f) break;
		Shot(*FString::Printf(TEXT("91_hover_%d_configs_end"), N));
		Next(24);
		break;
	// Each mod slot (at the first count only: they don't change with the configs): its list of mods.
	case 24:
		if (HoverClock < 0.3f) break;
		{
			FSlateRect Panel;
			TArray<FSlateRect> Slots;
			Menu->AutoTestModRects(Panel, Slots);
			if (N != Counts[0] || !Slots.IsValidIndex(HoverTile)) { Next(30); break; }
			MoveMouse(Slots[HoverTile].GetCenter());
		}
		Next(26);
		break;
	case 26: // a slot opens on a click
		if (HoverClock < 0.15f) break;
		Click(true);
		Next(27);
		break;
	case 27:
		if (HoverClock < 0.05f) break;
		Click(false);
		Next(25);
		break;
	case 25:
		if (HoverClock < 0.6f) break;
		{
			FSlateRect Panel;
			TArray<FSlateRect> Slots;
			const bool bOpen = Menu->AutoTestModRects(Panel, Slots);
			const FString SlotName = UWWOLoadoutSubsystem::ModSlots().IsValidIndex(HoverTile) ? UWWOLoadoutSubsystem::ModSlots()[HoverTile].ToString() : FString(TEXT("?"));
			Check(bOpen && Inside(Panel, View), FString::Printf(TEXT("clicking the %s slot opens its mods inside the window (%s)"), *SlotName, bOpen ? *RectText(Panel) : TEXT("not open")));
			Check(Menu->ScrollOverflow() < 1.f, FString::Printf(TEXT("the Weapon tab with the %s mods open fits (%.0f to scroll)"), *SlotName, Menu->ScrollOverflow()));
			Shot(*FString::Printf(TEXT("91_hover_mods_%s"), *SlotName));
		}
		++HoverTile;
		Next(24);
		break;
	// Next count, or done.
	case 30:
		MoveMouse(OffEverything);
		Menu->AutoTestShow(TEXT(""), FString());
		if (++HoverCount < Counts.Num()) { Next(1); break; }
		Next(31);
		break;
	case 31:
		if (HoverClock < 0.5f) break;
		if (Menu->IsOpen()) PC->ToggleCharacterMenu();
		UE_LOG(LogWWO, Display, TEXT("AutoTest hover: done"));
		WWOTest::Exit();
		HoverStage = -1;
		break;
	default: break;
	}
}
