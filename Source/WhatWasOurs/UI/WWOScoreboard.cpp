#include "UI/WWOScoreboard.h"
#include "UI/WWOUI.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOGameState.h"
#include "Game/WWOPlayerState.h"
#include "Net/WWONetSubsystem.h"
#include "Systems/WWOSound.h"
#include "WWOTypes.h"
#include "WhatWasOurs.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

namespace
{
	constexpr float K = WWOUI::K;
}

void UWWOScoreboard::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<UOverlay>();
	SetCursor(EMouseCursor::None); // the ring cursor is drawn instead (NativePaint)
}

int32 UWWOScoreboard::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bOpen) return Layer;
	WWOUI::PaintCursor(this, AllottedGeometry, OutDrawElements, Layer + 1);
	return Layer + 1;
}

bool UWWOScoreboard::CanPick() const
{
	return GetWorld() && GetWorld()->GetNetMode() != NM_Client;
}

void UWWOScoreboard::Open()
{
	bOpen = true;
	bMaps = false;
	bLeaving = false;
	OpenedAt = FPlatformTime::Seconds();
	SetVisibility(ESlateVisibility::Visible);
	Rebuild();
}

void UWWOScoreboard::Close()
{
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UWWOScoreboard::Back()
{
	if (!bMaps || bLeaving) return;
	bMaps = false;
	UWWOSound::PlayFlat(this, "ui_close");
	Rebuild();
}

void UWWOScoreboard::NextMatch()
{
	AWWOGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AWWOGameMode>() : nullptr;
	if (!GM || bLeaving) return;
	bLeaving = true;
	UE_LOG(LogWWO, Display, TEXT("Scoreboard: next match"));
	GM->Restart();
}

void UWWOScoreboard::ShowMaps()
{
	if (!CanPick() || bLeaving) return;
	bMaps = true;
	Rebuild();
}

void UWWOScoreboard::ChooseMap(FName Level)
{
	if (!CanPick() || bLeaving) return;
	bLeaving = true;
	UE_LOG(LogWWO, Display, TEXT("Scoreboard: change map to %s"), *Level.ToString());
	AWWOGameMode::LoadMap(this, Level);
}

void UWWOScoreboard::Quit()
{
	UWWONetSubsystem* Net = UWWONetSubsystem::Get(this);
	if (!Net || bLeaving) return;
	bLeaving = true;
	UE_LOG(LogWWO, Display, TEXT("Scoreboard: quit to the launch screen"));
	Net->LeaveToTitle();
}

FString UWWOScoreboard::Signature() const
{
	const AWWOGameState* GS = GetWorld() ? GetWorld()->GetGameState<AWWOGameState>() : nullptr;
	if (!GS) return FString();
	FString Text = FString::Printf(TEXT("%d|%d|"), (int32)GS->MatchWinner, bMaps ? 1 : 0);
	for (int32 Score : GS->TeamScores) Text += FString::Printf(TEXT("%d,"), Score);
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AWWOPlayerState* P = Cast<AWWOPlayerState>(PS)) Text += FString::Printf(TEXT("%s:%d:%d:%d;"), *P->GetPlayerName(), P->Team, P->Kills, P->PrecisionKills);
	}
	return Text;
}

void UWWOScoreboard::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bOpen && !bLeaving && Signature() != Shown) Rebuild(); // a joiner's last scores can arrive after the page opened
}

UButton* UWWOScoreboard::Button(const FString& Title, const FString& Sub, bool bEnabled, TFunction<void()> Fn)
{
	UWidgetTree* Tree = WidgetTree;
	UVerticalBox* Words = Tree->ConstructWidget<UVerticalBox>();
	Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Title, WWOUI::Type::Title, bEnabled ? FLinearColor::White : WWOUI::Fainter, 220))->SetHorizontalAlignment(HAlign_Center);
	if (!Sub.IsEmpty()) Words->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Sub, WWOUI::Type::Micro, bEnabled ? WWOUI::Dim : WWOUI::Fainter, 90))->SetHorizontalAlignment(HAlign_Center);
	// A card; one this machine can't press (a joiner's NEXT MATCH) is dimmed, the same in every state, and silent.
	WWOUI::FButtonLook Look = bEnabled ? WWOUI::CardLook(WWOUI::CardColor, WWOUI::CardEdge, 1.5f, FMargin(18.f, 13.f))
		: WWOUI::StillLook(WWOUI::RoundBrush(WWOUI::CardColor, WWOUI::Corner, FLinearColor(1, 1, 1, 0.15f), 1.5f), FMargin(18.f, 13.f));
	Look.HAlign = HAlign_Center;
	Look.VAlign = VAlign_Center;
	UButton* B = WWOUI::Button(Tree, Words, Look);
	if (bEnabled) Handlers.Add(WWOUI::OnClick(B, this, MoveTemp(Fn)));
	return B;
}

