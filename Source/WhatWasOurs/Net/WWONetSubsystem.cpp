#include "Net/WWONetSubsystem.h"
#include "WWOSwitches.h"
#include "Systems/WWOConfig.h"
#include "Game/WWOPlayerState.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Systems/WWOWorldSubsystem.h"
#include "WhatWasOurs.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

namespace
{
	const FName SessionName("GameSession");
	const FName MapKey("WWOMAP");
	const FName PvPKey("WWOPVP");
	const FName PlayersKey("WWOPLAYERS");
	const FName StartMap("L_Arena"); // the launch screen sits over it (GameDefaultMap)
	FString TagPath() { return WWOConfig::SavedPath(TEXT("GamerTag.txt")); } // Saved/WWO/GamerTag.txt
}

const TArray<FWWOMapInfo>& UWWONetSubsystem::Maps()
{
	static const TArray<FWWOMapInfo> List = {
		{ "L_Arena", TEXT("Small Arena"), TEXT("One level, close quarters"), false, true, 4 },
		{ "L_ArenaLarge", TEXT("Large Arena"), TEXT("Two levels, platforms and halls"), false, true, 4 },
		{ "L_ArenaRing", TEXT("Ring"), TEXT("4 v 4 · a ring of rooms around a center"), true, false, 4 },
		{ "L_Arboretum", TEXT("Arboretum"), TEXT("4 v 4 · greenhouses over a tunnel"), true, false, 4 },
		{ "L_Cascade", TEXT("Cascade"), TEXT("6 v 6 · decks and bridges over the falls"), true, true, 6 },
		{ "L_TestRange", TEXT("Test Range"), TEXT("Gray-box sandbox, no waves"), false, true, 4 },
	};
	return List;
}

const FWWOMapInfo* UWWONetSubsystem::FindMap(FName Level)
{
	return Maps().FindByPredicate([Level](const FWWOMapInfo& M) { return M.Level == Level; });
}

FString UWWONetSubsystem::MapTitle(FName Level)
{
	const FWWOMapInfo* Info = FindMap(Level);
	return Info ? Info->Title : Level.ToString();
}

FString UWWONetSubsystem::MatchOptions(bool bListen, bool bPvP, int32 Bots)
{
	TArray<FString> Parts;
	if (bListen) Parts.Add(TEXT("listen"));
	if (bPvP) Parts.Add(TEXT("pvp"));
	if (bPvP && Bots >= 0) Parts.Add(FString::Printf(TEXT("bots=%d"), Bots));
	return FString::Join(Parts, TEXT("?"));
}

FString UWWONetSubsystem::CleanTag(const FString& Typed)
{
	FString Out;
	for (const TCHAR C : Typed)
	{
		if (Out.Len() >= MaxTagLength) break;
		if (FChar::IsAlnum(C) && C < 128 || C == TEXT('_') || C == TEXT('-')) Out.AppendChar(C);
	}
	return Out;
}

void UWWONetSubsystem::SetGamerTag(const FString& Typed, bool bSave)
{
	GamerTag = CleanTag(Typed);
	if (bSave) FFileHelper::SaveStringToFile(GamerTag, *TagPath());
}

FString UWWONetSubsystem::WithName(const FString& Options) const
{
	if (GamerTag.IsEmpty()) return Options;
	// (Tag, not the engine's Name: the engine overwrites Name with the computer's own name on the way in.)
	return Options.IsEmpty() ? TEXT("Tag=") + GamerTag : Options + TagOption();
}

bool UWWONetSubsystem::WantsTitle(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return false;
	if (!bTitleDecided)
	{
		bTitleDecided = true;
		// A test run or a developer's direct start (a map or a -WWO switch on the command line, or
		// Play-in-Editor) goes straight into the game, as before there was a launch screen.
		const bool bDirect = World->WorldType != EWorldType::Game || WWOSwitches::HasAnyWWOSwitch() || WWOSwitches::NamesGamePath();
		bShowTitle = WWOSwitches::Has(TEXT("WWOTitle")) || WWOSwitches::HasTitleTestSwitch() || !bDirect;
		UE_LOG(LogWWO, Display, TEXT("Launch: %s"), bShowTitle ? TEXT("opening on the launch screen") : TEXT("straight into the game (a map or a -WWO switch on the command line, or Play-in-Editor)"));
	}
	if (!bShowTitle || World->GetNetMode() != NM_Standalone) return false;
	bShowTitle = false;
	return true;
}

