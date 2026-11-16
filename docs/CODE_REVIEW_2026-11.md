# Code quality review, November 2026

A read-through of all of `Source/WhatWasOurs` (102 files, about 34,000 lines) after pre-alpha 1.1.0,
looking for redundancy, readability and how hard the code is to change. This is the list of structural
debt and the order to pay it in. Update the **Status** column as stages land; delete a row's detail once
it is done and the code speaks for itself.

## Summary

The game logic is sound and the data flow is consistent (see `ARCHITECTURE.md`). The debt is the kind
that comes from many sessions each solving their own problem:

1. **Six god classes** hold about a third of the code: `UWWOWeaponComponent` (2.2k lines),
   `UWWOSettingsMenu` (1.9k), `UWWOCharacterMenu` (1.8k), `AWWOCharacter` (2.0k across two files),
   `AWWOPlayerController` (1.4k plus eleven test files), `AWWOGameMode` (1.1k).
2. **Test code is compiled into gameplay classes.** About 40% of the player controller and of the game
   mode is test harness, with no shipping guard.
3. **The same small job is written many times**: JSON config loading (7 loaders), UI button and text
   factories (4 each), area and target queries (about 8 shapes), AI path following (3), the "is this my
   human player" test (11 spellings), "is this a test run" (5), the predict / confirm / refuse flow (3).
4. **Behaviour is keyed by string id in scattered places.** About 45 ability, perk and grenade ids, each
   compared in 2 to 5 files, so adding an ability or perk means finding every branch.
5. **Lists that must stay in step by hand**: movement state (5 places), move stats (5 parallel lists),
   perks (4 places), settings row / preference flag / tab placement (3 tables).

## Roadmap

Ordered so that each stage makes the next one smaller and safer. Size: S under a day, M a few days,
L a week or more of agent time including test runs. Risk is the risk of changing behaviour.

### Stage 1: shared foundations and dead code

| # | Change | Size | Risk | Status |
|---|---|---|---|---|
| 1.1 | Delete the old single first-person arms (`PlaceArm`, `BuildArm`, `FWWOFPArm` meshes, `WWOFPArmPaths.h`, their settings rows) and other unused members (`IWWOTargetable::IsEnemy`, `UWWOLoadoutSubsystem::NetChanges`) | S | low | done |
| 1.2 | One config loader (`WWOConfig`: load by name, cached; saved file with shipped fallback) and shared JSON readers (vector, colour) replacing the seven hand-rolled loaders and the two copies of `ModelConfig` | S-M | low | done (`Systems/WWOConfig`). Left: the weapon component still keeps its own named colours beside the palette in `WWOConfig::Color`; the weapon model is still read field by field when it is built (no longer every frame; 4.4) |
| 1.3 | One home for command-line switches (`WWOSwitches`: cached `IsTestRun()`, `IsStress()`, bot switches), replacing the five spellings of "test run" and the per-frame parses | S-M | low | done (`WWOSwitches`, one predicate per exact meaning; none merged). Left: deciding whether the text matches (`HasAutoTestText`, `HasStressText`) should become whole-switch tests is a behaviour change |
| 1.4 | Named role predicates on the character (`IsLocalHuman`, `IsJoiner`, `IsHostCopyOfRemote`) and one `IsOnline` | S | low | part: `IsLocalHuman`, `IsHostCopyOfRemote`, `WWO::IsOnline` |
| 1.5 | `IWWOTargetable::TakeHit` as one non-virtual wrapper over a virtual `ApplyHit` (three identical copies today) | S | low | done |
| 1.6 | Small duplicate helpers folded into `WWO::` / `WWOFX`: `BackOut` (3), spawn params (7 files), point-light setup (6), exponential smoothing, team counts and "place at PvP spawn" in the game mode | S | low | part: `WWO::BackOut`, `Damp`, `AlwaysSpawn`, `AlwaysSpawnDeferred`, `WWOFX::AddPointLight`, the game mode's `CountTeams`, the net subsystem's `EndSession`. Left because the copies differ: the target's ease-in back, the lights of the ult orb, lightning bolt and discharge, the bot count in `RetireBotFor`, and "place at PvP spawn" (three sites, three different sets of steps) |