UWidget* UWWOScoreboard::TeamTable(int32 Team) const
{
	UWidgetTree* Tree = WidgetTree;
	const AWWOGameState* GS = GetWorld()->GetGameState<AWWOGameState>();
	const FLinearColor TeamColor = AWWOPlayerState::TeamColor(Team);
	const APlayerController* PC = GetOwningPlayer();
	const APlayerState* Me = PC ? PC->PlayerState : nullptr;
	TArray<const AWWOPlayerState*> Rows;
	if (GS)
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			if (const AWWOPlayerState* P = Cast<AWWOPlayerState>(PS); P && P->Team == Team) Rows.Add(P);
		}
	}
	Rows.StableSort([](const AWWOPlayerState& A, const AWWOPlayerState& B)
	{
		return A.Kills != B.Kills ? A.Kills > B.Kills : A.PrecisionKills > B.PrecisionKills;
	});
	constexpr float NumberWide = 96.f;
	auto Row = [&](UWidget* Name, UWidget* Kills, UWidget* Precision)
	{
		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* NameSlot = Line->AddChildToHorizontalBox(Name);
		NameSlot->SetSize(WWOUI::Fill());
		NameSlot->SetVerticalAlignment(VAlign_Center);
		for (UWidget* Number : { Kills, Precision })
		{
			UOverlay* Cell = Tree->ConstructWidget<UOverlay>();
			UOverlaySlot* CellSlot = Cell->AddChildToOverlay(Number);
			CellSlot->SetHorizontalAlignment(HAlign_Right);
			CellSlot->SetVerticalAlignment(VAlign_Center);
			Line->AddChildToHorizontalBox(WWOUI::Wide(Tree, Cell, NumberWide))->SetVerticalAlignment(VAlign_Center);
		}
		return Line;
	};
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
	// The team and its score.
	UHorizontalBox* Head = Tree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* HeadName = Head->AddChildToHorizontalBox(WWOUI::Spaced(WidgetTree, AWWOPlayerState::TeamName(Team), WWOUI::Type::Heading, TeamColor, 260));
	HeadName->SetSize(WWOUI::Fill());
	HeadName->SetVerticalAlignment(VAlign_Center);
	Head->AddChildToHorizontalBox(WWOUI::Text(WidgetTree, FString::FromInt(GS && GS->TeamScores.IsValidIndex(Team) ? GS->TeamScores[Team] : 0), WWOUI::Type::Heading, TeamColor))->SetVerticalAlignment(VAlign_Center);
	Column->AddChildToVerticalBox(Head);
	Column->AddChildToVerticalBox(WWOUI::Rule(Tree, WWO::WithAlpha(TeamColor, 0.8f), 2.f))->SetPadding(FMargin(0, 8.f * K, 0, 10.f * K));
	Column->AddChildToVerticalBox(Row(WWOUI::Spaced(WidgetTree, TEXT("Player"), WWOUI::Type::Caption, WWOUI::Dim, 160), WWOUI::Spaced(WidgetTree, TEXT("Kills"), WWOUI::Type::Caption, WWOUI::Dim, 160), WWOUI::Spaced(WidgetTree, TEXT("Precision"), WWOUI::Type::Caption, WWOUI::Dim, 160)))
		->SetPadding(FMargin(0, 0, 0, 6.f * K));
	for (const AWWOPlayerState* P : Rows)
	{
		const bool bMe = P == Me;
		const FLinearColor Ink = bMe ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.82f);
		Column->AddChildToVerticalBox(Row(WWOUI::Text(WidgetTree, P->GetPlayerName() + (bMe ? TEXT("  (you)") : TEXT("")), WWOUI::Type::Title, Ink),
			WWOUI::Text(WidgetTree, FString::FromInt(P->Kills), WWOUI::Type::Title, Ink), WWOUI::Text(WidgetTree, FString::FromInt(P->PrecisionKills), WWOUI::Type::Title, P->PrecisionKills > 0 ? Ink : WWOUI::Fainter)))
			->SetPadding(FMargin(0, 5.f * K));
	}
	if (Rows.Num() == 0) Column->AddChildToVerticalBox(WWOUI::Text(WidgetTree, TEXT("nobody"), WWOUI::Type::Body, WWOUI::Fainter));
	return Column;
}

