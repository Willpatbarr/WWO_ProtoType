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
  (The old `MoveClientAuth` escape hatch for the lunge and Spore Dash is gone: see phase 3.)
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
- **Kill credit: done.** The host counts what a joiner's shot or punch killed and tells it (`ClientKillCredit`); a
  joiner's kill perks (Hammer, Scavenger, Adrenal: `ApplyKillPerks`) wait for that instead of its own guess. The
  kill marker still shows at once from the guess, and also shows when the host confirms a kill it didn't guess.
  Ult and ability charge come from time, not kills, so nothing else needed it. Tested: 3 kills credited, 3 received.
- **Each player's own gun and mods: done.** Each `FWWOGunShot` names its mods; the host rebuilds that setup's
  limits from the shared mod data (`GunLimits`: modded damage, fire rate, range, crit; Overpressure, Final Round,
  Focus, Hunter, Patience, Hammer at their best; x1.1) and refuses mods that don't fit the gun or share a slot.
  The joiner's Settings sliders for gun stats aren't known to the host (the host's tuning counts online).
  Tested: damage cap 121 for the hand cannon (was 372 with the old flat allowance), a made-up mod refused.
- **Not yet:** grenades and abilities from a joiner still only happen on its own screen (phase 3 kit); taking
  damage doesn't interrupt a revive (only letting go or stepping away does).

**Phase 2 on two Macs (2026-11-07):** the whole test passed host Mac mini / joiner MacBook Air (LAN search, gun checks
with the joiner's own mods, melee, kill credit, revive, everyone-down restart). Two things seen there: corrections
while running into a downed player (fixed: a downed body no longer blocks players, `SetBodyBlocks`, on every
machine), and one 8.5 m correction right after jumping off a crate stack into a melee lunge that fixed itself
within the move (positions matched a second later); likely the lunge's `MoveClientAuth` shortcut, to go when
phase 3 simulates the lunge on the host.

## Phase 3: kit (in progress)
- **Each player's loadout on the host: done** (`FWWONetLoadout`, `UWWOLoadoutSubsystem::ToNet / NewCopy / ApplyNet`,
  `AWWOCharacter::NetLoadout`, `ServerSetLoadout`). A joiner sends its loadout as ids whenever it changes (checked
  every 0.5 s with the move stats); the host keeps an unregistered, never-saved copy of the loadout system for that
  player. `UWWOLoadoutSubsystem::Get(Context)` returns it when the context is that character, one of its components,
  or an actor it owns or instigated (a few levels up), so traits, abilities, grenade data and frame parts answer
  for the right player without changing call sites. Spawned kit must set its Owner (or Instigator) for this.
  Tested: the host's copy matches the joiner's own loadout; single-player full test passes.
- **Grenades: done** (`AWWOGrenade`, `UWWOGrenadeComponent::HostThrow / ThrowFrom / UpdateCharge`,
  `ServerThrowGrenade`). Every grenade is the host's: a joiner's release sends hand + direction, the host checks it
  (alive, a charge ready, hand near the eye; the host runs the joiner's recharge too) and throws it from the
  joiner's loadout copy (grenade, Seedburst, Spore Patches, traits). The grenade replicates its look once
  (`FWWOGrenadeLook`) and its movement; joiners draw it and stop simulating it. Blasts go out with
  `MulticastBlast`; online a spent grenade hides for 0.5 s before it's destroyed so the blast arrives first.
  Owned by the thrower (spawn Owner), so hits go to them (`ClientHitMarker` for a joiner) and their loadout answers.
  What grenades leave behind replicates the same way (sent once, looks built in BeginPlay, effect host-only,
  host destroys): `AWWOSporePool`, `AWWOSporePatch`, `AWWOSporeFog` (also from Spore Dash and the Veil ult, owned by
  the player), `AWWOBarkWall` (solid on every machine; only the host raises its box). Not replicated yet: poison
  drips (`UWWODamageOverTime`, host-only looks), enemy blind/slow looks.
  Tested: the joiner's shock pool thrown by the host (owner the joiner), a second throw with no charge refused,
  the dummy hurt 200 -> 53, the joiner saw the grenade and the pool; single-player full test passes.
- **Abilities and ults: done.** A joiner's own machine runs its ability or ult at once (feel, camera, its own
  launch and veil/entrench state) and tells the host (`ServerUseAbility(Slot)`, `ServerActivateUlt`); the host's
  copy of that player runs the same thing and owns the world side: Spore Dash's fog, Root Bombs, Thornfist's boost
  and an armed Static Charge for its punches (`UWWOAbilityComponent::HostUseAbility`, 0.95 charge slack), the ult's
  blast / veil fog / roots (`UWWOUltComponent::Release` spawns only on the host; roots go to everyone else with
  `MulticastEntrenchRoots`). The host now ticks a joiner's abilities, ult, grenade charge and status timers
  (`TickRemoteOnHost`), so a joiner's veil and entrench run out there and its casts can be checked. Camera and
  first-person gun changes in the ult only touch the player's own character.
  A landed punch's kit (`UWWOWeaponComponent::MeleeKitEffects`: Melee Guard, Thornfist discharge, Static Charge's
  chain) runs on the host for every punch; a joiner only shows its own discharge. Gun arc perks (Ricochet, Static)
  for a joiner's shots run on the host from the mods its shot names (`HostStaticHits`).
  Static Charge fills on a joiner from the damage the host reports (`OnDamaged`), so both copies agree.
  Slows the host puts on a joiner reach it (`ClientApplySlow`): its moves must be slowed too.
  Visual relay on the game state (`AWWOGameState::Blast / Bolt / Discharge`): the host draws it and multicasts it.
  Grenade blasts go through it too (a blast that goes off the moment it exists, like the ult's, can't use the
  grenade's own channel yet). Chain lightning uses it, so everyone sees the jumps.
  Tested: the joiner's Thornfist on in the host's copy; the joiner's ult cast on the host (blast 584 damage) and
  seen by the joiner; single-player full test passes (every ability and ult).
- **Lunge and Spore Dash simulated on the host: done** (the `MoveClientAuth` shortcut is gone). The lunging
  machine steers the lunge at its target in `TickMovement` (`ApplyLunge`) and sends the velocity with each move
  (`MoveLunge` + `FWWOMoveInput::Push`, whole cm/s); both machines fly it in `SimulateMove` (`bLungeFlight`, in
  the move state) and end it the same way (`EndLungeFlight`). Spore Dash's launch goes out with the next move
  (`MoveLaunch`, `ApplyLaunch`). The host caps the pushed velocity (lunge speed x1.05, launches 30 m/s).
  Tested: 0 corrections in every step of the network test (lunges included, a mantle too); alone, lunge and dash
  distances match the old ones (2.01 vs 2.04 m, 6.04 vs 6.09 m).
- **Everyone sees the rest: done.** Through the game state relay: ally tracers, the Tank's slam shockwave,
  BLINDED / SLOWED texts, poison (each machine runs its own `UWWODamageOverTime`: drips and numbers; only the
  host's deals damage). The Tank's bubble shield replicates (`Shield`, `MaxShield`, `OnRep_Shield`: flash on a hit,
  the pop when it breaks), so joiners see it and guess kills through it. Shield Share refills other players'
  shields too, not just allies'.

## Phase 4: PvP (in progress)
Agreed with Will: **team deathmatch** (up to 6 v 6), **respawn after a delay** (no downed state, no reviving),
**no friendly fire**, and a **kill target and a time limit**, either of which can be off (whichever comes first).
- **Done** (`Game/WWOPlayerState`, `AWWOGameMode` PvP parts, `AWWOGameState::bPvP / TeamScores`):
  hosting with `?pvp` (Settings, Multiplayer: "Host a PvP game"; command line `-WWOHostPvP`; restarts and map
  changes keep it; the LAN session takes 12). No waves or enemies. Players join the smaller team (`PostLogin`,
  `BalanceTeam`); "Switch team (PvP)" in the menu (`ServerSwitchTeam`). Score line: "BLUE 3 – 1 ORANGE · first to
  50 · 7:42"; at the kill target or when time runs out (most kills; a tie is a draw) "BLUE WINS ..." and a
  restart 6 s later. Settings, PvP: kill target (50), time limit (10 min), respawn delay (3 s); host's values count.
  Players are targets in PvP (`AWWOCharacter` is an `IWWOTargetable`): body and head hitboxes (`BodyHitbox`,
  `HeadHitbox` on the camera, head = crit), on only in PvP (`UpdateHitboxes`; the movement capsule then stops taking
  shots). `UWWOWorldSubsystem::CanHurt(Attacker, Victim)` (players only in PvP, only the other team, never yourself)
  is checked where a hit lands (shots on both machines, the host's gun and melee checks, grenade bounces, chain
  lightning); `LivingTargets(For)` adds the other team's players for aim assist, melee, grenades, pools, mines,
  spikes and the HUD's health bars. Kill credit: `FWWODamageBy` marks who is dealing damage while a hit is applied
  (gunfire, melee, grenades, pools, chain lightning); a player's `TakeHit` stores it (`LastAttacker`) and the game
  mode scores the death (`OnPvPDeath`). PvP respawns go to the spawn spot (enemy spawn points and player starts)
  farthest from the other team (`PvPSpawnFor`).
  Tested (`-WWOAutoTestPvP`, host also `-WWOKillTarget=3`): different teams; the joiner shoots the host dead (kill
  to ORANGE, host respawns); on the same team its shots don't hurt (270 -> 270); the host's kills end the match at 3
  ("BLUE WINS 3 – 1" on both), the restart stays PvP. Co-op network test and single-player test still pass.
- **Team markers: done.** In PvP the HUD puts a team-colored square over every other player in sight (bigger for
  the other team), above their health bar. AI allies are removed in PvP.
- **Move values in PvP: done.** Everyone moves by the host's movement settings: the host ignores a joiner's
  `ServerSetMoveStats`, gives its copy the host's base values and sends them (`ClientSetMoveBase`) so the joiner
  predicts the same; each player's loadout still applies on both (the host's loadout copy). `SpeedScale` and
  `ExtraJumps` in a joiner's moves are capped at what its loadout allows (`MoveAutonomous`). Co-op is unchanged.
  Tested: host and joiner both move by walk 5 / sprint 10 / jump 2 in the PvP test.
- **Bots and the 12-player stress test: done.** `AWWOBotController` (an AI controller with a player state, so it
  has a team and a score) drives a normal player character: it holds buttons and pushes the stick, and the
  character and gun read those instead of a player controller's (`GetBot()` in `Pressed / JustPressed /
  MoveInput`). It fights the nearest visible player of the other team (aims in at the chest with a little noise,
  strafes, keeps 5-14 m, fires on a semi-auto rhythm) and otherwise runs between spawn spots, jumping now and then.
  A bot shows its full body like another player. Host: `-WWOHostPvP -WWOBots=N` (up to 11) spawns them once the
  match starts; `-WWOStress [-WWOStressTime=s]` logs the load every 5 s on the host (frame time, bandwidth per
  joiner, packet loss, kills) and on a joiner (frame time, bandwidth, corrections).
  Result (Mac mini, two editor copies, host + joiner + 10 bots, 95 s): 54 kills; host 8 ms a frame with the bots
  alone, 16 ms with the joiner's copy sharing the GPU; joiner 15 ms; ~11 KB/s to the joiner, 0 packets lost,
  1 correction a minute. Bandwidth grows with joiners (about 11 KB/s each at 12 players): fine on a LAN.
- **On two Macs (2026-11-07): done.** Host: the editor build on the Mac mini (its logs stay readable; the signed
  app's container needs approval to read); joiner: the packaged, signed app on the MacBook Air. Co-op test: the
  Air's own loadout on the host (rifle, Volt Grenade, Thornfist + Static Charge, 4 traits), Thornfist on in the
  host's copy, grenade throw and a refused second one, 6 of 7 shots applied, revive, ult, loss and restart.
  PvP test: opposite teams, both moving by the host's settings, the Air's rifle took the host 270 -> 30, no damage
  on the same team, BLUE won 3-0 and the match restarted as PvP. The first launch of a newly signed build asked
  for Local Network access on the Air again; after Allow, later signed builds kept it.
  The network test now puts the joiner back at its start before the shooting (each machine's own movement
  settings take the run to different places) and uses whichever ability slot holds Thornfist.

## What's left
- Phase 1: hand Will the packaged app (it's at /Volumes/UnrealSSD/Builds/WhatWasOurs, signed); fold this file
  into HANDOFF.md.
- PvP extras worth considering: a kill feed, a scoreboard screen, team spawn sides, team-colored bodies.
- The Metal renderer crash (`BindUniformBuffer`) still happens now and then on its own (single player too).
- Builds are now signed with this Mac's Apple Development identity when there is one (`Tools/package_mac.sh`), so
  macOS keeps the Local Network permission across builds. The first signed build asks once on each Mac
  ("differs from previously opened versions": Open Anyway). Reading a signed sandboxed app's container from
  another app (the test scripts' log copy) may need the Mac's approval too.

## Phase 2 notes (from the design discussion)
- Shots: client predicts (flash, tracer, hit marker), sends shot (origin, direction, timestamp);
  host rewinds target positions to that time, validates fire rate / ammo / line of sight, applies
  damage, multicasts hits (damage numbers, flash). Hidden aim assist stays client-side input help.
- Slow / veil / entrench should be host state replicated to the owner (the owner's `SpeedScale` must agree
  with the host's, or corrections loop). Health already is.
- Other effects (`AWWOFXActor`: grenade blasts, ability visuals) still exist only where they're made.

## PvP bot AI (2026-11-11)

`AWWOBotController` is no longer just a stress-test bot. All of it is in `Player/WWOBotController.cpp`; tuning is
`AWWOGameMode`'s `Bot*` values (Settings, Bots). The "Bots" slider adds bots mid-match (`-WWOBots=N` still works).
- **Moving and seeing:** navmesh paths (`WWONav::FindPath`); a view cone, sight range, fog and Veil; a reaction
  delay; it hunts a lost enemy at its last known spot for `BotMemory` s; being shot by an unseen enemy reveals them.
- **Aim and fire:** limited turn speed, an aim error that settles on target, fires only when its aim is on them;
  taps / bursts / range by gun.
- **Kit:** each bot has its own loadout (`GiveKit`: a `NetLoadout` like a joiner's, random flora, grenade, two
  abilities, two guns). Melee, grenades (arc solved in `ThrowDirection`), abilities, ult, gun swaps (`PickGun`).
  Bots press ability and ult buttons through `GetBot()` in `UWWOAbilityComponent` / `UWWOUltComponent`; the gun
  follows the bot's loadout through `UWWOWeaponComponent::RebindLoadout`.
- **Cover:** badly hurt (threshold from `BotAggression`), it runs to the nearest navmesh spot out of the enemy's
  sight (`FindCover`).
- **Team:** callouts to teammates in `BotCalloutRange` (`CallOut` / `HearCallout`; humans being shot, or the host
  firing at someone, count: `ListenToHumans`), focus fire in the target score, and `PickLeader`: a share of the
  team's bots (`BotStickToPlayer`) escort the human, the rest move in pairs.
- **Checking it:** host with `-WWOHostPvP -WWOBots=11 -WWOStress -WWOStressTime=90`; the log has "Bots:",
  "Bots kit:" and "Bots team:" lines every 10 s. With `-nullrhi` the whole match costs about 1.3 ms a frame.
- Not done: joiners' `ReticleTarget` isn't known to the host (only the host's shooting calls bots in), bots don't
  avoid a teammate's line of fire, and no objective play (there is only team deathmatch).