void UWWONetSubsystem::PlayLocal(FName Map, bool bPvP, int32 Bots)
{
	EndSession();
	Status = FString();
	bShowTitle = false;
	CancelSearch();
	UE_LOG(LogWWO, Display, TEXT("Net: playing %s alone (%s)"), *Map.ToString(), bPvP ? TEXT("PvP against bots") : TEXT("waves"));
	UGameplayStatics::OpenLevel(GetWorld(), Map, true, WithName(MatchOptions(false, bPvP, Bots)));
}

void UWWONetSubsystem::PlayTester()
{
	EndSession();
	Status = FString();
	bShowTitle = false;
	CancelSearch();
	UE_LOG(LogWWO, Display, TEXT("Net: opening the firing tester"));
	UGameplayStatics::OpenLevel(GetWorld(), "L_TestRange", true, TEXT("tester"));
}

void UWWONetSubsystem::LeaveToTitle()
{
	EndSession();
	Status = FString();
	bShowTitle = true;
	CancelSearch();
	UE_LOG(LogWWO, Display, TEXT("Net: back to the launch screen"));
	UGameplayStatics::OpenLevel(GetWorld(), StartMap, true);
}

void UWWONetSubsystem::PublishMatch(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const IOnlineSessionPtr S = Sessions();
	if (!World || World->GetNetMode() != NM_ListenServer || !S.IsValid()) return;
	FOnlineSessionSettings* Settings = S->GetSessionSettings(SessionName);
	if (!Settings) return;
	const AGameStateBase* GS = World->GetGameState();
	// People only: a bot has a player state too, but doesn't take a place a joiner could have.
	int32 Players = 0;
	if (GS) for (const APlayerState* PS : GS->PlayerArray) if (PS && !PS->IsABot()) ++Players;
	const bool bPvP = UWWOWorldSubsystem::IsPvP(World);
	FOnlineSessionSettings Next = *Settings;
	Next.Set(MapKey, UGameplayStatics::GetCurrentLevelName(World), EOnlineDataAdvertisementType::ViaOnlineService);
	Next.Set(PvPKey, bPvP ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineService);
	Next.Set(PlayersKey, FMath::Max(Players, 1), EOnlineDataAdvertisementType::ViaOnlineService);
	S->UpdateSession(SessionName, Next, true);
}

UWWONetSubsystem* UWWONetSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UWWONetSubsystem>() : nullptr;
}

void UWWONetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Saved;
	if (FFileHelper::LoadFileToString(Saved, *TagPath())) GamerTag = CleanTag(Saved);
	// A test run that drives the launch screen has nobody to type one.
	// -WWOGamerTag=<name>: this run's tag (not saved), for tests with more than one copy of the game.
	FString Given;
	if (WWOSwitches::Value(TEXT("WWOGamerTag="), Given)) SetGamerTag(Given, false);
	// (-WWONoTestTag: leave it empty, to see the launch screen asking for one.)
	if (GamerTag.IsEmpty() && WWOSwitches::HasAutoTestSwitch() && !WWOSwitches::Has(TEXT("WWONoTestTag"))) GamerTag = TEXT("tester");
	if (GEngine)
	{
		NetFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UWWONetSubsystem::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UWWONetSubsystem::OnTravelFailure);
	}
}

void UWWONetSubsystem::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	Super::Deinitialize();
}

void UWWONetSubsystem::CancelSearch()
{
	bJoinFirst = false;
	if (!bSearching) return;
	bSearching = false;
	if (const IOnlineSessionPtr S = Sessions())
	{
		S->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle); // its late answer mustn't touch the status of the game that started
		S->CancelFindSessions();
	}
}

