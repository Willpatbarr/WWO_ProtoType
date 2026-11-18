#include "UI/WWOSettingsMenu.h"
#include "UI/WWOSettingsMenuPrivate.h"
#include "Player/WWOPlayerController.h"
#include "WWOTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"

using namespace WWOSettingsUI;

// --- Slider rows -------------------------------------------------------------------------------

FString UWWOSettingsMenu::ValueText(const FWWOSettingRow& Row, float V)
{
	if (Row.Show) return Row.Show(V);
	return Row.Step < 0.1f ? FString::Printf(TEXT("%.2f"), V) : FString::Printf(TEXT("%.1f"), V);
}

UWidget* UWWOSettingsMenu::MakeRow(const FString& Key, int32 RowIndex)
{
	// Two lines, so every slider gets the panel's full width: [label, fills] [value, right-aligned]
	// in the theme's 22 px font, then the slider (28 px tall) underneath. (Godot had them on one line:
	// label 270 px, slider, value.)
	const FWWOSettingRow& Row = Find(Key)->Rows[RowIndex];
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(MakeText(Row.Label, 22.f));
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	Root->AddChildToVerticalBox(Line);

	// The slider: Godot's theme draws a 6 px track (0.3, 0.32, 0.37) filled up to the grabber
	// (0.55, 0.6, 0.7), with the default round grabber. A progress bar under the slider draws
	// the track and the fill; the slider itself draws only the grabber.
	const float Thumb = 16.f * K;
	UOverlay* SliderBox = WidgetTree->ConstructWidget<UOverlay>();
	UProgressBar* Fill = WidgetTree->ConstructWidget<UProgressBar>();
	FProgressBarStyle BarStyle;
	BarStyle.SetBackgroundImage(WWOUI::RoundBrush(FLinearColor(0.3f, 0.32f, 0.37f), 3.f));
	BarStyle.SetFillImage(WWOUI::RoundBrush(FLinearColor(0.55f, 0.6f, 0.7f), 3.f));
	BarStyle.SetMarqueeImage(WWOUI::RoundBrush(FLinearColor(0.55f, 0.6f, 0.7f), 3.f));
	Fill->SetWidgetStyle(BarStyle);
	Fill->SetFillColorAndOpacity(FLinearColor::White);
	Fill->SetPercent(FMath::GetRangePct(Row.Min, Row.Max, Row.Get()));
	USizeBox* FillBox = WidgetTree->ConstructWidget<USizeBox>();
	FillBox->SetHeightOverride(6.f * K);
	FillBox->AddChild(Fill);
	UOverlaySlot* FillSlot = SliderBox->AddChildToOverlay(FillBox);
	FillSlot->SetHorizontalAlignment(HAlign_Fill);
	FillSlot->SetVerticalAlignment(VAlign_Center);
	FillSlot->SetPadding(FMargin(Thumb * 0.5f, 0.f)); // the grabber's center travels from end to end of the fill

	USlider* Slider = WidgetTree->ConstructWidget<USlider>();
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Slider->IsFocusable = false;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	Slider->SetMinValue(Row.Min);
	Slider->SetMaxValue(Row.Max);
	Slider->SetStepSize(Row.Step);
	Slider->SetValue(Row.Get());
	FSliderStyle Style = Slider->GetWidgetStyle();
	FSlateBrush NoBar;
	NoBar.DrawAs = ESlateBrushDrawType::NoDrawType;
	FSlateBrush Grabber = WWOUI::RoundBrush(FLinearColor(0.88f, 0.88f, 0.9f), 8.f);
	Grabber.ImageSize = FVector2D(Thumb, Thumb);
	FSlateBrush GrabberHover = WWOUI::RoundBrush(FLinearColor(1.f, 1.f, 1.f), 8.f);
	GrabberHover.ImageSize = Grabber.ImageSize;
	Style.SetNormalBarImage(NoBar).SetHoveredBarImage(NoBar).SetDisabledBarImage(NoBar);
	Style.SetNormalThumbImage(Grabber).SetHoveredThumbImage(GrabberHover).SetDisabledThumbImage(Grabber);
	Slider->SetWidgetStyle(Style);
	Slider->SetSliderBarColor(FLinearColor::White);
	Slider->SetSliderHandleColor(FLinearColor::White);
	USizeBox* SliderHeight = WidgetTree->ConstructWidget<USizeBox>();
	SliderHeight->SetHeightOverride(28.f * K);
	SliderHeight->AddChild(Slider);
	UOverlaySlot* SliderSlot = SliderBox->AddChildToOverlay(SliderHeight);
	SliderSlot->SetHorizontalAlignment(HAlign_Fill);
	SliderSlot->SetVerticalAlignment(VAlign_Center);
	Root->AddChildToVerticalBox(SliderBox)->SetPadding(FMargin(0.f, 2.f * K, 0.f, 0.f)); // the whole width, under the label

	USizeBox* ValueBox = WidgetTree->ConstructWidget<USizeBox>();
	ValueBox->SetMinDesiredWidth((Row.Show ? 170.f : 80.f) * K);
	UTextBlock* Value = MakeText(ValueText(Row, Row.Get()), 22.f);
	Value->SetJustification(ETextJustify::Right);
	ValueBox->AddChild(Value);
	UHorizontalBoxSlot* ValueSlot = Line->AddChildToHorizontalBox(ValueBox);
	ValueSlot->SetVerticalAlignment(VAlign_Center);
	ValueSlot->SetPadding(FMargin(12.f * K, 0.f, 0.f, 0.f));

	UWWOSliderHandler* H = NewObject<UWWOSliderHandler>(this);
	TWeakObjectPtr<UTextBlock> WeakValue = Value;
	TWeakObjectPtr<UProgressBar> WeakFill = Fill;
	const FWWOSettingRow RowCopy = Row;
	H->Fn = [RowCopy, WeakValue, WeakFill](float V)
	{
		if (RowCopy.Step > 0.f) V = RowCopy.Min + FMath::RoundToFloat((V - RowCopy.Min) / RowCopy.Step) * RowCopy.Step; // snap to the step
		V = FMath::Clamp(V, RowCopy.Min, RowCopy.Max);
		RowCopy.Set(V);
		if (WeakValue.IsValid()) WeakValue->SetText(FText::FromString(ValueText(RowCopy, V)));
		if (WeakFill.IsValid()) WeakFill->SetPercent(FMath::GetRangePct(RowCopy.Min, RowCopy.Max, V));
	};
	Slider->OnValueChanged.AddDynamic(H, &UWWOSliderHandler::Handle);
	H->For = Slider;
	KeepHandler(H);
	Live.Add({ Slider, Value, Fill, Key, RowIndex });
	return Root;
}

