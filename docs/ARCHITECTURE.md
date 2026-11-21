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
  team). Area effects ask `ForEachTargetInRadius` / `NearestTargetInRadius` with an `FWWOAreaQuery` (where to
  measure, who, line of sight); melee and aim assist pick a part with `BestAimPart`.
- Enemy-to-friendly damage does not use `TakeHit`: every enemy attack (bite, leap, bolt, splash, slam)
  calls `AWWOEnemy::HurtFriendly(Victim, Amount, CameFrom, bMelee)`: a player's `DamageCameFrom` then
  `TakeDamage`, a teammate's `AWWOAlly::TakeAllyDamage`. No `FWWODamageBy` scope, no push; it returns the
  player hurt (the slam slows them).

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

- **Ids** the code tests for are named once in `WWOIds.h` (`WWOIds::Flora`, `Ability`, `Perk`, `Grenade`:
  `FName` constants). The data, the wire and the tests name them as strings.
- The grenade, ability and ult components fill an `FWWOGrenadeSpec` and call `AWWOGrenade::Throw` or
  `ExplodeAt`. The grenade runs on the host. `Explode` loops the living targets (nearest aim part, line
  of sight, falloff), then the kind's own burst. What each `EWWOGrenadeKind` does is one
  `FWWOGrenadeKindRules` entry in `AWWOGrenade::KindRules` (impact fuse allowed, brighter flash, life
  without a fuse, handlers per tick, on a touch, on the burst); a grenade with no asset (seeds, the ult,
  Root Bombs) has no kind.
- **Abilities** are a table (`Player/WWOAbilities.h` / `.cpp`): one `FWWOAbility` per ability id (what it
  does when used, its own timer, when it is lit, the fists' glow, its effects on its owner, what it does
  on a melee hit landed or damage taken). `UWWOAbilityComponent` runs the recharge and arming by kind and
  keeps each ability's `FWWOAbilityState` (charge, effect time, armed, guard time, live seeds or patches)
  in one map by id. The ult goes by the flora's `EWWOUltKind` (`UWWOUltComponent::Kind`).