void UWWOScoreboard::Rebuild()
{
	UOverlay* Root = Cast<UOverlay>(WidgetTree->RootWidget);
	if (!Root) return;
	Root->ClearChildren();
	Handlers.Reset();
	Shown = Signature();
	UWidgetTree* Tree = WidgetTree;
	const AWWOGameState* GS = GetWorld()->GetGameState<AWWOGameState>();
	UBorder* Page = Tree->ConstructWidget<UBorder>();
	Page->SetBrushColor(WWO::Lin(WWO::WithAlpha(WWOUI::PageColor, 0.94f)));
	UOverlaySlot* PageSlot = Root->AddChildToOverlay(Page);
	PageSlot->SetHorizontalAlignment(HAlign_Fill);
	PageSlot->SetVerticalAlignment(VAlign_Fill);

	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>();
	const int32 Winner = GS ? (int32)GS->MatchWinner : -1;
	const int32 Blue = GS && GS->TeamScores.Num() > 0 ? GS->TeamScores[0] : 0;
	const int32 Orange = GS && GS->TeamScores.Num() > 1 ? GS->TeamScores[1] : 0;
	Column->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, TEXT("Match over"), WWOUI::Type::Small, WWOUI::Dim, 300))->SetHorizontalAlignment(HAlign_Center);
	Column->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, Winner < 0 ? FString(TEXT("Draw")) : AWWOPlayerState::TeamName(Winner) + TEXT(" wins"), WWOUI::Type::Display,
		Winner < 0 ? FLinearColor(0.9f, 0.9f, 0.9f) : AWWOPlayerState::TeamColor(Winner), 320))->SetHorizontalAlignment(HAlign_Center);
	Column->AddChildToVerticalBox(WWOUI::Text(WidgetTree, FString::Printf(TEXT("%d  –  %d"), Blue, Orange), WWOUI::Type::Heading, WWOUI::Dim))->SetHorizontalAlignment(HAlign_Center);

	if (!bMaps)
	{
		UHorizontalBox* Teams = Tree->ConstructWidget<UHorizontalBox>();
		Teams->AddChildToHorizontalBox(WWOUI::Wide(Tree, TeamTable(0), 420.f))->SetPadding(FMargin(0, 0, 40.f * K, 0));
		Teams->AddChildToHorizontalBox(WWOUI::Wide(Tree, TeamTable(1), 420.f))->SetPadding(FMargin(40.f * K, 0, 0, 0));
		UVerticalBoxSlot* TeamsSlot = Column->AddChildToVerticalBox(Teams);
		TeamsSlot->SetHorizontalAlignment(HAlign_Center);
		TeamsSlot->SetPadding(FMargin(0, 34.f * K, 0, 38.f * K));
		const bool bHost = CanPick();
		UHorizontalBox* Buttons = Tree->ConstructWidget<UHorizontalBox>();
		TWeakObjectPtr<UWWOScoreboard> Weak(this);
		Buttons->AddChildToHorizontalBox(WWOUI::Wide(Tree, Button(TEXT("Next match"), TEXT("The same map again"), bHost, [Weak] { if (Weak.IsValid()) Weak->NextMatch(); }), 250.f));
		Buttons->AddChildToHorizontalBox(WWOUI::Wide(Tree, Button(TEXT("Change map"), TEXT("Pick another map"), bHost, [Weak] { if (Weak.IsValid()) Weak->ShowMaps(); }), 250.f))->SetPadding(FMargin(18.f * K, 0));
		Buttons->AddChildToHorizontalBox(WWOUI::Wide(Tree, Button(TEXT("Quit"), TEXT("Back to the title"), true, [Weak] { if (Weak.IsValid()) Weak->Quit(); }), 250.f));
		Column->AddChildToVerticalBox(Buttons)->SetHorizontalAlignment(HAlign_Center);
		if (!bHost) Column->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, TEXT("The host picks the next match"), WWOUI::Type::Caption, WWOUI::Dim, 160))->SetPadding(FMargin(0, 14.f * K, 0, 0));
	}
	else
	{
		// The PvP maps; the one just played is marked.
		Column->AddChildToVerticalBox(WWOUI::Spaced(WidgetTree, TEXT("Change map"), WWOUI::Type::Small, WWOUI::Dim, 260))->SetPadding(FMargin(0, 34.f * K, 0, 14.f * K));
		const FName Now(*UGameplayStatics::GetCurrentLevelName(this));
		TWeakObjectPtr<UWWOScoreboard> Weak(this);
		for (const FWWOMapInfo& Info : UWWONetSubsystem::Maps())
		{
			if (!Info.bPvP) continue;
			const FName Level = Info.Level;
			Column->AddChildToVerticalBox(WWOUI::Wide(Tree, Button(Info.Title + (Level == Now ? TEXT("  ·  just played") : TEXT("")), Info.Blurb, true,
				[Weak, Level] { if (Weak.IsValid()) Weak->ChooseMap(Level); }), 560.f))->SetPadding(FMargin(0, 0, 0, 10.f * K));
		}
		Column->AddChildToVerticalBox(WWOUI::Wide(Tree, Button(TEXT("Back"), FString(), true, [Weak] { if (Weak.IsValid()) Weak->Back(); }), 200.f))->SetPadding(FMargin(0, 14.f * K, 0, 0));
		for (UPanelSlot* S : Column->GetSlots())
		{
			if (UVerticalBoxSlot* V = Cast<UVerticalBoxSlot>(S)) V->SetHorizontalAlignment(HAlign_Center);
		}
	}
	UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column);
	ColumnSlot->SetHorizontalAlignment(HAlign_Center);
	ColumnSlot->SetVerticalAlignment(VAlign_Center);
}