UWidget* UWWOSettingsMenu::MakeRowFor(const FString& Key, FName Id)
{
	const FWWOSettingSection* S = Find(Key);
	if (!S) return nullptr;
	const int32 Index = S->Rows.IndexOfByPredicate([Id](const FWWOSettingRow& Row) { return Row.Header.IsEmpty() && Row.Id == Id; });
	return Index == INDEX_NONE ? nullptr : MakeRow(Key, Index);
}

void UWWOSettingsMenu::BuildRows(UPanelWidget* Container, const FString& Key)
{
	// Godot: a 12 px margin around the rows, 8 px between them; headers in 18 px at 55%.
	FWWOSettingSection* S = Find(Key);
	if (!S) return;
	auto Add = [Container](UWidget* W, bool bFirst, bool bLeft)
	{
		const FMargin Pad(12.f * K, (bFirst ? 12.f : 8.f) * K, 12.f * K, 0.f);
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(Container->AddChild(W)))
		{
			Slot->SetPadding(Pad);
			if (bLeft) Slot->SetHorizontalAlignment(HAlign_Left);
		}
	};
	// Each header with sliders under it is a collapsible group: click it to open (+) or close (-). The first
	// group of a section starts open, the rest closed; what you open or close stays that way while
	// the game runs. A header with no sliders under it (a weapon's archetype) stays a plain label.
	// In someone else's game: only your own preferences, and the headers that still have some under them.
	TArray<int32> Kept;
	for (int32 i = 0; i < S->Rows.Num(); ++i)
	{
		const FWWOSettingRow& R = S->Rows[i];
		bool bKeep = Allowed(R);
		if (!bKeep && !R.Header.IsEmpty())
		{
			for (int32 j = i + 1; j < S->Rows.Num() && S->Rows[j].Header.IsEmpty(); ++j) bKeep |= S->Rows[j].bPref;
		}
		if (bKeep) Kept.Add(i);
	}
	int32 Count = 0;
	UVerticalBox* Group = nullptr;
	for (int32 k = 0; k < Kept.Num(); ++k, ++Count)
	{
		const int32 i = Kept[k];
		const FWWOSettingRow& R = S->Rows[i];
		if (R.Header.IsEmpty())
		{
			UWidget* Row = MakeRow(Key, i);
			if (Group) Group->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 8.f * K, 0.f, 0.f));
			else Add(Row, Count == 0, false);
			continue;
		}
		const bool bHasRows = Kept.IsValidIndex(k + 1) && S->Rows[Kept[k + 1]].Header.IsEmpty();
		if (R.LiveHeader)
		{
			UTextBlock* Text = MakeText(R.LiveHeader(), 20.f, FLinearColor(1, 1, 1, 0.9f), true);
			LiveTexts.Add({ Text, R.LiveHeader });
			Add(Text, Count == 0, false);
			Group = nullptr;
			continue;
		}
		if (!bHasRows)
		{
			Add(MakeText(R.Header, 18.f, FLinearColor(1, 1, 1, 0.55f), true), Count == 0, false);
			Group = nullptr;
			continue;
		}
		const FString GroupKey = Key + TEXT("|") + R.Header;
		const bool bFirst = Group == nullptr && !FirstGroupSeen.Contains(Key);
		FirstGroupSeen.Add(Key);
		if (bFirst && !GroupState.Contains(GroupKey)) GroupState.Add(GroupKey, true);
		const bool bGroupOpen = GroupState.FindRef(GroupKey);
		Group = WidgetTree->ConstructWidget<UVerticalBox>();
		Group->SetVisibility(bGroupOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		TWeakObjectPtr<UVerticalBox> WeakGroup = Group;
		TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
		const FString Title = R.Header;
		TSharedRef<TWeakObjectPtr<UButton>> Self = MakeShared<TWeakObjectPtr<UButton>>();
		UButton* Toggle = MakeButton(this, (bGroupOpen ? TEXT("-  ") : TEXT("+  ")) + Title, [WeakThis, WeakGroup, GroupKey, Title, Self]
		{
			if (!WeakThis.IsValid() || !WeakGroup.IsValid()) return;
			const bool bNow = !WeakThis->GroupState.FindRef(GroupKey);
			WeakThis->GroupState.Add(GroupKey, bNow);
			WeakGroup->SetVisibility(bNow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			if (UTextBlock* Label = Self->IsValid() ? Cast<UTextBlock>(Self->Get()->GetChildAt(0)) : nullptr)
			{
				Label->SetText(FText::FromString((bNow ? TEXT("-  ") : TEXT("+  ")) + Title));
			}
		}, 18.f, FLinearColor(0.15f, 0.16f, 0.19f));
		*Self = Toggle;
		Add(Toggle, Count == 0, true);
		Add(Group, false, false);
	}
	for (const FWWOMenuButton& Button : S->Buttons)
	{
		if (bRestricted && !Button.bPref) continue;
		Add(MakeButton(this, Button.Text, Button.Fn, 22.f), Count++ == 0, true);
	}
	if (UScrollBoxSlot* Last = Container->GetChildrenCount() > 0 ? Cast<UScrollBoxSlot>(Container->GetChildAt(Container->GetChildrenCount() - 1)->Slot) : nullptr)
	{
		FMargin Pad = Last->GetPadding();
		Pad.Bottom = 12.f * K;
		Last->SetPadding(Pad);
	}
}

