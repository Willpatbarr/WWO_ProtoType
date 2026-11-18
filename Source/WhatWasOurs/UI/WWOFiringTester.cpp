#include "UI/WWOFiringTester.h"
#include "UI/WWOSettingsMenu.h"
#include "UI/WWOUI.h"
#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "Player/WWOHealthComponent.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOTesterRules.h"
#include "World/WWOTarget.h"
#include "Net/WWONetSubsystem.h"
#include "Systems/WWOLoadoutSubsystem.h"
#include "Data/WWOData.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

namespace
{
	constexpr float K = WWOUI::K;
	constexpr float StrafeLeg = 0.9f;   // s each way
	constexpr float AimLead = 0.3f;     // s aimed in (after the aim-in itself) before the first shot
	constexpr float FireFor = 1.6f;     // s of firing
	constexpr float AimTail = 0.9f;     // s held on aim after the last shot, to watch it settle

	// The sliders listed for the gun in hand (ids in its Settings section), in groups. An id the
	// section doesn't have is skipped, so this list can name ones only some builds have.
	struct FGroup { const TCHAR* Title; TArray<FName> Ids; };
	const TArray<FGroup>& GunGroups()
	{
		static const TArray<FGroup> Groups = {
			{ TEXT("Aiming in"), { "AdsTime", "Zoom", "ViewmodelFov", "SightHeight", "AdsTilt" } },
			{ TEXT("Recoil: the view (where shots go)"), { "RecoilUp", "RecoilSide", "RecoilKickTime", "RecoilRecovery", "RecoilRecoveryTime", "RecoilShape", "BurstRecoilLater", "HipSpread" } },
			{ TEXT("Kick: the gun model (looks only)"), { "VerticalKick", "HorizontalKick", "KickHold", "KickReturn", "AdsKick", "KickPush", "KickPivotDown", "KickPivotAhead", "KickSide" } },
			{ TEXT("Movement: the gun model"), { "Sway", "GunBob", "AdsGunBob", "StrafeShift", "StrafeRoll", "SlideTilt" } },
			{ TEXT("Firing"), { "FireRate", "SemiAuto", "Burst", "BurstRate" } },
		};
		return Groups;
	}
	const TArray<FName>& CameraIds()
	{
		static const TArray<FName> Ids = { "ReticleDrop", "AdsGunScale", "PunchScale", "BobIntensity", "AdsBob" };
		return Ids;
	}

	void Place(UCanvasPanel* Canvas, UWidget* W, float X0, float Y0, float X1, float Y1)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(W);
		Slot->SetAnchors(FAnchors(X0, Y0, X1, Y1));
		Slot->SetOffsets(FMargin(0.f));
	}
}

UWWOSettingsMenu* UWWOFiringTester::Menu() const
{
	const AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	return PC ? PC->GetSettingsMenu() : nullptr;
}

void UWWOFiringTester::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UCanvasPanel>();
	SetCursor(EMouseCursor::None); // the ring cursor is drawn instead (NativePaint)
}

void UWWOFiringTester::NativeDestruct()
{
	SetViewRect(false);
	if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(GetOwningPlayer())) L->OnChanged.Remove(LoadoutHandle);
	Super::NativeDestruct();
}

int32 UWWOFiringTester::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	// Only when this pane is what the cursor is working (a menu opened over it draws its own).
	if (!WantsCursor() || !PC || PC->IsOtherMenuOpen()) return Layer;
	WWOUI::PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

void UWWOFiringTester::SetViewRect(bool bWindow) const
{
	// The player's view is drawn in the window only: the engine lays a lone player out from its
	// "no split" entry, so that entry is what changes (and goes back to the whole screen after).
	const UWorld* World = GetWorld();
	UGameViewportClient* Client = World ? World->GetGameViewport() : nullptr;
	if (!Client || !Client->SplitscreenInfo.IsValidIndex(ESplitScreenType::None) || Client->SplitscreenInfo[ESplitScreenType::None].PlayerData.Num() == 0) return;
	FPerPlayerSplitscreenData& Data = Client->SplitscreenInfo[ESplitScreenType::None].PlayerData[0];
	Data.SizeX = bWindow ? WinW : 1.f;
	Data.SizeY = bWindow ? WinH : 1.f;
	Data.OriginX = bWindow ? WinX : 0.f;
	Data.OriginY = bWindow ? WinY : 0.f;
	if (ULocalPlayer* LP = GetOwningLocalPlayer())
	{
		LP->Size = FVector2D(Data.SizeX, Data.SizeY);
		LP->Origin = FVector2D(Data.OriginX, Data.OriginY);
	}
}

