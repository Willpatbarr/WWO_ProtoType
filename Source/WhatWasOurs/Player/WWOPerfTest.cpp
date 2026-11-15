// -WWOPerfTest: a repeatable performance check. After the level settles it walks the same fixed
// cameras as the map autotest, averages frame time and GPU time at each one, runs one GPU profile
// (profilegpu, into the log), logs a summary and quits. Run it before and after render changes.

#include "Player/WWOAutoTest.h"
#include "Player/WWOPlayerController.h"
#include "WWOSwitches.h"
#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "Camera/CameraActor.h"
#include "HAL/IConsoleManager.h"

#include "Player/WWOProf.h"

bool FWWOProf::bOn = false;
TMap<FString, double>& FWWOProf::Totals() { static TMap<FString, double> Map; return Map; }
TMap<FString, int32>& FWWOProf::Calls() { static TMap<FString, int32> Map; return Map; }

namespace
{
	double PerfGameMs = 0.0, PerfRenderMs = 0.0;
	int32 ProfFrames = 0;
	void LogProf()
	{
		FWWOProf::Totals().ValueSort([](double A, double B) { return A > B; });
		for (const TPair<FString, double>& T : FWWOProf::Totals())
		{
			UE_LOG(LogWWO, Display, TEXT("PerfTest code %-28s %.3f ms a frame (%.1f calls)"), *T.Key, T.Value * 1000.0 / FMath::Max(ProfFrames, 1), float(FWWOProf::Calls().FindRef(T.Key)) / FMath::Max(ProfFrames, 1));
		}
	}
}
#include "Engine/World.h"

// -WWOPerfSweep="r.A 0|r.B 4,r.C 1|..." (with -WWOPerfTest, and -WWOPerfView=N for which map view,
// default 1): holds that one view and tries each entry in turn (an entry is one or more
// "cvar value" pairs, comma separated): sets it, lets it settle, averages the frame time, puts the
// old values back. The plain view is measured first and again last, so drift shows. Each line is
// "PerfSweep <entry>: frame X ms (change from the first plain measure)". Then it quits.
bool FWWOAutoTest::UpdatePerfSweep(AWWOCharacter* P, float DeltaTime)
{
	static bool bParsed = false, bOn = false;
	static TArray<FString> Entries;
	static int32 WantView = 1, At = -1;
	static float Clock = 0.f;
	static double Sum = 0.0, GpuSum = 0.0, Base = 0.0;
	static int32 Count = 0;
	static TArray<TPair<FString, FString>> Old; // what the current entry changed, and from what
	if (!bParsed)
	{
		bParsed = true;
		FString Arg;
		if (WWOSwitches::Value(TEXT("WWOPerfSweep="), Arg, false))
		{
			bOn = true;
			Arg.TrimQuotesInline();
			Entries.Add(FString()); // the plain view
			TArray<FString> Parts;
			Arg.ParseIntoArray(Parts, TEXT("|"));
			Entries.Append(Parts);
			Entries.Add(FString()); // and again
			WWOSwitches::Value(TEXT("WWOPerfView="), WantView);
		}
	}
	if (!bOn) return false;
	// Walk the map views up to the one asked for, then stay there.
	if (AutoMapIndex < WantView || (AutoMapIndex == WantView && AutoMapClock < 1.5f))
	{
		UpdateAutoMapTest(P, DeltaTime);
		return true;
	}
	auto Apply = [](const FString& Entry)
	{
		TArray<FString> Pairs;
		Entry.ParseIntoArray(Pairs, TEXT(","));
		for (const FString& Pair : Pairs)
		{
			FString Name, Value;
			if (!Pair.TrimStartAndEnd().Split(TEXT(" "), &Name, &Value)) continue;
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
			{
				Old.Add(TPair<FString, FString>(Name, Var->GetString()));
				Var->Set(*Value, ECVF_SetByConsole);
			}
			else UE_LOG(LogWWO, Warning, TEXT("PerfSweep: no cvar %s"), *Name);
		}
	};
	if (At < 0) { At = 0; Clock = 0.f; Apply(Entries[0]); }
	Clock += DeltaTime;
	// -WWOPerfMove: the camera slides 4 m each way and turns 25 degrees each way, once every 3 s
	// (one whole swing per measure), so what moving costs (fresh shadow pages, culling) is in the numbers.
	static const bool bMove = WWOSwitches::Has(TEXT("WWOPerfMove"));
	static FVector Home = FVector::ZeroVector;
	static FRotator HomeTurn = FRotator::ZeroRotator;
	static float MoveClock = -1.f;
	if (bMove && AutoMapCamera)
	{
		if (MoveClock < 0.f) { MoveClock = 0.f; Home = AutoMapCamera->GetActorLocation(); HomeTurn = AutoMapCamera->GetActorRotation(); }
		MoveClock += DeltaTime;
		const float Swing = FMath::Sin(MoveClock * UE_TWO_PI / 3.f);
		AutoMapCamera->SetActorLocationAndRotation(Home + HomeTurn.RotateVector(FVector(0.f, 400.f * Swing, 0.f)), HomeTurn + FRotator(0.f, 25.f * Swing, 0.f));
	}
	constexpr float Settle = 1.5f, Measure = 3.f;
	if (Clock > Settle && DeltaTime < 0.1f)
	{
		Sum += DeltaTime * 1000.0;
		GpuSum += FPlatformTime::ToMilliseconds64(RHIGetGPUFrameCycles(0));
		++Count;
	}
	if (Clock < Settle + Measure) return true;
	const double Avg = Count > 0 ? Sum / Count : 0.0;
	if (At == 0) Base = Avg;
	UE_LOG(LogWWO, Display, TEXT("PerfSweep %s: frame %.2f ms (%+.2f), GPU %.2f ms"), Entries[At].IsEmpty() ? TEXT("(plain)") : *Entries[At], Avg, Avg - Base, Count > 0 ? GpuSum / Count : 0.0);
	for (int32 i = Old.Num() - 1; i >= 0; --i)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Old[i].Key)) Var->Set(*Old[i].Value, ECVF_SetByConsole);
	}
	Old.Reset();
	Sum = GpuSum = 0.0;
	Count = 0;
	Clock = 0.f;
	if (++At >= Entries.Num())
	{
		bPerfTest = false;
		PC->ConsoleCommand(TEXT("quit"));
		return true;
	}
	Apply(Entries[At]);
	return true;
}

