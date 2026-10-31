class_name Grenade
extends RigidBody3D
# A thrown grenade: a glowing ball in the equipped grenade's color that bounces,
# then explodes when its fuse runs out. The explosion damages targets in range
# (full at the center, half at the edge) if nothing blocks the blast.
# With Seedburst equipped it bursts into smaller seed grenades when it explodes.

const RADIUS := 0.12 # size of the ball (seeds are smaller)
const EDGE_DAMAGE := 0.5 # fraction of full damage at the very edge of the blast

var color := Color.WHITE
var damage := 120.0
var blast_radius := 4.0
var fuse := 1.5
var gun: Gun # to show hit markers
var seedburst: AbilityData = null # set when thrown with Seedburst equipped
var is_seed := false # one of the seeds from a Seedburst


func _ready() -> void:
	if seedburst or is_seed:
		add_to_group(AbilityManager.SEED_GROUP) # keeps the Seedburst HUD lit
	var size := RADIUS * (0.65 if is_seed else 1.0)
	continuous_cd = true # fast and small: don't tunnel through thin walls
	physics_material_override = PhysicsMaterial.new()
	physics_material_override.bounce = 0.35
	physics_material_override.friction = 0.8

	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	(shape.shape as SphereShape3D).radius = size
	add_child(shape)

	var mesh := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = size
	sphere.height = size * 2.0
	mesh.mesh = sphere
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.emission_enabled = true
	mat.emission = color
	mat.emission_energy_multiplier = 3.0
	mesh.material_override = mat
	add_child(mesh)

	var light := OmniLight3D.new()
	light.light_color = color
	light.light_energy = 1.5
	light.omni_range = 2.5
	add_child(light)

	get_tree().create_timer(fuse, false).timeout.connect(_explode) # false: the fuse pauses with the game


func _explode() -> void:
	var center := global_position
	_spawn_blast(center)
	var space := get_world_3d().direct_space_state
	for node in get_tree().get_nodes_in_group("targets"):
		var target := node as Target
		# Closest part of the target to the blast.
		var best_part := {}
		var best_distance := INF
		for part in target.aim_parts():
			var distance: float = maxf((part.position - center).length() - part.radius, 0.0)
			if distance < best_distance:
				best_distance = distance
				best_part = part
		if best_distance > blast_radius:
			continue
		# Walls block the blast.
		var query := PhysicsRayQueryParameters3D.create(center + Vector3.UP * 0.05, best_part.position)
		query.exclude = [get_rid()]
		var check := space.intersect_ray(query)
		if not check.is_empty() and check.collider != target:
			continue
		var dealt := damage * lerpf(1.0, EDGE_DAMAGE, best_distance / blast_radius)
		var killed := target.hit(dealt, false, best_part.position)
		if gun:
			gun.hit_confirmed.emit(false, killed)
	if seedburst:
		_burst(center)
	queue_free()


func _burst(center: Vector3) -> void:
	# Seedburst: scatter smaller seed grenades outward from the blast.
	var root := get_tree().current_scene if get_tree().current_scene else get_tree().root
	var count := roundi(seedburst.sub_count)
	for i in count:
		var pod := Grenade.new()
		pod.is_seed = true
		pod.color = color.lightened(0.25)
		pod.damage = damage * seedburst.sub_scale
		pod.blast_radius = blast_radius * seedburst.sub_scale
		pod.fuse = seedburst.sub_fuse
		pod.gun = gun
		root.add_child(pod)
		pod.global_position = center + Vector3.UP * 0.3
		var angle := TAU * i / count + randf_range(-0.3, 0.3) # spread evenly around
		var out := Vector3(cos(angle), 0.0, sin(angle))
		pod.linear_velocity = out * seedburst.sub_spread + Vector3.UP * seedburst.sub_spread * 0.8


func _spawn_blast(center: Vector3) -> void:
	# A colored sphere that pops out to the blast size and fades, plus a light flash.
	var root := get_tree().current_scene if get_tree().current_scene else get_tree().root
	var blast := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 1.0
	sphere.height = 2.0
	blast.mesh = sphere
	var mat := StandardMaterial3D.new()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(color, 0.55)
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	blast.material_override = mat
	blast.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	blast.scale = Vector3.ONE * 0.1
	root.add_child(blast)
	blast.global_position = center

	var flash := OmniLight3D.new()
	flash.light_color = color
	flash.light_energy = 6.0
	flash.omni_range = blast_radius * 2.0
	blast.add_child(flash)

	var tween := blast.create_tween().set_parallel(true)
	tween.tween_property(blast, "scale", Vector3.ONE * blast_radius, 0.15).set_ease(Tween.EASE_OUT)
	tween.tween_property(mat, "albedo_color:a", 0.0, 0.4)
	tween.tween_property(flash, "light_energy", 0.0, 0.4)
	tween.chain().tween_callback(blast.queue_free)
