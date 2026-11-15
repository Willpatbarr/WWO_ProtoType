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
| 1.2 | One config loader (`WWOConfig`: load by name, cached; saved file with shipped fallback) and shared JSON readers (vector, colour) replacing the seven hand-rolled loaders and the two copies of `ModelConfig` | S-M | low | done (`Systems/WWOConfig`). Left: the weapon component still keeps its own named colours beside the palette in `WWOConfig::Color`; the weapon model is still read field by field (4.4) |
| 1.3 | One home for command-line switches (`WWOSwitches`: cached `IsTestRun()`, `IsStress()`, bot switches), replacing the five spellings of "test run" and the per-frame parses | S-M | low | done (`WWOSwitches`, one predicate per exact meaning; none merged). Left: deciding whether the text matches (`HasAutoTestText`, `HasStressText`) should become whole-switch tests is a behaviour change |
| 1.4 | Named role predicates on the character (`IsLocalHuman`, `IsJoiner`, `IsHostCopyOfRemote`) and one `IsOnline` | S | low | part: `IsLocalHuman`, `IsHostCopyOfRemote`, `WWO::IsOnline` |
| 1.5 | `IWWOTargetable::TakeHit` as one non-virtual wrapper over a virtual `ApplyHit` (three identical copies today) | S | low | done |
| 1.6 | Small duplicate helpers folded into `WWO::` / `WWOFX`: `BackOut` (3), spawn params (7 files), point-light setup (6), exponential smoothing, team counts and "place at PvP spawn" in the game mode | S | low | part: `WWO::BackOut`, `Damp`, `AlwaysSpawn`, `AlwaysSpawnDeferred`, `WWOFX::AddPointLight`, the game mode's `CountTeams`, the net subsystem's `EndSession`. Left because the copies differ: the target's ease-in back, the lights of the ult orb, lightning bolt and discharge, the bot count in `RetireBotFor`, and "place at PvP spawn" (three sites, three different sets of steps) |

### Stage 2: test code out of gameplay classes

| # | Change | Size | Risk | Status |
|---|---|---|---|---|
| 2.1 | Game mode's stress and screenshot harness (`LogStress`, the test half of `UpdateBots`) into its own object and file, created only under `-WWOStress` | S | low | done (`FWWOStressHarness`, `Game/WWOGameModeStress.*`; the game mode calls it from two places). Left: its function-local statics stay statics, because they last across a match restart and members would not; the canned-bot switches in the spawn half of `UpdateBots` (`-WWOBotsPeace`, `-WWOBotsLineUp`) are still inline (4.8) |
| 2.2 | Shared test kit in `WWOTestRun`: one step clock (`Crossed`, `Step`, `Next`), one `Check`, one `Shot`, one `Exit`, a scoped loadout backup, a two-player session helper | M | low | part: `WWOTest::GuardLoadout` / `RestoreLoadout` (one loadout backup for every test run, put back at shutdown), `Crossed`, `Check`, `Shot` / `ShotPath`. Left: `Step` / `Next` (each test's step counter is a controller member with its own shape: 2.3), the two-player session helper, and the three ways out that are not `WWOTest::Exit` (perf test `quit`, wave test and stress `RequestExit`: they print no result line and always exit 0, which `Exit` would change) |
| 2.3 | Autotest state and code off `AWWOPlayerController`: a test driver per test owning its own state, the controller keeping one pointer; a narrow scripted-input API instead of writing the controller's private input arrays; compiled out of shipping builds | L | low-med | |
| 2.4 | Test tallies and probes (`Net*` statics, `ProbeHeadShots`, `SeenRemote`, `DebugGunOffset`) grouped in one non-shipping struct | S | low | |

### Stage 3: UI

| # | Change | Size | Risk | Status |
|---|---|---|---|---|
| 3.1 | A shared UI style layer (`WWOUI`: palette, `Font`, `RoundBrush`, `Text`, `Spaced`, `Rule`, `Button`, text-box style, `Accent`) and a small menu base class (open / close, cursor paint, click-handler lifetime). Removes the hidden "Styler" settings menu three menus create only to borrow its helpers | M-L | low-med | |
| 3.2 | Shared page frame and tab strip (the header, tabs and rule are copied in three menus) | M | low | |
| 3.3 | HUD: `NativePaint` (690 lines) split into element painters over one context; target marking moved out of paint into game code, so hiding the HUD no longer changes behaviour | M + M | low / med | |
| 3.4 | Settings: registry (sections, rows, defaults, JSON) split from the view; preference flag and tab placement set where a row is registered; data assets contribute their own rows instead of id chains | L | med | |
| 3.5 | Character menu: a file per tab, the preview actor in its own file, one hover-popup helper for the two copies, descriptions moved onto the data assets | L | med | |

Compare menu screenshots before and after each UI stage (`-WWOAutoTestMenus`, `-WWOAutoTestTabs`,
`-WWOAutoTestTitle`).

### Stage 4: gameplay structure

| # | Change | Size | Risk | Status |
|---|---|---|---|---|
| 4.1 | Movement state as one `FWWOMoveState` member on the character (declared in five places today; `ToString` already misses three fields) | M | med | |
| 4.2 | Move stats from one table of {member, stat name, low, high} instead of five positional lists and a magic 14 | S | low | |
| 4.3 | One perk table (id, numbers, condition, HUD text) and an `FWWOPerkState`, used by `Shoot`, `RefreshStats`, `PerkStatus` and the host's check; one `ComputeGunStats` shared by the weapon and the host validation | M | med | |
| 4.4 | Split `UWWOWeaponComponent`: weapon model (JSON parsed once into a struct), first-person hands component, melee, view pose; the firing core stays | L | med | |
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
