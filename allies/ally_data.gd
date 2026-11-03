class_name AllyData
extends Resource
# Tuning shared by every AI teammate (data/allies/ally.tres). Settings → Allies edits it.

@export var ai_enabled := 1.0 # 0 = teammates stand still and don't shoot
@export var max_health := 100.0
@export var max_shield := 100.0
@export var recharge_delay := 4.0 # seconds without damage before shields refill
@export var shield_regen := 40.0 # per second
@export var health_regen := 25.0 # per second (after shields are full)
@export var move_speed := 6.0
@export var follow_min := 3.0 # stays at least this far from you
@export var follow_max := 7.0 # walks back toward you past this
@export var damage := 10.0 # per shot
@export var fire_interval := 0.35 # seconds between shots
@export var spread := 2.0 # degrees of aim error
@export var sight_range := 35.0
@export var respawn_time := 8.0 # seconds down before getting back up next to you