void UWWOFiringTester::Open()
{
	bOpen = true;
	bControl = false;
	SetViewRect(true);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(GetOwningPlayer()))
	{
		L->OnChanged.Remove(LoadoutHandle);
		LoadoutHandle = L->OnChanged.AddWeakLambda(this, [this] { ShownWeapon = NAME_None; }); // the rows follow the gun in hand (NativeTick)
	}
}

void UWWOFiringTester::SetControl(bool bOn)
{
	if (bControl == bOn) return;
	bControl = bOn;
	if (bOn) StopScript();
	if (ControlLabel) ControlLabel->SetText(FText::FromString(bOn ? TEXT("YOU HAVE CONTROL") : TEXT("TAKE CONTROL")));
	if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer()))
	{
		if (HintLabel) HintLabel->SetText(FText::FromString(bOn
			? FString(TEXT("Tab, Esc or Options returns to the settings"))
			: FString(TEXT("Take control to play it yourself, or use the buttons to watch one action"))));
		PC->RefreshMenuMode();
	}
}

void UWWOFiringTester::StopScript()
{
	Act = EAct::None;
	bStrafe = false;
	if (StrafeLabel) StrafeLabel->SetText(FText::FromString(TEXT("STRAFE")));
	if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer())) PC->ClearScript();
}

void UWWOFiringTester::ResetPlayer()
{
	const AWWOGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AWWOGameMode>() : nullptr;
	const FWWOTesterRules* Rules = GM ? GM->Tester() : nullptr;
	AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	AWWOCharacter* P = PC ? PC->GetWWOCharacter() : nullptr;
	if (!Rules || !P || !Rules->bHomeSet) return;
	P->TeleportTo(Rules->Home.GetLocation(), Rules->Home.Rotator());
	// Level, at the dummy.
	float Yaw = Rules->Home.Rotator().Yaw;
	if (const AActor* Target = Rules->Target.Get()) Yaw = (Target->GetActorLocation() - P->GetActorLocation()).Rotation().Yaw;
	P->SetViewRotation(0.f, Yaw);
	StrafeClock = 0.f;
}

void UWWOFiringTester::Play(EAct What)
{
	if (bControl) SetControl(false);
	AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	if (!PC) return;
	const bool bWasStrafing = bStrafe;
	PC->ClearScript();
	if (!bWasStrafing) ResetPlayer(); // every run starts from the same picture (a strafe in progress carries on)
	Act = What;
	ActClock = 0.f;
	NextPull = 0.f;
}

void UWWOFiringTester::SetStrafe(bool bOn)
{
	if (bControl) SetControl(false);
	bStrafe = bOn;
	StrafeClock = 0.f;
	if (bOn && Act == EAct::None) ResetPlayer();
	if (StrafeLabel) StrafeLabel->SetText(FText::FromString(bOn ? TEXT("STOP STRAFE") : TEXT("STRAFE")));
	if (!bOn) if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer())) PC->ScriptMove(FVector2D::ZeroVector);
}

