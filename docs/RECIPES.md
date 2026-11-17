# Recipes

What the code requires today to add each kind of thing. Paths are relative to `Source/WhatWasOurs/`
unless they start with `Config/`, `Content/` or `Tools/`. Where a recipe has many steps because
behaviour is keyed by id in several files, `CODE_REVIEW_2026-11.md` has the planned fix.

## A weapon

1. A `UWWOWeaponData` asset in `Content/WWO/Data` (primary asset type `WWOWeapon`): unique `Id`, `Order`,
   `bUnlocked`. `Tools/create_burst_rifle.py` shows how to copy an existing one.
2. An entry in `Config/WeaponModels.json`: mesh, scale, rotation, offset from the sight point, muzzle,
   an anchor per mod slot, the `hand` grip. Without one the gun falls back to a box model.
3. Optional mods in `Config/WeaponMods.json` with `"frame": "<Id>"`. A weapon is a frame as soon as the
   file has mods for it.
4. A `fire_<Id>` event in `Config/Sounds.json` (else it plays `fire`).
5. Its Settings section appears by itself (`weapon/<Id>`).
6. Id branches to check: the box-model fallback (`ModelParts`, `Player/WWOWeaponModel.cpp`), the
   pistol hand pose (`bHandsPistol`, `Player/WWOWeaponHands.cpp`; `Aim_Pistol` in `Player/WWOCharacterLook.cpp`), and
   iron-sight marks (`Data/WWOData.h`).
7. Test: `-WWOAutoTestGuns` and `-WWOAutoTestFrames` on `L_TestRange`.

## A weapon perk

A perk is one entry in the perk table, `WWOPerk::Table()` in `Weapons/WWOPerks.cpp`; the gun and the
host's check of a joiner's shot both read it, so nothing else needs to know the id.

