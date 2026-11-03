class_name SoldierData
extends EnemyData
# Tuning shared by every Soldier (data/enemies/soldier.tres). The settings menu's
# Soldier tab edits this live. Values come from the approved "Enemy: Soldier" doc.
# Health, speed, sight and turning are in EnemyData.

@export var min_range := 15.0 # backs off if you're closer than this
@export var max_range := 25.0 # walks toward you if you're farther than this
@export var strafe_time := 1.8 # average seconds before switching strafe direction

@export_group("Shooting")
@export var windup_time := 0.5 # glow before each burst, so you can react
@export var burst_count := 3.0
@export var burst_interval := 0.15 # seconds between shots in a burst
@export var burst_cooldown := 2.0 # seconds between bursts
@export var group_spacing := 0.6 # soldiers wait this long after another starts a burst
@export var projectile_damage := 8.0
@export var projectile_speed := 40.0 # m/s; slow enough to dodge at range
@export var spread := 1.5 # degrees of random aim error per shot

@export_group("Cover")
@export var cover_health := 0.35 # below this fraction of health it heads for cover


func _init() -> void:
	max_health = 150.0
	walk_speed = 4.0
	sight_range = 35.0
	turn_speed = 8.0