void FWWOAutoTest::UpdatePerfTest(AWWOCharacter* P, float DeltaTime)
{
	PerfStartClock += DeltaTime;
	if (PerfStartClock < 6.f) return; // shaders, streaming and the first sky capture settle
	// -WWOPerfCsv: the engine's own per-frame timings too (Saved/Profiling/CSV), from here to the end.
	static const bool bCsv = WWOSwitches::Has(TEXT("WWOPerfCsv"));
	static float QuitIn = -1.f;
	if (QuitIn >= 0.f)
	{
		QuitIn -= DeltaTime;
		if (QuitIn < 0.f) { bPerfTest = false; PC->ConsoleCommand(TEXT("quit")); }
		return;
	}
	if (bCsv && !FWWOProf::bOn) PC->ConsoleCommand(TEXT("CsvProfile Start"));
	FWWOProf::bOn = true;
	++ProfFrames;

	if (UpdatePerfSweep(P, DeltaTime)) return;
	const int32 View = AutoMapIndex;
	const bool bDone = UpdateAutoMapTest(P, DeltaTime);
	if (View >= 0 && AutoMapClock > 1.0f)
	{
		if (PerfSums.Num() <= View) { PerfSums.SetNumZeroed(View + 1); PerfFrames.SetNumZeroed(View + 1); }
		const double GpuMs = FPlatformTime::ToMilliseconds64(RHIGetGPUFrameCycles(0));
		if (DeltaTime < 0.1f) // (not the hitch of the GPU profile below)
		{
			PerfSums[View] += FVector2D(DeltaTime * 1000.0, GpuMs);
			++PerfFrames[View];
			PerfGameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
			PerfRenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		}
		if (View == 1 && !bPerfProfiled)
		{
			bPerfProfiled = true;
			PC->ConsoleCommand(TEXT("profilegpu"));
		}
	}
	if (!bDone) return;

	FVector2D Total = FVector2D::ZeroVector;
	int32 Frames = 0;
	for (int32 i = 0; i < PerfSums.Num(); ++i)
	{
		if (PerfFrames[i] == 0) continue;
		const FVector2D Avg = PerfSums[i] / PerfFrames[i];
		UE_LOG(LogWWO, Display, TEXT("PerfTest view %d: frame %.2f ms (%.1f fps), GPU %.2f ms"), i, Avg.X, 1000.0 / Avg.X, Avg.Y);
		Total += PerfSums[i];
		Frames += PerfFrames[i];
	}
	if (Frames > 0)
	{
		const FVector2D Avg = Total / Frames;
		UE_LOG(LogWWO, Display, TEXT("PerfTest %s: frame %.2f ms (%.1f fps), GPU %.2f ms, game thread %.2f ms, render thread %.2f ms over %d frames"),
			*PC->GetWorld()->GetMapName(), Avg.X, 1000.0 / Avg.X, Avg.Y, PerfGameMs / Frames, PerfRenderMs / Frames, Frames);
	}
	LogProf();
	FWWOProf::bOn = false;
	if (bCsv)
	{
		PC->ConsoleCommand(TEXT("CsvProfile Stop"));
		QuitIn = 3.f; // the file is written in the background
		return;
	}
	bPerfTest = false;
	PC->ConsoleCommand(TEXT("quit"));
}