1. Its numbers in the `WWOPerk` namespace (`Weapons/WWOPerks.h`).
2. Its entry (an `FWWOPerk`, written with designated fields; leave out what it doesn't do). Put it where
   it should show in the HUD line: the table's order is the line's order, and the order in which perks
   scaling the same number multiply.
   - `Id`: the perk name a mod carries.
   - `Stats`: stat changes while it is on (`RefreshStats`, every frame), on an `FWWOGunStats`.
   - `ShotOn` with `ShotScale` / `ShotCrit`: this shot does more (`Shoot`); `TargetOn` with
     `TargetScale`: per target hit, given the distance and the target.
   - `Hud`: its text on the HUD's perk line while lit (`PerkStatus`), empty when not.
   - `OnShot`, `OnKill`, `OnBurstEnd`: handlers for behaviour of its own (counters, arcs, refunds).
     State it keeps goes in `FWWOPerkState` (and its resets in `UWWOWeaponComponent::Equip`,
     `ResetForRespawn` and the mod-change handler in `RebindLoadout`, if it should reset there).
     Conditions read `FWWOPerkView`; add a field there if they need something new.
   - The host's allowance, or it will refuse a joiner's honest shot: `HostDamage` (with no check it is
     allowed on every shot; with `HostOnTarget` or `HostOnShot`, only on hits where the host's own
     numbers agree, with some room), `HostCrit`, `HostRate`, `HostBurst`, and `HostShot` if the host
     must run something for the joiner (an arc).
3. A mod carrying it in `Config/WeaponMods.json` (`"perk"`, and its words in `perk_text`). Any frame can
   carry any perk.
4. Its regression scenario: the perk family in `Player/WWORegressGuns.cpp` ((b), one met and one not met
   per mod) sets up each perk's condition by id; add a branch for the new one, and the golden entries
   in their own commit. The `hostcheck.*` scenarios pick it up from the catalog.

## An ability or a grenade

1. A `UWWOAbilityData` or `UWWOGrenadeData` asset with its `Flora` set. Its id goes in `WWOIds.h`
   (`WWOIds::Ability::...` / `WWOIds::Grenade::...`) if any game code tests for it; game code compares
   against the constant, not a literal (tests keep strings).
2. Grenade: behaviour goes by kind (`EWWOGrenadeKind`, `WWOTypes.h`), not id. An existing kind needs nothing
   more. A new kind is one more entry at the end of `AWWOGrenade::KindRules` (`Weapons/WWOGrenade.cpp`, in the
   enum's order): whether the Impact Grenades trait applies (`bImpactFuse`), a brighter blast
   (`bBrightFlash`), its life when it has no fuse (`Life`, then it fizzles), and its handlers: `OnTick` (every
   host tick), `OnTouch` (return true when the touch is handled, so nothing else happens on it), `OnBurst`
   (after the blast's damage, Seedburst and Shield Share). Handlers run on the host; a field left empty is
   something the kind doesn't do.
3. Ability: one entry in `WWOAbility::Table()` (`Player/WWOAbilities.cpp`), with its `Id`. The recharge,
   arming and button go by kind (`EWWOAbilityKind`) in `UWWOAbilityComponent`; what the ability itself
   does is its entry's fields: `OnActivate` (Active, just used: also runs on the host for a joiner),
   `Tick` (every update, equipped or not), `Lit` (Triggered: the HUD box), `bFistsGlow`, the effects on
   its owner (`MoveSpeed`, `ExtraJumps`, `GrenadeCharges`, `MeleeDealt`, `MeleeTaken`) and the events
   (`OnMeleeLanded`, `OnDamageTaken`). What it remembers goes in `FWWOAbilityState` (`Player/WWOAbilities.h`),
   read through `UWWOAbilityComponent::State` / `FindState`; the regression runner empties it with the
   rest of the kit. Something an ability does elsewhere (a grenade's seeds, the fog's damage) asks
   `Equipped(WWOIds::Ability::...)` there; the entry's comment says where.
4. A joiner's use: the host's checks return an `EWWOKitRefusal` (`Player/WWOKitRefusal.h`);
   `WWOKit::RefusalText` is where a reason becomes words. A new reason is a value there and a line there.
5. The look on the body: a branch in `Characters/WWOBodyGrafts.cpp` (`F.Has(WWOIds::Ability::...)`).
6. Text: a line in `UWWOCharacterMenu::BuildOverview` (`UI/WWOCharacterMenuOverview.cpp`, the Loadout
   tab's Build notes), and per-ability rows in `UWWOSettingsMenu::RegisterAbilitySections`
   (`UI/WWOSettingsSections.cpp`; the row names are the asset's property names, saved-file keys).
7. Bots: a hint in `AWWOBotController::UseKit` if they should use it (a branch on the id there, not in the table).
8. An `ability` / `grenade_throw` sound plays by default.
9. Test: the regression scenarios pick up every ability and grenade asset by themselves (`ability.<id>`,
   `kit.host.ability.<id>`, `grenade.<id>.*`, `kit.impact.<id>`); add a `kit.*` scenario for what the new
   entry does that those don't record. `-WWOAutoTest` sets off every ability and throws every grenade.

## An area effect

Anything that acts on "everyone within R of a point" (a blast, a pool, an aura, a slam):

1. Fill an `FWWOAreaQuery` (`Systems/WWOWorldSubsystem.h`): `Center`, `Radius` (cm), `Measure` (feet,
   centre, chest, nearest part, body centre, last part), `bInclusive`, `bFlat` / `MaxHeight` for a ground
   effect, who (`Targets`, `Players`, `bTeammates`), `For` (whose effect: the team rules), and `SightParams` /
   `SightFrom` if walls should block it.
2. `UWWOWorldSubsystem::Get(this)->ForEachTargetInRadius(Q, [&](const FWWOAreaHit& Hit) { ...; return true; })`
   (return false to stop at the first), or `NearestTargetInRadius` for one victim. Hurt through `Hit.Target`
   inside an `FWWODamageBy`; AI teammates come with no `Target`.
3. Never a hand loop over `LivingTargets` / `GetPlayers`. A cone (melee, aim) is `BestAimPart` with your
   own score.
4. Test: an `area.*` scenario (`Player/WWORegressArea.cpp`) with one victim just inside and one just
   outside, measured your effect's way.

## A lingering effect

A world effect that appears, lasts a while and goes (a pool, a fog, a wall):

1. An actor in `Flora/` with a static `Spawn` that sets its numbers (as replicated `COND_InitialOnly`
   properties if a joiner must see it: `Duration` at least) before `FinishSpawning`; `BeginPlay` builds the
   looks on every machine.
2. A member `FWWOEffectLife Life`, made in the constructor with its grow time, tail time and whether the
   tail has its own clock (`Flora/WWOEffectLife.h`): no tail if it fades inside its duration (pool, fog),
   a tail on the same clock (patch), or a restarted one (wall, roots).
3. `Tick`: `const EWWOLifeStep Step = Life.Advance(DeltaSeconds, Duration);` then its own work by step:
   looks from `Life.GrowIn()`, `FadeIn`, `LeftOf(Duration, Seconds)`, `TailLeft(Duration)`; what happens
   once on `Leaving` (the wall drops its cover); on `Over`, `FWWOEffectLife::End(*this)` (the host destroys;
   a joiner's copy keeps getting `Over` until the host's goes: decide what it shows meanwhile). Damage only
   under `HasAuthority()`, every 0.5 s, inside an `FWWODamageBy`, through an `FWWOAreaQuery` (above).
4. Opt out of the navmesh and set Owner (AGENTS.md rules 4 and 8).
5. Test: a `life.*` scenario (`Player/WWORegressLife.cpp`): hits by tick, its looks around each edge of its
   life, its switches and the tick it goes, and a `.joiner` copy if it replicates.

## An enemy type

1. An `AWWOEnemy` subclass in `Enemies/WWOEnemyTypes.*` overriding the hooks it needs (`Setup`, `Think`,
   `Animate`, `AnimateRemote`, `OnHit`, `OnDeath`...), and a `UWWOEnemyData` subclass with its asset
   (`Content/WWO/Data`, primary asset type `WWOEnemy`). Its `D()` is
   `L->EnemyDataAs<UWWOMyData>(WWOIds::Enemy::MyKind)`, its `Data()` returns `D()`.
2. An id in `WWOIds::Enemy` (`WWOIds.h`) and one entry in `WWOEnemyKinds::All()` (`Enemies/WWOEnemyKinds.cpp`):
   id, class, data class, and its spawn rules (`SpawnScatter`: how far a wave spawn lands from its marker,
   0 for right on it; `WaveTestFacing`: the wave test's facing slack). That is the whole registration: the
   loadout's catalog finds its asset by the data class (or makes a code default), the wave spawn takes the
   class and rules from the entry, the tests look it up by id. Rows in `Waves()` (`Game/WWOWaveRules.cpp`)
   to put it in the waves.
3. A `Config/CharacterModels.json` key if it has its own model; set `Body->ModelKey` before `BeginPlay`.
4. Spawn points: `ATargetPoint`s tagged `EnemySpawn` plus the kind's id (a point with only `EnemySpawn` takes any kind).
5. What joiners see goes in `AnimateRemote`, from replicated state.
6. A Settings page for its data asset in `RegisterEnemySections` (`UI/WWOSettingsSections.cpp`) if it should be tunable.
7. Test: the `wave.spawn.*` regression scenarios (`Player/WWORegressWaves.cpp`) take a list of kinds: add
   it there (`update-new wave.spawn.<id>`), then `-WWOWaveTest`.

## The loadout

`UWWOLoadoutSubsystem` owns four parts (`Systems/`); put a change in the part it belongs to:

- **Something new to pick** (a data asset type, a list the pickers show): `FWWOLoadoutCatalog`
  (`WWOLoadoutCatalog.*`: `Load`, `GrenadesFor` and the other lists). Callers read `L->Catalog.Weapons`.
- **Something new to wear** (a slot, an equip rule): `FWWOEquipped` (`WWOLoadoutEquipped.*`) for the state
  and the rule, a forward on the subsystem that calls it and then `Changed()` (every change is announced
  once, and saved later). Anything saved goes in `ToSave` / `FromSave`, anything the host needs in `ToNet`.
- **A new stat or modifier**: `WWOLoadoutStats` (`WWOLoadoutStats.*`): a stat in `StatsInGroup` / `StatName`.
  Keep the order of the sums and products in `Apply`: the `loadout.stats.*` scenarios compare every bit.
- **Weapon configs and slots**: `FWWOModConfigs` (`WWOModConfigs.*`).
- **The saved file and the wire** (`UWWOLoadoutSave`, `FWWOModConfig`, `FWWONetLoadout` in
  `WWOLoadoutTypes.h`) are players' files and the joiner's RPC: never rename, retype or remove a property.
  A new one is added at the end and must read sensibly as empty from an old file. `loadout.file.owner`
  reads a real `Loadout.sav` (`Tools/regress/fixtures`) and checks it saves back to the same body.
- `PointValue` stays on the subsystem: a Settings row binds it by name.

## Moving an AI

An enemy, the teammate or a bot that walks somewhere (or a new way of walking):

1. A path: an `FWWOPathFollower` member (`Systems/WWONav.h`; give it a starting `Goal` if "never pathed"
   should read as far away) and the mover's own `FWWOPathRules` (repath age, goal moved, reach, `<` or `<=`).
   Each tick call `Update(Querier, Feet, Goal, Here, dt, Rules)`, then decide from `Points` yourself: the
   next point, or what to do with fewer than two (straight at the goal, stand when `bPartial` and blocked).
2. Probes: `WWONav::WallAhead` (a capsule sweep or a line, from where and how far: the mover's numbers) and
   `WWONav::LedgeAhead` (a line down from a step out), each with an `FWWOProbe` and the mover's own
   `FCollisionQueryParams` (a `SCENE_QUERY_STAT`, itself ignored). One trace per question: no extra traces
   in a per-tick path (swarms).
3. Turning: `WWONav::TurnYaw(Current, Target, Rate, dt)`; pass the target in the precision the mover used.
4. Host only (`HasAuthority()` / the bot's own controller). Random choices through `WWO::FRand` (the bot
   still draws on `FMath`).
5. Test: `move.<mover>.*` scenarios (`Player/WWORegressMove.cpp`): a walk on open floor, at a wall, at a
   platform it can't climb, around the back wall's corners, its probes at their edges, its turn. Walks wait
   for the run-time navmesh build before switching the AI on.

## A game mode (a new kind of match)

1. A class deriving from `FWWOMatchRules` (`Game/WWOMatchRules.h`) in `Game/WWO<Name>Rules.*`, like
   `FWWOTesterRules` (the smallest) or `FWWOTeamDeathmatch`. It reaches the game mode through `Mode`
   (the shell names the rules classes as friends: add yours).
2. `Begin`: clear what the mode doesn't use from the level (enemies, allies, dummies) and set `State`.
   `Update` (every tick, the mode's bookkeeping: bind each new player's death once, run clocks), `Advance`
   (every tick, last: a state machine that should see this tick's restart first), `OnPlayerJoined`,
   `OnPlayerLeaving` / `OnPlayerLeft`, `IsOver`, `StatusText` (the HUD's line: phases of your own, or new
   `EWWOWaveState` values at the end of the enum). Override only what it needs.
3. A death binding: `P->Health->OnDied.AddRaw` or `AddWeakLambda(&Mode, ...)`, keeping the handle, and remove
   every handle in the destructor (see `FWWOWaveRules`): the scenarios switch rules objects mid-run.
4. Choose it in `AWWOGameMode::InitGame` from a map URL option (and keep it across restarts in `LoadMap` /
   `UWWONetSubsystem::MatchOptions`, as `bPvP` is). Add an `As<Name>()` view to the interface if other code
   needs its state.
5. Its tunables are `UPROPERTY(EditAnywhere) float`s on `AWWOGameMode` (Settings rows bind them there by
   name, in `RegisterMatchSections`), read through `Mode`.
6. Replicated match state goes on `AWWOGameState` / `AWWOPlayerState`; the rules run on the host only.
7. Tests: scenarios in `Player/WWORegressMatch.cpp` switch a fresh rules object on with `M::Enter` (add yours
   to `M::MakeRules`) and put the level's back with `M::Done`; drive events directly (a hit, a kill, a call)
   and let the game mode's own `Tick` run what crosses ticks.

## A map

1. The level: one `PlayerStart`, `ATargetPoint`s tagged `EnemySpawn` (add `PvEOnly` to keep one out of
   PvP respawns), a `NavMeshBoundsVolume`, optionally a `TargetPoint` tagged `KillZ`.
   Generated maps are built by scripts in `Tools/` from a geometry JSON; change the script, not the map.
2. A row in `UWWONetSubsystem::Maps()` (`Net/WWONetSubsystem.cpp`): level, title, blurb, PvP, PvE, team
   size. The launch screen and the Maps page read this table.
3. `+MapsToCook` in `Config/DefaultGame.ini`.
4. Test: `-WWOAutoTestMaps` on the map, then a bot match with `-WWOPerfTest`.

## A settings slider

1. A `UPROPERTY(EditAnywhere) float` on the class that uses the value.
2. One `Row(Obj, "PropertyName", label, min, max, step, Show)` line in the right block of the right
   `Register...Sections` function in `UI/WWOSettingsSections.cpp` (`RegisterSections` lists them).
   Saving, reset and search then work by themselves. Don't move existing sections or rows: their order
   is the pages', the search's and the saved file's.
3. If joiners should have it (a player's own preference), add its id to `OwnSettings()` (same file).
4. A new section also needs its title in the `TabLayout` table in `UI/WWOSettingsMenu.cpp`, or it lands
   under Player.
5. To show it in the firing tester, add the id to `GunGroups` / `CameraIds` in `UI/WWOFiringTester.cpp`.
6. If the shipped default should change, update `Config/ShippedSettings.json`.

## A HUD element

1. A painter: a private `void PaintX(const FWWOHUDFrame& F) const` on `UWWOHUDWidget`, declared with the
   others in `WWOHUDWidget.h` and written in `WWOHUDPaint.cpp`. It draws through `F.P` (`FWWOPainter`:
   `Box` / `Outline` / `Line` / `Tri` / `Circle` / `Text`, ...) in 1600 x 900 units, and finds the player,
   gun, reticle point (`F.AimAt`) and world-to-screen (`F.ToScreen`) on the frame. Something every
   painter needs goes on `FWWOHUDFrame`, worked out once in `NativePaint`.
2. Call it from `NativePaint` at its place in the list: paint order is z-order.
3. Animated values are members eased in `NativeTick`.
4. Tunables are float properties on `AWWOCharacter`, exposed as settings rows under HUD.
5. Read game state; don't change it from paint (paint doesn't run while the HUD is hidden). A decision
   gameplay also reads belongs in game code, as marking is (`UWWOWeaponComponent::UpdateMarks`).

## A menu

1. A `UUserWidget` built in code into its own `WidgetTree`, with `Open` / `Close` / `Back` / `IsOpen`, the
   OS cursor hidden (`SetCursor(EMouseCursor::None)`) and the ring cursor painted in `NativePaint`
   (`WWOUI::PaintCursor`).
2. Build it with `WWOUI` (`UI/WWOUI.h`): `Text` / `Spaced` labels, `Rule`, `Sized` / `Wide`, `Fill`, the
   palette (`PageColor`, `CardColor`, `CardEdge`, `Dim`, ...), `Accent(this)`. Buttons: `WWOUI::Button` (or
   `TextButton`) with a look (`CardLook`, `FilledLook`, `PlainLook`) sets `IsFocusable = false`;
   `WWOUI::OnClick(B, this, Fn)` plays ui_click and returns the handler, which the menu keeps in a
   `UPROPERTY() TArray<TObjectPtr<UObject>>` (reset on rebuild, or `WWOUI::KeepPruned` for pages rebuilt in
   pieces). A text box: `WWOUI::StyleTextBox`. A full-window page's header (name, tabs, keys, rule):
   `WWOUI::HeaderName`, `TabStrip`, `AddPageHeader`, and `SetActiveTab` when the tab changes.
3. In `AWWOPlayerController`: create it, `AddToViewport` with a z-order, and add it to `IsMenuOpen` /
   `IsOtherMenuOpen`, `HandleMenuPress`, `CloseMenus` and the scoreboard's close list.
4. A text box must hand keyboard focus back to the viewport when done.
5. A big menu splits across `.cpp` files by concern, with helpers they share in a private header under a
   named namespace: see the settings menu (`WWOSettingsSections` / `Store` / `Rows`) and the character
   menu (a file per tab) in `ARCHITECTURE.md`, UI. A popup that opens on hover and closes a moment after
   the cursor leaves: `FWWOHoverPopup` (`UI/WWOCharacterMenu.h`).

## An input action

1. A value in `EWWOAction` (`WWOTypes.h`).
2. A row in `AWWOPlayerController::BindRows()` if players can rebind it, or a `Map()` line in `MapKeys`
   for a fixed key. Add it to the menu list in `BuildInput` if it must work while paused.
3. Gameplay polls it with `IsHeld` / `WasJustPressed`. Give `AWWOBotController` the same action if bots
   should use it.
4. Update `Config/ShippedKeybinds.json` if the shipped default should include it.

## A sound

1. A `.wav` in `Tools/audio_src`, then `Tools/import_sounds.py`.
2. An event in `Config/Sounds.json`: sounds, volume, pitch range, distance (0 = flat only).
3. A hook beside the matching visual effect: `UWWOSound::Play` / `PlayFlat` / `PlayFor`. An event missing
   from the file is silent, so the hook can go in first.

## A scripted test

1. A new `Player/WWOAutoTest<Name>.cpp` with an `FWWOAutoTest::UpdateAuto<Name>Test`. Nothing goes in the
   player controller: reach it through `PC->` (the driver is its friend).
2. Its state in `FWWOAutoTest` and a `bAutoTest<Name>` flag in `FWWOAutoTestModes` (both
   `Player/WWOAutoTest.h`); in `WWOAutoTest.cpp` the switch read in `FWWOAutoTestModes::Read` with
   `WWOSwitches::Has(TEXT("WWOAutoTest<Name>"))`, the flag in `Any()`, and a branch in `FWWOAutoTest::Tick`.
   Pointers get `= nullptr` (the driver is not a UObject, so nothing zeroes it); an actor the test spawns
   and keeps is added to `AddReferencedObjects`.
3. Checks go through `WWOTest::Check(TEXT("<name>"), bOk, What)` (it logs `AutoTest <name>: ... ok` or
   `... FAIL`); timed steps through `WWOTest::Crossed(Before, Now, T)`; end with `WWOTest::Exit`.
4. Screenshots through `Shot("<NN>_<name>")`, with a number prefix no other test uses.
5. Change the loadout freely: the run's loadout guard (`WWOTest::GuardLoadout`, already called for every
   `-WWOAutoTest...` switch) puts `Saved/SaveGames/Loadout.sav` back when the game closes. Don't write
   your own backup. If the switch has no `WWOAutoTest` in it, call `WWOTest::GuardLoadout()` yourself first.
6. Add the switch to the comment table in `WWOSwitches.h` and to the table in `TESTING.md`.

## A regression scenario

1. Find the family it belongs to: guns (`Player/WWORegressGuns.cpp`), kit (`WWORegressKit.cpp`: grenades,
   abilities, ults, flora effects) or combat (`WWORegressCombat.cpp`: melee, the damage pipeline, enemies), or area
   (`WWORegressArea.cpp`: the edges of area effects and aim cones), or life (`WWORegressLife.cpp`: a
   lingering effect followed tick by tick, its hits, looks, switches and the tick it goes), or move
   (`WWORegressMove.cpp`: AI walking, turning and probing), or waves (`WWORegressWaves.cpp`: the game mode's
   wave spawn and queue), or loadout (`WWORegressLoadout.cpp`: catalog, equipping, stat maths, configs,
   the save and net payloads), or match (`WWORegressMatch.cpp`: the rules the game mode runs and the bot
   roster, through its `M` helpers).
   A new family is a new `FWWORegress::Add<Family>Scenarios` member (declared in `Player/WWORegress.h`,
   called from `BuildScenarios` in `WWORegress.cpp`).
2. Add one `FWWORegressScenario` to `Out`: a unique dotted `Name` (`family.subject.case`), `bPvP` if it
   needs team rules, and `MaxTicks` if it runs longer than 20 s. Generate from the catalogs (`L.Catalog.Weapons`,
   `L.Catalog.WeaponMods`, `L.Catalog.Grenades`, `L.Catalog.Abilities`, `L.Catalog.Floras`) where the scenario applies to each item.
3. `Setup`: spawn with `R.SpawnDummy` / `SpawnEnemy` (inert) / `SpawnAlly` (inert) / `SpawnBot(Team, At)`
   (parked, fixed kit), at `R.Ahead(Metres, SideMetres)`; equip with `R.EquipWeapon` / `EquipMod` /
   `EquipFlora` / `EquipGrenade` / `EquipAbility`; change a float tunable for this scenario with
   `R.Tune(Object, "Property", Value)`. Keep per-scenario state in a `TSharedRef` struct captured by both
   lambdas and reset it at the top of `Setup`.
4. `Step(R, Tick)`: drive it by tick number (`FWWORegress::Ticks(Seconds)`), with `R.Press` / `Hold` /
   `Move` / `AimAt` / `Face` for input, or by calling the game's own functions. Never use real time.
   `Record` every number that would change if the logic changed, then return true.
5. Run it alone (`Tools/regress.sh norender -WWORegressOnly=<name>`) a few times: the numbers must be the
   same every time. If they legitimately differ with rendering, set `Modes` to `PerMode` (or
   `RenderOnly`, or `NoRenderOnly` when only the no-draw run repeats) with a `ModeReason`.
6. Add its golden entry in its own commit: `Tools/regress.sh update-new <name or prefix>` (adds only
   missing entries and checks the two modes agree on the new ones; `git diff` must show only additions).
7. Randomness in gameplay code goes through `WWO::FRand` / `FRandRange` / `RandRange` / `RandomInCone`
   (`WWOTypes.h`), so the scenarios can seed it; looks-only randomness stays on `FMath`.

## A JSON config file

1. The file in `Config/`, and its name in the list in `WhatWasOurs.Build.cs` so packaged builds stage it.
2. Read it with `WWOConfig::Load(WWOConfig::Path(TEXT("<Name>.json")))`, or `WWOConfig::LoadCached` if
   it is asked for often and a restart to pick up edits is fine. A player's own file with a shipped
   default: `WWOConfig::LoadSavedElseShipped` and `WWOConfig::Save` with `WWOConfig::SavedPath`.
3. Parse the object's fields in the system that owns them; use `WWOConfig::Vec` / `Color` for vectors
   and colours.