void UWWOFiringTester::UpdateScript(float DeltaTime)
{
	AWWOPlayerController* PC = Cast<AWWOPlayerController>(GetOwningPlayer());
	AWWOCharacter* P = PC ? PC->GetWWOCharacter() : nullptr;
	if (!PC || !P || bControl) return;
	if (bStrafe)
	{
		// Side to side about the start: half a leg out, then full legs back and forth.
		StrafeClock += DeltaTime;
		const float T = StrafeClock + StrafeLeg * 0.5f;
		const bool bRight = FMath::FloorToInt(T / StrafeLeg) % 2 == 0;
		PC->ScriptMove(FVector2D(bRight ? 1.f : -1.f, 0.f));
	}
	if (Act == EAct::None) return;
	ActClock += DeltaTime;
	const UWWOWeaponData* W = P->Weapon->GetWeaponData();
	const float AimIn = W ? W->AdsTime : 0.2f;
	const bool bAims = Act == EAct::Ads || Act == EAct::AdsFire;
	const bool bFires = Act == EAct::AdsFire || Act == EAct::HipFire;
	const float FireFrom = bAims ? AimIn + AimLead : 0.25f;
	const float FireTo = FireFrom + FireFor;
	const float End = bFires ? FireTo + AimTail : AimIn + 2.5f;
	PC->ScriptHold(EWWOAction::Aim, bAims && ActClock < End);
	const bool bFiring = bFires && ActClock >= FireFrom && ActClock < FireTo;
	PC->ScriptHold(EWWOAction::Fire, bFiring);
	if (bFiring && ActClock >= NextPull)
	{
		// A fresh pull each shot for one-shot-a-pull guns; held between pulls for automatic ones.
		PC->ScriptPress(EWWOAction::Fire);
		const float Gap = W ? 60.f / FMath::Max(W->FireRate, 1.f) : 0.2f;
		NextPull = ActClock + FMath::Max(Gap + 0.03f, 0.12f);
	}
	if (ActClock >= End)
	{
		Act = EAct::None;
		PC->ScriptHold(EWWOAction::Aim, false);
		PC->ScriptHold(EWWOAction::Fire, false);
	}
}

void UWWOFiringTester::StepWeapon(int32 Step)
{
	if (UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(GetOwningPlayer())) L->CycleWeapon(Step);
}

void UWWOFiringTester::StepConfig(int32 Step)
{
	UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(GetOwningPlayer());
	if (!L || L->GetConfigs().Num() == 0) return;
	const int32 Count = L->GetConfigs().Num();
	const int32 Now = L->GetActiveConfig();
	L->ApplyConfig(((Now < 0 ? (Step > 0 ? -1 : 0) : Now) + Step + Count) % Count);
}

void UWWOFiringTester::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bOpen) return;
	UWWOSettingsMenu* S = Menu();
	if (!S || !S->IsPrepared()) return; // its sections exist once the player is in
	if (!bBuilt) Build();
	const UWWOLoadoutSubsystem* L = UWWOLoadoutSubsystem::Get(GetOwningPlayer());
	const UWWOWeaponData* W = L ? L->GetWeapon() : nullptr;
	const int32 Config = L ? L->GetActiveConfig() : -1;
	if (W && (W->Id != ShownWeapon || Config != ShownConfig))
	{
		ShownWeapon = W->Id;
		ShownConfig = Config;
		if (WeaponLabel) WeaponLabel->SetText(FText::FromString(W->DisplayName.ToUpper()));
		if (ConfigLabel) ConfigLabel->SetText(FText::FromString(L->GetConfigs().IsValidIndex(Config) ? L->GetConfigs()[Config].Name : FString(TEXT("No saved config"))));
		BuildRows();
	}
	UpdateScript(InDeltaTime);
}

