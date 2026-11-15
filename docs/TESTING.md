# Testing

There are no unit tests. The game tests itself: a command-line switch makes it play a scripted run, log
checks, save screenshots and quit with an exit code.

## Running a test

```
pkill -f "WhatWasOurs.uproject.*-game"
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/WWO/Maps/L_Arena \
  -game -windowed -ResX=1600 -ResY=900 -WWOAutoTest -abslog=/tmp/run.log
```

- Result: the last line is `AutoTest result: PASS` or `AutoTest result: FAIL (n)`; the exit code is 1 on
  failure. Find the failures with `grep "AutoTest.*FAIL" /tmp/run.log`.
- Screenshots: `Saved/AutoTest/<NN>_<name>.png`. The number prefix groups them by test.
- Pick the narrowest switch that covers your change. The full run takes several minutes.

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
| `-WWOTestScoreboard` | Match-end scoreboard (`-WWOKillTarget=N`) | PvP |

### Bot matches and stress

| Switch | Does |
|---|---|
| `-WWOHost`, `-WWOHostPvP`, `-WWOJoin[=address]` | Host co-op, host PvP, join |
| `-WWOBots=N` | Number of bots (PvP) |
| `-WWOStress`, `-WWOStressTime=S` | Timed bot match; logs `Bots:` lines every 10 s. Also turns off mouse, sticks and saved keybinds |
| `-WWOBotsIdle`, `-WWOBotsPeace`, `-WWOBotsLineUp`, `-WWOBotsMarch`, `-WWOBotsStopGo`, `-WWOBotsBusy` | Canned bot behaviour: stand still, don't fight, line up, walk, stop and go, throw and cast constantly |
| `-WWOBotShots`, `-WWOStressShots`, `-WWOFxShots`, `-WWOFxGallery` | Screenshot runs: PvP views, gun effects, the effects gallery |
| `-WWOTestKills`, `-WWOTestFog`, `-WWOTestCharge` | Kill feed and popups; spore fog damage; Static Charge (the last two equip an ability: back up `Loadout.sav`) |

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

- `AWWOPlayerController::BeginPlay` reads the switches (`WWOSwitches::Has`); `Tick` calls the one `UpdateAuto*Test` for the
  mode. Each test lives in its own `Player/WWOPlayerControllerAuto<Name>.cpp`.
- The full run is a ladder of timed steps in `UpdateAutoTest`: a step id is seconds x 10, tested with
  `At(T)` / `Next(T)`. Sub-tests return `true` when done.
- `Shot(name)` saves `Saved/AutoTest/<name>.png`. Leave a gap after it: a screenshot frame is long.
- A check fails by logging to `LogWWO` a line containing both `AutoTest` and `FAIL`. `WWOTest::Watch`
  counts them; `WWOTest::Exit` logs the result and sets the exit code (`Player/WWOTestRun.*`).
- Tests run on real time (`FApp::GetDeltaTime`), because menus pause the game.
- Test state that must survive a map load has to be file-scope static, not a controller member: the
  controller is recreated with the map.
- `JustPressed` is cleared at the top of `PlayerTick`; tests set input from `Tick`. `PlayerTick` does not
  run while paused, `Tick` does.

Adding a test: see `RECIPES.md`.
