class_name AimAssist
extends Node
# Controller aim assist, modeled on Destiny 2. Three tools:
# - Bullet magnetism: a shot that just misses gets bent onto a target inside a small
#   cone around the reticle, onto the head if that's the part you were closest to.
# - Friction: look speed drops while the reticle is near a target.
# - Adhesion (off by default, like Destiny): when a target slides across your view,
#   your aim gets dragged along a little. Only while you're moving a stick.
# Cones are angles around the reticle, measured to the edge of the head or body.
# Everything scales with the Aim Assistance stat, fades out past the assist range
# (which stretches with zoom while aimed), and gets stronger while aimed in.
# Full strength on controller; mouse gets `mouse_assist` worth (none by default).

# --- Global values (the settings menu edits these live) ---
@export var mouse_assist := 0.0 # 0-1: how much assist mouse players get
@export var show_debug := 1.0 # 1 = draw the cones on screen

# --- Per-weapon tuning, read live from the equipped weapon (see WeaponData) ---
var aim_assistance: float: # weapon stat, 0-100: scales every cone
	get: return gun.weapon.aim_assistance
var magnetism_angle: float: # degrees, at stat 100 and hip fire
	get: return gun.weapon.magnetism_angle
var friction_strength: float: # how much look speed drops on target (0-0.9)
	get: return gun.weapon.friction_strength
var friction_angle: float: # degrees, at stat 100 and hip fire
	get: return gun.weapon.friction_angle
var adhesion: float: # 0-1: how much of a target's sideways motion your aim follows
	get: return gun.weapon.adhesion
var assist_range: float: # meters of full assist; fades to nothing by 1.5x this
	get: return gun.weapon.assist_range
var ads_boost: float: # assist multiplier when fully aimed in
	get: return gun.weapon.ads_boost

const STAND_EYE_HEIGHT := 0.6 # eye height above the player's origin, for adhesion

var gun: Gun # set by the gun before this node is added
var using_controller := false # last input came from a controller
var friction_part := {} # the head/body currently inside the friction cone (for the debug view)
var _adhesion_key := ""
var _adhesion_last_dir := Vector3.ZERO


func _input(event: InputEvent) -> void:
	# Track which device you're using, ignoring tiny stick drift and mouse jitter.
	if event is InputEventJoypadMotion:
		if absf(event.axis_value) > 0.25:
			using_controller = true
	elif event is InputEventJoypadButton:
		using_controller = true
	elif event is InputEventMouseMotion:
		if event.relative.length() > 2.0:
			using_controller = false
	elif event is InputEventKey or event is InputEventMouseButton:
		using_controller = false


func device_scale() -> float:
	return 1.0 if using_controller else mouse_assist


func cone_scale() -> float:
	return device_scale() * aim_assistance / 100.0 * lerpf(1.0, ads_boost, gun.ads_amount)


func effective_range() -> float:
	return assist_range * lerpf(1.0, gun.zoom, gun.ads_amount) # zoomed guns reach further


func _range_falloff(distance: float) -> float:
	var full := effective_range()
	return clampf(1.0 - (distance - full) / (full * 0.5), 0.0, 1.0)


func find_part(base_angle_deg: float) -> Dictionary:
	# The head or body closest to the reticle inside the cone, or {} if none.
	var scale := cone_scale()
	if scale <= 0.0 or base_angle_deg <= 0.0:
		return {}
	var origin := gun.camera.global_position
	var forward := -gun.camera.global_basis.z
	var best := {}
	for node in get_tree().get_nodes_in_group("targets"):
		var target := node as Target
		for part in target.aim_parts(): # head first, so heads win ties
			var to: Vector3 = part.position - origin
			var distance := to.length()
			var cone := deg_to_rad(base_angle_deg) * scale * _range_falloff(distance)
			if cone <= 0.0:
				continue
			var angle := maxf(forward.angle_to(to) - atan(part.radius / distance), 0.0)
			if angle > cone or (not best.is_empty() and angle >= best.angle):
				continue
			var check := gun.cast_ray_to(part.position) # must be in line of sight
			if check.is_empty() or check.collider != target:
				continue
			best = {"target": target, "position": part.position, "critical": part.critical,
				"angle": angle, "cone": cone}
	return best


func magnetize() -> Dictionary:
	# For a shot that missed: bend it onto the nearest target part in the magnetism cone.
	var part := find_part(magnetism_angle)
	if part.is_empty():
		return {}
	var result := gun.cast_ray_to(part.position)
	if result.is_empty() or result.collider != part.target:
		return {}
	return result


func friction_scale() -> float:
	# Multiplier for stick look speed: drops as the reticle nears a target.
	friction_part = find_part(friction_angle)
	if friction_part.is_empty():
		return 1.0
	var closeness: float = 1.0 - friction_part.angle / friction_part.cone
	var slow := friction_strength * closeness * lerpf(1.0, ads_boost, gun.ads_amount)
	return 1.0 - clampf(slow, 0.0, 0.9)


func _physics_process(_delta: float) -> void:
	_update_adhesion()


func _update_adhesion() -> void:
	var player := gun.player
	var look := Input.get_vector("look_left", "look_right", "look_up", "look_down")
	var move := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	var active := adhesion > 0.0 and (look.length() > 0.1 or move.length() > 0.1)
	var part := find_part(friction_angle) if active else {}
	if part.is_empty():
		_adhesion_key = ""
		return
	# Direction from your eyes to the target, ignoring where you're looking,
	# so this only measures movement (theirs or yours), not your own turning.
	var eye := player.global_position + Vector3.UP * STAND_EYE_HEIGHT
	var dir: Vector3 = (part.position - eye).normalized()
	var key := "%d:%s" % [part.target.get_instance_id(), part.critical]
	if key == _adhesion_key:
		var yaw_change := wrapf(_yaw(dir) - _yaw(_adhesion_last_dir), -PI, PI)
		var pitch_change := asin(dir.y) - asin(_adhesion_last_dir.y)
		var pull := adhesion * device_scale()
		player.apply_recoil(pitch_change * pull, yaw_change * pull)
	_adhesion_key = key
	_adhesion_last_dir = dir


func _yaw(dir: Vector3) -> float:
	return atan2(-dir.x, -dir.z)
