# Networking: phase 1 handoff

Goal: LAN multiplayer. Up to 6 players in wave survival now; PvP (up to 12) later, so everything is
host-authoritative (listen server) with client prediction, not client trust. Second player plays a
packaged Mac build (Windows later). Read HANDOFF.md first for project conventions.

## Phases (agreed with Will)
1. **Foundation (nearly done):** packaged Mac build; Host / Join with LAN discovery; host runs waves
   and enemies; predicted player movement; seeing the other player's model and animations.
2. **Combat:** predicted shots validated by the host (lag compensation, fire rate, ammo, line of
   sight), damage, health, downed / respawn, shared wave progress, enemy projectiles visible to all.
3. **Kit:** grenades, abilities, ults, allies, flora effects, each player's loadout / mods / perks.
4. **PvP mode:** teams, friendly fire, spawns, scoring, 12-player bot stress test.

## Done in phase 1 (compiles; single-player full autotest passes)
- **Packaging:** `Tools/package_mac.sh` builds, cooks and copies the staged app to
  `/Volumes/UnrealSSD/Builds/WhatWasOurs/WhatWasOurs.app` (UAT's `-archive` copies a bare app on
  Mac, so the script copies `Saved/StagedBuilds/Mac/WhatWasOurs.app` instead). Verified: the
  packaged build runs, loads the Lyra guns, Paragon models and configs, and passes the gun test.
  Config JSONs and `Config/ShippedSettings.json` (a copy of Saved/WWO/SavedSettings.json, made by
  the script, git-ignored) are staged via `RuntimeDependencies` in `WhatWasOurs.Build.cs`; the
  Settings menu falls back to ShippedSettings when a build has no saved settings. Cook list in
  `Config/DefaultGame.ini` (Paragon mesh folders only, not their demo maps). The last packaged app
  predates the networking code: **re-run the script.**
- **Sessions:** `Net/WWONetSubsystem` (LAN via OnlineSubsystemNull, `DefaultPlatformService=Null`):
  `Host(Map)` (create LAN session, reopen map `?listen`), `FindAndJoin()`, `JoinAddress(ip)`,
  `Leave()`. Command line: `-WWOHost`, `-WWOJoin` (LAN search), `-WWOJoin=<ip>`; handled once from
  `AWWOPlayerController::BeginPlay`. **No menu buttons yet** (add Host / Join to the Settings menu,
  e.g. a "Multiplayer" global tab).
- **Game state:** `Game/WWOGameState` replicates the wave line; the HUD reads it. Game mode binds
  every player's death, loses only when nobody is up, spawns enemies away from all players.
  `UWWOWorldSubsystem::GetPlayers()` (all characters) vs `GetPlayer()` (this machine's).
- **Enemies / allies:** `bReplicates`, replicated movement; AI (`Think`/`Animate`) only on the host
  (`HasAuthority()` gate at the top of `Tick`); enemy `Health`, `MaxHealth`, `bDead` replicated.
  Enemies target the closest living player; Soldier shots and Tank slam check every player.
  Enemy projectiles exist only on the host (phase 2 makes them visible).
- **Body animation:** `UWWOBodyComponent` replicates the host's animation commands (`FWWOBodyNet`:
  clip + speed, queued clip, upper-body layer, mix, hunch, upper shot), `COND_SkipOwner`, applied in
  `OnRep_Net`. Recorded only when `Replicating()` (host, online), so single-player costs nothing.