bool UWWONetSubsystem::IsHosting() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_ListenServer;
}

void UWWONetSubsystem::OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	// The host left or the connection dropped: the engine takes you back to the start map alone,
	// which opens on the launch screen with this as its status line.
	// (A host hears of a joiner's dropped connection here too: its own game goes on.)
	if (IsHosting()) return;
	// A connection that never got going (the host is gone, the address is wrong) comes with no world.
	const bool bNeverJoined = Type == ENetworkFailure::PendingConnectionFailure || !World || World->GetNetMode() != NM_Client;
	Status = bNeverJoined ? FString::Printf(TEXT("Couldn't join (%s)"), ENetworkFailure::ToString(Type))
		: FString::Printf(TEXT("Lost the connection (%s)"), ENetworkFailure::ToString(Type));
	bShowTitle = true; // either way the engine reloads the start map: open it on the launch screen
	EndSession();
	UE_LOG(LogWWO, Warning, TEXT("Net: network failure %s: %s"), ENetworkFailure::ToString(Type), *Error);
}

void UWWONetSubsystem::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	UE_LOG(LogWWO, Warning, TEXT("Net: travel failure %s: %s"), ETravelFailure::ToString(Type), *Error);
	if (IsHosting())
	{
		Status = FString::Printf(TEXT("Couldn't change maps (%s)"), ETravelFailure::ToString(Type));
		return;
	}
	// Not a host (any more): the engine falls back to the start map alone. No session of ours should
	// stay advertised, and that map opens on the launch screen with the reason.
	Status = FString::Printf(TEXT("Couldn't join (%s)"), ETravelFailure::ToString(Type));
	bShowTitle = true;
	EndSession();
}

FString UWWONetSubsystem::LocalAddress()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Sockets) return FString();
	bool bCanBind = false;
	const TSharedRef<FInternetAddr> Addr = Sockets->GetLocalHostAddr(*GLog, bCanBind);
	return Addr->IsValid() ? Addr->ToString(false) : FString();
}

FString UWWONetSubsystem::DescribeStatus(const UObject* WorldContext) const
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return Status;
	const AGameStateBase* GS = World->GetGameState();
	int32 Players = 0; // people: a bot has a player state too
	if (GS) for (const APlayerState* PS : GS->PlayerArray) if (PS && !PS->IsABot()) ++Players;
	Players = FMath::Max(Players, 1);
	FString Line;
	const bool bPvP = UWWOWorldSubsystem::IsPvP(World);
	switch (World->GetNetMode())
	{
	case NM_ListenServer:
		Line = FString::Printf(TEXT("Hosting a LAN %s: %d of %d players. Others join at %s"), bPvP ? TEXT("PvP game") : TEXT("game"), Players,
			bPvP ? MaxPvPPlayers : MaxPlayers, *LocalAddress());
		break;
	case NM_Client:
		Line = FString::Printf(TEXT("In a LAN %s: %d players"), bPvP ? TEXT("PvP game") : TEXT("game"), Players);
		if (Status.StartsWith(TEXT("Only the host"))) Line += TEXT(". ") + Status;
		break;
	default:
		Line = TEXT("Playing alone");
		if (!Status.IsEmpty()) Line += TEXT(". ") + Status;
		break;
	}
	// PvP: which team you're on.
	if (bPvP)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const AWWOPlayerState* PS = PC ? PC->GetPlayerState<AWWOPlayerState>() : nullptr;
		if (PS) Line += FString::Printf(TEXT(". You're on %s (%d kills, %d deaths)"), *AWWOPlayerState::TeamName(PS->Team), PS->Kills, PS->Deaths);
	}
	return Line;
}

IOnlineSessionPtr UWWONetSubsystem::Sessions() const
{
	return Online::GetSessionInterface(GetWorld());
}

void UWWONetSubsystem::EndSession()
{
	if (const IOnlineSessionPtr S = Sessions(); S.IsValid() && S->GetNamedSession(SessionName)) S->DestroySession(SessionName);
}

