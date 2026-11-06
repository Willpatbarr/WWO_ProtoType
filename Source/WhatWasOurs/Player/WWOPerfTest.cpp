// -WWOPerfTest: a repeatable performance check. After the level settles it walks the same fixed
// cameras as the map autotest, averages frame time and GPU time at each one, runs one GPU profile
// (profilegpu, into the log), logs a summary and quits. Run it before and after render changes.

#include "Player/WWOPlayerController.h"
#include "Player/WWOCharacter.h"
#include "WhatWasOurs.h"
#include "RHI.h"
#include "Engine/World.h"

void AWWOPlayerController::UpdatePerfTest(AWWOCharacter* P, float DeltaTime)
{
	PerfStartClock += DeltaTime;
	if (PerfStartClock < 6.f) return; // shaders, streaming and the first sky capture settle

	const int32 View = AutoMapIndex;
	const bool bDone = UpdateAutoMapTest(P, DeltaTime);
	if (View >= 0 && AutoMapClock > 1.0f)
	{
		if (PerfSums.Num() <= View) { PerfSums.SetNumZeroed(View + 1); PerfFrames.SetNumZeroed(View + 1); }
		const double GpuMs = FPlatformTime::ToMilliseconds64(RHIGetGPUFrameCycles(0));
		PerfSums[View] += FVector2D(DeltaTime * 1000.0, GpuMs);
		++PerfFrames[View];
		if (View == 1 && !bPerfProfiled)
		{
			bPerfProfiled = true;
			ConsoleCommand(TEXT("profilegpu"));
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
		UE_LOG(LogWWO, Display, TEXT("PerfTest %s: frame %.2f ms (%.1f fps), GPU %.2f ms over %d frames"),
			*GetWorld()->GetMapName(), Avg.X, 1000.0 / Avg.X, Avg.Y, Frames);
	}
	bPerfTest = false;
	ConsoleCommand(TEXT("quit"));
}
