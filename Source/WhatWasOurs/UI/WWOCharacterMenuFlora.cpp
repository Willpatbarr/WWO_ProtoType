#include "UI/WWOCharacterMenu.h"
#include "UI/WWOCharacterMenuPrivate.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
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
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	auto Row = [Tree]() -> UHorizontalBox* { return Tree->ConstructWidget<UHorizontalBox>(); };
	auto AddToRow = [](UHorizontalBox* R, UWidget* W) { R->AddChildToHorizontalBox(W)->SetPadding(FMargin(R->GetChildrenCount() > 1 ? 12.f * K : 0.f, 0, 0, 0)); };
	auto Flow = [Tree]() -> UWrapBox*
	{
		UWrapBox* W = Tree->ConstructWidget<UWrapBox>();
		W->SetInnerSlotPadding(FVector2D(10.f * K, 10.f * K));
		return W;
	};
	auto Hint = [this](const FString& Text)
	{
		AddContent(WWOUI::Text(WidgetTree, Text, 18.f, FLinearColor(1, 1, 1, 0.6f), true));
	};
	auto Pick = [WeakThis](TFunction<void()> Equip)
	{
		return [WeakThis, Equip] { Equip(); if (WeakThis.IsValid()) { WeakThis->Picker.Reset(); WeakThis->Refresh(); } };
	};

	// Flora
	AddRowLabel(TEXT("Flora"));
	UWWOFloraData* Flora = L->GetFlora();
	const FLinearColor FloraColor = Flora ? Flora->Color : FLinearColor(0.4f, 0.85f, 0.3f);
	FString FloraText = TEXT("Flora\n") + (Flora ? Flora->DisplayName : FString(TEXT("None")));
	if (Flora && !Flora->UltName.IsEmpty()) FloraText += TEXT("\nUlt: ") + Flora->UltName;
	{
		UHorizontalBox* R = Row();
		AddToRow(R, Slot(FloraText, FVector2D(300, 120), WWO::Darkened(FloraColor, 0.6f), 20.f,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("flora")); }, &FloraColor, 3.f));
		AddContent(R);
	}
	if (Picker == TEXT("flora"))
	{
		AddRowLabel(TEXT("Choose a flora (switches your grenade, abilities and ult)"));
		UHorizontalBox* R = Row();
		for (UWWOFloraData* Option : L->Floras)
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = Option == Flora;
			const FLinearColor Edge = bOn ? WWO::Lightened(Option->Color, 0.3f) : Option->Color;
			AddToRow(R, Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\nUlt: ") + Option->UltName, FVector2D(240, 90),
				WWO::Darkened(Option->Color, 0.5f), 20.f, Pick([WL, Option] { if (WL.IsValid()) WL->EquipFlora(Option); }), &Edge, bOn ? 5.f : 2.f, Option->Description));
		}
		AddContent(R);
		if (Flora) Hint(Flora->Description);
	}

	// Abilities: grenade, ability 1, ability 2.
	AddRowLabel(TEXT("Abilities"));
	{
		UHorizontalBox* R = Row();
		if (const UWWOGrenadeData* G = L->GetGrenade())
		{
			AddToRow(R, Slot(TEXT("Grenade\n") + G->DisplayName, FVector2D(220, 110), WWO::Darkened(G->Color, 0.6f), 20.f,
				[WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("grenade")); }, &G->Color, 3.f));
		}
		for (int32 SlotIndex = 0; SlotIndex < UWWOLoadoutSubsystem::AbilitySlots; ++SlotIndex)
		{
			const UWWOAbilityData* A = L->GetAbility(SlotIndex);
			const FString Title = FString::Printf(TEXT("Ability %d"), SlotIndex + 1);
			if (!A)
			{
				AddToRow(R, Slot(Title, FVector2D(220, 110), FLinearColor(0.24f, 0.25f, 0.3f), 20.f, [] {})); // an empty slot does nothing
				continue;
			}
			const FString Name = FString::Printf(TEXT("ability%d"), SlotIndex);
			AddToRow(R, Slot(Title + TEXT("\n") + A->DisplayName, FVector2D(220, 110), WWO::Darkened(A->Color, 0.6f), 20.f,
				[WeakThis, Name] { if (WeakThis.IsValid()) WeakThis->TogglePicker(Name); }, &A->Color, 3.f));
		}
		AddContent(R);
	}
	if (Picker == TEXT("grenade"))
	{
		AddRowLabel(TEXT("Choose a grenade"));
		UHorizontalBox* R = Row();
		for (UWWOGrenadeData* Option : L->GrenadesForFlora())
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = Option == L->GetGrenade();
			const FString Stats = FString::Printf(TEXT("%s · %d dmg · %s m"), *StaticEnum<EWWOGrenadeKind>()->GetNameStringByValue((int64)Option->Kind),
				FMath::RoundToInt(Option->Damage), *FString::SanitizeFloat(Option->BlastRadius));
			const FLinearColor Edge = bOn ? WWO::Lightened(Option->Color, 0.3f) : Option->Color;
			AddToRow(R, Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Stats, FVector2D(200, 90), WWO::Darkened(Option->Color, 0.45f), 20.f,
				Pick([WL, Option] { if (WL.IsValid()) WL->EquipGrenade(Option); }), &Edge, bOn ? 5.f : 2.f));
		}
		AddContent(R);
	}
	else if (Picker.StartsWith(TEXT("ability")))
	{
		const int32 SlotIndex = FCString::Atoi(*Picker.Mid(7));
		AddRowLabel(FString::Printf(TEXT("Choose ability %d"), SlotIndex + 1));
		UHorizontalBox* R = Row();
		TArray<FString> Lines;
		for (UWWOAbilityData* Option : L->AbilitiesForFlora())
		{
			Lines.Add(FString::Printf(TEXT("%s: %s"), *Option->DisplayName, *Option->Description));
			if (!Option->bUnlocked) continue;
			const int32 In = L->AbilitySlotOf(Option);
			const FString Tag = In == SlotIndex ? TEXT("  ✓") : (In != INDEX_NONE ? FString::Printf(TEXT("  (slot %d)"), In + 1) : FString());
			const FLinearColor Edge = In == SlotIndex ? WWO::Lightened(Option->Color, 0.3f) : Option->Color;
			AddToRow(R, Slot(Option->DisplayName + Tag + TEXT("\n") + KindText(Option->Kind), FVector2D(200, 90), WWO::Darkened(Option->Color, 0.5f), 20.f,
				Pick([WL, Option, SlotIndex] { if (WL.IsValid()) WL->EquipAbility(SlotIndex, Option); }), &Edge, In == SlotIndex ? 5.f : 2.f, Option->Description));
		}
		AddContent(R);
		Hint(FString::Join(Lines, TEXT("\n")));
	}

	// Traits: the slots (equipped ones first), then the picker.
	const TArray<UWWOTraitData*> Equipped = L->EquippedTraits();
	AddRowLabel(FString::Printf(TEXT("Traits (%d of %d)"), Equipped.Num(), UWWOLoadoutSubsystem::TraitSlots));
	UWrapBox* Slots = Flow();
	for (int32 i = 0; i < UWWOLoadoutSubsystem::TraitSlots; ++i)
	{
		const bool bHas = i < Equipped.Num();
		Slots->AddChild(Slot(bHas ? Equipped[i]->DisplayName : TEXT("Empty"), FVector2D(170, 90), bHas ? FLinearColor(0.16f, 0.22f, 0.34f) : WWOUI::ButtonColor, 16.f,
			[WeakThis] { if (WeakThis.IsValid()) WeakThis->TogglePicker(TEXT("traits")); }, bHas ? &TraitEdge : nullptr, 2.f, bHas ? Equipped[i]->Description : FString()));
	}
	AddContent(Slots);
	if (Picker == TEXT("traits"))
	{
		AddRowLabel(FString::Printf(TEXT("Choose traits (click to equip / unequip, up to %d)"), UWWOLoadoutSubsystem::TraitSlots));
		UWrapBox* R = Flow();
		const bool bFull = Equipped.Num() >= UWWOLoadoutSubsystem::TraitSlots;
		const FLinearColor OffEdge(0.4f, 0.42f, 0.5f);
		for (UWWOTraitData* Option : L->TraitsForFlora())
		{
			if (!Option->bUnlocked) continue;
			const bool bOn = L->IsTraitEquipped(Option);
			UWidget* B = Slot(Option->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Option->Description, FVector2D(250, 110),
				bOn ? FLinearColor(0.16f, 0.22f, 0.34f) : FLinearColor(0.2f, 0.21f, 0.26f), 14.f,
				[WeakThis, WL, Option] { if (WL.IsValid()) WL->ToggleTrait(Option); if (WeakThis.IsValid()) WeakThis->Refresh(); },
				bOn ? &TraitEdge : &OffEdge, bOn ? 4.f : 1.f, FString(), true);
			if (bFull && !bOn) B->SetRenderOpacity(0.5f); // every slot is taken
			R->AddChild(B);
		}
		AddContent(R);
	}
}