void UWWONetSubsystem::Host(FName Map, bool bPvP, int32 Bots)
{
	PendingMap = Map;
	bPendingPvP = bPvP;
	PendingBots = Bots;
	bShowTitle = false;
	CancelSearch(); // (a LAN search still running would also hold off the advertising)
	Status = TEXT("Starting a LAN game...");
	const IOnlineSessionPtr S = Sessions();
	if (!S.IsValid())
	{
		// No session system: still host, just without being found by search (join by address).
		UGameplayStatics::OpenLevel(GetWorld(), PendingMap, true, HostOptions());
		return;
	}
	if (S->GetNamedSession(SessionName))
	{
		// An old session (from a game you hosted or joined): end it first.
		DestroyHandle = S->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &UWWONetSubsystem::OnDestroyed));
		S->DestroySession(SessionName);
		return;
	}
	CreateSession();
}

void UWWONetSubsystem::OnDestroyed(FName Name, bool bOk)
{
	if (const IOnlineSessionPtr S = Sessions()) S->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	if (!PendingMap.IsNone()) CreateSession();
}

void UWWONetSubsystem::CreateSession()
{
	const IOnlineSessionPtr S = Sessions();
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.NumPublicConnections = bPendingPvP ? MaxPvPPlayers : MaxPlayers;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bUsesPresence = false;
	Settings.bAllowJoinViaPresence = false;
	Settings.Set(MapKey, PendingMap.ToString(), EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(PvPKey, bPendingPvP ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(PlayersKey, 1, EOnlineDataAdvertisementType::ViaOnlineService);
	CreateHandle = S->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &UWWONetSubsystem::OnCreated));
	if (!S->CreateSession(0, SessionName, Settings))
	{
		// It couldn't advertise. The completion callback has usually run already (and opened the
		// map, clearing PendingMap); if not, host anyway: others can still join by address.
		S->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		if (!PendingMap.IsNone())
		{
			const FName Map = PendingMap;
			PendingMap = NAME_None;
			UGameplayStatics::OpenLevel(GetWorld(), Map, true, HostOptions());
		}
	}
}

void UWWONetSubsystem::OnCreated(FName Name, bool bOk)
{
	if (const IOnlineSessionPtr S = Sessions()) S->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	UE_LOG(LogWWO, Display, TEXT("Net: LAN session %s, hosting %s"), bOk ? TEXT("created") : TEXT("failed"), *PendingMap.ToString());
	Status = FString();
	if (PendingMap.IsNone()) return;
	const FName Map = PendingMap;
	PendingMap = NAME_None;
	UGameplayStatics::OpenLevel(GetWorld(), Map, true, HostOptions()); // the same map, now accepting players
}

void UWWONetSubsystem::FindAndJoin()
{
	bJoinFirst = true;
	FindGames();
}

void UWWONetSubsystem::FindGames()
{
	const IOnlineSessionPtr S = Sessions();
	if (!S.IsValid())
	{
		Status = TEXT("No session system: join by address");
		bJoinFirst = false;
		return;
	}
	if (bSearching) return; // one search at a time; its results are a moment away
	bSearching = true;
	Status = TEXT("Searching the LAN...");
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 20;
	FindHandle = S->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &UWWONetSubsystem::OnFound));
	S->FindSessions(0, Search.ToSharedRef());
}

