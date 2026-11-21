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
#include "Components/Spacer.h"
#include "WWOTypes.h"

using namespace WWOCharacterMenuUI;

void UWWOCharacterMenu::BuildFrameRows()
{
	// The whole Shell tab on one screen, no scrolling: a row of cards per slot, then the net effect.
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(this);
	if (!L) return;
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	// Section names a little closer to what they name than on the other tabs: the parts' pictures make the cards
	// taller and the tab must still fit.
	auto Label = [this](const FString& Text)
	{
		AddRowLabel(Text);
		if (Content->GetChildrenCount() > 1) Cast<UVerticalBoxSlot>(Content->GetChildAt(Content->GetChildrenCount() - 1)->Slot)->SetPadding(FMargin(0, 14.f * K, 0, 0));
	};
	// Each slot's parts as a row of cards on the editors' grid (ItemCard, AddCardGrid), under its name, each with
	// its picture (AWWOPartPictures: the part alone, in this slot's colours).
	for (const FName& FrameSlot : UWWOLoadoutSubsystem::FrameSlots())
	{
		Label(FrameSlot.ToString());
		TArray<UWidget*> Cards;
		for (UWWOFramePartData* Part : L->FramePartsFor(FrameSlot))
		{
			if (!Part->bUnlocked) continue;
			FItemCard C;
			C.Title = Part->DisplayName;
			C.Sub = Part->Summary(); // (a stat a line: beside the picture there's room for one)
			C.bOn = Part == L->GetFramePart(FrameSlot);
			C.MinHeight = 64.f;
			C.Picture = PartPicture(Part, FrameSlot);
			C.PictureSize = FVector2D(60.f, 40.f);
			Cards.Add(ItemCard(C, [WeakThis, WL, Part] { if (WL.IsValid()) WL->EquipFramePart(Part); if (WeakThis.IsValid()) WeakThis->Refresh(); }));
		}
		AddCardGrid(Cards, 4);
	}
	BuildColourRows(L, Label);
	Label(TEXT("Net effect"));
	const TArray<FString> Lines = L->GroupSummary();
	UTextBlock* Net = WWOUI::Text(WidgetTree, Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("No changes (all Standard parts)"), WWOUI::Type::Body, WWOUI::TextPrimary);
	Content->AddChildToVerticalBox(Net)->SetPadding(FMargin(0, 8.f * K, 0, 0));
}

namespace
{
	// The swatches (sRGB hex): near-black, greys and white, then muted and saturated hues. The glow's are brighter.
	struct FSwatch { const TCHAR* Name; const TCHAR* Hex; };
	const FSwatch PaintSwatches[] = {
		{ TEXT("Near black"), TEXT("1A1B1E") }, { TEXT("Charcoal"), TEXT("3A3C40") }, { TEXT("Grey"), TEXT("74777C") },
		{ TEXT("Light grey"), TEXT("B4B7BB") }, { TEXT("White"), TEXT("E8E9EA") }, { TEXT("Crimson"), TEXT("B3261E") },
		{ TEXT("Rust"), TEXT("C8622A") }, { TEXT("Ochre"), TEXT("D9A93A") }, { TEXT("Olive"), TEXT("6B7436") },
		{ TEXT("Green"), TEXT("2F8F4E") }, { TEXT("Teal"), TEXT("2A8C8C") }, { TEXT("Blue"), TEXT("2D5FB8") },
		{ TEXT("Navy"), TEXT("263A66") }, { TEXT("Violet"), TEXT("6A3F9E") }, { TEXT("Rose"), TEXT("C25A86") },
	};
	const FSwatch GlowSwatches[] = {
		{ TEXT("White"), TEXT("F2F6FF") }, { TEXT("Ice"), TEXT("9FD8FF") }, { TEXT("Cyan"), TEXT("30E0FF") },
		{ TEXT("Blue"), TEXT("3A7BFF") }, { TEXT("Violet"), TEXT("A060FF") }, { TEXT("Magenta"), TEXT("FF40C8") },
		{ TEXT("Pink"), TEXT("FFB0D8") }, { TEXT("Red"), TEXT("FF3B30") }, { TEXT("Orange"), TEXT("FF8A1E") },
		{ TEXT("Amber"), TEXT("FFC21E") }, { TEXT("Yellow"), TEXT("F5F04A") }, { TEXT("Lime"), TEXT("A8FF3C") },
		{ TEXT("Green"), TEXT("3CFF7A") }, { TEXT("Teal"), TEXT("2EF0C0") }, { TEXT("Dim"), TEXT("5A5F66") },
	};
	const TCHAR* const PaintTargets[] = { TEXT("All"), TEXT("Head"), TEXT("Core"), TEXT("Arms"), TEXT("Legs") }; // INDEX_NONE, then EShellSlot's order
	const TCHAR* const PaintRoles[] = { TEXT("Primary"), TEXT("Secondary"), TEXT("Glow") };                     // FWWOEquipped::EShellRole
}