void UWWOFiringTester::Build()
{
	UWWOSettingsMenu* S = Menu();
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!S || !Canvas) return;
	bBuilt = true;
	UWidgetTree* Tree = WidgetTree;
	TWeakObjectPtr<UWWOFiringTester> Weak = this;
	auto Fill = [Tree](const FLinearColor& Color) { UBorder* B = Tree->ConstructWidget<UBorder>(); B->SetBrushColor(WWO::Lin(Color)); return B; };
	// A filled text button (the settings menu's look); its handler is kept for good (built once).
	auto MakeButton = [this, Tree](const FString& Text, TFunction<void()> Fn, float FontPx, const FLinearColor& Color, const FMargin& Padding)
	{
		UButton* B = WWOUI::TextButton(Tree, Text, FontPx, WWOUI::FilledLook(Color, Padding));
		Handlers.Add(WWOUI::OnClick(B, this, MoveTemp(Fn)));
		return B;
	};

	// Around the window: everything that isn't the player's view is page.
	Place(Canvas, Fill(WWOUI::PageColor), PaneW, 0.f, 1.f, WinY);
	Place(Canvas, Fill(WWOUI::PageColor), PaneW, WinY, WinX, WinY + WinH);
	Place(Canvas, Fill(WWOUI::PageColor), WinX + WinW, WinY, 1.f, WinY + WinH);

	// Left: the pane.
	UBorder* Pane = Fill(WWOUI::PageColor);
	Pane->SetPadding(FMargin(22.f * K, 16.f * K, 14.f * K, 16.f * K));
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
	Pane->SetContent(Column);
	Column->AddChildToVerticalBox(WWOUI::Text(WidgetTree, TEXT("FIRING TESTER"), 26.f));
	auto Picker = [&](const TCHAR* What, TObjectPtr<UTextBlock>& Label, TFunction<void(int32)> Step)
	{
		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>();
		Line->AddChildToHorizontalBox(MakeButton(TEXT("<"), [Step] { Step(-1); }, 20.f, WWOUI::ButtonColor, FMargin(12.f, 4.f)));
		UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
		Words->AddChildToVerticalBox(WWOUI::Text(WidgetTree, What, 13.f, WWOUI::Dim));
		Label = WWOUI::Text(WidgetTree, TEXT("-"), 20.f);
		Words->AddChildToVerticalBox(Label);
		UHorizontalBoxSlot* WordsSlot = Line->AddChildToHorizontalBox(Words);
		WordsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		WordsSlot->SetPadding(FMargin(12.f * K, 0.f));
		WordsSlot->SetVerticalAlignment(VAlign_Center);
		Line->AddChildToHorizontalBox(MakeButton(TEXT(">"), [Step] { Step(1); }, 20.f, WWOUI::ButtonColor, FMargin(12.f, 4.f)));
		Column->AddChildToVerticalBox(Line)->SetPadding(FMargin(0.f, 10.f * K, 8.f * K, 0.f));
	};
	Picker(TEXT("WEAPON"), WeaponLabel, [Weak](int32 Step) { if (Weak.IsValid()) Weak->StepWeapon(Step); });
	Picker(TEXT("SAVED CONFIG"), ConfigLabel, [Weak](int32 Step) { if (Weak.IsValid()) Weak->StepConfig(Step); });
	Rows = Tree->ConstructWidget<UScrollBox>();
	UVerticalBoxSlot* RowsSlot = Column->AddChildToVerticalBox(Rows);
	RowsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RowsSlot->SetPadding(FMargin(0.f, 12.f * K, 0.f, 8.f * K));
	UHorizontalBox* Foot = Tree->ConstructWidget<UHorizontalBox>();
	auto FootButton = [&](const TCHAR* Text, TFunction<void()> Fn)
	{
		UHorizontalBoxSlot* Slot = Foot->AddChildToHorizontalBox(MakeButton(Text, MoveTemp(Fn), 15.f, WWOUI::ButtonColor, FMargin(8.f, 8.f)));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Slot->SetPadding(FMargin(0.f, 0.f, 6.f * K, 0.f));
	};
	FootButton(TEXT("Save as defaults"), [Weak] { if (Weak.IsValid()) if (UWWOSettingsMenu* M = Weak->Menu()) M->SaveDefaults(); });
	FootButton(TEXT("Loadout"), [Weak] { if (Weak.IsValid()) if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(Weak->GetOwningPlayer())) PC->ToggleCharacterMenu(); });
	FootButton(TEXT("All settings"), [Weak] { if (Weak.IsValid()) if (AWWOPlayerController* PC = Cast<AWWOPlayerController>(Weak->GetOwningPlayer())) PC->ToggleSettingsMenu(); });
	FootButton(TEXT("Main menu"), [Weak] { if (Weak.IsValid()) if (UWWONetSubsystem* Net = UWWONetSubsystem::Get(Weak.Get())) Net->LeaveToTitle(); });
	Column->AddChildToVerticalBox(Foot);
	Place(Canvas, Pane, 0.f, 0.f, PaneW, 1.f);

	// Under the window: what the player does.
	UBorder* Strip = Fill(WWOUI::PageColor);
	Strip->SetPadding(FMargin(22.f * K, 14.f * K, 22.f * K, 14.f * K));
	UVerticalBox* StripColumn = Tree->ConstructWidget<UVerticalBox>();
	Strip->SetContent(StripColumn);
	UHorizontalBox* Buttons = Tree->ConstructWidget<UHorizontalBox>();
	auto Action = [&](const TCHAR* Text, TFunction<void()> Fn, const FLinearColor& Color, TObjectPtr<UTextBlock>* LabelOut = nullptr)
	{
		UButton* B = MakeButton(Text, MoveTemp(Fn), 19.f, Color, FMargin(10.f, 16.f));
		if (LabelOut) *LabelOut = Cast<UTextBlock>(B->GetChildAt(0));
		UHorizontalBoxSlot* Slot = Buttons->AddChildToHorizontalBox(B);
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Slot->SetPadding(FMargin(0.f, 0.f, 10.f * K, 0.f));
	};
	Action(TEXT("TAKE CONTROL"), [Weak] { if (Weak.IsValid()) Weak->SetControl(true); }, FLinearColor(0.2f, 0.34f, 0.5f), &ControlLabel);
	Action(TEXT("ADS"), [Weak] { if (Weak.IsValid()) Weak->Play(EAct::Ads); }, WWOUI::ButtonColor);
	Action(TEXT("ADS + FIRE"), [Weak] { if (Weak.IsValid()) Weak->Play(EAct::AdsFire); }, WWOUI::ButtonColor);
	Action(TEXT("HIP FIRE"), [Weak] { if (Weak.IsValid()) Weak->Play(EAct::HipFire); }, WWOUI::ButtonColor);
	Action(TEXT("STRAFE"), [Weak] { if (Weak.IsValid()) Weak->SetStrafe(!Weak->IsStrafing()); }, WWOUI::ButtonColor, &StrafeLabel);
	Action(TEXT("RESET"), [Weak] { if (Weak.IsValid()) { Weak->StopScript(); Weak->ResetPlayer(); } }, WWOUI::ButtonColor);
	StripColumn->AddChildToVerticalBox(Buttons);
	HintLabel = WWOUI::Text(WidgetTree, TEXT("Take control to play it yourself, or use the buttons to watch one action"), 15.f, WWOUI::Dim);
	StripColumn->AddChildToVerticalBox(HintLabel)->SetPadding(FMargin(0.f, 12.f * K, 0.f, 0.f));
	Place(Canvas, Strip, PaneW, WinY + WinH, 1.f, 1.f);
}

