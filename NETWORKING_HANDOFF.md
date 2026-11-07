# Networking: phase 1 handoff

Goal: LAN multiplayer. Up to 6 players in wave survival now; PvP (up to 12) later, so everything is
host-authoritative (listen server) with client prediction, not client trust. Second player plays a
packaged Mac build (Windows later). Read HANDOFF.md first for project conventions.

## Phases (agreed with Will)
1. **Foundation (in progress):** packaged Mac build; Host / Join with LAN discovery; host runs waves
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
2. **Expected issues to check:** checked by the test above: corrections while walking, sprinting,
   sliding, crouching and jumping (0); crouch matches on the host's copy; the remote body's model shows
   (screenshots); the joiner's HUD shows the host's wave line. Still open: mantling; the remote body's
   animation (check the screenshots by eye, or log the clip); the
   player controller's per-frame code on the host for remote controllers (BeginPlay skips widgets;
   check nothing in `Tick` assumes a HUD); `AWWOGameMode::Restart` / `LoadMap` use OpenLevel, which
   online should be a `ServerTravel` (with `?listen`).
3. **Packaged build networking on macOS:** the packaged app runs App-Sandboxed (data in
   `~/Library/Containers/com.YourCompany.WhatWasOurs/...`) and `Build/Mac/Resources` has
   `Sandbox.NoNet.entitlements`. Make sure the packaged app gets network client/server entitlements
   (or disable the sandbox for development builds) or hosting / joining will silently fail.
   Also set a real bundle identifier. LAN discovery uses UDP broadcast (port 14001): the macOS
   firewall may prompt on first host.
4. Host / Join / Leave buttons in the menu, with the subsystem's `Status` line.
5. Re-package and hand Will the app; update HANDOFF.md with a Networking section (fold this file in).

## Phase 2 notes (from the design discussion)
- Shots: client predicts (flash, tracer, hit marker), sends shot (origin, direction, timestamp);
  host rewinds target positions to that time, validates fire rate / ammo / line of sight, applies
  damage, multicasts hits (damage numbers, flash). Hidden aim assist stays client-side input help.
- Player health / slow / veil / entrench should be host state replicated to the owner (the owner's
  `SpeedScale` must agree with the host's, or corrections loop).
- Target dummies on the test range are placed, non-replicated actors: make them replicate.
