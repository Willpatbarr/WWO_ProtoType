class_name GrenadeData
extends Resource
# One grenade option. Each lives as a .tres file in res://data/grenades/.
# Only the menu/HUD data for now; throwing comes later.

@export var id := ""
@export var display_name := ""
@export var color := Color.WHITE
@export var order := 0 # position in the picker
@export var damage := 120.0 # at the center of the blast (half at the edge)
@export var blast_radius := 4.0 # meters; bigger blasts hit softer
# Adjustments on top of the global grenade settings (1.0 = same as global).
@export var throw_distance_mult := 1.0
@export var recharge_mult := 1.0
@export var fuse_mult := 1.0
@export var unlocked := true # unlock logic slots in later
@export var flora := "voltvine" # which flora this grenade belongs to (FloraData.id)
# What it does: explosive = blows up on its fuse; pool = small blast that leaves a
# damaging pool; spike = bounces around until it touches an enemy, then poisons it;
# tripmine = sticks where it lands, arms, and blows up (slowing enemies) when one comes close;
# wall = blows up and raises a wall facing you; blind = blows up and blinds enemies.
@export_enum("explosive", "pool", "spike", "tripmine", "wall", "blind") var kind := "explosive"
@export var bounce := 0.35 # how bouncy it is (0-1)

@export_group("Lingering pool")
@export var pool_dps := 25.0 # damage per second to anything standing in the pool
@export var pool_time := 5.0 # seconds the pool lasts

@export_group("Poison spike")
@export var poison_dps := 20.0 # poison damage per second on the target it touches
@export var poison_time := 5.0 # seconds the poison lasts
@export var spike_life := 6.0 # seconds it bounces around looking for a target
@export var seek_radius := 7.0 # meters: it steers toward the nearest target this close
@export var seek_strength := 160.0 # degrees per second it can turn toward that target

@export_group("Slowing tripmine")
@export var arm_time := 0.6 # seconds after it sticks before it can go off
@export var trigger_radius := 2.5 # goes off when an enemy comes this close
@export var mine_life := 25.0 # seconds before it fizzles out
@export var slow_amount := 0.5 # enemies caught in the blast move this much slower
@export var slow_time := 3.0 # seconds the slow lasts

@export_group("Wall")
@export var wall_width := 5.0
@export var wall_height := 2.6
@export var wall_time := 10.0 # seconds before it sinks back into the ground

@export_group("Blinding")
@export var blind_time := 3.0 # seconds enemies caught in it can't see or attack