- **Predicted movement:** `AWWOCharacter::TickMovement` is now the local per-frame part (timers,
  recoil, look, `CaptureMoveInput`); the movement rules moved to `SimulateMove`, which reads only
  `MoveIn` (`FWWOMoveInput`: stick, button bits, speed scale, extra jumps). `UWWOCharacterMovement`
  runs it in `UpdateCharacterStateBeforeMovement` (not for simulated proxies), saves the input per
  move (`FWWOSavedMove`, never combining moves with a press), sends it (`FWWOMoveData`), applies it on
  the host in `MoveAutonomous`, and on a correction restores the rules' state (`FWWOMoveResponse`
  carries `FWWOMoveState`) before the replay. The owner sends its effective movement values with
  `ServerSetMoveStats` (loadout + its Settings sliders; host's `M()` then returns them as-is).
  **Escape hatch:** `MoveClientAuth` (melee lunge, Spore Dash launch via `ClientAuthLeft`) makes the
  host accept the client position; phase 3 should replace it with real simulation.
  Known trust surfaces to harden for PvP: move stats, `SpeedScale`, `ExtraJumps`.
- **Other players' look:** `AWWOCharacter::SetupVisuals` (from `BeginPlay` / `NotifyControllerChanged`):
  yours = first-person gun + shadow-only body; someone else's = full body on their flora's model
  (`NetFlora`, replicated, sent with `ServerSetFlora`), first-person gun/arms hidden
  (`UWWOWeaponComponent::SetFirstPersonHidden`). Non-local characters skip the rest of `Tick`; the
  host runs their `UpdateModel` so their body replicates.

## Not done / next steps for phase 1
1. **Two-instance test: done** (`-WWOAutoTestNet`, `Player/WWOPlayerControllerAutoNet.cpp`). Run the host with
   `-WWOHost -WWOAutoTestNet`, the joiner 45 s later with `-WWOJoin=127.0.0.1 -WWOAutoTestNet` (both
   `-game -windowed -ResX=960 -ResY=540`, separate `-abslog`). Two editor instances fit in 16 GB. The joiner idles, walks,
   sprints, slides, stops, crouch walks, jumps (ground + air) and runs at a crate stack, logging each step's
   corrections (`AutoTest net: step ...`) and each correction in detail (`NetCorrection ...`: host vs. your position,
   velocity and movement-rule state for that same move). The host logs forced updates (`NetForcedUpdate`).
   Both log the joiner's position every second and screenshot each other (`50_net_host_N`, `50_net_client_N`).
   Before each run, close leftover `CrashReportClient` windows: a crashed host's reporter keeps port 7777
   open, and the next host silently listens on 7778.
   **Result: 0 corrections in every step** after three fixes:
   - Merged moves: Unreal merges same-input moves by rewinding position and velocity only, so the
     movement rules' state (slide speed, air time, timers) ran twice on the joiner. `FWWOSavedMove`
     now saves that state at the move's start (`SetInitialPosition`) and puts it back in `CombineWith`.
   - Forced updates: after 0.25 s without moves from a player (any joiner stall) the host moved them itself
     and threw away their real moves for as long as the stall lasted. `MAXCLIENTUPDATEINTERVAL=2.0` in
     `DefaultGame.ini` (revisit for PvP: it's Unreal's lag-switch guard).
   - Input rounding: `FWWOMoveInput::Quantize` rounds the stick and speed scale on the joiner to what the host receives.
   Still to check: **mantling online** (the test's crate run hasn't triggered one yet; an accidental mantle in
   an earlier run ended earlier on the host than on the joiner), moving into another player or enemies
   (positions there differ between machines), sliding along walls (two small corrections in one run).
   The renderer crash (`(OldPacked & DeletingBit) == 0` in Metal `BindUniformBuffer`, base pass) hits single
   player too; it crashed the first two-instance run and is unrelated to networking.
2. **Checked by the test (all pass):** the joiner's HUD shows the host's wave line; the remote body's
   model and clips (each second the host's copy of the joiner plays the same clip the joiner plays:
   Jog_Fwd, Sprint, Slide, Crouch_Fwd, Jump, Idle); the host's per-frame code with a remote controller
   (no HUD needed); the Settings menu online (no pause; your character ignores buttons while it's open).
3. **Packaged build on macOS: done.** Development builds already used `Sandbox.Server.entitlements` (network
   client + server); Shipping now does too (`DefaultEngine.ini`, `XcodeProjectSettings`). Bundle ID
   `com.williambarr.WhatWasOurs` (saved data moves to that sandbox container, so the first run starts from
   the shipped settings). `NSLocalNetworkUsageDescription` in `Info.Template.plist`: macOS asks before LAN
   search. The macOS firewall may also ask on first host.
4. **Menu: done.** Settings, Global, **Multiplayer**: Host a LAN game (this map), Find and join a LAN game,
   Join a game on this Mac (testing), Leave, and a live status line (hosting / in a game / alone, players,
   this Mac's address for others; lost connections and failed joins show there). `FWWOSettingRow::LiveHeader`.
5. **Online restarts and map changes: done.** `AWWOGameMode::LoadMap` / `Restart`: alone `OpenLevel`; hosting
   `ServerTravel(map?listen)` (joiners follow); a joiner is told only the host can.
6. **Spawning: fixed.** One Player Start per map: the engine's pick failed when the host stood on it with no
   room beside it (the joiner never spawned). `AWWOGameMode::SpawnDefaultPawnFor` puts each player on the first
   free spot in rings 1.5 / 3 / 4.5 m around it.
7. **Two machines: done** (2026-11-07). Host: the Mac mini (packaged); joiner: the M1 MacBook Air (packaged,
   macOS 26.6, reached over SSH as `williambarr@192.168.4.45`; copy the app to `~/Applications/WhatWasOurs/`,
   launch with `open -n ... --args -WWOJoin -WWOAutoTestNet -log=NetJoin.log`). LAN search found the host; the
   whole test passed (health, menu, movement, gunfire with the forged hit refused, melee, restart round 2).
   Two things it took:
   - **SM5 shaders.** The Air's GPU falls back to Metal SM5, and the build only had SM6 shaders ("Shader platform
     SF_METAL_SM5 was not cooked"). `DefaultEngine.ini` now targets both; packaging takes ~9 min the first time.
   - **Local Network permission.** macOS asks on the joiner's first LAN search; until you click Allow the search
     silently finds nothing. Both Macs have their firewall off, so no firewall prompt.
   One 23 cm correction while sprinting at full speed (same speed and movement state on both sides, one frame
   ahead): probably a frame hitch on the Air (it runs ~30 FPS at 1280x720). Watch for it in longer sessions.
8. **Still open for phase 1:** mantling online (the kit crate stacks have no clean chest-height face, so the
   test can't trigger one; try it by hand, or add a mantle block to the test range); fold this file into
   HANDOFF.md once phase 1 is closed.

## Phase 2 groundwork (done)
- **Player health** (`UWWOHealthComponent`): host-owned, `Health` / `Shield` / `bDead` replicated. Damage, regen,
  death and respawn run on the host only; a joiner replays the effects (`OnDamaged` on a drop, death and
  respawn on `bDead`). Tested: the host hits the joiner for 30, the joiner sees shield 70 and one damage event.
- **Target dummies** (`AWWOTarget`): replicated health, death and strafing (host only). A joiner's hit
  flashes and shows a number locally but changes nothing until phase 2 sends hits to the host.
- **Enemy projectiles** (`AWWOEnemyProjectile`): replicated once at spawn (velocity, gravity, look); each
  joiner flies its own copy for looks, which pops or splashes where it is when the host's copy goes.
  Damage stays on the host. The test counts about 70 host shots arriving per run.

## Phase 2: combat (in progress)
- **Gunfire: done** (`Player/WWOCharacterGunNet.cpp`, `UWWOWeaponComponent::Shoot`). A joiner's shot shows at
  once on its screen (impacts, flash, damage number, hit marker; a kill is predicted from the health the host
  last sent) and goes to the host as `FWWOGunShot` (gun id, eye, one `FWWOGunHit` per target, every impact).
  The host checks each hit before applying it (`CheckGunHit`): target alive, point within its bounds + 75 cm,
  shot origin within 1.5 m of the host's copy, in range, nothing blocking from the eye (Visibility), damage at
  most the gun's base x pellets x (crit + 0.6) x 2.6 (mods and perks aren't known on the host yet), fire rate
  within 1.35x the gun's with up to 3 shots bunched. No target rewind yet (fine on a LAN). The host's own shots
  apply directly. Everyone else sees each shot (`MulticastGunShot`: tracers and impact marks from the shooter).
  On a joiner, `TakeHit` on an enemy or dummy only flashes and shows the number; health is the host's.
  Tested (`-WWOAutoTestNet` shoot step, aimed): 6 joiner shots taken, 5 hits applied, a forged 99999-damage
  hit rejected, the dummy's health drops on both sides, the joiner sees the host's shots.
- **Melee: done** (`ServerMeleeSwing` / `ServerMeleeHit` in `Player/WWOCharacterGunNet.cpp`,
  `UWWOWeaponComponent::UpdateMelee` / `MeleeStrike`). A joiner's punch shows at once on its screen (lunge,
  flash, number, hit-stop, predicted kill) and the swing goes to the host, whose copy of its body plays
  `Melee_Hook` on the upper body (replicated, so everyone else sees the punch; the host's own punches too).
  The hit goes to the host, which checks it (`CheckMeleeHit`): puncher and target alive, damage at most 10x the
  base punch (Thornfist 2.5x, veiled 3x, traits; abilities aren't known on the host yet), at least half a swing
  since the last applied hit, eye within 1.5 m, point within reach + 1.5 m, on the target and in sight (shared
  `CheckReach` with gunfire). The host then applies the damage and the knockback (a joiner no longer shoves
  enemies itself: their movement is the host's). Thornfist's discharge and Static Charge's chain lightning still
  run only on the puncher's screen (phase 3 kit).
  Tested (`-WWOAutoTestNet` melee step: the joiner walks to the dummy it sees best and punches it three times):
  3 swings and 3 hits taken, the dummy 200 -> 0 on both sides, a forged 99999 punch and a second punch in the
  same instant refused, 0 corrections. One earlier run had 3 tiny (2 cm, vertical) corrections just after
  lunges: the host takes the joiner's position at the lunge's end (`MoveClientAuth`) and then thinks it's
  falling for a moment. Harmless; goes away when phase 3 simulates the lunge on the host.
- **Downed and revive: done** (`UWWOHealthComponent`: `Revive`, `UpdateRevive`, `FindReviveTarget`). Agreed with Will:
  online, 0 health means down where you fell (body plays `Death01` once and holds it), no respawn timer and no
  bleed-out; a teammate within `ReviveRange` (2.5 m) holds **Revive** (E, or Square, which then doesn't reload)
  for `ReviveTime` (3 s) and you're back up in place with `ReviveHealth` (50%) health and no shield; letting go
  or stepping away starts over. Everyone down loses the round (allies don't count online, they can't revive),
  which restarts it as before. Alone nothing changed (die, respawn after 2 s). Host-run: the reviver's
  `bWantsRevive` (own input, or `ServerSetRevive`), progress on the host, `BeingRevived` replicated on the
  downed player for both HUDs ("You're down" / "Being revived" + bar; "Hold E / Square to revive" + bar).
  `bRevived` tells a joiner not to snap its view to the start. Settings: Player, Revive rows.
  Tested (`-WWOAutoTestNet` revive step): the host goes down (no timer), the joiner sprints over and holds
  Revive, the bar fills on the joiner, the host is back up where it fell; then the host downs both and the
  lost round restarts the run (round 2 follows).
- **Not yet:** grenades and abilities from a joiner still only happen on its own screen (phase 3 kit); kill credit back to the shooter (perks, ult charge use the predicted kill);
  each player's own gun and mods on the host (it validates against base stats); taking damage doesn't interrupt
  a revive yet (only letting go or stepping away does).

## Phase 2 notes (from the design discussion)
- Shots: client predicts (flash, tracer, hit marker), sends shot (origin, direction, timestamp);
  host rewinds target positions to that time, validates fire rate / ammo / line of sight, applies
  damage, multicasts hits (damage numbers, flash). Hidden aim assist stays client-side input help.
- Slow / veil / entrench should be host state replicated to the owner (the owner's `SpeedScale` must agree
  with the host's, or corrections loop). Health already is.
- Other effects (`AWWOFXActor`: grenade blasts, ability visuals) still exist only where they're made.