void UWWOFiringTester::BuildRows()
{
	UWWOSettingsMenu* S = Menu();
	if (!S || !Rows) return;
	Rows->ClearChildren();
	auto Add = [this](UWidget* W, float Top)
	{
		if (UScrollBoxSlot* Slot = Cast<UScrollBoxSlot>(Rows->AddChild(W))) Slot->SetPadding(FMargin(0.f, Top * K, 16.f * K, 0.f));
	};
	const FString GunKey = TEXT("weapon/") + ShownWeapon.ToString();
	for (const FGroup& Group : GunGroups())
	{
		bool bHeader = false;
		for (const FName& Id : Group.Ids)
		{
			UWidget* Row = S->MakeRowFor(GunKey, Id);
			if (!Row) continue;
			if (!bHeader) { Add(WWOUI::Text(WidgetTree, FString(Group.Title).ToUpper(), 14.f, WWOUI::Dim), 16.f); bHeader = true; }
			Add(Row, 8.f);
		}
	}
	bool bHeader = false;
	for (const FName& Id : CameraIds())
	{
		UWidget* Row = S->MakeRowFor(TEXT("global/Camera"), Id);
		if (!Row) continue;
		if (!bHeader) { Add(WWOUI::Text(WidgetTree, TEXT("EVERY GUN"), 14.f, WWOUI::Dim), 16.f); bHeader = true; }
		Add(Row, 8.f);
	}
}
