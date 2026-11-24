#pragma once

// The game's command-line switches: the one place in code that lists them and reads them.
// A new switch is read with WWOSwitches::Has / Value and added to the table below (and, for one a
// person would type, to docs/TESTING.md).
//
// Scripted tests (Player/WWOAutoTest*.cpp)
//   -WWOAutoTest                the full scripted play-through
//   -WWOAutoTestMenus           menus by injected controller and keyboard input
//   -WWOAutoTestBody            body states, third person, first-person hands
//   -WWOBodyFlora=<id>          with -WWOAutoTestBody: wear that flora (its grafts) for the third-person shots
//   -WWOShell=<n> or <h,c,a,l>  every robot wears shell n (0 base, 1 sleek, 2 scout, 3 tan, 4 bone-white), or a shell per part: head, core, arms, legs (also the wwo.Shell console command)
//   -WWOShellColor=<slot>:<role>:<hex|default>[,...]  your loadout's shell colours for this run, in memory only (the loadout is not saved
//                               that run); slot head|core|arms|legs|skeleton|all, role primary|secondary|glow (also the wwo.ShellColor console command, which saves)
//   -WWOAnchorGrafts            robots place flora grafts from their shell parts' anchors (Config/GraftRecipes.json), as wwo.AnchorGrafts 1
//                               (wwo.ShellAnchors 0|1|2 draws those anchors and paths; not a switch: -ExecCmds="wwo.ShellAnchors 2")
//   -WWOAutoTestGrafts          eight loadouts in the character preview
//   -WWOGraftsCloseYaw=<deg>    its close-up shots (35_*) turned this far instead of -35 (90: the robot's side)
//   -WWOAutoTestMaps            fixed camera views of the level
//   -WWOAutoTestMods            weapon mods and perks against the mod data
//   -WWOAutoTestGuns            (mods test) gun model screenshots
//   -WWOAutoTestHidden          (mods test) hidden aim assist, off then on
//   -WWOAutoTestFrames          (mods test) the newer frames and burst counts
//   -WWOAutoTestTabs            character menu's Weapon and Shell tabs fit on one screen
//   -WWOAutoTestHover           character menu's hover rows on screen and clickable with many saved configs
//   -WWOHoverCounts=<a,b,...>   (hover test) these numbers of saved configs instead of 3,12,40,150
//   -WWOHoverGC                 (hover test) force a garbage collection before each weapon slot's walk (crash repro)
//   -WWOAutoTestVignette        (tabs test) low-health vignette steps
//   -WWOAutoTestPose            (tabs test) your body seen from outside
//   -WWOPoseWeapon=<id>         (pose test) with that weapon in hand; changes the saved loadout
//   -WWOAutoTestTitle           launch screen, a local PvP match, quit to title, host
//   -WWOAutoTestTitleJoin       a second copy joins the first
//   -WWOAutoTestTitleJoinFail   a join that fails
//   -WWOTitleHostTime=<s>       (title test) how long the host stays up for a joiner
//   -WWOAutoTestNet             host and joiner, co-op
//   -WWOAutoTestPvP             host and joiner, team deathmatch with forged-shot checks
//   -WWOAutoTestTester          the firing tester (with -WWOTester)
//   -WWOAutoTestFootRace        FootRace's breach portals (on L_FootRaceTest)
//   -WWOAutoTestFootRaceBots    a FootRace bot match played to a win (on L_FootRaceTest, with -WWOBots=7)
//   -WWOAutoTestSurfaces        pools, spore patches and tripmines on the dressed FootRace map's surfaces
//   -WWOAutoTestLobby           FootRace's lobby: teams, switching, bots per team, the start
//   -WWOFootRaceLobby           open FootRace's lobby even with other -WWO switches (any -WWO switch skips it otherwise)
//   -WWOTester                  open the firing tester (also the ?tester map option)
//   -WWOTesterGun=<id>          (tester test) equip that gun
//   -WWOTesterNoMods            (tester test) the bare frame
//   -WWOTesterFrames=<n>        (tester test) save n frames in a row around one shot
//   -WWOTesterHip / -WWOTesterStrafe / -WWOTesterNoShot   (tester frames) from the hip; strafing; no shot
//   -WWORegress                 the regression scenarios (Player/WWORegress*.cpp), compared with Tools/regress/golden.json
//   -WWORegressOnly=<a,b>       (regress) only the scenarios whose name contains one of those texts
//   -WWORegressPerModeOnly      (regress) only the scenarios with one golden entry per mode (the drawn-only check)
//   -WWORegressUpdate           (regress) rewrite the golden file from this run (this mode's entries)
//   -WWORegressUpdateNew        (regress) add the golden entries this run made that it lacks; compare the rest
//   -WWORegressReverse          (regress) run the scenarios in reverse order (a check that they are independent)
//   -WWOWaveTest                all 8 waves with god mode, logging AI state
//   -WWOTestScoreboard          pictures of the match-end scoreboard, then NEXT MATCH
//   -WWOKillTarget=<n>          PvP kill target
//   -WWOTimeLimit=<s>           PvP time limit
//
// Sessions and the launch screen (Net/WWONetSubsystem.cpp)
//   -WWOHost / -WWOHostPvP      host co-op / host PvP on the current map
//   -WWOJoin[=<address>]        join that address, or find a LAN game
//   -WWOTitle                   force the launch screen
//   -WWOGamerTag=<name>         this run's gamer tag (not saved)
//   -WWONoTestTag               a test run keeps an empty tag instead of "tester"
//
// Bot matches and stress (Game/WWOGameMode.cpp, Player/WWOBotController.cpp)
//   -WWOBots=<n>                number of bots (PvP, 0 to 11)
//   -WWOBotFlora=<id>           every bot is that flora (voltvine, ironbark, veilspore)
//   -WWOStress                  timed bot match: logs load, ignores mouse and sticks and saved keybinds
//   -WWOStressTime=<s>          quit after that long
//   -WWOStressShots             the host's view of other players, saved every 4 s
//   -WWOBotsIdle / -WWOBotsPeace / -WWOBotsLineUp / -WWOBotsMarch / -WWOBotsStopGo / -WWOBotsBusy
//                               canned bot behaviour: stand still, don't fight, line up, walk, stop and go, throw and cast constantly
//   -WWOBotShots                PvP view screenshots
//   -WWOFxShots                 (mods test) gun effect screenshots
//   -WWOFxGallery               the effects gallery screenshots
//   -WWOTestKills / -WWOTestFog / -WWOTestCharge   kill feed and popups; spore fog damage; Static Charge
//   -WWOFogTest=<n>             n thick spore fogs around the host 3 s in
//   -WWOFogAhead=<m>            (fog test) how far in front
//   -WWOProfileGPU              one GPU pass breakdown in the log, and a screenshot 2 s before it
//   -WWOProfileAt=<s>           (profile) when; 12 s without it
//
// Performance (Player/WWOPerfTest.cpp)
//   -WWOPerfTest                walk the map views logging frame times, then quit
//   -WWOPerfSweep="cvar value|cvar value,cvar value"   hold one view and time each entry
//   -WWOPerfView=<n>            (sweep) which view
//   -WWOPerfMove / -WWOPerfCsv  swing the camera while measuring; record the engine's CSV profile

