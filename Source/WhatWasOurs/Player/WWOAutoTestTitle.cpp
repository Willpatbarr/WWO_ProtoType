// The launch screen's test (-WWOAutoTestTitle, and -WWOAutoTestTitleJoin for a second copy).
// Its state is static: each game it starts loads a map, which makes a new player controller.
// -WWOAutoTestTitle: the title, every step of the setup screen (60_title to 65_setup_*), a local
//   PvP match against bots on the Ring (66_local_pvp), back to the title through the settings
//   menu's button (67_title_again), then hosts a PvE game on the small arena, opens its own
//   settings (everything there: 68_host_settings) and stays up for a joiner (-WWOTitleHostTime=s).
// -WWOAutoTestTitleJoin (start it once the first copy is hosting): the title, Online join, the
//   list with the host's game in it (70_join_list), joins it, opens its settings (only a
//   player's own: 71_joiner_settings) and quits.
// Logs `AutoTest title: ... ok|FAIL`.

#include "Player/WWOAutoTest.h"
#include "Player/WWOPlayerController.h"
#include "WWOSwitches.h"
#include "Player/WWOTestRun.h"
#include "Player/WWOCharacter.h"
#include "Player/WWOWeaponComponent.h"
#include "UI/WWOLaunchMenu.h"
#include "UI/WWOSettingsMenu.h"
#include "Net/WWONetSubsystem.h"
#include "Game/WWOGameMode.h"
#include "Game/WWOPlayerState.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WhatWasOurs.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	int32 TitleStage = 0;
	float TitleClock = 0.f; // real seconds in this stage

	void Check(bool bOk, const FString& What) { WWOTest::Check(TEXT("title"), bOk, What); }
}

