class_name AbilityData
extends Resource
# One flora ability. Each lives as a .tres file in res://data/abilities/.
# kind decides how it works and how the HUD shows it:
#   passive   - always on (HUD: half-grayed)
#   active    - press its button to use, then it recharges (HUD: like the grenade)
#   triggered - lights up on its own when something sets it off (HUD: dark until then)
# The effect itself is picked by `id` (see AbilityManager). Unused fields are ignored.

@export var id := ""
@export var display_name := ""
@export_multiline var description := ""
@export var color := Color.WHITE
@export_enum("passive", "active", "triggered") var kind := "passive"
@export var order := 0 # position in the picker
@export var unlocked := true # unlock logic slots in later

@export_group("Overgrowth")
@export var speed_mult := 1.0 # walk, sprint and crouch speed multiplier
@export var extra_jumps := 0.0 # added on top of the double jump

@export_group("Thornfist")
@export var melee_damage_mult := 1.0
@export var duration := 0.0 # seconds the boost lasts
@export var cooldown := 0.0 # seconds to recharge after use

@export_group("Seedburst")
@export var sub_count := 0.0 # seed grenades per burst
@export var sub_scale := 0.5 # seed blast radius and damage, as a fraction of the grenade's
@export var sub_spread := 5.0 # how hard the seeds fly out (m/s)
@export var sub_fuse := 0.6 # seconds before each seed explodes