- **A joiner's kit**: the host's checks (`HostThrow`, `HostUseAbility`) return an `EWWOKitRefusal`
  (`ServerActivateUlt` logs one); `ClientKitRefused(What)` names the kit with an `EWWOKit` (a `uint8` on the wire);
  `WWOKit::RefusalText` (`Player/WWOKitRefusal.h`) is the one place a reason becomes words (the host's log).
- Flora effects (`Flora/`) are actors with static `Spawn` / `Raise` factories. The lingering ones (spore
  pool, spore fog, spore patch, bark wall, Entrench roots) each keep an `FWWOEffectLife`
  (`Flora/WWOEffectLife.h`): the age, the step through hold, tail and over (`Advance` with the effect's own
  replicated `Duration`), the grow-in and fade fractions, and `End` (the host destroys; a joiner's copy
  waits for the host's to go, the not-replicated roots destroy themselves). Each effect keeps its own
  numbers and per-tick work. The host deals damage every 0.5 s inside an `FWWODamageBy` scope.
- Effects everyone must see go through the `AWWOGameState` statics (`Blast`, `Bolt`, `Text`, `Sound`,
  `Poison`, `Tracer`, `Shockwave`, `Discharge`): do it here, then `HostRelay(World)` gives the game state to
  multicast through when this is a listen server (null otherwise).
- `FX/WWOFX` is hand-built: shape and material helpers, `AWWOFXActor` (a list of tweens),
  `AWWOParticles` (CPU instanced particles), lightning. No Niagara.
- `UWWOSound` plays `Config/Sounds.json` events on this machine only (`Play`, `PlayFlat`, `PlayFor`).
  Each hook sits beside the visual effect it belongs to; `AWWOGameState::Sound` is the networked one.

## Loadout and data

- **`UWWOLoadoutSubsystem`** (GameInstance subsystem, `Systems/WWOLoadoutSubsystem.*`) is a thin owner of
  four parts and does the saving (slot `Loadout`, written when the menu closes, a map changes or the game
  exits: `FlushSave`), the sending (`ToNet` / `ApplyNet`), joiners' copies (`NewCopy`) and the change
  notice (`OnChanged`, once per change). Its equip, stat and config functions are one-line forwards:
  - `FWWOLoadoutCatalog Catalog` (`WWOLoadoutCatalog.*`): every option, loaded once (`Load`: the data
    assets and `Config/WeaponMods.json`), the picker lists, `FindById`, the enemies' data by kind
    (`EnemyData`) and the allies'.
  - `FWWOEquipped Equipped` (`WWOLoadoutEquipped.*`): flora, weapon, grenade, abilities, traits, frame
    parts and each frame's mods, the shell colours (`ShellColors`: per slot a primary, secondary and glow colour,
    alpha 0 for the part's own; empty, and not written, while all are default); the equip rules; to and from
    the save and the wire. The body paints them on every shell part it draws (`UWWOBodyComponent::PaintShellPart`:
    dynamic instances of the part's own `M_WWO_Robot` instances), and the first-person hands on their arms part.
  - `WWOLoadoutStats` (`WWOLoadoutStats.*`): the stat maths and its tables, pure functions of an `FWWOEquipped`.
  - `FWWOModConfigs ModConfigs` (`WWOModConfigs.*`): saved weapon configs and the two weapon slots.
  - The saved and sent shapes, `UWWOLoadoutSave`, `FWWOModConfig`, `FWWONetLoadout`, are in
    `WWOLoadoutTypes.h` (file and wire format).
- **Stats**: `M(Stat, Base)` on the character, `Apply(Stat, Base)` in the subsystem (`WWOLoadoutStats::Apply`):
  `(base + flat) x (1 + percent) x multiplier`, from frame parts, traits and weapon mods.
- **Other players' kit**: `UWWOLoadoutSubsystem::Get(Context)` returns your own loadout for you, and a
  per-character copy (`NetLoadout`, built from the ids in `FWWONetLoadout`) for anyone else. That is why
  spawned kit must carry its Owner.
- **Data assets** (`Data/WWOData.h`): `UPrimaryDataAsset` subclasses for weapons, grenades, abilities,
  frame parts, floras, traits, enemies and allies, loaded by type through the Asset Manager, sorted by
  `Order`, found by `FName Id` (`FWWOLoadoutCatalog::FindById`). Weapon mods are not assets: they come from
  `Config/WeaponMods.json`.

## Game rules

- **`AWWOGameMode`** (`Game/WWOGameMode.*`) is a shell: logins (gamer tag, the full-game check), spawning
  (the ring of spots around the one Player Start), map changes (`LoadMap`, `Restart`), the status line every
  tick, the restart timer, and the tunables of every mode (Settings binds its sliders to these properties
  by name). It owns **one rules object** (`TUniquePtr<FWWOMatchRules> Rules`), chosen in `InitGame` from the
  map URL, and the **bot roster**. The rules run on the host only and write the replicated match state on
  the game state and player states; nothing about replication is theirs.
  - `FWWOMatchRules` (`Game/WWOMatchRules.h`): the interface (`Begin`; `Update`, each tick where the mode's
    bookkeeping always ran; `Advance`, last in the tick; `OnPlayerJoined`, `OnPlayerLeaving` / `OnPlayerLeft`;
    `IsOver`; `StatusText`), the match's phase `State` (`EWWOWaveState`, read as `MatchState()`), and typed
    views (`AsWaves`, `AsDeathmatch`, `AsTester`; the game mode's `Waves()`, `Deathmatch()`, `Tester()`).
  - `FWWOWaveRules` (`Game/WWOWaveRules.*`): PvE waves, the default (the `Waves()` table, the countdown,
    spawning one by one, a cleared wave, won, everyone down lost, `SkipWave` / `JumpToWave`).
  - `FWWOTeamDeathmatch` (`Game/WWOTeamDeathmatch.*`, `?pvp`): teams (`BalanceTeam`, `SwitchTeam`), the
    death and the kill (`OnDeath` then `OnKill`: credit, score, the kill feed, the kill limit), the time
    limit, and `SpawnSpot` (the game mode's `PvPSpawnFor`, which the health component asks whenever the game
    state says PvP).
  - `FWWOTesterRules` (`Game/WWOTesterRules.*`, `?tester` / `-WWOTester`): clears the level, puts one dummy
    ahead of where the player started (its `Home` and `Target` are what `UWWOFiringTester` reads).
  - `FWWOFootRaceRules` (`Game/WWOFootRaceRules.*`, `?footrace`, or any map named FootRace such as
    `L_FootRaceTest`): two teams race through their own lane of PvE rooms; a hub between the lanes is the PvP.
    So far: teams, each team's spawn room (`SpawnSpot`, the game mode's `PvPSpawnFor`: the game state says
    PvP), and the breach portals (`World/WWOBreachPortal.*`, spawned on the level's `PortalAnchor` and
    `HubPortal` markers): a room's doorway takes its team to the team's hub doorway, the hub doorway back to
    the room doorway they came through. The map's markers are listed in `Tools/footrace_layout.py`.
    Breach energy: one pool per team (`AWWOGameState::BreachEnergy`, the game mode's FootRace tunables),
    earned by kills (an enemy's `LastHitBy`, a player's `LastAttacker`), needed to enter the hub
    (`BreachThreshold`), drained per breacher per second; out of energy sends the team's breachers back,
    a breacher's death costs a share. The HUD draws both pools under the match line (`PaintBreachEnergy`).
    Lane rooms (`RoomArea` markers, their scale the size in m): a team's furthest room (the furthest any
    of its players has stood in; Room1 from the start) fills to `RoomEnemies` and refills after deaths;
    rooms behind keep their enemies without refilling. Each enemy guards its lane (`AWWOEnemy::Lane`) and
    only targets that team's players.
    Doors and levers (`World/WWOBreachDoor.*`, `WWOBreachLever.*` on `DoorSlot` / `LeverSlot`): a lever is
    live when its door is the team's next and the team's current breach went in by the room that door
    leads out of; a breacher holds the Revive key at it (`AWWOCharacter::bHoldingUse`) for `LeverHoldTime`.
    A door opened mid-breach freezes that pool (`BreachFrozen`); earnings go to `BreachNext` (hub kills
    split) until nobody is in the hub, then the next pool takes over. Leaving the hub lands in the furthest
    room opened.
    The boss (`AWWOBoss`, a bigger Tank, on `BossSpawn`): up and shielded when Door3 opens; `OpenBossWindow`
    (for now the console's `wwo.BossWindow`) drops the shield for `BossWindow` s, a third longer each
    window; killing it ends the match (`EndMatch`, `IsOver`) with that team the winner.
    Boss cores (`World/WWOBossCore.*`, on `CoreSpawn`): `CoreDelay` after a boss is shielded a core for its
    team appears in the hub with a wave of `BossWave` in its boss room; carried out through the team's hub
    doorway (`LeaveHub`) it opens the next window; a carrier's death drops it (anyone can take it then);
    the other team delivering it heals the boss `CoreHeal`; a dropped core goes home after `CoreDropTime`.
    Bots (`FWWOBotRoster`, as in team deathmatch; `-WWOBots=N`): with no enemy player to fight, a bot plays
    the race (`AWWOBotController::FootRaceTick`, state `Objective`): it shoots its lane's enemies (`SenseMobs`,
    and its boss in a window), breaches when the team has the energy and a lever or core to go for (two at
    most), fetches a core, pulls the live lever, leaves, and carries a core out. Its aim and fire are the
    player fight's (`AimAt`, `Shoot`).
    The kill feed shows player kills and, as event lines (`AWWOGameState::ReportEvent`, `KillEvent`), each
    breach and door; AI kills stay out. `Begin` loads every enemy's model, clips and the effect materials
    (`UWWOBodyComponent::Preload`, `PreloadAnims`; kept in `Preloaded`) and sets up one of each enemy out of
    sight for half a second, so the first Tank (Room3) or boss doesn't stall the match.
    The HUD's race track (`PaintRaceTrack`, right edge): each team's three doors as circles (Door1 at the
    bottom, filled when open), your team's track on the left, and between them each breaching team's bar,
    its share of the pool the breach began on (`AWWOGameState::BreachStart`), shrinking toward its middle.
  - `FWWOBotRoster` (`Game/WWOBotRoster.*`, the game mode's `Roster`, PvP only): how many bots the match
    wants, adding one a tick, one making room for a joiner (`RetireFor`), one for a leaver, bots' health
    following the Bots settings.
  The rules and roster are plain C++ objects holding actors by weak pointer; a rules object removes its own
  death bindings when it is destroyed. The stress and screenshot harness (`FWWOStressHarness`,
  `Game/WWOGameModeStress.*`) and the wave test (`Game/WWOGameModeWaveTest.cpp`) are test code beside them.
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

**The enemy registry** (`Enemies/WWOEnemyKinds.*`): one `FWWOEnemyKind` per kind (id from `WWOIds::Enemy`,
class, data asset class, `SpawnScatter`, `WaveTestFacing`), looked up by id (`WWOEnemyKinds::Find`,
`FindOrSwarm`: an unknown id is a Swarm) or by enemy (`Of`). `SpawnDeferred` places one at a spawn
marker (scatter, navmesh, clear of solids): the waves and FootRace's rooms both spawn through it. The waves (`Waves()`) and spawn point tags name
kinds by id; `FWWOWaveRules::SpawnEnemy` takes the class and offset rule from the entry; the loadout's
catalog keeps one data asset per kind (`UWWOLoadoutSubsystem::EnemyDataAs<T>(Id)`).

**Moving** (`Systems/WWONav.*`), shared by the enemies, the ally and the bots: `WWONav::FindPath` (a
navmesh path, start and end snapped onto it), `FWWOPathFollower` (the path being walked: `Update` ages it,
paths again when its `FWWOPathRules` say so, drops reached points), `WWONav::WallAhead` / `LedgeAhead` (one
trace each, shaped by an `FWWOProbe`: capsule or line, level geometry or a channel) and `WWONav::TurnYaw`
(an eased yaw step). What each mover does with the answers stays in its own class (`AWWOEnemy::PathDirection`,
`AWWOAlly::Follow`, `AWWOBotController::PathDirection` / `SideIsClear`); the bot's `TurnView` (yaw and pitch
as one rate-limited angle) is its own. The navmesh is built at run time (`RuntimeGeneration=Dynamic`).

## Bodies

`UWWOBodyComponent` drives the UE mannequin through a native `UWWOBodyAnimInstance` (no AnimBP): clips by
name, `/Game/WWO/Anims/Body/A_<name>` first, then `/Game/WWO/Anims/UAL/A_<name>`. A character model from
`Config/CharacterModels.json` is a second skeletal mesh retargeted off the hidden mannequin
(`UWWOModelAnimInstance`, `RTG_Manny_<key>`), so clips, hitboxes and grafts work the same for every
model. Flora grafts are procedural meshes (`FWWOGraftBuilder`, `WWOBodyGrafts.cpp`). The three floras share the robot
frame (`SK_RobotFrame`, `Tools/import_robot_frame.py`), surfaced by `M_WWO_Robot` and its role instances (armour, trim,
joint, visor) from `Tools/create_robot_materials.py` (rerun it after re-importing the frame); its skinned grafts come from
`Tools/import_robot_grafts.py`. Each skinned graft has three levels of detail; `UWWOBodyComponent::UpdateGraftDetail` forces the level from the camera's distance five times a second (full within 4 m, middle to 12 m, lowest beyond; scaled by zoom), because a mesh led by the body would otherwise follow the body's single level. The menu preview sets `bFullDetailGrafts`.

## UI

- Every widget is a `UUserWidget` built in C++ into its `WidgetTree`. `AWWOPlayerController` creates them
  and sets their z-order by hand (HUD 0, tester 4, launch 5, settings 10, character 11, scoreboard 12).
- **Menus** are driven by a synthetic mouse, not focus: `FWWOMenuCursor` is a Slate input pre-processor
  (left stick moves the pointer, Cross clicks, right stick scrolls). Every button has
  `IsFocusable = false`.
- **Shared style** (`UI/WWOUI.*`, `namespace WWOUI`): the palette, the type (`Font` / `FontAt`: Poppins, read from
  `Content/WWO/Fonts/Poppins-<Weight>.ttf`, a missing weight falling back to the nearest there is; the scale `Type`;
  text colours), the box style (`Corner`, `BoxEdge`, `Grid`), `RoundBrush`, `SetTip`, `ScrollBox`, the logo, the ring
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
  `WWOCharacterMenuFlora.cpp`, `WWOCharacterMenuWeapon.cpp` (weapons, the gun's render, mod slots, configs, the mod hover
  and the weapon stat-bar maths), `WWOCharacterMenuShell.cpp` (the shell parts, and the colour picker: `BuildColourRows`). The editors' cards are one builder, `ItemCard` (an item's
  badge, its name over a line of detail), laid out on equal columns by `AddCardGrid`. Both hover popups close through one
  `FWWOHoverPopup` timer. The Loadout tab's hover row (`OpenHoverPicker`) stays inside the window whatever the number of
  choices: one row while it fits, else it wraps into rows (weapon configs prefer five across, wider only as the height
  needs), goes over the tile when there is more room above, and scrolls (wheel, right stick) only when rows as wide as
  the window are still too tall; names too long for a choice end in "..." (the tip has them whole).
  `-WWOAutoTestHover` checks it with 3 to 40 configs held in memory only (`-WWOHoverCounts=150`: the scrolling row). The character is `AWWOPreviewActor` (`UI/WWOPreviewActor.*`), a mannequin with
  its own scene capture far below the level: a small studio (key with soft shadows, fill, two rims, top and bounce
  lights, all directional on lighting channel 2 and switched on only while the menu is open; no sky light or level
  reflections), fixed exposure near the arenas' (`CharacterExposure`) so flora glow keeps its colour, soft bloom, temporal
  AA on a supersampled target, a backdrop (`M_WWO_PreviewBackdrop`) whose edges match the page with a soft pool of light
  behind the figure, and a contact shadow under the feet (`M_WWO_PreviewShadow`; both from `Tools/create_preview_materials.py`). The Weapon tab's gun is `AWWOGunPreviewActor` (`UI/WWOGunPreviewActor.*`): The preview character stands in the unarmed idle loop (`AWWOPreviewActor::IdleAnim`), animating while the menu pauses the game. The HUD's weapon corner (`PaintAmmo`) shows two more `AWWOGunPreviewActor`s in picture mode (`MakePicture`: side on, no backdrop, scene colour with the gun's cut-out in alpha, retaken only when the weapon or its mods change) through `M_WWO_HUDGun` (`Tools/create_hud_materials.py`): the weapon in hand and the stowed one, beside the magazine count and the reserve sign. The shell part pictures (the Loadout tab's four part cards, every part card on the Shell tab) are `AWWOPartPictures` (`UI/WWOPartPictures.*`): taken live, not baked, so they follow the shell meshes and the player's colours. Four capture stations beside the preview each show one part mesh alone (`UWWOBodyComponent::ShellMeshFor`: the `wwo.Shell` override, else the part's `shell_looks`; painted by `PaintShellPartWith`) in its reference pose, turned three-quarters on and framed on its bounds, on lighting channel 2 so the preview's own studio lights it; scene colour with the cut-out in alpha, shown through `M_WWO_HUDGun`. A picture is a render target per part mesh, slot and colours, kept (up to 40) and shared by both tabs; the menu asks for them as it builds a tab and its tick takes the waiting ones, four at a time, two frames each, only while it is open. A new colour's picture shows the part's last one until it is taken.
  `WWOWeapon::BuildShowcase` builds the same model as the first-person gun (mesh, kept and extra parts, the mods' parts),
  lit by its own key, rim and fill lights on lighting channel 1, captured only while that tab is open.
  Shared helpers: `WWOCharacterMenuPrivate.h`.
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
