#include "UI/WWOCharacterMenu.h"
#include "UI/WWOCharacterMenuPrivate.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

using namespace WWOCharacterMenuUI;

void UWWOCharacterMenu::BuildFrameRows()
{
	// The whole Shell tab on one screen, no scrolling: a row per slot, its name at the left and its
	// parts sharing the rest of the width, then the net effect.
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	const FLinearColor OnEdge(0.9f, 0.9f, 0.95f);
	for (const FName& FrameSlot : UWWOLoadoutSubsystem::FrameSlots())
	{
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
		USizeBox* Name = Tree->ConstructWidget<USizeBox>();
		Name->SetWidthOverride(74.f * K);
		Name->AddChild(WWOUI::Spaced(WidgetTree, FrameSlot.ToString(), 13.f, FLinearColor(1, 1, 1, 0.6f), 220));
		Row->AddChildToHorizontalBox(Name)->SetVerticalAlignment(VAlign_Center);
		for (UWWOFramePartData* Part : L->FramePartsFor(FrameSlot))
		{
			if (!Part->bUnlocked) continue;
			const bool bOn = Part == L->GetFramePart(FrameSlot);
			UHorizontalBoxSlot* PartSlot = Row->AddChildToHorizontalBox(Slot(Part->DisplayName + (bOn ? TEXT("  ✓") : TEXT("")) + TEXT("\n") + Part->Summary(), FVector2D(140, 104), WWOUI::CardColor, 15.f,
				[WeakThis, WL, Part] { if (WL.IsValid()) WL->EquipFramePart(Part); if (WeakThis.IsValid()) WeakThis->Refresh(); }, bOn ? &OnEdge : nullptr, 4.f));
			PartSlot->SetSize(WWOUI::Fill(1.f));
			PartSlot->SetPadding(FMargin(10.f * K, 0, 0, 0));
		}
		AddContent(Row);
	}
	AddRowLabel(TEXT("Net effect"));
	const TArray<FString> Lines = L->GroupSummary();
	AddContent(WWOUI::Text(WidgetTree, Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("No changes (all Standard parts)"), 16.f));
}
