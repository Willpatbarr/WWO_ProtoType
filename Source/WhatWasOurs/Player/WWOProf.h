#pragma once

// A tiny timer for -WWOPerfTest: WWO_PROF("Name") at the top of a function adds the time spent in
// it (and in what it calls) to that name. The perf test logs each name's average per frame at the
// end ("PerfTest code ..."). It does nothing unless a perf test is running.

#include "CoreMinimal.h"

struct FWWOProf
{
	static WHATWASOURS_API bool bOn;
	static WHATWASOURS_API TMap<FString, double>& Totals(); // seconds, by name (game thread only)
	static WHATWASOURS_API TMap<FString, int32>& Calls();
	const TCHAR* Name;
	double Start;
	explicit FWWOProf(const TCHAR* InName) : Name(InName), Start(bOn && IsInGameThread() ? FPlatformTime::Seconds() : 0.0) {}
	~FWWOProf()
	{
		if (Start > 0.0)
		{
			Totals().FindOrAdd(Name) += FPlatformTime::Seconds() - Start;
			++Calls().FindOrAdd(Name);
		}
	}
};
#define WWO_PROF(Name) FWWOProf WWOProfScope_##__LINE__(TEXT(Name))
