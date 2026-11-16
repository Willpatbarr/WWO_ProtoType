#include "Player/WWOTestRun.h"
#include "WhatWasOurs.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include <atomic>
#include <cstdlib>

namespace
{
	std::atomic<int32> GFailures{ 0 };
	bool GExitFailed = false; // leave with exit code 1 (set by Exit)

	// The loadout guard (see GuardLoadout in the header).
	bool GLoadoutGuarded = false, GLoadoutExisted = false;
	TArray<uint8> GLoadoutBytes;
	FString LoadoutFile() { return FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.sav"); }
	FString LoadoutBackup() { return FPaths::ProjectSavedDir() / TEXT("SaveGames/Loadout.autotest-backup"); }

	// Makes the saved file what it was when the guard started, and says so if it had changed.
	void PutLoadoutBack(const TCHAR* When)
	{
		if (!GLoadoutGuarded) return;
		IFileManager& Files = IFileManager::Get();
		const FString File = LoadoutFile();
		TArray<uint8> Now;
		const bool bExists = Files.FileExists(*File) && FFileHelper::LoadFileToArray(Now, *File, FILEREAD_Silent);
		if (GLoadoutExisted)
		{
			if (bExists && Now == GLoadoutBytes) return;
			FFileHelper::SaveArrayToFile(GLoadoutBytes, *File);
			UE_LOG(LogWWO, Display, TEXT("TestRun: saved loadout put back %s (%d bytes; the run had left %d)"), When, GLoadoutBytes.Num(), bExists ? Now.Num() : 0);
		}
		else if (Files.FileExists(*File))
		{
			Files.Delete(*File);
			UE_LOG(LogWWO, Display, TEXT("TestRun: there was no saved loadout; the one the run wrote is removed %s (%d bytes)"), When, Now.Num());
		}
	}

	// The very end of shutdown, after the game's own saves (the loadout is written when the player
	// controller ends play and again when the game instance shuts down; both come before this).
	void AtShutdown()
	{
		if (GLoadoutGuarded)
		{
			PutLoadoutBack(TEXT("at exit"));
			IFileManager::Get().Delete(*LoadoutBackup());
		}
#if !PLATFORM_WINDOWS
		// Only Windows carries a status through a normal exit; elsewhere, leave with it here.
		if (GExitFailed) { if (GLog) GLog->Flush(); std::_Exit(1); }
#endif
	}

	void HookShutdown()
	{
		static bool bHooked = false;
		if (bHooked) return;
		bHooked = true;
		FCoreDelegates::OnExit.AddStatic(&AtShutdown);
	}

	struct FFailCounter : FOutputDevice
	{
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			static const FName Ours(TEXT("LogWWO"));
			if (Category == Ours && FCString::Strstr(V, TEXT("AutoTest")) && FCString::Strstr(V, TEXT("FAIL")) && !FCString::Strstr(V, TEXT("AutoTest result:"))) ++GFailures;
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
	};
}

void WWOTest::Watch()
{
	static FFailCounter* Counter = nullptr;
	if (Counter || !GLog) return;
	Counter = new FFailCounter; // lives as long as the log does
	GLog->AddOutputDevice(Counter);
}

int32 WWOTest::Failures() { return GFailures.load(); }

void WWOTest::Exit()
{
	ExitWith(Failures(), TEXT("AutoTest"));
}

void WWOTest::ExitWith(int32 Failed, const TCHAR* Label)
{
	static bool bLeaving = false;
	if (bLeaving) return;
	bLeaving = true;
	if (Failed > 0) { UE_LOG(LogWWO, Warning, TEXT("%s result: FAIL (%d)"), Label, Failed); }
	else { UE_LOG(LogWWO, Display, TEXT("%s result: PASS"), Label); }
	GExitFailed = Failed > 0;
	HookShutdown(); // the guarded loadout goes back, then (off Windows) the exit code is set
#if PLATFORM_WINDOWS
	FPlatformMisc::RequestExitWithStatus(false, Failed > 0 ? 1 : 0);
#else
	FPlatformMisc::RequestExit(false);
#endif
}

void WWOTest::GuardLoadout()
{
	if (GLoadoutGuarded) return;
	GLoadoutGuarded = true;
	IFileManager& Files = IFileManager::Get();
	const FString File = LoadoutFile(), Backup = LoadoutBackup();
	// A copy left by a run that stopped half way is the owner's loadout: it goes back first. (An
	// empty copy means there was no save then.)
	if (Files.FileExists(*Backup))
	{
		if (Files.FileSize(*Backup) > 0) Files.Copy(*File, *Backup);
		else Files.Delete(*File);
		UE_LOG(LogWWO, Display, TEXT("TestRun: saved loadout put back from the copy an unfinished run left"));
	}
	GLoadoutExisted = Files.FileExists(*File) && FFileHelper::LoadFileToArray(GLoadoutBytes, *File, FILEREAD_Silent);
	if (!GLoadoutExisted) GLoadoutBytes.Reset();
	FFileHelper::SaveArrayToFile(GLoadoutBytes, *Backup);
	HookShutdown();
}

void WWOTest::RestoreLoadout() { PutLoadoutBack(TEXT("mid-run")); }

bool WWOTest::Check(const TCHAR* Tag, bool bOk, const FString& What)
{
	UE_LOG(LogWWO, Display, TEXT("AutoTest %s: %s %s"), Tag, *What, bOk ? TEXT("ok") : TEXT("FAIL"));
	return bOk;
}

FString WWOTest::ShotPath(const FString& Name)
{
	return FPaths::ProjectSavedDir() / TEXT("AutoTest") / Name + TEXT(".png");
}

FString WWOTest::Shot(const FString& Name)
{
	const FString Path = ShotPath(Name);
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	return Path;
}
