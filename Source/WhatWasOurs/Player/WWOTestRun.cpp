#include "Player/WWOTestRun.h"
#include "WhatWasOurs.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/CoreDelegates.h"
#include <atomic>
#include <cstdlib>

namespace
{
	std::atomic<int32> GFailures{ 0 };

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
	static bool bLeaving = false;
	if (bLeaving) return;
	bLeaving = true;
	const int32 Failed = Failures();
	if (Failed > 0) { UE_LOG(LogWWO, Warning, TEXT("AutoTest result: FAIL (%d)"), Failed); }
	else { UE_LOG(LogWWO, Display, TEXT("AutoTest result: PASS")); }
#if PLATFORM_WINDOWS
	FPlatformMisc::RequestExitWithStatus(false, Failed > 0 ? 1 : 0);
#else
	// Only Windows carries a status through a normal exit; elsewhere, leave with it at the very end
	// of shutdown (after saves and the log are written).
	if (Failed > 0) FCoreDelegates::OnExit.AddLambda([] { if (GLog) GLog->Flush(); std::_Exit(1); });
	FPlatformMisc::RequestExit(false);
#endif
}
