# Unreal port polish tickets

Each ticket covers one piece of the game. The work is the same each time: play it side by side with the Godot build (`~/Documents/WhatWasOurs`), list what feels or looks off, and fix it in the Unreal project (`/Volumes/UnrealSSD/Projects/WhatWasOurs`). Tuning numbers should stay in Godot units, as described in HANDOFF.md.

---

## 1. Ground movement feel
Covers walking, sprinting, crouching, sliding and stopping: `WWOCharacter` and `WWOCharacterMovement`.
- Compare the speed you reach, how fast you speed up and stop, and how far a slide goes, with stick and keyboard.
- Check that Unreal's own movement code (braking, friction, step-up, slope handling) isn't fighting the Godot rules.
- **Done when:** a lap of the small arena feels the same in both builds.

## 2. Jump and air feel
Covers jump height, the hang at the top of a jump, air control, falling gravity and landing.
- Compare the jump arc, how well you can steer mid-air, and how coyote time and jump buffering feel.
- Check landing onto ledges and the step-ups in the large arena.
- **Done when:** the large-arena platforming feels like Godot.

## 3. Look and camera with the controller
Covers right-stick look on the DualSense: dead zone, response curve, sensitivity, ADS slowdown, and aim-assist friction and adhesion.
- Compare small aim corrections and full turns. Check that the 0.15 dead zone and the curve match Godot.
- Also check head bob, camera tilt and FOV changes (sprint, ADS).
- **Done when:** tracking a moving Swarm with the stick feels like Godot.

## 4. Gun feel
Covers firing, recoil, spread, the muzzle flash, tracers, hit markers, damage numbers, reload and weapon swap.
- Compare viewmodel sway, recoil kick and recovery, and how readable hits are. The rumble and hitstop on hits should match.
- **Done when:** emptying a magazine into a target feels as punchy as Godot.

## 5. Grenades and melee
Covers the grenade throw arc, bounce, fuse, blast visuals, and each grenade kind (pool, spike, tripmine, wall, blind). Also the melee lunge, hit and arm animation.
- Each grenade kind should be tested at least once.
- The melee lunge distance and its snap onto the target should match.
- **Done when:** every grenade kind and melee behave and read like Godot.

## 6. Abilities and ults
Covers the ability slots (Static Charge and friends) and the three ult kinds (Blast, Veil, Entrench).
- Check the timing and readability of the ult's third-person cast, the camera move in and out of it, and the blast.
- Check charge and cooldown feedback on the HUD.
- **Done when:** each ability and ult works and looks right in play.

## 7. Enemy behavior
Covers Soldier, Swarm and Tank: navmesh pathing, attack timing, projectiles, knockback, death, and wave pacing in `WWOGameMode`.
- Look for enemies sliding, getting stuck on corners, or bunching up. Compare how hard waves 1 to 8 are.
- **Done when:** a full 8-wave run feels as fair and readable as Godot.

## 8. Character bodies and animation
Covers the Manny bodies on the player, enemies and allies: tint, hit reactions, death, and the template-animation gaps (no crouch, slide or flinch).
- Find or build the missing poses: crouch, slide, hit flinch, the Swarm's hunch, and an ult cast.
- **Done when:** none of the bodies look stiff or wrong next to Godot.

## 9. Flora grafts and frame parts on the body
Covers the visual grafts (vines, mushrooms, bark) and frame-part region colors, which are simplified right now.
- Rebuild them closer to the Godot procedural look, on the third-person body and in the character-menu preview.
- **Done when:** the character menu preview reads like the Godot one.

## 10. HUD and menus
Covers the HUD layout and scaling, the settings menu, the character menu, the controller menu cursor, and the map switch.
- Compare fonts, spacing and colors, and how menus navigate on the DualSense. Esc, Tab and Create should all work in standalone and in the editor.
- **Done when:** every menu works by controller alone and looks like Godot.

## 11. Arena look and lighting
Covers the visuals of the three maps: the prototype-grid blocks, lighting, exposure, fog and colors.
- Get closer to the Godot (Quaternius kit) look, either with Unreal-native assets or with better materials and lighting.
- **Done when:** the small and large arenas no longer look like a gray box next to Godot.