void UWWOCharacterMenu::BuildColourRows(UWWOLoadoutSubsystem* L, TFunctionRef<void(const FString&)> Label)
{
	// Which part (All, Head, Core, Arms, Legs) and which of its colours, on one row; then the swatches and Default. The
	// preview robot follows the loadout, so a click shows at once.
	TWeakObjectPtr<UWWOCharacterMenu> WeakThis = this;
	TWeakObjectPtr<UWWOLoadoutSubsystem> WL = L;
	Label(TEXT("Colours"));
	const FLinearColor Off = WWOUI::CardColor, On = WWO::Lightened(WWOUI::CardColor, 0.12f);
	const FLinearColor DimEdge(1.f, 1.f, 1.f, 0.2f);
	UHorizontalBox* Picks = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto AddPick = [&](const FString& Text, bool bOn, float Width, TFunction<void()> Fn, float Gap)
	{
		UHorizontalBoxSlot* Cell = Picks->AddChildToHorizontalBox(Slot(Text, FVector2D(Width, 34.f), bOn ? On : Off, WWOUI::Type::Small, MoveTemp(Fn),
			bOn ? &OnEdge : &DimEdge, bOn ? WWOUI::BoxEdgeOn : WWOUI::BoxEdge));
		Cell->SetPadding(FMargin(Gap * K, 0, 0, 0));
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(PaintTargets); ++i)
	{
		const int32 Target = i - 1;
		AddPick(PaintTargets[i], PaintTarget == Target, 74.f, [WeakThis, Target] { if (WeakThis.IsValid()) { WeakThis->PaintTarget = Target; WeakThis->Refresh(); } }, i == 0 ? 0.f : 6.f);
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(PaintRoles); ++i)
	{
		AddPick(PaintRoles[i], PaintRole == i, 98.f, [WeakThis, i] { if (WeakThis.IsValid()) { WeakThis->PaintRole = i; WeakThis->Refresh(); } }, i == 0 ? 28.f : 6.f);
	}
	AddContent(Picks);

	// The colour the target has now: the slot's, or with All the one every slot shares (none marked if they differ).
	bool bSame = true;
	const FColor Now = L->GetShellColor(PaintTarget == INDEX_NONE ? 0 : PaintTarget, PaintRole);
	if (PaintTarget == INDEX_NONE) for (int32 S = 1; S < FWWOEquipped::ShellSlots; ++S) bSame &= L->GetShellColor(S, PaintRole) == Now;
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto AddSwatch = [&](const FString& Text, const FLinearColor& Fill, FColor Value, const FString& Tip, float Width)
	{
		const bool bOn = bSame && Value == Now;
		const int32 Target = PaintTarget, Role = PaintRole;
		UWidget* W = Slot(Text, FVector2D(Width, 36.f), Fill, WWOUI::Type::Small,
			[WeakThis, WL, Target, Role, Value] { if (WL.IsValid()) WL->SetShellColor(Target, Role, Value); if (WeakThis.IsValid()) WeakThis->Refresh(); },
			bOn ? &FLinearColor::White : &DimEdge, bOn ? 3.f : WWOUI::BoxEdge, Tip);
		const float Gap = Row->GetChildrenCount() > 0 ? 6.f * K : 0.f;
		Row->AddChildToHorizontalBox(W)->SetPadding(FMargin(Gap, 0, 0, 0));
	};
	AddSwatch(TEXT("Default"), Off, FColor(0, 0, 0, 0), TEXT("The part's own colour"), 84.f);
	const bool bGlow = PaintRole == FWWOEquipped::ShellGlow;
	for (const FSwatch& Sw : bGlow ? TArrayView<const FSwatch>(GlowSwatches) : TArrayView<const FSwatch>(PaintSwatches))
	{
		FColor C = FColor::FromHex(Sw.Hex);
		C.A = 255;
		AddSwatch(FString(), C.ReinterpretAsLinear(), C, FString::Printf(TEXT("%s #%s"), Sw.Name, Sw.Hex), 36.f); // (the menu's colours are sRGB)
	}
	AddContent(Row);
}
