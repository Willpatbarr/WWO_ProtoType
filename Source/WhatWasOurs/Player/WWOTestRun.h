#pragma once

// What every scripted test run (-WWOAutoTest...) shares: a count of the checks that failed and one
// way out. A check fails by logging a LogWWO line with both "AutoTest" and "FAIL" in it (as the
// tests always have); Watch() counts those lines from the moment a test run starts. Exit() logs
// `AutoTest result: PASS` or `AutoTest result: FAIL (n)` and closes the game, with exit code 1
// when anything failed, so a script can tell without reading the log.

#include "CoreMinimal.h"

namespace WWOTest
{
	void Watch();     // start counting (safe to call again)
	int32 Failures();
	void Exit();
}
