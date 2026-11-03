class_name SwarmData
extends EnemyData
# Tuning shared by every Swarm (data/enemies/swarm.tres), from the "Enemy: Swarm" doc.
# The settings menu's Swarm tab edits this live.

@export var leap_range := 4.0 # leaps at you from this close
@export var leap_speed := 11.0 # m/s forward during a leap
@export var leap_height := 1.2 # meters up during a leap
@export var leap_damage := 15.0
@export var leap_cooldown := 2.5 # seconds between leaps
@export var bite_range := 1.4 # bites when this close
@export var bite_damage := 10.0
@export var bite_interval := 0.8 # seconds between bites
@export var spread_distance := 1.6 # keeps this far from other swarm so packs don't stack


func _init() -> void:
	max_health = 40.0
	walk_speed = 8.0 # its run speed
	sight_range = 30.0
	turn_speed = 12.0