### Stage 2: test code out of gameplay classes

| # | Change | Size | Risk | Status |
|---|---|---|---|---|
| 2.1 | Game mode's stress and screenshot harness (`LogStress`, the test half of `UpdateBots`) into its own object and file, created only under `-WWOStress` | S | low | done (`FWWOStressHarness`, `Game/WWOGameModeStress.*`; the game mode calls it from two places). Left: its function-local statics stay statics, because they last across a match restart and members would not; the canned-bot switches in the spawn half of `UpdateBots` (`-WWOBotsPeace`, `-WWOBotsLineUp`) are still inline (4.8) |
| 2.2 | Shared test kit in `WWOTestRun`: one step clock (`Crossed`, `Step`, `Next`), one `Check`, one `Shot`, one `Exit`, a scoped loadout backup, a two-player session helper | M | low | part: `WWOTest::GuardLoadout` / `RestoreLoadout` (one loadout backup for every test run, put back at shutdown), `Crossed`, `Check`, `Shot` / `ShotPath`. Left: `Step` / `Next` (each test's step counter is a controller member with its own shape: 2.3), the two-player session helper, and the three ways out that are not `WWOTest::Exit` (perf test `quit`, wave test and stress `RequestExit`: they print no result line and always exit 0, which `Exit` would change) |
| 2.3 | Autotest state and code off `AWWOPlayerController`: a test driver per test owning its own state, the controller keeping one pointer; a narrow scripted-input API instead of writing the controller's private input arrays; compiled out of shipping builds | L | low-med | part: all test state and code moved to one driver, `FWWOAutoTest` (`Player/WWOAutoTest.h`, `WWOAutoTest*.cpp`, `WWOPerfTest.cpp`); the controller keeps one pointer, makes the driver in `BeginPlay` only for a test switch and ticks it once from `Tick` (header 325 to 185 lines, .cpp 1405 to 797). Left: one driver for every test, not one each (the full run's sub-tests share state); the driver is a `friend` and writes the input arrays through `PC->` (no narrow input API yet); the `-WWOTestScoreboard` steps are still inside `UpdateScoreboard`; not compiled out of shipping (packaged Development builds run the tests): a guard would wrap `WWOAutoTest*.cpp`, `WWOPerfTest.cpp`, the header, and the controller's `friend` line, pointer, `MakeIfAsked` call and `Tick` call |
| 2.4 | Test tallies and probes (`Net*` statics, `ProbeHeadShots`, `SeenRemote`, `DebugGunOffset`) grouped in one non-shipping struct | S | low | not started: the tallies are incremented in eight gameplay files (character, gun net, movement, grenade component, grenade, spore pool, enemy projectile, weapon component), so grouping them is more than a trivial edit there. Their only readers are `WWOAutoTestNet.cpp`, `WWOAutoTestPvP.cpp`, `WWOAutoTestBody.cpp` and the stress harness |

### Stage 3: UI

