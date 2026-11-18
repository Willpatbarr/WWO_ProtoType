#include "UI/WWOCharacterMenu.h"
#include "UI/WWOCharacterMenuPrivate.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

using namespace WWOCharacterMenuUI;

void UWWOCharacterMenu::BuildFrameRows()
{
	// The whole Shell tab on one screen, no scrolling: a row of cards per slot, then the net effect.
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	// Each slot's parts as a row of cards on the editors' grid (ItemCard, AddCardGrid), under its name.
	for (const FName& FrameSlot : UWWOLoadoutSubsystem::FrameSlots())
	{
		AddRowLabel(FrameSlot.ToString());
		TArray<UWidget*> Cards;
		for (UWWOFramePartData* Part : L->FramePartsFor(FrameSlot))
		{
			if (!Part->bUnlocked) continue;
			FItemCard C;
			C.Title = Part->DisplayName;
			C.Sub = Part->Summary().Replace(TEXT("\n"), TEXT(" · "));
			C.bOn = Part == L->GetFramePart(FrameSlot);
			C.MinHeight = 64.f;
			Cards.Add(ItemCard(C, [WeakThis, WL, Part] { if (WL.IsValid()) WL->EquipFramePart(Part); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
		}
		AddCardGrid(Cards, 4);
	}
	AddRowLabel(TEXT("Net effect"));
	const TArray<FString> Lines = L->GroupSummary();
	UTextBlock* Net = WWOUI::Text(WidgetTree, Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("No changes (all Standard parts)"), WWOUI::Type::Body, WWOUI::TextPrimary);
	Content->AddChildToVerticalBox(Net)->SetPadding(FMargin(0, 8.f * K, 0, 0));
}