void FWWOAutoTest::UpdateAutoTitleTest(float DeltaTime)
{
	static const bool bJoiner = WWOSwitches::Has(TEXT("WWOAutoTestTitleJoin"));
	UWWONetSubsystem* Net = UWWONetSubsystem::Get(PC);
	if (!Net) return;
	TitleClock += DeltaTime;
	auto Next = [](int32 Stage = -1) { TitleStage = Stage < 0 ? TitleStage + 1 : Stage; TitleClock = 0.f; };
	UWWOLaunchMenu* Launch = PC->LaunchMenu && PC->LaunchMenu->IsOpen() ? PC->LaunchMenu.Get() : nullptr;
	const FString Level = UGameplayStatics::GetCurrentLevelName(PC);
	auto Quit = [] { UE_LOG(LogWWO, Display, TEXT("AutoTest title: done")); WWOTest::Exit(); };

	// -WWOAutoTestTitleJoinFail: from the title, join an address nobody is hosting at. When the
	// connection gives up, the game must be back on the launch screen (a freshly loaded start map)
	// with the reason, not in a solo game.
	static const bool bJoinFail = WWOSwitches::Has(TEXT("WWOAutoTestTitleJoinFail"));
	if (bJoinFail)
	{
		static uint32 FirstController = 0;
		switch (TitleStage)
		{
		case 0:
			if (TitleClock > 8.f && Launch)
			{
				FirstController = PC->GetUniqueID();
				Net->JoinAddress(TEXT("127.0.0.1:7999"));
				Next();
			}
			break;
		case 1:
			if (PC->GetUniqueID() != FirstController && TitleClock > 3.f)
			{
				Check(Launch && PC->IsPaused() && PC->GetNetMode() == NM_Standalone && Net->Status.StartsWith(TEXT("Couldn't join")),
					FString::Printf(TEXT("a join that fails comes back to the launch screen (%s; status \"%s\")"), Launch ? *Launch->DebugState() : TEXT("no launch screen"), *Net->Status));
				Shot(TEXT("73_join_failed"));
				Next();
			}
			else if (TitleClock > 150.f) { Check(false, TEXT("the failed join ended")); Quit(); }
			break;
		case 2: if (TitleClock > 1.f) Quit(); break;
		default: break;
		}
		return;
	}
	if (bJoiner)
	{
		switch (TitleStage)
		{
		case 0: if (TitleClock > 8.f) { Check(Launch && Launch->DebugState() == TEXT("title") && PC->IsPaused(), TEXT("opens on the title, paused")); if (Launch) Launch->Play(); Next(); } break;
		case 1: if (TitleClock > 0.5f && Launch) { Launch->ChooseConnection(UWWOLaunchMenu::ConnJoin); Next(); } break;
		case 2:
			// The list fills from the searches the screen runs itself.
			if (Net->Games.Num() > 0 && TitleClock > 2.f)
			{
				const FWWOGameInfo& G = Net->Games[0];
				Check(true, FString::Printf(TEXT("found %d game(s): %s's on %s, %s, %d of %d players"), Net->Games.Num(), *G.Host, *G.MapTitle, G.bPvP ? TEXT("PvP") : TEXT("PvE"), G.Players, G.MaxPlayers));
				if (Launch) Launch->ChooseGame(0);
				Next();
			}
			else if (TitleClock > 40.f) { Check(false, TEXT("found a game on the network")); Quit(); }
			break;
		case 3: if (TitleClock > 0.6f) { Check(Launch && Launch->CanStart(), FString::Printf(TEXT("a picked game can be joined (%s)"), Launch ? *Launch->DebugState() : TEXT("-"))); Shot(TEXT("70_join_list")); Next(); } break;
		case 4: if (TitleClock > 0.6f && Launch) { Launch->Start(); Next(); } break;
		case 5:
			if (PC->GetNetMode() == NM_Client && PC->GetPawn() && TitleClock > 8.f)
			{
				Check(!PC->LaunchMenu, FString::Printf(TEXT("joined the host's game on %s"), *Level));
				PC->ToggleSettingsMenu();
				Next();
			}
			else if (TitleClock > 45.f) { Check(false, TEXT("joined the host's game")); Quit(); }
			break;
		case 6:
			if (TitleClock > 1.5f)
			{
				const int32 Pages = PC->SettingsMenu->ShownPageCount();
				Check(PC->SettingsMenu->IsRestricted() && Pages >= 4 && Pages <= 6, FString::Printf(TEXT("a joiner's settings show only its own (%d pages)"), Pages));
				PC->SettingsMenu->ShowPage(TEXT("Camera"));
				Shot(TEXT("71_joiner_settings"));
				Next();
			}
			break;
		case 7: if (TitleClock > 1.f) { PC->SettingsMenu->ShowPage(TEXT("Multiplayer")); Next(); } break;
		case 8: if (TitleClock > 0.6f) { Shot(TEXT("72_joiner_multiplayer")); Next(); } break;
		case 9: if (TitleClock > 1.f) Quit(); break;
		default: break;
		}
		return;
	}

	switch (TitleStage)
	{
	case 0:
		if (TitleClock > 8.f) // shaders, the first frames
		{
			Check(Launch && Launch->DebugState() == TEXT("title") && PC->IsPaused(), TEXT("opens on the title, paused"));
			Shot(TEXT("60_title"));
			Next();
		}
		break;
	case 1: if (TitleClock > 0.6f && Launch) { Launch->Play(); Next(); } break;
	case 2: if (TitleClock > 0.6f) { Check(Launch && Launch->DebugState().StartsWith(TEXT("setup none")), TEXT("PLAY opens the setup screen")); Shot(TEXT("61_setup")); Next(); } break;
	case 3: if (TitleClock > 0.6f && Launch) { Launch->ChooseConnection(UWWOLaunchMenu::ConnJoin); Next(); } break;
	case 4: if (TitleClock > 3.5f) { Check(Launch && !Launch->CanStart(), TEXT("Online join lists games; nothing to join yet")); Shot(TEXT("62_setup_join")); Next(); } break;
	case 5: if (TitleClock > 0.6f && Launch) { Launch->ChooseConnection(UWWOLaunchMenu::ConnHost); Launch->ChooseMode(UWWOLaunchMenu::ModePvP); Next(); } break;
	case 6: if (TitleClock > 0.6f) { Check(Launch && !Launch->CanStart(), TEXT("no START before a map is picked")); Shot(TEXT("63_setup_host_pvp")); Next(); } break;
	case 7: if (TitleClock > 0.6f && Launch) { Launch->ChooseMap("L_Arboretum"); Next(); } break;
	case 8: if (TitleClock > 0.6f) { Check(Launch && Launch->CanStart(), FString::Printf(TEXT("START shows once everything is picked (%s)"), Launch ? *Launch->DebugState() : TEXT("-"))); Shot(TEXT("64_setup_ready")); Next(); } break;
	case 9:
		if (TitleClock > 0.6f && Launch)
		{
			// Back steps one choice at a time.
			Launch->Back();
			const bool bMapGone = Launch->DebugState() == TEXT("setup host pvp -");
			Launch->Back();
			Check(bMapGone && Launch->DebugState() == TEXT("setup host none -"), TEXT("back clears the map, then the mode"));
			Launch->ChooseConnection(UWWOLaunchMenu::ConnLocal);
			Launch->ChooseMode(UWWOLaunchMenu::ModePvE);
			Next();
		}
		break;
	case 10: if (TitleClock > 0.6f) { Shot(TEXT("65_setup_local_pve")); Next(); } break;
	case 11:
		if (TitleClock > 0.6f && Launch)
		{
			Launch->ChooseMode(UWWOLaunchMenu::ModePvP);
			Launch->ChooseMap("L_ArenaRing");
			Launch->Start();
			Next();
		}
		break;
	case 12:
		// The local PvP match (a new controller, on the Ring).
		if (Level == TEXT("L_ArenaRing") && PC->GetPawn() && TitleClock > 22.f)
		{
			const AGameStateBase* GS = PC->GetWorld()->GetGameState();
			int32 Bots = 0, Teams[2] = { 0, 0 };
			if (GS) for (const APlayerState* PS : GS->PlayerArray)
			{
				if (PS && PS->IsABot()) ++Bots;
				if (const AWWOPlayerState* WPS = Cast<AWWOPlayerState>(PS); WPS && WPS->Team >= 0 && WPS->Team <= 1) ++Teams[WPS->Team];
			}
			Check(!PC->LaunchMenu && PC->GetNetMode() == NM_Standalone && UWWOWorldSubsystem::IsPvP(PC) && !PC->IsPaused(), TEXT("local PvP runs alone, unpaused, with no launch screen"));
			Check(Bots == 7 && Teams[0] == 4 && Teams[1] == 4, FString::Printf(TEXT("bots fill both teams of the Ring (%d bots, %d v %d)"), Bots, Teams[0], Teams[1]));
			Shot(TEXT("66_local_pvp"));
			Next();
		}
		else if (TitleClock > 90.f) { Check(false, TEXT("local PvP started")); Quit(); }
		break;
	case 13: if (TitleClock > 1.f) { Net->LeaveToTitle(); Next(); } break;
	case 14:
		if (Launch && TitleClock > 6.f)
		{
			Check(Launch->DebugState() == TEXT("title") && PC->IsPaused() && PC->GetNetMode() == NM_Standalone, TEXT("quitting a game comes back to the title"));
			Shot(TEXT("67_title_again"));
			Launch->ChooseConnection(UWWOLaunchMenu::ConnHost);
			Launch->ChooseMode(UWWOLaunchMenu::ModePvE);
			Launch->ChooseMap("L_Arena");
			Launch->Start();
			Next();
		}
		else if (TitleClock > 60.f) { Check(false, TEXT("came back to the title")); Quit(); }
		break;
	case 15:
		if (PC->GetNetMode() == NM_ListenServer && PC->GetPawn() && TitleClock > 8.f)
		{
			Check(!PC->LaunchMenu && Level == TEXT("L_Arena") && !UWWOWorldSubsystem::IsPvP(PC), TEXT("hosting a PvE game on the small arena"));
			PC->ToggleSettingsMenu();
			Next();
		}
		else if (TitleClock > 60.f) { Check(false, TEXT("hosted a game")); Quit(); }
		break;
	case 16:
		if (TitleClock > 0.2f && TitleClock < 1.2f)
		{
			// Hosting, the game runs on under the menu: the trigger, held, must not reach the gun.
			PC->Held[(int32)EWWOAction::Fire] = true;
			PC->JustPressed[(int32)EWWOAction::Fire] = true;
		}
		if (TitleClock > 1.5f)
		{
			PC->Held[(int32)EWWOAction::Fire] = false;
			if (const AWWOCharacter* Me = PC->GetWWOCharacter())
			{
				Check(PC->IsMenuOpen() && !PC->IsPaused() && Me->Weapon->Ammo == Me->Weapon->MagazineSize(),
					FString::Printf(TEXT("online, an open menu keeps the gun quiet (ammo %d of %d after 1 s of trigger)"), Me->Weapon->Ammo, Me->Weapon->MagazineSize()));
			}
			const int32 Pages = PC->SettingsMenu->ShownPageCount();
			Check(!PC->SettingsMenu->IsRestricted() && Pages > 20, FString::Printf(TEXT("the host's settings show everything (%d pages)"), Pages));
			PC->SettingsMenu->ShowPage(TEXT("Multiplayer"));
			Next();
		}
		break;
	case 17: if (TitleClock > 0.8f) { Shot(TEXT("68_host_settings")); Next(); } break;
	case 18: if (TitleClock > 1.f) { PC->ToggleSettingsMenu(); Next(); } break;
	case 19:
	{
		// Stay up for a joiner (the second copy), then quit.
		static float HostTime = -1.f;
		if (HostTime < 0.f) { HostTime = 0.f; WWOSwitches::Value(TEXT("WWOTitleHostTime="), HostTime); }
		if (TitleClock > HostTime)
		{
			const AGameStateBase* GS = PC->GetWorld()->GetGameState();
			UE_LOG(LogWWO, Display, TEXT("AutoTest title: hosted for %.0f s, %d player(s) at the end"), TitleClock, GS ? GS->PlayerArray.Num() : 0);
			Quit();
		}
		break;
	}
	default: break;
	}
}
