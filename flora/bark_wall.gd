class_name BarkWall
extends StaticBody3D
# The IronBark Wall Grenade's wall: a slab of iron bark that rises out of the ground
# where the grenade goes off, facing the thrower, then sinks back after a while.
# It blocks movement, enemy shots and your shots, and soldiers can use it as cover.

const COLOR := Color(0.42, 0.3, 0.19)
const RISE_TIME := 0.25

var width := 5.0
var height := 2.6
var duration := 10.0


static func raise(from: Node, center: Vector3, facing_from: Vector3, wall_width: float, wall_height: float, seconds: float) -> BarkWall:
	var wall := BarkWall.new()
	wall.width = wall_width
	wall.height = wall_height
	wall.duration = seconds
	var root := DamageNumbers.world_root(from)
	root.add_child(wall)
	# Stand it on the ground below the blast, turned to face where it was thrown from.
	var ground := center
	var query := PhysicsRayQueryParameters3D.create(center + Vector3.UP * 0.5, center + Vector3.DOWN * 6.0)
	var hit := wall.get_world_3d().direct_space_state.intersect_ray(query)
	if not hit.is_empty():
		ground = hit.position
	var toward := facing_from - ground
	toward.y = 0.0
	wall.global_position = ground
	if toward.length() > 0.1:
		wall.look_at(ground + toward, Vector3.UP)
	return wall


func _ready() -> void:
	add_to_group("cover")
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(width, height, 0.5)
	shape.shape = box
	shape.position.y = height * 0.5
	add_child(shape)
	var mesh := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = box.size
	mesh.mesh = bm
	mesh.position.y = height * 0.5
	var mat := StandardMaterial3D.new()
	mat.albedo_color = COLOR
	mat.roughness = 0.9
	mesh.material_override = mat
	add_child(mesh)
	# A few vertical ridges so it reads as bark.
	for i in 5:
		var ridge := MeshInstance3D.new()
		var rm := BoxMesh.new()
		rm.size = Vector3(0.18, height * randf_range(0.85, 1.05), 0.62)
		ridge.mesh = rm
		ridge.position = Vector3(lerpf(-width * 0.4, width * 0.4, i / 4.0), rm.size.y * 0.5, 0.0)
		var rmat := StandardMaterial3D.new()
		rmat.albedo_color = COLOR.darkened(0.3)
		ridge.material_override = rmat
		add_child(ridge)
	# Rise out of the ground.
	scale = Vector3(1.0, 0.01, 1.0)
	create_tween().tween_property(self, "scale", Vector3.ONE, RISE_TIME).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	get_tree().create_timer(duration, false).timeout.connect(_sink)


func _sink() -> void:
	remove_from_group("cover")
	var tween := create_tween()
	tween.tween_property(self, "scale", Vector3(1.0, 0.01, 1.0), 0.4)
	tween.tween_callback(queue_free)
