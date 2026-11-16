# Testing

There are no unit tests. The game tests itself: a command-line switch makes it play a scripted run, log
checks, save screenshots and quit with an exit code.

## Running a test

```
pkill -f "WhatWasOurs.uproject.*-game"
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/WWO/Maps/L_Arena \
  -game -windowed -ResX=1600 -ResY=900 -WWOAutoTest -abslog=/tmp/run.log
```

- Result: the run ends with `AutoTest result: PASS` or `AutoTest result: FAIL (n)`; the exit code is 1 on
  failure. Find the failures with `grep "AutoTest.*FAIL" /tmp/run.log`.
- Your saved loadout is safe: a test run copies `Saved/SaveGames/Loadout.sav` aside as it starts and puts
  the same bytes back as the game closes (a `TestRun: saved loadout put back ...` line says when it had
  to). A run that is killed leaves `Loadout.autotest-backup` beside it; the next test run puts that back.
- Screenshots: `Saved/AutoTest/<NN>_<name>.png`. The number prefix groups them by test.
- Pick the narrowest switch that covers your change. The full run takes several minutes.

## Regression scenarios

`Tools/regress.sh` runs `-WWORegress`: a few hundred tiny scenarios ("one body shot with the rifle at a
dummy 10 m away", "the poison spike next to an other-team player", "Focus with 5 hits banked"), each set
up, run for a fixed number of ticks, recorded and torn down, and compared value for value with a golden
file in git, `Tools/regress/golden.json`. The numbers are exactly the same every run, so a refactor that
keeps behaviour passes with no differences at all; any difference is a change in behaviour.

**Before a refactor that touches gameplay logic, run `Tools/regress.sh`: it must pass before and after.**

```
Tools/regress.sh                    # both modes (no rendering, then a small window); about 14 min
Tools/regress.sh norender           # no rendering only: about 20 s in all (the scenarios take about 12 s)
Tools/regress.sh render             # the small window only
Tools/regress.sh norender -WWORegressOnly=perk.   # the scenarios whose name contains "perk."
```

- Output: one `Regress <name>: key=value ...` line per scenario in the log (`Saved/Regress/run-<mode>.log`),
  `Regress DIFF <name>.<key>: golden X, now Y` for each difference, then `Regress result: PASS` or
  `FAIL (n)`; exit code 1 on failure. With both modes the script also fails if the two modes disagree on a
  scenario that should give the same numbers in both (`Regress MODES DIFF ...`). Results:
  `Saved/Regress/results.json` (and `results-<mode>.json`), sorted, one value per line.
- Windows: `UnrealEditor.exe <proj> /Game/WWO/Maps/L_TestRange -game -nullrhi -nosound -WWORegress
  -abslog=<log>` (no rendering), or with `-windowed -ResX=640 -ResY=360` instead of `-nullrhi`; the exit code
  is the result. (The golden was recorded on the Mac; a Windows run is expected to match, but that has not
  been checked.)
- The saved loadout, settings and key bindings are never written; the run checks that `Loadout.sav` has
  the same bytes at the end (`Regress: saved loadout untouched`).

### What makes it exact

- **A fixed step**: the runner sets `FApp::SetUseFixedTimeStep` with 1/60 s, so every frame is 1/60 s of
  game time however long it takes, and runs as fast as the machine allows. Scenarios count ticks.
- **Gameplay's own random numbers**: gameplay draws through `WWO::FRand` / `FRandRange` / `RandRange` and
  `WWO::RandomInCone` (`WWOTypes.h`). In a normal run those are exactly `FMath`'s; under `-WWORegress`
  they come from a stream seeded at the start of every scenario from its name, so a scenario's numbers
  don't depend on what ran before it, on a filter, or on the order. The engine's shared generator is also
  drawn on by effects, sounds and (when the game draws) other threads, which is why it can't be used.
  Effects, flashes and sounds stay on `FMath` (they don't change any result).
- **The same start**: the empty south end of `L_TestRange` (its dummies, enemies and allies are removed as
  the run starts), the player reset (health, shield, gun, perks, kit charges, status, movement, view) and
  put on the same spot, a fixed loadout (first flora, first weapon, no mods, grenade, abilities or traits,
  Standard frame parts), the shipped settings (`Config/ShippedSettings.json` only), no real input (the
  input mappings are cleared), the HUD hidden, `-nosound` (the sound player's duplicate filter uses real
  time). Each scenario waits 45 ticks after its setup (the weapon swap, eased values at rest).
- **Teardown**: everything of this game's classes that wasn't there when the run started is destroyed
  (what the scenario spawned and what that left: grenades, pools, fog, walls, effects), tuned values put
  back, the player reset again.
- Enemies and allies are inert (their data asset's `AiEnabled` is 0 for the scenario) and bots are parked
  with a fixed kit, except in the `enemy.<kind>.attacks` scenarios (AI on from tick 0).
- **Values are written exactly** (`%.9g`, every float bit), with three stated exceptions, each found by
  running the suite in reverse order (`-WWORegressReverse`): positions (`RecordVec`) to 0.01 cm and view
  angles (`kick_pitch`, `kick_yaw`) to 0.0001 degrees, because the player's eye height carries a float
  residue of about 6e-5 cm that depends on which scenarios ran before (the camera's relative height after
  some earlier scenarios; not pinned down further); and status timers (`slow_left`, `blind_left`) to
  0.1 s, because which of two actors ticks first in a frame is the engine's choice (the order of its tick
  set) and can change with what ran before, which moves a status by one tick. Within one order every
  value is bit-identical run to run.

### Modes

The suite runs without rendering (`-nullrhi`) and in a 640 x 360 window. Each scenario says whether its
numbers hold in both modes (`EWWORegressModes::Same`, the default), legitimately differ (`PerMode`: golden
keeps `<name>@norender` and `<name>@render`), or need rendering (`RenderOnly`: skipped, and logged as
skipped, without it). Today 11 scenarios are `PerMode` and none `RenderOnly`; every other scenario gives
identical numbers in both modes:

- `view.reticle_and_marks.*`: without rendering the viewport has no size, so the low reticle's angle is 0
  and nothing is marked (marking works in HUD pixels on the screen).
- `grenade.<id>.throw`: the grenade leaves from a point fixed to the camera, which the reticle angle tilts,
  so with no viewport it leaves about 5 cm lower and lands elsewhere.

The window mode runs at the display's 60 Hz (about real time: the fixed step doesn't help when each frame
waits for the screen), so it is the slow half. Never use `-fullscreen` (it hangs on the Mac).

### Adding a scenario

A scenario is an `FWWORegressScenario` (`Player/WWORegress.h`): a name, `bPvP`, `Setup` (spawn what it
needs with `SpawnDummy` / `SpawnEnemy` / `SpawnAlly` / `SpawnBot`, equip with `EquipWeapon`, `EquipMod`,
`EquipFlora`, `EquipGrenade`, `EquipAbility`, change a float tunable for this scenario only with `Tune`) and
`Step` (called every tick from 0; script input with `Press` / `Hold` / `Move` / `AimAt`; `Record` the
numbers; return true when done). Add it to the list in one of the family files
(`Player/WWORegressGuns.cpp`, `WWORegressKit.cpp`, `WWORegressCombat.cpp`), generating from the catalogs
where it is natural. Family functions are members of `FWWORegress`, which the gameplay classes name as a
friend, so a scenario can set a perk's private state (`G.HitStreak = 5`). See `RECIPES.md`.

### Updating the golden file

`Tools/regress.sh both -WWORegressUpdate` rewrites the golden entries this run produced (a filtered run only
its own). **Only for an intended behaviour change** (a new scenario, a retune, a fix), in its own commit
that says what changed and why, never to make a refactor pass: a refactor that needs a new golden changed
behaviour. Run the suite again afterwards; it must pass.

## Switches

### Scripted tests

| Switch | Covers | Map |
|---|---|---|
| `-WWOAutoTest` | Everything below in one run: fire, ADS, movement, jumps, every gun, grenade, melee, ability and ult, menus, bodies, grafts, maps, mods | `L_Arena` |
| `-WWOAutoTestMenus` | Both menus by injected controller and keyboard input (about 30 s) | any |
| `-WWOAutoTestBody` | Body states, your own body in third person, first-person hands (about 1 min) | `L_Arena` |
| `-WWOAutoTestGrafts` | Six loadouts in the character preview (about 25 s) | any |
| `-WWOAutoTestMaps` | Fixed camera views of the level (about 25 s) | every map |
| `-WWOAutoTestMods` | Weapon mods and perks against the mod data | `L_TestRange` |
| `-WWOAutoTestGuns`, `-WWOAutoTestHidden`, `-WWOAutoTestFrames` | Variants inside the mods test: gun screenshots, hidden aim assist, the newer frames and burst counts | `L_TestRange` |
| `-WWOAutoTestTabs` | Character menu's Weapon and Shell tabs fit on one screen | any |
| `-WWOAutoTestVignette`, `-WWOAutoTestPose` | Low-health vignette steps; your body seen from outside | any |
| `-WWOAutoTestTitle` | Launch screen, a local PvP match, quit to title, host (no map on the command line) | none |
| `-WWOAutoTestTitleJoin`, `-WWOAutoTestTitleJoinFail` | A second copy joins the first; a join that fails | none |
| `-WWOAutoTestNet`, `-WWOAutoTestPvP` | Host and joiner: co-op, and team deathmatch with forged-shot checks | two copies |
| `-WWOTester -WWOAutoTestTester` | The firing tester (`-WWOTesterGun=<id>`, `-WWOTesterFrames=N`, `-WWOTesterHip`, `-WWOTesterStrafe`, `-WWOTesterNoShot`, `-WWOTesterNoMods`) | `L_TestRange` |
| `-WWOWaveTest` | All 8 waves with god mode; logs AI state and flags anyone stuck (about 4.5 min) | `L_Arena` |
| `-WWORegress` | The regression scenarios against `Tools/regress/golden.json` (`-WWORegressOnly=<text>`, `-WWORegressUpdate`, `-WWORegressReverse`); run it through `Tools/regress.sh` | `L_TestRange` |
| `-WWOTestScoreboard` | Match-end scoreboard (`-WWOKillTarget=N`) | PvP |

### Bot matches and stress

| Switch | Does |
|---|---|
| `-WWOHost`, `-WWOHostPvP`, `-WWOJoin[=address]` | Host co-op, host PvP, join |
| `-WWOBots=N` | Number of bots (PvP) |
| `-WWOStress`, `-WWOStressTime=S` | Timed bot match; logs `Bots:` lines every 10 s. Also turns off mouse, sticks and saved keybinds |
| `-WWOBotsIdle`, `-WWOBotsPeace`, `-WWOBotsLineUp`, `-WWOBotsMarch`, `-WWOBotsStopGo`, `-WWOBotsBusy` | Canned bot behaviour: stand still, don't fight, line up, walk, stop and go, throw and cast constantly |
| `-WWOBotShots`, `-WWOStressShots`, `-WWOFxShots`, `-WWOFxGallery` | Screenshot runs: PvP views, gun effects, the effects gallery |
| `-WWOTestKills`, `-WWOTestFog`, `-WWOTestCharge` | Kill feed and popups; spore fog damage; Static Charge (the last two equip an ability; the loadout guard puts the saved loadout back) |

### Performance

| Switch | Does |
|---|---|
| `-WWOPerfTest` | Walks the map views, logs frame, game-thread, render-thread and GPU time per view (`PerfTest ...`), then quits |
| `-WWOPerfSweep="cvar value\|cvar value,cvar value"` | Holds one view (`-WWOPerfView=N`) and prints each entry's frame time against the plain view |
| `-WWOPerfMove`, `-WWOPerfCsv`, `-WWOProfileGPU` | Swing the camera while measuring; record the engine's CSV profile; dump `profilegpu` |

Timed functions are wrapped in `WWO_PROF` (`Player/WWOProf.h`) and show as `PerfTest code ...` lines.

### Other

`-WWOTitle` forces the launch screen. `-WWOGamerTag=<name>` sets the tag for one run; `-WWONoTestTag`
leaves it empty. `-WWOTimeLimit=S` sets the PvP time limit. Console: `wwo.SkipWave`, `wwo.Wave <n>`.

**Any `-WWO...` switch on the command line skips the launch screen**, and a switch containing
`WWOAutoTest` marks the run as a test run (keybinds are not loaded or saved, the gamer tag is `tester`,
the scoreboard is suppressed, the ult starts full).

Code reads switches through `WWOSwitches` (`Has`, `Value`), never `FParse` directly. `WWOSwitches.h`
lists every switch in a comment table, and has one named predicate for each meaning of "test run"
(they match different text: read the comments before picking one). A new switch goes in that table and,
if a person would type it, in the tables above.

## How a scripted test is built

- All test state and code is in the test driver, `FWWOAutoTest` (`Player/WWOAutoTest.h`), not in the
  player controller. `AWWOPlayerController::BeginPlay` calls `FWWOAutoTest::MakeIfAsked`, which reads the
  switches (`FWWOAutoTestModes::Read`, through `WWOSwitches::Has`) and makes a driver only when one asks
  for a test; the controller keeps that one pointer and calls `AutoTest->Tick` once a frame from its own
  `Tick`. With no test switch there is no driver. `FWWOAutoTest::Tick` calls the one `UpdateAuto*Test` for
  the mode. Each test lives in its own `Player/WWOAutoTest<Name>.cpp` (the perf test in `WWOPerfTest.cpp`).
- The driver reaches its controller through `PC->` and is a `friend` of it: tests write the controller's
  input arrays (`PC->Held`, `PC->JustPressed`, `PC->MoveValue`, `PC->LookValue`) and use its menus and
  widgets directly. Actors a test spawns and keeps are referenced in `AddReferencedObjects`.
- The full run is a ladder of timed steps in `UpdateAutoTest` (`WWOAutoTest.cpp`): a step id is seconds
  x 10, tested with `At(T)` / `Next(T)`. Sub-tests return `true` when done.
- Sub-tests with their own clock step with `WWOTest::Crossed(Before, Now, T)`: true on the one frame
  the clock passes `T`.
- `Shot(name)` saves `Saved/AutoTest/<name>.png` and logs the frame's timings. Leave a gap after it: a
  screenshot frame is long. Outside the driver (game mode test code): `WWOTest::Shot(name)`, or
  `WWOTest::ShotPath(name)` for a picture taken later from a timer.
- A check fails by logging to `LogWWO` a line containing both `AutoTest` and `FAIL`:
  `WWOTest::Check(TEXT("<name>"), bOk, What)` writes `AutoTest <name>: <What> ok` or `... FAIL`.
  `WWOTest::Watch` counts them; `WWOTest::Exit` logs the result and sets the exit code
  (`Player/WWOTestRun.*`).
- The loadout: `WWOTest::GuardLoadout` runs when any `-WWOAutoTest...` run starts (once a process, so it
  holds over map loads) and restores `Loadout.sav` at shutdown, after the game's own last save. A test
  equips what it needs and does not back anything up. One that is done with the loadout mid-run can
  call `WWOTest::RestoreLoadout()` to put the file back at once. Test code under any other switch that
  changes the loadout calls `GuardLoadout()` first (the stress harness does for `-WWOTestFog` / `-WWOTestCharge`).
- The game mode's stress and screenshot runs are `FWWOStressHarness` (`Game/WWOGameModeStress.*`), made
  only when the command line has a `WWOStress` switch; the wave test is `Game/WWOGameModeWaveTest.cpp`.
  Neither goes through `WWOTest::Exit`: they print no result line and exit 0.
- Tests run on real time (`FApp::GetDeltaTime`), because menus pause the game.
- Test state that must survive a map load has to be file-scope static, not a driver member: the
  controller, and its driver with it, is recreated with the map.
- `JustPressed` is cleared at the top of `PlayerTick`; tests set input from the controller's `Tick`
  (where the driver is ticked). `PlayerTick` does not run while paused, `Tick` does.

Adding a test: see `RECIPES.md`.
