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
@export var flora := "voltvine" # which flora this ability belongs to (FloraData.id)

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

@export_group("Spore Fog Damage")
@export var fog_dps := 12.0 # damage per second spore fog does to Swarm and Soldiers

@export_group("Spore Dash")
@export var dash_speed := 13.0 # m/s backwards (uses `cooldown` above for its recharge)
@export var dash_up := 5.0 # m/s upward, so you leave the ground and keep the momentum
@export var dash_fog_radius := 4.0
@export var dash_fog_time := 6.0

@export_group("Spore Patches")
@export var patch_radius := 2.0 # fog around each patch
@export var patch_time := 6.0 # seconds each patch lasts
@export var patch_max := 4.0 # most patches one grenade can leave

@export_group("Melee Guard")
@export var guard_reduction := 0.5 # melee damage you take is cut by this much (uses `duration`)

@export_group("Root Bombs")
@export var bomb_damage := 150.0 # each bomb, at the center (uses `cooldown` for its recharge)
@export var bomb_radius := 3.5
@export var bomb_speed := 22.0 # m/s when thrown

@export_group("Shield Share")
@export var shield_amount := 60.0 # shield your explosions give each teammate caught in them

@export_group("Extra Grenade")
@export var extra_charges := 1.0 # grenade charges on top of the normal one