void UWWOSettingsMenu::SyncSliders()
{
	// Push current values back into whatever sliders are on screen (after a reset).
	Live.RemoveAll([](const FLiveSlider& L) { return !L.Slider.IsValid(); });
	for (const FLiveSlider& L : Live)
	{
		const FWWOSettingSection* S = Find(L.Key);
		if (!S || !S->Rows.IsValidIndex(L.Row)) continue;
		const FWWOSettingRow& Row = S->Rows[L.Row];
		const float V = Row.Get();
		L.Slider->SetValue(V);
		if (L.Value.IsValid()) L.Value->SetText(FText::FromString(ValueText(Row, V)));
		if (L.Fill.IsValid()) L.Fill->SetPercent(FMath::GetRangePct(Row.Min, Row.Max, V));
	}
}

// --- Item browsers (list of items <-> one item's sliders) ------------------------------------

UWidget* UWWOSettingsMenu::MakeBrowser(TFunction<TArray<FWWOBrowserItem>()> Provider)
{
	const int32 Index = Browsers.AddDefaulted();
	FWWOBrowser& B = Browsers[Index];
	B.Provider = MoveTemp(Provider);
	B.Switcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	B.List = WWOUI::ScrollBox(WidgetTree);
	B.Switcher->AddChild(B.List);
	UVerticalBox* Detail = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>();
	TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
	Bar->AddChildToHorizontalBox(MakeButton(this, TEXT("‹ Back"), [WeakThis, Index] { if (WeakThis.IsValid()) WeakThis->ShowList(Index); }, 22.f));
	B.Title = MakeText(TEXT(""), 26.f);
	UHorizontalBoxSlot* TitleSlot = Bar->AddChildToHorizontalBox(B.Title);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetPadding(FMargin(12.f * K, 0.f));
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	Bar->AddChildToHorizontalBox(MakeButton(this, TEXT("Reset item"), [WeakThis, Index]
	{
		if (WeakThis.IsValid()) WeakThis->ResetSection(WeakThis->Browsers[Index].OpenKey);
	}, 22.f));
	Detail->AddChildToVerticalBox(Bar);
	B.Rows = WWOUI::ScrollBox(WidgetTree);
	Detail->AddChildToVerticalBox(B.Rows)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	B.Switcher->AddChild(Detail);
	ShowList(Index);
	return B.Switcher;
}

