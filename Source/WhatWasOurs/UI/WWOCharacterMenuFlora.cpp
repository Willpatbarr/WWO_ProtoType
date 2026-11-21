#include "UI/WWOCharacterMenu.h"
#include "UI/WWOCharacterMenuPrivate.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"

using namespace WWOCharacterMenuUI;

namespace
{
	FString KindText(EWWOAbilityKind Kind)
	{
		switch (Kind)
		{
		case EWWOAbilityKind::Active: return TEXT("Active");
		case EWWOAbilityKind::Triggered: return TEXT("Triggered");
		case EWWOAbilityKind::Charged: return TEXT("Charged");
		default: return TEXT("Passive");
		}
	}
}

void UWWOCharacterMenu::BuildFloraRows()
{
	// Every row is cards on one grid (ItemCard, AddCardGrid): each item's badge at the card's left, its
	// name and a line of detail beside it. Three columns for the flora, grenade and abilities (and their
	// pickers), four for the traits.
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	constexpr int32 Wide = 3, Narrow = 4;
	auto Hint = [this](const FString& Text)
	{
		if (Text.IsEmpty()) return;
		UTextBlock* T = WWOUI::Text(WidgetTree, Text, WWOUI::Type::Small, WWOUI::TextSecondary, true);
		Content->AddChildToVerticalBox(T)->SetPadding(FMargin(0, 10.f * K, 0, 0));
	};
	auto Pick = [WeakThis](TFunction<void()> Equip)
	{
		return [WeakThis, Equip] { Equip(); if (WeakThis.IsValid()) { WeakThis->Picker.Reset(); WeakThis->Refresh(); } };
	};
	auto Toggle = [WeakThis](const FString& Name) { return [WeakThis, Name] { if (WeakThis.IsValid()) WeakThis->TogglePicker(Name); }; };
	// The card's edge while its picker is open: as if chosen, so you see which slot you are choosing for.
	auto Open = [this](const FString& Name) { return Picker == Name; };

	// Flora
	UWWOFloraData* Flora = L->GetFlora();
	AddRowLabel(TEXT("Flora"));
	{
		FItemCard C;
		C.Kind = "Flora";
		C.Id = Flora ? Flora->Id : NAME_None;
		C.Color = Flora ? Flora->Color : FLinearColor(0.4f, 0.85f, 0.3f);
		C.Title = Flora ? Flora->DisplayName : FString(TEXT("None"));
		C.Sub = Flora && !Flora->UltName.IsEmpty() ? TEXT("Ult: ") + Flora->UltName : FString(TEXT("Flora"));
		C.bOn = Open(TEXT("flora"));
		C.bMark = false;
		C.Tip = Flora ? Flora->Description : FString();
		AddCardGrid({ ItemCard(C, Toggle(TEXT("flora"))) }, Wide);
	}
	if (Picker == TEXT("flora"))
	{
		AddRowLabel(TEXT("Choose a flora (switches your grenade, abilities and ult)"));
		TArray<UWidget*> Cards;
		for (UWWOFloraData* Option : L->Catalog.Floras)
		{
			if (!Option->bUnlocked) continue;
			FItemCard C;
			C.Kind = "Flora";
			C.Id = Option->Id;
			C.Color = Option->Color;
			C.Title = Option->DisplayName;
			C.Sub = TEXT("Ult: ") + Option->UltName;
			C.bOn = Option == Flora;
			C.Tip = Option->Description;
			Cards.Add(ItemCard(C, Pick([WL, Option] { if (WL.IsValid()) WL->EquipFlora(Option); })));
		}
		AddCardGrid(Cards, Wide);
		if (Flora) Hint(Flora->Description);
	}

	// Abilities: grenade, ability 1, ability 2.
	AddRowLabel(TEXT("Abilities"));
	{
		TArray<UWidget*> Cards;
		if (const UWWOGrenadeData* G = L->GetGrenade())
		{
			FItemCard C;
			C.Kind = "Grenade";
			C.Id = G->Id;
			C.Color = G->Color;
			C.Title = G->DisplayName;
			C.Sub = TEXT("Grenade");
			C.bOn = Open(TEXT("grenade"));
			C.bMark = false;
			Cards.Add(ItemCard(C, Toggle(TEXT("grenade"))));
		}
		for (int32 SlotIndex = 0; SlotIndex < UWWOLoadoutSubsystem::AbilitySlots; ++SlotIndex)
		{
			const UWWOAbilityData* A = L->GetAbility(SlotIndex);
			const FString Name = FString::Printf(TEXT("ability%d"), SlotIndex);
			FItemCard C;
			C.Sub = FString::Printf(TEXT("Ability %d"), SlotIndex + 1);
			if (A)
			{
				C.Kind = "Ability";
				C.Id = A->Id;
				C.Color = A->Color;
				C.Title = A->DisplayName;
				C.Sub += TEXT(" · ") + KindText(A->Kind);
				C.bOn = Open(Name);
				C.bMark = false;
				C.Tip = A->Description;
				Cards.Add(ItemCard(C, Toggle(Name)));
			}
			else
			{
				C.Title = TEXT("Empty");
				// An empty slot has nothing to choose: like a click on the page, it closes an open picker.
				Cards.Add(ItemCard(C, [WeakThis] { if (WeakThis.IsValid() && !WeakThis->Picker.IsEmpty()) WeakThis->TogglePicker(WeakThis->Picker); }));
			}
		}
		AddCardGrid(Cards, Wide);
	}
	if (Picker == TEXT("grenade"))
	{
		AddRowLabel(TEXT("Choose a grenade"));
		TArray<UWidget*> Cards;
		for (UWWOGrenadeData* Option : L->GrenadesForFlora())
		{
			if (!Option->bUnlocked) continue;
			FItemCard C;
			C.Kind = "Grenade";
			C.Id = Option->Id;
			C.Color = Option->Color;
			C.Title = Option->DisplayName;
			C.Sub = FString::Printf(TEXT("%s · %d dmg · %s m"), *StaticEnum<EWWOGrenadeKind>()->GetNameStringByValue((int64)Option->Kind),
				FMath::RoundToInt(Option->Damage), *FString::SanitizeFloat(Option->BlastRadius));
			C.bOn = Option == L->GetGrenade();
			Cards.Add(ItemCard(C, Pick([WL, Option] { if (WL.IsValid()) WL->EquipGrenade(Option); })));
		}
		AddCardGrid(Cards, Wide);
	}
	else if (Picker.StartsWith(TEXT("ability")))
	{
		const int32 SlotIndex = FCString::Atoi(*Picker.Mid(7));
		AddRowLabel(FString::Printf(TEXT("Choose ability %d"), SlotIndex + 1));
		TArray<UWidget*> Cards;
		for (UWWOAbilityData* Option : L->AbilitiesForFlora())
		{
			if (!Option->bUnlocked) continue;
			const int32 In = L->AbilitySlotOf(Option);
			FItemCard C;
			C.Kind = "Ability";
			C.Id = Option->Id;
			C.Color = Option->Color;
			C.Title = Option->DisplayName;
			C.Sub = KindText(Option->Kind) + (In != INDEX_NONE && In != SlotIndex ? FString::Printf(TEXT(" · in slot %d"), In + 1) : FString());
			C.bOn = In == SlotIndex;
			C.Tip = Option->Description;
			Cards.Add(ItemCard(C, Pick([WL, Option, SlotIndex] { if (WL.IsValid()) WL->EquipAbility(SlotIndex, Option); })));
		}
		AddCardGrid(Cards, Wide);
		// The one in this slot (each choice's own is its tip).
		if (const UWWOAbilityData* Current = L->GetAbility(SlotIndex)) Hint(FString::Printf(TEXT("%s: %s"), *Current->DisplayName, *Current->Description));
	}

	// Traits: the slots (equipped ones first), then the picker.
	const TArray<UWWOTraitData*> Equipped = L->EquippedTraits();
	const FLinearColor TraitColor = Flora ? Flora->Color : TraitEdge; // a trait's badge is on its flora's colour
	AddRowLabel(FString::Printf(TEXT("Traits (%d of %d)"), Equipped.Num(), UWWOLoadoutSubsystem::TraitSlots));
	{
		TArray<UWidget*> Cards;
		for (int32 i = 0; i < UWWOLoadoutSubsystem::TraitSlots; ++i)
		{
			FItemCard C;
			C.Badge = 40.f;
			C.MinHeight = 60.f;
			C.bOn = Open(TEXT("traits"));
			C.bMark = false;
			if (i < Equipped.Num())
			{
				C.Kind = "Trait";
				C.Id = Equipped[i]->Id;
				C.Color = TraitColor;
				C.Title = Equipped[i]->DisplayName;
				C.Tip = Equipped[i]->Description;
			}
			else
			{
				C.Title = TEXT("Empty");
				C.Sub = TEXT("Choose a trait");
			}
			Cards.Add(ItemCard(C, Toggle(TEXT("traits"))));
		}
		AddCardGrid(Cards, Narrow);
	}
	if (Picker == TEXT("traits"))
	{
		AddRowLabel(FString::Printf(TEXT("Choose traits (click to equip or unequip, up to %d)"), UWWOLoadoutSubsystem::TraitSlots));
		TArray<UWidget*> Cards;
		const bool bFull = Equipped.Num() >= UWWOLoadoutSubsystem::TraitSlots;
		for (UWWOTraitData* Option : L->TraitsForFlora())
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = L->IsTraitEquipped(Option);
			FItemCard C;
			C.Kind = "Trait";
			C.Id = Option->Id;
			C.Color = TraitColor;
			C.Title = Option->DisplayName;
			C.Sub = Option->Description;
			C.bOn = bOn;
			C.Badge = 40.f;
			C.MinHeight = 60.f;
			UWidget* B = ItemCard(C, [WeakThis, WL, Option] { if (WL.IsValid()) WL->ToggleTrait(Option); if (WeakThis.IsValid()) WeakThis->Refresh(); });
			if (bFull && !bOn) B->SetRenderOpacity(0.5f); // every slot is taken
			Cards.Add(B);
		}
		AddCardGrid(Cards, Wide);
	}
}
