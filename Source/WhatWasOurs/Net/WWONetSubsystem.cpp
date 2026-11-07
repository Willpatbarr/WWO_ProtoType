#include "Net/WWONetSubsystem.h"
#include "WhatWasOurs.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/Engine.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

namespace
{
	const FName SessionName("GameSession");
	const FName MapKey("WWOMAP");
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

void UWWONetSubsystem::OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	// The host left or the connection dropped: the engine takes you back to the start map alone.
	Status = FString::Printf(TEXT("Lost the connection (%s)"), ENetworkFailure::ToString(Type));
	UE_LOG(LogWWO, Warning, TEXT("Net: network failure %s: %s"), ENetworkFailure::ToString(Type), *Error);
}

void UWWONetSubsystem::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	Status = FString::Printf(TEXT("Couldn't join (%s)"), ETravelFailure::ToString(Type));
	UE_LOG(LogWWO, Warning, TEXT("Net: travel failure %s: %s"), ETravelFailure::ToString(Type), *Error);
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
	const int32 Players = GS ? GS->PlayerArray.Num() : 1;
	FString Line;
	switch (World->GetNetMode())
	{
	case NM_ListenServer:
		Line = FString::Printf(TEXT("Hosting a LAN game: %d of %d players. Others join at %s"), Players, MaxPlayers, *LocalAddress());
		break;
	case NM_Client:
		Line = FString::Printf(TEXT("In a LAN game: %d players"), Players);
		if (Status.StartsWith(TEXT("Only the host"))) Line += TEXT(". ") + Status;
		break;
	default:
		Line = TEXT("Playing alone");
		if (!Status.IsEmpty()) Line += TEXT(". ") + Status;
		break;
	}
	return Line;
}

IOnlineSessionPtr UWWONetSubsystem::Sessions() const
{
	return Online::GetSessionInterface(GetWorld());
}

bool UWWONetSubsystem::IsOnline(const UObject* WorldContext) const
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World && World->GetNetMode() != NM_Standalone;
}

void UWWONetSubsystem::Host(FName Map)
{
	PendingMap = Map;
	Status = TEXT("Starting a LAN game...");
	const IOnlineSessionPtr S = Sessions();
	if (!S.IsValid())
	{
		// No session system: still host, just without being found by search (join by address).
		UGameplayStatics::OpenLevel(GetWorld(), PendingMap, true, TEXT("listen"));
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
	Settings.NumPublicConnections = MaxPlayers;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bUsesPresence = false;
	Settings.bAllowJoinViaPresence = false;
	Settings.Set(MapKey, PendingMap.ToString(), EOnlineDataAdvertisementType::ViaOnlineService);
	CreateHandle = S->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &UWWONetSubsystem::OnCreated));
	if (!S->CreateSession(0, SessionName, Settings))
	{
		S->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		UGameplayStatics::OpenLevel(GetWorld(), PendingMap, true, TEXT("listen"));
	}
}

void UWWONetSubsystem::OnCreated(FName Name, bool bOk)
{
	if (const IOnlineSessionPtr S = Sessions()) S->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	UE_LOG(LogWWO, Display, TEXT("Net: LAN session %s, hosting %s"), bOk ? TEXT("created") : TEXT("failed"), *PendingMap.ToString());
	Status = FString();
	const FName Map = PendingMap;
	PendingMap = NAME_None;
	UGameplayStatics::OpenLevel(GetWorld(), Map, true, TEXT("listen")); // the same map, now accepting players
}

void UWWONetSubsystem::FindAndJoin()
{
	const IOnlineSessionPtr S = Sessions();
	if (!S.IsValid())
	{
		Status = TEXT("No session system: join by address");
		return;
	}
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
	if (!S.IsValid() || Count == 0)
	{
		Status = TEXT("No LAN games found");
		return;
	}
	Status = TEXT("Joining...");
	JoinHandle = S->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &UWWONetSubsystem::OnJoined));
	S->JoinSession(0, SessionName, Search->SearchResults[0]);
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
	UE_LOG(LogWWO, Display, TEXT("Net: joining %s"), *Address);
	PC->ClientTravel(Address, TRAVEL_Absolute);
}

void UWWONetSubsystem::Leave()
{
	if (const IOnlineSessionPtr S = Sessions())
	{
		if (S->GetNamedSession(SessionName)) S->DestroySession(SessionName);
	}
	Status = FString();
	const UWorld* World = GetWorld();
	if (World) UGameplayStatics::OpenLevel(GetWorld(), FName(*UGameplayStatics::GetCurrentLevelName(World)), true);
}

void UWWONetSubsystem::HandleCommandLine(const UObject* WorldContext)
{
	if (bCommandLineDone) return;
	bCommandLineDone = true;
	const TCHAR* Cmd = FCommandLine::Get();
	FString Address;
	if (FParse::Param(Cmd, TEXT("WWOHost")))
	{
		Host(FName(*UGameplayStatics::GetCurrentLevelName(WorldContext)));
	}
	else if (FParse::Value(Cmd, TEXT("WWOJoin="), Address) && !Address.IsEmpty())
	{
		JoinAddress(Address);
	}
	else if (FParse::Param(Cmd, TEXT("WWOJoin")))
	{
		FindAndJoin();
	}
}
