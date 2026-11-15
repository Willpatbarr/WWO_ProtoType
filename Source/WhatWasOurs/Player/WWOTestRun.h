#pragma once

// What every scripted test run (-WWOAutoTest...) shares.
// - A count of the checks that failed and one way out. A check fails by logging a LogWWO line with
//   both "AutoTest" and "FAIL" in it (as the tests always have); Watch() counts those lines from the
//   moment a test run starts. Exit() logs `AutoTest result: PASS` or `AutoTest result: FAIL (n)` and
//   closes the game, with exit code 1 when anything failed, so a script can tell without reading
//   the log.
// - The loadout guard. A test run equips what it likes; the owner's Saved/SaveGames/Loadout.sav
//   comes back byte for byte when the game closes (GuardLoadout).
// - The small things every test wrote for itself: Crossed (a step clock), Check (the ok / FAIL
//   line), Shot (a picture in Saved/AutoTest).

#include "CoreMinimal.h"

namespace WWOTest
{
	void Watch();     // start counting (safe to call again)
	int32 Failures();
	void Exit();

	// Copies the saved loadout aside, the first time it is called in this process (so it holds over
	// map loads), and puts those exact bytes back when the game shuts down, after the game's own
	// last save. If there was no save, whatever the run wrote is deleted instead. Every -WWOAutoTest
	// run calls this as it starts (the player controller's BeginPlay); a test under another switch
	// that changes the loadout calls it before it does. A copy on disk (Loadout.autotest-backup)
	// covers a run that is killed half way: the next test run puts it back before it starts.
	void GuardLoadout();
	// Puts the saved file back now (the guard still does so again at shutdown). For a test that has
	// finished with the loadout mid-run. FlushSave the loadout first if a write is still due.
	void RestoreLoadout();

	// A step on a clock that only counts up: true on the one frame the clock passes T.
	// Before is the clock before this frame's time was added, Now after.
	inline bool Crossed(float Before, float Now, float T) { return Before < T && Now >= T; }

	// Logs `AutoTest <Tag>: <What> ok` or `... FAIL` (which Watch() counts). Returns bOk.
	bool Check(const TCHAR* Tag, bool bOk, const FString& What);

	// Saved/AutoTest/<Name>.png, and a screenshot saved there (the HUD and menus included).
	FString ShotPath(const FString& Name);
	FString Shot(const FString& Name); // returns the path
}
