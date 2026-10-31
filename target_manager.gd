class_name TargetManager
extends Node
# Owns the settings shared by every target, and resets them.

@export var target_health := 200.0: set = _set_target_health
@export var respawn_time := 0.0 # seconds before a dead target comes back; 0 = only on reset


func _ready() -> void:
	for target in _all_targets():
		target.manager = self
	reset_targets()


func reset_targets() -> void:
	# Bring every target back at full health.
	for target in _all_targets():
		target.revive(target_health)


func _set_target_health(value: float) -> void:
	target_health = value
	if is_node_ready():
		reset_targets() # new health applies right away


func _all_targets() -> Array[Node]:
	return get_tree().get_nodes_in_group("all_targets")