#include "CoreMinimal.h"

// Game thread only. The command line does not change during a run, so answers are remembered.
namespace WWOSwitches
{
	// Whether -<Name> is on the command line as a whole switch (FParse::Param: "-WWOStress" is not
	// "-WWOStressShots"). Name is written without the dash. Parsed once per name a run.
	bool Has(const TCHAR* Name);

	// FParse::Value for the -WWOX=value forms. Match is written without the dash and with the '=',
	// e.g. Value(TEXT("WWOBots="), Text). False when the switch isn't there.
	// Not remembered: call at start-up or on an event, not every frame.
	bool Value(const TCHAR* Match, FString& Out, bool bStopOnSeparator = true);
	bool Value(const TCHAR* Match, int32& Out);
	bool Value(const TCHAR* Match, float& Out);

	// --- "Is this a test run": five different questions the code asks. They are not interchangeable. ---

	// The text "WWOAutoTest" appears anywhere on the command line, in any case, with or without a
	// dash: any -WWOAutoTest... switch, but also e.g. a log path with that text in it.
	// (FAIL-line counting, the game mode's bTestRun, no match-end scoreboard, the ult starts full in PvP.)
	bool HasAutoTestText();

	// The text "-WWOAutoTest" (with its dash) appears on the command line, in any case: any
	// -WWOAutoTest... switch. Narrower than HasAutoTestText. (The gamer tag "tester".)
	bool HasAutoTestSwitch();

	// The text "-WWOAutoTestTitle" appears on the command line, in any case: -WWOAutoTestTitle,
	// -WWOAutoTestTitleJoin or -WWOAutoTestTitleJoinFail. (These open on the launch screen.)
	bool HasTitleTestSwitch();

	// The text "-WWO" appears on the command line, in any case: any of this game's switches at all.
	// (A developer's direct start: skips the launch screen.)
	bool HasAnyWWOSwitch();

	// The text "/Game/" appears on the command line, in any case: a map (or other asset path) was
	// named. (A developer's direct start: skips the launch screen.)
	bool NamesGamePath();

	// Exactly the switch -WWOStress. (Stress logging, ignoring the mouse and sticks.)
	bool IsStress();

	// The text "WWOStress" appears anywhere on the command line, in any case: -WWOStress, but also
	// -WWOStressShots or -WWOStressTime=... on their own. (The game mode's bot diagnostics.)
	bool HasStressText();

	// Exactly the switch -WWORegress: the regression scenarios. (Shipped settings only, default keys.)
	bool IsRegress();

	// HasAutoTestText(), IsStress() or IsRegress(): a run that presses the default keys, so it neither
	// reads nor writes anyone's saved bindings.
	bool UsesDefaultBinds();
}
