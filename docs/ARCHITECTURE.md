# Architecture

How the game is put together, system by system. Paths are relative to `Source/WhatWasOurs/`.
For the rules that follow from this, see `CONVENTIONS.md`; for step-by-step additions, `RECIPES.md`.

## The frame, in order

1. `UWWOCharacterMovement` ticks first and calls `AWWOCharacter::PrepareMovement`: health, look, recoil,
   then `CaptureMoveInput` fills `MoveIn` (`FWWOMoveInput`).
2. Every move (your own, the host's copy of a joiner, a replay after a correction) runs
   `AWWOCharacter::SimulateMove` from `MoveIn` and the move state (`MoveState`, an `FWWOMoveState`) only.
   It sets `WishVelocity`, which `CalcVelocity` copies. `FWWOMoveInput` rides each saved move;
   `FWWOMoveState` is saved with each move and rides corrections.
3. `AWWOCharacter::Tick` then calls `UpdateX(Delta)` on each component in a fixed order. **Components do
   not tick themselves.** The order mirrors the Godot prototype and matters.
4. Once every actor has ticked, the world updates each local player's camera; `AWWOPlayerController::
   UpdateCameraManager` then has the gun mark targets for the frame (`UWWOWeaponComponent::UpdateMarks`).
5. The HUD eases its animated values in `NativeTick` and draws in `NativePaint`. It only reads game state.

## Player

- **`AWWOCharacter`** (`Player/WWOCharacter.h`, `Player/WWOCharacter*.cpp`): camera, eight components,
  movement rules, lunge and mantle, the recoil state machine, PvP hitboxes, what other machines see of
  this body, and about 25 RPCs. Implements `IWWOTargetable`.
- **The character's files** (one class; the header's sections follow them): `WWOCharacter.cpp`
  construction, `BeginPlay`, the per-frame `Tick` driver and `TickRemoteOnHost` (their call order
  matters), controllers and input reads (`Pressed`, `JustPressed`); `WWOCharacterMove.cpp`
  `PrepareMovement`, `CaptureMoveInput`, `SimulateMove` and every rule it calls (sprint, crouch, slide,
  air, jump, landing, lunge flight, launch, mantle), `FWWOMoveState` / `FWWOMoveInput` on the wire,
  `MoveStatTable`, the move-stat RPCs and `M()`; `WWOCharacterView.cpp` `Look`, `SetViewRotation`,
  `TurnToward`, `UpdateCamera` (eye height, landing dip, head bob) and the low reticle (`AimForward`);
  `WWOCharacterRecoil.cpp` `AddRecoil`, `UpdateRecoil`, `ApplyRecoil` and the `RiseShape` / `FallShape`
  tables; `WWOCharacterCombat.cpp` the PvP hitboxes, headshots, `ApplyHit` and the status effects
  (slow, blind, veil, entrench) with their RPCs; `WWOCharacterLook.cpp` what other machines see
  (`SetupVisuals`, `NetFlora` / `NetLook` / `NetFists`, `ShowLook`, the team tint, `SendPose`,
  `UpdateModel`, remote footsteps); `WWOCharacterNet.cpp` `GetLifetimeReplicatedProps`, the kit RPCs
  and revive; `WWOCharacterGunNet.cpp` the host's checks of a joiner's shots and punches. What more
  than one of them uses is in `WWOCharacterPrivate.h` (`namespace WWOCharacter`). The HUD and audio
  sliders (`Radar*`, `HealthBar*`, `SoundVolume`...) are still members of the character, in the
  header's tunables section, because Settings binds them by name.
- **Components** (all owned by the character, all assume their owner is an `AWWOCharacter`):
  `UWWOWeaponComponent` (firing, reload, ADS, perks, melee, gun model, viewmodel pose, first-person
  hands, third-person gun), `UWWOHealthComponent`, `UWWOAbilityComponent`, `UWWOUltComponent`,
  `UWWOGrenadeComponent`, `UWWOAimAssistComponent`, `UWWOBodyComponent`.
- **The weapon component's files** (one class, `Player/WWOWeapon*.cpp`; the header's sections follow them):
  `WWOWeaponComponent.cpp` construction, the per-frame `UpdateWeapon` driver (its call order is part of
  the gun feel), equip and swap, reload, ADS, ammo, input; `WWOWeaponFiring.cpp` the trigger, bursts and
  the shot; `WWOWeaponPerks.cpp` `RefreshStats`, shot results, kill perks, `PerkStatus`;
  `WWOWeaponMelee.cpp` the swing, lunge target and strike; `WWOWeaponModel.cpp` the first-person model
  (`ModelConfig`, `ModelParts`, `BuildModel`, mod and flash parts), the throw ball and `UpdateWorldGun`;
  `WWOWeaponView.cpp` `UpdatePose`, FOV, punch, shake, rumble; `WWOWeaponHands.cpp` the first-person
  arms (IK and the punch / throw clips); `WWOWeaponTargeting.cpp` `UpdateReticleTarget`, `UpdateMarks`,
  `SightOnScreen`. What more than one of them uses is in `WWOWeaponPrivate.h` (`namespace WWOWeapon`).
- **Input**: `AWWOPlayerController` builds Enhanced Input in code. One `UInputAction` per `EWWOAction`
  (`WWOTypes.h`); `BindRows()` is the one table of rebindable rows; `MapKeys` rebuilds mappings from it
  plus `Saved/WWO/Keybinds.json` (fallback `Config/ShippedKeybinds.json`). Gameplay **polls**
  `IsHeld` / `WasJustPressed` / `GetMoveAxis` / `GetLookAxis` / `ConsumeChord`; it never binds delegates.
  All return nothing while a menu is open. `AWWOBotController` exposes the same functions, so the
  character reads a bot and a person the same way.
- **Bots** (`Player/WWOBotController.*`): timed `Sense` (about every 0.13 s) and `PickLeader` (about every
  1 s), a state derived from flags (Roam / Hunt / Fight / Retreat), one block per state, then shared
  sprint and stuck handling. Tuning comes from the game mode (`TuningFor`).

## Damage

```
IWWOTargetable::TakeHit            (one wrapper, Systems/WWOTargetable.cpp; skips the dead)
  -> ApplyHit                      each class's own work; on a joiner: flash and number only
       host: Health->TakeDamage    shield first, OnDamaged, Die
  -> UWWOWorldSubsystem::ConfirmHit   hit marker for the attacking player
```

- **One marker per hit** (`OnHitConfirmed`, which the HUD's hit and kill marker listens to): `ConfirmHit`
  marks every hit, except inside a `bAttackerSawHit` scope, where the attacker marks it itself. The gun's
  `Shoot` and the fist's `MeleeStrike` set it around their own `TakeHit` and broadcast once per shot or
  punch (with the joiner's predicted kill); the host sets it while applying a joiner's shot or punch (that
  joiner already marked it). Kit damage (grenades, fog, lightning, ricochets) goes through `ConfirmHit`, and
  to a joiner as `ClientHitMarker`.

- **Who did it** is ambient: an `FWWODamageBy` scope sets `UWWOWorldSubsystem::CurrentAttacker` for the
  hits made inside it. Kill credit, hit markers and which damage numbers show all read it.
- **Who may hurt whom** is one function, `UWWOWorldSubsystem::CanHurt(Attacker, Victim)`: non-players
  always; players only in PvP, on the other team, never themselves; AI teammates (`AWWOAlly`) never by a
  player (or their kit) in co-op. `PlayerOf(Actor)` walks Owner and
  Instigator to find the player behind a grenade, pool or fog.
- **Targets** come from `UWWOWorldSubsystem::LivingTargets` (PvE enemies, dummies, and in PvP the other
  team).
- Enemy-to-player damage does not use `TakeHit`: enemies call `Health->DamageCameFrom` and
  `Health->TakeDamage` directly, and `AWWOAlly::TakeAllyDamage` for teammates.

## A shot

`UWWOWeaponComponent::UpdateFiring` -> `FireRound` -> `Shoot` (`Player/WWOWeaponFiring.cpp`; the
shot's perk results, `OnShotResult`, in `WWOWeaponPerks.cpp`): a line trace per pellet from the eye on
`ECC_Visibility`, falling back to `UWWOAimAssistComponent::Magnetize`; damage summed per target; then
`TakeHit`. Effects are `AWWOFXActor::BulletTrail` / `HitSpark` / `ImpactMark` and `UWWOSound`. It ends
with `AWWOCharacter::AddRecoil`, rumble and the view punch.

Online, a joiner predicts the hit and sends `ServerGunShot(FWWOGunShot)`. The host validates it in
`WWOCharacterGunNet.cpp` (`GunLimits`, a fire-rate budget per gun, `CheckGunHit`, `CheckReach`), applies
`TakeHit`, and sends `ClientKillCredit` and `MulticastGunShot`. Melee, grenades, abilities and ults
follow the same shape: predict locally, the host confirms, `ClientKitRefused` undoes a refusal.

**Weapon stats**: the component's `Weapon` is a copy of the data asset. `RefreshStats` re-copies it every
frame (`WWOWeaponPerks.cpp`) and applies mod percentages and active perks, so never store state on it. Timing stats (reload,
aim-in, swap, ready) go through `M()` instead. The mods' scaling is `WWOPerk::ComputeGunStats`, which the
host's `GunLimits` calls too, so the gun and the host's check work from the same stats.

**Perks** are a table (`Weapons/WWOPerks.h` / `.cpp`): one `FWWOPerk` per perk id, holding its stat
changes, shot and per-target conditions and multipliers, HUD text, handlers (shot, kill, finished
burst) and the host's allowance (per-shot best, per-hit checks, crit, rate, burst, the arcs it runs for
a joiner). The gun keeps the perks' state in one `FWWOPerkState` (`UWWOWeaponComponent::PerkState`), and
its conditions read an `FWWOPerkView` of the frame. `WWOPerk::Equipped(Loadout)` gives the equipped
weapon's entries; the host's `FWWOGunLimits::Perks` the entries of the mods a shot names. Every user
walks the table in its order (the HUD line's).

## Kit: grenades, abilities, ults, flora effects

- The grenade, ability and ult components fill an `FWWOGrenadeSpec` and call `AWWOGrenade::Throw` or
  `ExplodeAt`. The grenade runs on the host. `Explode` loops the living targets (nearest aim part, line
  of sight, falloff), then the kind's own effect (wall, blind / slow, pool).
- Flora effects (`Flora/`) are actors with static `Spawn` / `Raise` factories. They age in `Tick`; the
  host deals damage every 0.5 s inside an `FWWODamageBy` scope.
- Effects everyone must see go through the `AWWOGameState` statics (`Blast`, `Bolt`, `Text`, `Sound`,
  `Poison`, `Tracer`, `Shockwave`, `Discharge`): do it here, and multicast if this is a listen server.
- `FX/WWOFX` is hand-built: shape and material helpers, `AWWOFXActor` (a list of tweens),
  `AWWOParticles` (CPU instanced particles), lightning. No Niagara.
- `UWWOSound` plays `Config/Sounds.json` events on this machine only (`Play`, `PlayFlat`, `PlayFor`).
  Each hook sits beside the visual effect it belongs to; `AWWOGameState::Sound` is the networked one.

## Loadout and data

- **`UWWOLoadoutSubsystem`** (GameInstance subsystem) owns the catalogs, what is equipped, the stat
  maths, weapon-mod configs and the save (slot `Loadout`, written when the menu closes).
- **Stats**: `M(Stat, Base)` on the character, `Apply(Stat, Base)` in the subsystem:
  `(base + flat) x (1 + percent) x multiplier`, from frame parts, traits and weapon mods.
- **Other players' kit**: `UWWOLoadoutSubsystem::Get(Context)` returns your own loadout for you, and a
  per-character copy (`NetLoadout`, built from the ids in `FWWONetLoadout`) for anyone else. That is why
  spawned kit must carry its Owner.
- **Data assets** (`Data/WWOData.h`): `UPrimaryDataAsset` subclasses for weapons, grenades, abilities,
  frame parts, floras, traits, enemies and allies, loaded by type through the Asset Manager, sorted by
  `Order`, found by `FName Id` (`FindById`). Weapon mods are not assets: they come from
  `Config/WeaponMods.json`.

## Game rules

- **`AWWOGameMode`** (`Game/WWOGameMode.cpp`) runs PvE waves (the `Waves()` table), PvP team deathmatch
  (chosen by `?pvp` on the map URL), the bot population (`UpdateBots`) and the firing tester (`?tester`).
  It also carries the stress and screenshot test hooks (`LogStress`).
- **`AWWOGameState`** replicates the status line, `bPvP`, `TeamScores`, `bMatchOver`, `MatchWinner`, and
  keeps the kill feed. **`AWWOPlayerState`** replicates `Team`, `Kills`, `PrecisionKills`, `Deaths`.
- **`UWWONetSubsystem`** (`Net/`): hosting, joining, the LAN game list, travel, the gamer tag, and
  `Maps()`, the one table of maps and the modes each suits.

## What replicates

Little is replicated as properties; most things are RPCs or effect relays.

| Object | Replicated properties |
|---|---|
| Health component | `Health`, `Shield`, `bDead`, `bRevived`, `BeingRevived`, `SpawnYaw` |
| Character | `NetFlora`, `NetLook`, `NetFists` |
| Body component | animation commands (`FWWOBodyNet`, skips the owner) |
| Enemy | `Health`, `MaxHealth`, `bDead`; Soldier `NetGlow`, `bNetAlerted`; Tank `Shield`, `MaxShield` |
| Ally | `Health`, `Shield`, `DownLeft`, `bDown` |
| Game state, player state | see Game rules |

AI runs on the host only. Joiners replay effects from `OnRep_` functions. The design and its phases are
in `NETWORKING_HANDOFF.md`.

## Enemies and allies

`AWWOEnemy` is a template-method base: health, hit, death and respawn, retargeting, navmesh
`PathDirection`, `Unstick`, `WallAhead` / `LedgeAhead`, with hooks `Data` / `Setup` / `Think` / `Animate`
/ `AnimateRemote` / `Absorb` / `OnHit` / `OnDeath` / `OnReset` / `AfterMove`. `WWOEnemyTypes` has the
Soldier (ranged bursts, cover, strafing), Swarm (bites, leaps) and Tank (shield, slam, lob).
`AWWOAlly` is the AI teammate.

## Bodies

`UWWOBodyComponent` drives the UE mannequin through a native `UWWOBodyAnimInstance` (no AnimBP): clips by
name, `/Game/WWO/Anims/Body/A_<name>` first, then `/Game/WWO/Anims/UAL/A_<name>`. A character model from
`Config/CharacterModels.json` is a second skeletal mesh retargeted off the hidden mannequin
(`UWWOModelAnimInstance`, `RTG_Manny_<key>`), so clips, hitboxes and grafts work the same for every
model. Flora grafts are procedural meshes (`FWWOGraftBuilder`, `WWOBodyGrafts.cpp`).

## UI

- Every widget is a `UUserWidget` built in C++ into its `WidgetTree`. `AWWOPlayerController` creates them
  and sets their z-order by hand (HUD 0, tester 4, launch 5, settings 10, character 11, scoreboard 12).
- **Menus** are driven by a synthetic mouse, not focus: `FWWOMenuCursor` is a Slate input pre-processor
  (left stick moves the pointer, Cross clicks, right stick scrolls). Every button has
  `IsFocusable = false`.
- **Shared style** (`UI/WWOUI.*`, `namespace WWOUI`): the palette, `Font`, `RoundBrush`, the logo, the ring
  cursor, and builders that take the widget tree to build into (`Text`, `Spaced`, `Rule`, `Sized`, `Fill`,
  `Button` / `TextButton` with a look, `StyleTextBox`, `Accent`) and the page header (`HeaderName`,
  `TabStrip`, `AddPageHeader`, `SetActiveTab`: settings, character, launch setup). Each menu builds in its own `WidgetTree`
  through it; no menu needs another to build a label or button. A click goes through a
  `UWWOClickHandler` (`OnClick`, which plays ui_click); the menu keeps the handler alive in a `UPROPERTY`
  array: for good (built once: tester, the character menu's tabs), until the next rebuild (launch,
  scoreboard, the character overview's cards), or pruned as its button leaves the page (`KeepPruned`:
  settings, the character editors' slots).
- **Settings** (`UI/WWOSettingsMenu.*`): `RegisterSections` fills sections of rows, each row bound to a
  float `UPROPERTY` by name. Saved as `{sectionKey: {rowId: number}}` in `Saved/WWO/SavedSettings.json`,
  falling back to `Config/ShippedSettings.json`. A joiner sees only rows marked as a player's own
  (`OwnSettings()`).
  One class over four files: `WWOSettingsSections.cpp` is the table of what exists (`RegisterSections`
  calls one `Register...Sections` per area in a fixed order, which is the order of the pages, the search
  and the saved file; `OwnSettings`, `MarkPreferences`), `WWOSettingsStore.cpp` the values (defaults,
  `LoadSaved`, `SaveDefaults`, reset), `WWOSettingsRows.cpp` the widgets for a page (`BuildRows`,
  `MakeRow` / `MakeRowFor`, the item browsers, `BuildBinds`), and `WWOSettingsMenu.cpp` the menu itself
  (`Prepare` and its layout steps `BuildFrame`, `BuildPages`, `BuildSidebar`, `BuildPageArea`; the tab
  layout table `TabLayout`; tabs, pages, search, open / close). Helpers they share: `WWOSettingsMenuPrivate.h`.
- **Character menu** (`UI/WWOCharacterMenu.*`): `WWOCharacterMenu.cpp` builds the frame and tabs and
  handles open / close, back, turning the preview, tick and the autotest hooks; one file per tab:
  `WWOCharacterMenuOverview.cpp` (Loadout: stat bars, Build notes, cards, the hover pickers),
  `WWOCharacterMenuFlora.cpp`, `WWOCharacterMenuWeapon.cpp` (weapons, mod slots, configs, the mod hover
  and the weapon stat-bar maths), `WWOCharacterMenuShell.cpp`. Both hover popups close through one
  `FWWOHoverPopup` timer. The character is `AWWOPreviewActor` (`UI/WWOPreviewActor.*`), a mannequin with
  its own scene capture far below the level. Shared helpers: `WWOCharacterMenuPrivate.h`.
- **HUD** (`UI/WWOHUDWidget.*`, `UI/WWOHUDPaint.cpp`): immediate-mode paint through `FWWOPainter`
  (`UI/WWOPainter.h`), in 1600 x 900 units scaled to the player's screen (`WWOHUD::Scale`). `NativePaint`
  builds one `FWWOHUDFrame` (player, gun, world subsystem, layout size, reticle point, `ToScreen`) and
  calls the element painters in z-order (`PaintScreenEffects`, `PaintWorldLabels`, `PaintReticle`, ...,
  `PaintFps`). `NativeTick` eases the animated values and lists the status chips. The HUD changes no
  game state: who is marked (health bars, the PvP outline flash) is decided by
  `UWWOWeaponComponent::UpdateMarks` every frame, HUD shown or not, from the same reticle and marker
  range in HUD pixels; the HUD reads `Marked` and `MatesInSight`.

## Tests

Scripted runs live in the test driver `FWWOAutoTest` (`Player/WWOAutoTest*.cpp`, made by the player controller only for a test run), driven by `-WWOAutoTest...` switches;
`Player/WWOTestRun.*` counts failures and sets the exit code. The regression scenarios (`-WWORegress`,
`Tools/regress.sh`) are `FWWORegress` (`Player/WWORegress*.cpp`), made by that driver: exact numbers on a
fixed time step, compared with `Tools/regress/golden.json`. See `TESTING.md`.