| # | Change | Size | Risk | Status |
|---|---|---|---|---|
| 3.1 | A shared UI style layer (`WWOUI`: palette, `Font`, `RoundBrush`, `Text`, `Spaced`, `Rule`, `Button`, text-box style, `Accent`) and a small menu base class (open / close, cursor paint, click-handler lifetime). Removes the hidden "Styler" settings menu three menus create only to borrow its helpers | M-L | low-med | part: the style layer is done (`UI/WWOUI.*`; `K`, `Font`, `RoundBrush`, `MakeLogo`, `PaintCursor` and the click / slider handler classes moved off `UWWOSettingsMenu`). Every menu builds in its own widget tree; the Styler menus are gone (the firing tester still uses the real settings menu, for its slider rows and `SaveDefaults` only). One `Spaced`, `Rule` / `RuleDown`, `Sized`, `Wide`, `Fill`, `Accent`, `StyleTextBox`. Buttons: `WWOUI::Button` / `TextButton` with a look (`FilledLook`, `CardLook`, `PlainLook`, `StillLook`) and `OnClick`; the character menu's `Card`, the launch menu's `Boxed` and the settings `PlainButton` are now two-line wrappers over it (identical looks); the settings side and key buttons (`KeyLook`), the character menu's `Slot` and the scoreboard's dimmed button keep their own look, because their hovered / pressed shades differ. Left: the menu base class (open / close, cursor paint, one handler-lifetime policy: today each menu keeps its handlers its own way, see `ARCHITECTURE.md`, UI); settings still has thin `MakeText` / `MakeButton` members over `WWOUI` for its many calls. **Palette drifts for the owner to decide** (kept pixel-exact as separate names): card edge alpha 0.45 (settings, launch, scoreboard: `CardEdge`) vs 0.55 (character: `CardEdgeStrong`); secondary text alpha 0.6 (launch, scoreboard, tester, and the character menu's section labels: `Dim`) vs 0.62 (character overview and stat bars: `DimBright`); faint text alpha 0.4 (launch, character and settings key hints: `Faint`) vs 0.35 (scoreboard: `Fainter`); page alpha 1 (character, launch, tester) vs 0.95 (settings) vs 0.94 (scoreboard); the name CardColor meant (0.115, 0.125, 0.148) everywhere but the firing tester, where it was (0.2, 0.21, 0.25), now `ButtonColor`; the character editors' slot fills use both (0.2, 0.21, 0.25) and (0.2, 0.21, 0.26); a dimmed edge is 0.15 (scoreboard) vs 0.2 (character's empty tiles); hovered / pressed cards lighten 8% / 14% (`CardLook`: character cards, launch, scoreboard) vs fixed (0.17, 0.18, 0.21) / (0.22, 0.23, 0.26) shades (settings side and key buttons) vs 12% / 20% with a white edge (character slots) vs +0.08 / +0.15 added (`FilledLook`: settings, tester); corners are 8 px on filled buttons and 5 px on cards |
| 3.2 | Shared page frame and tab strip (the header, tabs and rule are copied in three menus) | M | low | done for the header: `WWOUI::HeaderName`, `TabStrip`, `AddPageHeader` and `SetActiveTab` build and highlight the header, tabs and rule of settings, the character menu and the launch screen's setup page. Left: the page frame itself (the full-window border with its padding) is still built by each menu; the settings sidebar's page list (marks and labels) has its own highlight code |
| 3.3 | HUD: `NativePaint` (690 lines) split into element painters over one context; target marking moved out of paint into game code, so hiding the HUD no longer changes behaviour | M + M | low / med || done. `NativePaint` is 37 lines: it builds one `FWWOHUDFrame` and calls 16 painters in z-order (`UI/WWOHUDPaint.cpp`; state and tick stay in `WWOHUDWidget.cpp`, 146 lines); the painter is `FWWOPainter` (`UI/WWOPainter.h`). Marking is `UWWOWeaponComponent::UpdateMarks`, called from `AWWOPlayerController::UpdateCameraManager` (after all ticks, same camera as the paint), with the marker range still in HUD pixels; the status chips and `ChipPeak` moved to `NativeTick`. Left: the teammate chevrons still trace for line of sight in paint (drawing only, nothing reads it); `UWWOStatBars` and the menu cursor ring keep their own drawing (their font, outline, layer and scale differ from `FWWOPainter`, so switching would change pixels) |
| 3.4 | Settings: registry (sections, rows, defaults, JSON) split from the view; preference flag and tab placement set where a row is registered; data assets contribute their own rows instead of id chains | L | med | part: the file split is done (pure moves; the registered sections, rows, values, defaults and page list are byte-identical, checked with a one-off dump). `WWOSettingsMenu.cpp` 600 lines (menu, `Prepare` cut into `BuildFrame` / `BuildPages` / `BuildSidebar` / `BuildPageArea`, tabs, search), `WWOSettingsSections.cpp` 705 (`RegisterSections` now calls ten `Register...Sections` in the old order; `OwnSettings`, `MarkPreferences`), `WWOSettingsStore.cpp` 130 (defaults, load, save, reset), `WWOSettingsRows.cpp` 364 (rows, browsers, key bindings), shared helpers in `WWOSettingsMenuPrivate.h`. Left: a registry object separate from the widget, the preference flag and tab placement set on the row / section where it is registered (today `OwnSettings` and `TabLayout`), data assets contributing their own rows (abilities are still an id chain in `RegisterAbilitySections`) |
| 3.5 | Character menu: a file per tab, the preview actor in its own file, one hover-popup helper for the two copies, descriptions moved onto the data assets | L | med | part: a file per tab (`WWOCharacterMenu.cpp` 399 lines: frame, tabs, open / close, turning, tick, autotest hooks; `WWOCharacterMenuOverview.cpp` 637: Loadout tab and hover pickers; `Flora` 170; `Weapon` 371: weapons, mods, configs, mod hover, weapon stat bars; `Shell` 44; helpers in `WWOCharacterMenuPrivate.h`) and `AWWOPreviewActor` in `UI/WWOPreviewActor.*` (same class, empty `Tick` override removed; it still ticks as before). The two hover popups share `FWWOHoverPopup`, the close timer only: they still differ in close delay (Loadout row 0.25 s, mod list 0.35 s, both kept), hit test (cached geometry under the cursor vs `IsHovered`), open rule (the Loadout row won't switch while the cursor is on the open row; the mod list is held open by the autotest preview). The never-shown `ExpandedTitle` text is gone. Left: string state keys (`Expanded` "Flora" / "Weapons", `Picker` "ability0") to enums, and the per-ability Build notes chosen by id in `BuildOverview` to the data assets |

Compare menu screenshots before and after each UI stage (`-WWOAutoTestMenus`, `-WWOAutoTestTabs`,
`-WWOAutoTestTitle`).

### Stage 4: gameplay structure

| # | Change | Size | Risk | Status |
|---|---|---|---|---|
| 4.1 | Movement state as one `FWWOMoveState` member on the character (declared in five places today; `ToString` already misses three fields) | M | med | done. `AWWOCharacter::MoveState` holds the 19 fields `GetMoveState` used to copy; the simulation and every reader (gun, bot, autotests) use it directly, and Get / Set are a struct copy. A new field is now the struct plus a `Serialize` line (and `ToString`, which prints every field again). Wire format unchanged (`JumpsUsed` is an int32 in memory, a clamped byte on the wire as before). Timers and easing a correction doesn't restore (camera, bob, `SprintBlend`, `LandKick`, `LastFallSpeed`, lunge steering, `PendingLaunch`, status timers) stay members. Seen while testing, not changed: a joiner's lunge gets 3 small corrections (10-20 cm) because the host caps the lunge's push by its own `LungeSpeed` slider (`UWWOCharacterMovement::MoveAutonomous`), not the joiner's; `LungeSpeed` isn't a sent move stat |
| 4.2 | Move stats from one table of {member, stat name, low, high} instead of five positional lists and a magic 14 | S | low | done: `MoveStatTable` in `WWOCharacter.cpp` (member pointer, loadout stat or null, host clamp low / high) generates `LocalMoveStats`, `BaseMoveStats`, `ApplyMoveBase`, the `ServerSetMoveStats` clamps and `M()`'s sent-stat set. Same order, count, clamps and RPCs |
| 4.3 | One perk table (id, numbers, condition, HUD text) and an `FWWOPerkState`, used by `Shoot`, `RefreshStats`, `PerkStatus` and the host's check; one `ComputeGunStats` shared by the weapon and the host validation | M | med | |
| 4.4 | Split `UWWOWeaponComponent`: weapon model (JSON parsed once into a struct), first-person hands component, melee, view pose; the firing core stays | L | med | first pass done, as pure moves: the class is split into eight files by concern (`Player/WWOWeaponComponent.cpp` core and the per-frame driver, `WWOWeaponFiring`, `WWOWeaponPerks`, `WWOWeaponMelee`, `WWOWeaponModel`, `WWOWeaponView`, `WWOWeaponHands`, `WWOWeaponTargeting`), shared helpers in `WWOWeaponPrivate.h` (`namespace WWOWeapon`, one `ModelConfig`), and the header grouped under one section per file. The hands' per-frame read of the model JSON (grip, fore-end, magazine points) is now read once per `BuildModel` (`ReadHandsHolds`). Checked with tester frame captures at a locked 60 fps against the original code (the run is not deterministic: the punch roll's sign and the burst's side recoil are random, so compared against the spread of four baseline runs). Left: separate components for the hands and the model (changes object layout, tick ownership and Settings bindings), the rest of the model JSON as a parsed struct (`BuildModel` and `UpdateWorldGun` still read fields, only on a build or a gun change), and the perk table (4.3) |
| 4.5 | Split `AWWOCharacter`: recoil component, host hit validator, HUD and audio tunables off the character | M each | low to med | |
| 4.6 | One area query (`UWWOWorldSubsystem::ForEachTargetInRadius` with the measure point and line of sight as parameters) and one best-aim-part-in-cone helper, replacing about eight shapes | M | med | |
| 4.7 | Ability and grenade behaviour by table: id constants (`WWOIds.h`), a per-ability state struct and handler table, `EWWOUltKind` used directly, an enum for kit refusal, a per-kind grenade behaviour table | S to L | low to med | |
| 4.8 | Game mode as a thin shell owning one rules object (waves, team deathmatch, tester) and a bot roster | L | med | |
| 4.9 | Shared AI locomotion: one path follower and `WWONav::WallAhead / LedgeAhead / TurnYaw` for enemies, allies and bots; enemy-to-friendly damage in one function | M | med | |
| 4.10 | Loadout subsystem split into catalog, equipped state, stat maths and mod configs; an enemy registry replacing the per-type pointers and string branches | M | med | |
| 4.11 | Effect lifecycle base (age, grow, fade, host destroys) for pools, patches, walls, roots and fog; game state relay boilerplate behind one helper | S-M | low | |

## Things to leave alone

- Recoil, ADS pivot, kick and burst timing (matched to footage). Move code, never rewrite it.
- The component update order in `AWWOCharacter::Tick` and the `PreparedFrame` guard.
- Settings property names and section keys: they are saved-file keys.
- The `Save` guard in the loadout subsystem that refuses to write when weapons or mods failed to load.

## How to verify a stage

Build, then the narrowest autotests that cover it (`TESTING.md`), then the full `-WWOAutoTest`. For
anything touching replication or host checks, also `-WWOAutoTestNet` and `-WWOAutoTestPvP` with two
copies.

## Known test weaknesses (found while verifying Stages 1 and 2)

- **`AutoTest mods: magazine refilled to N FAIL`** in the full run depends on the saved loadout the run
  starts from: it fails with Will's loadout as of 2026-11-14 and passes from others. The check reads the
  rifle's leftover ammunition, which earlier steps and ammo-refund perks change.
- **Measurement lines vary run to run** (rounds used in the rifle burst ranged 5 to 10 on the same code,
  from perks refunding rounds on crits). Compare stats and PASS / FAIL between runs, not exact lines.
- **`-WWOAutoTestPvP`, "after the joiner's shots"** only logs. When the two random spawns on `L_Arena`
  have a wall between them the joiner's shots cannot land and the run still passes with
  `joiner kills 0`. It should place the two players in sight of each other and fail on zero kills.
