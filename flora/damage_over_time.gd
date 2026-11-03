class_name DamageOverTime
extends Node
# Poison: damages a target a little every half second for a while. Lives as a child of
# the target, so it goes away with it. A new poison refreshes the old one.

const TICK := 0.5
const COLOR := Color(0.7, 0.4, 0.95) # poison numbers are purple

var dps := 20.0
var time_left := 5.0
var _tick := TICK


static func apply(target: Node, damage_per_second: float, seconds: float) -> void:
	for child in target.get_children():
		if child is DamageOverTime:
			child.dps = maxf(child.dps, damage_per_second)
			child.time_left = maxf(child.time_left, seconds)
			return
	var poison := DamageOverTime.new()
	poison.dps = damage_per_second
	poison.time_left = seconds
	target.add_child(poison)


func _physics_process(delta: float) -> void:
	var target := get_parent()
	if target.get("dead"):
		queue_free()
		return
	time_left -= delta
	_tick -= delta
	if _tick <= 0.0:
		_tick = TICK
		target.hit(dps * TICK, false, (target as Node3D).global_position + Vector3.UP * 1.0, COLOR)
	if time_left <= 0.0:
		queue_free()