void UWWOSettingsMenu::ShowList(int32 BrowserIndex)
{
	FWWOBrowser& B = Browsers[BrowserIndex];
	B.List->ClearChildren();
	TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
	for (const FWWOBrowserItem& Item : B.Provider())
	{
		const FString Text = Item.Title + (Item.Subtitle.IsEmpty() ? FString() : TEXT("\n") + Item.Subtitle);
		const FString Key = Item.Key;
		// Godot: left-aligned, at least 72 px tall, 20 px side padding, 10 px apart, 12 px page margin.
		UButton* Button = MakeButton(this, Text, [WeakThis, BrowserIndex, Key] { if (WeakThis.IsValid()) WeakThis->OpenItem(BrowserIndex, Key); },
			22.f, FLinearColor(0.18f, 0.19f, 0.23f), true, FMargin(20.f, 5.f));
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetMinDesiredHeight(72.f * K);
		Box->AddChild(Button);
		const bool bFirst = B.List->GetChildrenCount() == 0;
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(B.List->AddChild(Box))) Slot->SetPadding(FMargin(12.f * K, (bFirst ? 12.f : 10.f) * K, 12.f * K, 0.f));
	}
	B.Switcher->SetActiveWidgetIndex(0);
}

void UWWOSettingsMenu::OpenItem(int32 BrowserIndex, const FString& Key)
{
	FWWOBrowser& B = Browsers[BrowserIndex];
	const FWWOSettingSection* S = Find(Key);
	if (!S) return;
	B.OpenKey = Key;
	B.Title->SetText(FText::FromString(S->Title));
	B.Rows->ClearChildren();
	BuildRows(B.Rows, Key);
	B.Rows->ScrollToStart();
	B.Switcher->SetActiveWidgetIndex(1);
}

// --- Key bindings ------------------------------------------------------------------------------

