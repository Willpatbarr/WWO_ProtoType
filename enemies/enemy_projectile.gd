class_name EnemyProjectile
extends Node3D
# A slow glowing shot fired by enemies. Moves each step, checking with a ray so it
# can't skip through thin things. Hurts the player on contact and pops on anything else.
# Enemies don't hit each other.
# With `gravity` it flies in an arc; with `splash_radius` it explodes where it lands and
# hurts you if you're close (full at the center, half at the edge).

var velocity := Vector3.ZERO
var damage := 8.0
var life := 3.0
var gravity := 0.0
var splash_radius := 0.0
var color := Color(1.0, 0.35, 0.1)
var size := 0.09 # radius
var exclude: Array[RID] = [] # the enemies, so shots pass through them


func _ready() -> void:
	var mesh := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = size
	sphere.height = size * 2.0
	sphere.radial_segments = 12
	sphere.rings = 6
	mesh.mesh = sphere
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = color
	mat.emission_enabled = true
	mat.emission = color
	mat.emission_energy_multiplier = 4.0
	mesh.material_override = mat
	mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mesh)
	var light := OmniLight3D.new()
	light.light_color = color
	light.light_energy = 0.8
	light.omni_range = 1.5
	add_child(light)


func _physics_process(delta: float) -> void:
	life -= delta
	if life <= 0.0:
		if splash_radius > 0.0:
			_splash(global_position)
		queue_free()
		return
	velocity.y -= gravity * delta
	var from := global_position
	var to := from + velocity * delta
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.exclude = exclude
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		global_position = to
		return
	if splash_radius > 0.0:
		_splash(hit.position)
	elif (hit.collider as Node).is_in_group("player"):
		var health := get_tree().get_first_node_in_group("player_health")
		if health:
			health.take_damage(damage)
	elif (hit.collider as Node).is_in_group("teammates"):
		hit.collider.take_damage(damage)
	_pop(hit.position)
	queue_free()


func _splash(point: Vector3) -> void:
	var player := get_tree().get_first_node_in_group("player") as Node3D
	var health := get_tree().get_first_node_in_group("player_health")
	if player and health:
		var distance := point.distance_to(player.global_position)
		if distance <= splash_radius + 0.5: # +0.5: the player's body, not just its center
			var falloff := clampf(distance / splash_radius, 0.0, 1.0)
			health.take_damage(damage * lerpf(1.0, 0.5, falloff))
	for mate in get_tree().get_nodes_in_group("teammates"):
		var d := point.distance_to((mate as Node3D).global_position)
		if d <= splash_radius + 0.5:
			mate.take_damage(damage * lerpf(1.0, 0.5, clampf(d / splash_radius, 0.0, 1.0)))
	# The blast: a sphere that pops out to the splash size and fades.
	var blast := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 1.0
	sphere.height = 2.0
	blast.mesh = sphere
	var mat := StandardMaterial3D.new()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	mat.albedo_color = Color(color, 0.5)
	blast.material_override = mat
	blast.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	DamageNumbers.world_root(self).add_child(blast)
	blast.global_position = point
	blast.scale = Vector3.ONE * 0.1
	var tween := blast.create_tween().set_parallel(true)
	tween.tween_property(blast, "scale", Vector3.ONE * splash_radius, 0.15)
	tween.tween_property(mat, "albedo_color:a", 0.0, 0.4)
	tween.chain().tween_callback(blast.queue_free)


func _pop(point: Vector3) -> void:
	# A quick flash where the shot landed.
	var flash := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = size * 2.5
	sphere.height = size * 5.0
	flash.mesh = sphere
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.albedo_color = color
	flash.material_override = mat
	DamageNumbers.world_root(self).add_child(flash)
	flash.global_position = point
	var tween := flash.create_tween()
	tween.tween_property(mat, "albedo_color:a", 0.0, 0.2)
	tween.tween_callback(flash.queue_free)
