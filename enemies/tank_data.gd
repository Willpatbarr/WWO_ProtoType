class_name TankData
extends EnemyData
# Tuning for the Tank (data/enemies/tank.tres), from the "Enemy: Tank" doc.
# The settings menu's Tank tab edits this live.

@export_group("Shield")
@export var shield := 300.0 # must break before health takes damage; 0 = no shield
@export var shield_regen_delay := 6.0 # seconds without damage before it refills
@export var shield_regen_rate := 75.0 # shield per second while refilling

@export_group("Slam")
@export var slam_range := 4.0 # starts a slam when you're this close
@export var slam_radius := 4.5 # the slam hits anything this close
@export var slam_damage := 40.0
@export var slam_windup := 0.7 # seconds of arms-up warning before it lands
@export var slam_recover := 1.5 # seconds it stands still after (the window to hit its back)
@export var slam_cooldown := 4.0
@export var slow_amount := 0.5 # the slam slows you by this much (0.5 = half speed)
@export var slow_time := 2.5 # seconds the slow lasts

@export_group("Lobbed shot")
@export var lob_damage := 35.0 # at the center of the splash (half at the edge)
@export var lob_radius := 3.0 # splash radius
@export var lob_interval := 3.0 # seconds between shots
@export var lob_flight_time := 1.2 # seconds in the air (longer = higher arc)


func _init() -> void:
	max_health = 1200.0
	walk_speed = 3.0
	sight_range = 40.0
	turn_speed = 1.5 # turns slowly, so you can circle it