void UWWOSettingsMenu::BuildBinds()
{
	// One line per thing you do: its name, its keyboard / mouse key, its controller button. Click
	// (or Cross) a key, then press the new one; Esc leaves it as it was. A key already used on
	// another line changes places with it.
	AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	if (!BindsList || !PC) return;
	BindsList->ClearChildren();
	const FLinearColor Accent = WWOUI::Accent(this);
	TWeakObjectPtr<UWWOSettingsMenu> WeakThis = this;
	TWeakObjectPtr<AWWOPlayerController> WeakPC = PC;
	const float KeyWidth = 300.f;
	auto Line = [&](UWidget* Name, UWidget* Kbm, UWidget* Pad, float Top)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(WWOUI::Fill(1.f));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		for (UWidget* W : { Kbm, Pad })
		{
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
			Box->SetWidthOverride(KeyWidth * K);
			Box->AddChild(W);
			UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Box);
			S->SetPadding(FMargin(16.f * K, 0, 0, 0));
			S->SetVerticalAlignment(VAlign_Center);
		}
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(BindsList->AddChild(Row))) Slot->SetPadding(FMargin(12.f * K, Top * K, 16.f * K, 0));
	};
	Line(WWOUI::Spaced(WidgetTree, TEXT("Action"), 12.f, FLinearColor(1, 1, 1, 0.55f), 220), WWOUI::Spaced(WidgetTree, TEXT("Keyboard / mouse"), 12.f, FLinearColor(1, 1, 1, 0.55f), 220),
		WWOUI::Spaced(WidgetTree, TEXT("Controller"), 12.f, FLinearColor(1, 1, 1, 0.55f), 220), 12.f);
	for (const FWWOBindRow& R : AWWOPlayerController::BindRows())
	{
		auto Key = [&](bool bPad) -> UWidget*
		{
			const bool bHas = (bPad ? R.PadKey : R.KbmKey).IsValid();
			if (!bHas) return MakeText(bPad && R.MoveDir >= 0 ? TEXT("Left stick") : TEXT("—"), 18.f, FLinearColor(1, 1, 1, 0.4f));
			const bool bWaiting = PC->IsCapturing(R.Id, bPad);
			const FName Id = R.Id;
			const bool bChanged = PC->GetBind(R.Id, bPad) != (bPad ? R.PadKey : R.KbmKey);
			const FLinearColor Edge = bWaiting ? Accent : (bChanged ? FLinearColor(1.f, 0.82f, 0.2f, 0.9f) : WWOUI::CardEdge); // yours, not the default: gold
			UButton* B = WWOUI::TextButton(WidgetTree, bWaiting ? FString(TEXT("Press a key  ·  Esc cancels")) : AWWOPlayerController::KeyName(PC->GetBind(R.Id, bPad)),
				18.f, KeyLook(bWaiting ? WWO::WithAlpha(Accent, 0.18f) : WWOUI::CardColor, Edge, bWaiting ? 2.f : 1.5f, FMargin(10.f, 7.f)));
			KeepHandler(WWOUI::OnClick(B, this, [WeakThis, WeakPC, Id, bPad]
			{
				if (!WeakThis.IsValid() || !WeakPC.IsValid()) return;
				WeakPC->BeginKeyCapture(Id, bPad, [WeakThis] { if (WeakThis.IsValid()) WeakThis->BuildBinds(); });
				WeakThis->BuildBinds(); // shows which one is waiting
			}));
			return B;
		};
		Line(MakeText(R.Label, 20.f), Key(false), Key(true), 8.f);
	}
	UTextBlock* Note = MakeText(TEXT("Click a key, then press the new one (Esc leaves it). A key already on another line swaps with it. Gold = changed from the default. ")
		TEXT("The menu's own keys stay as they are: Tab / Esc / Options open and close, Q / E and the bumpers change tabs, Circle goes back."), 15.f, FLinearColor(1, 1, 1, 0.5f), true);
	if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(BindsList->AddChild(Note))) Slot->SetPadding(FMargin(12.f * K, 22.f * K, 16.f * K, 16.f * K));
}
