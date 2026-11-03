class_name EnemyData
extends Resource
# Tuning every enemy type has. Each type's data (SoldierData, SwarmData, TankData)
# extends this and sets its own starting values in _init().

@export var ai_enabled := 1.0 # 0 = these enemies stand still and don't attack (for testing)
@export var max_health := 100.0
@export var walk_speed := 4.0 # m/s
@export var sight_range := 35.0 # notices you inside this range (getting shot also alerts it)
@export var turn_speed := 8.0 # how fast it turns to face you