void UWWONetSubsystem::OnFound(bool bOk)
{
	const IOnlineSessionPtr S = Sessions();
	if (S.IsValid()) S->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	const int32 Count = Search.IsValid() ? Search->SearchResults.Num() : 0;
	UE_LOG(LogWWO, Display, TEXT("Net: LAN search %s, %d game(s) found"), bOk ? TEXT("done") : TEXT("failed"), Count);
	bSearching = false;
	Games.Reset();
	FoundResults.Reset();
	for (int32 i = 0; i < Count; ++i)
	{
		const FOnlineSessionSearchResult& R = Search->SearchResults[i];
		FoundResults.Add(R);
		FWWOGameInfo Info;
		// The LAN name is "<computer>[.local]-<32 hex digits>": keep the computer's name.
		Info.Host = R.Session.OwningUserName;
		int32 Dash = INDEX_NONE;
		if (Info.Host.FindLastChar(TEXT('-'), Dash) && Info.Host.Len() - Dash - 1 == 32) Info.Host.LeftInline(Dash);
		Info.Host.RemoveFromEnd(TEXT(".local"));
		FString Map;
		int32 PvP = 0, Players = 1;
		R.Session.SessionSettings.Get(MapKey, Map);
		R.Session.SessionSettings.Get(PvPKey, PvP);
		R.Session.SessionSettings.Get(PlayersKey, Players);
		Info.Map = FName(*Map);
		Info.MapTitle = MapTitle(Info.Map);
		Info.bPvP = PvP != 0;
		Info.Players = Players;
		Info.MaxPlayers = R.Session.SessionSettings.NumPublicConnections;
		Info.Ping = R.PingInMs;
		Games.Add(Info);
	}
	++GamesSerial;
	const bool bJoin = bJoinFirst;
	bJoinFirst = false;
	if (!S.IsValid() || Count == 0)
	{
		Status = TEXT("No LAN games found");
		return;
	}
	Status = FString();
	if (bJoin) JoinGame(0);
}

void UWWONetSubsystem::JoinGame(int32 Index)
{
	const IOnlineSessionPtr S = Sessions();
	if (!S.IsValid() || !FoundResults.IsValidIndex(Index))
	{
		Status = TEXT("That game is gone: search again");
		return;
	}
	CancelSearch();
	// An old session (a game you hosted or joined earlier this run) would refuse the new one.
	EndSession();
	Status = TEXT("Joining...");
	JoinHandle = S->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &UWWONetSubsystem::OnJoined));
	bShowTitle = false;
	if (!S->JoinSession(0, SessionName, FoundResults[Index]))
	{
		S->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		Status = TEXT("Couldn't join that game");
	}
}

void UWWONetSubsystem::OnJoined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
	const IOnlineSessionPtr S = Sessions();
	if (S.IsValid()) S->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	FString Url;
	if (Result != EOnJoinSessionCompleteResult::Success || !S.IsValid() || !S->GetResolvedConnectString(SessionName, Url))
	{
		Status = TEXT("Couldn't join that game");
		UE_LOG(LogWWO, Warning, TEXT("Net: join failed (%d)"), (int32)Result);
		return;
	}
	JoinAddress(Url);
}

void UWWONetSubsystem::JoinAddress(const FString& Address)
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	Status = TEXT("Joining ") + Address;
	bShowTitle = false;
	UE_LOG(LogWWO, Display, TEXT("Net: joining %s"), *Address);
	PC->ClientTravel(GamerTag.IsEmpty() ? Address : Address + TagOption(), TRAVEL_Absolute); // the host names you from it
}

void UWWONetSubsystem::Leave()
{
	EndSession();
	Status = FString();
	const UWorld* World = GetWorld();
	// The same map alone, in the mode it was (a PvP map's spawn points aren't for waves).
	if (World) UGameplayStatics::OpenLevel(GetWorld(), FName(*UGameplayStatics::GetCurrentLevelName(World)), true, MatchOptions(false, UWWOWorldSubsystem::IsPvP(World), -1));
}

void UWWONetSubsystem::HandleCommandLine(const UObject* WorldContext)
{
	if (bCommandLineDone) return;
	bCommandLineDone = true;
	FString Address;
	if (WWOSwitches::Has(TEXT("WWOHostPvP")))
	{
		Host(FName(*UGameplayStatics::GetCurrentLevelName(WorldContext)), true);
	}
	else if (WWOSwitches::Has(TEXT("WWOHost")))
	{
		Host(FName(*UGameplayStatics::GetCurrentLevelName(WorldContext)));
	}
	else if (WWOSwitches::Value(TEXT("WWOJoin="), Address) && !Address.IsEmpty())
	{
		JoinAddress(Address);
	}
	else if (WWOSwitches::Has(TEXT("WWOJoin")))
	{
		FindAndJoin();
	}
}
