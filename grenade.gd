class_name Grenade
extends RigidBody3D
# A thrown grenade: a glowing ball in the equipped grenade's color that bounces,
# then explodes when its fuse runs out. The explosion damages targets in range
# (full at the center, half at the edge) if nothing blocks the blast.
# With Seedburst equipped it bursts into smaller seed grenades when it explodes.
# VeilSpore grenades (by data.kind):
# - pool: a small blast that leaves a Lingering Pool (SporePool) that hurts over time.
# - spike: no fuse. Bounces around until it touches a target, then hits it and poisons
#   it (DamageOverTime). Fizzles if it finds nothing in time.
# - tripmine (VoltVine Snare Mine): sticks to the first surface it hits, arms, then
#   blows up when a target comes within trigger_radius, slowing enemies in the blast.
# With Spore Patches equipped, every time it touches down it sprouts a SporePatch.

const RADIUS := 0.12 # size of the ball (seeds are smaller)
const EDGE_DAMAGE := 0.5 # fraction of full damage at the very edge of the blast
const PROXIMITY := 0.45 # the Poison Spike counts as a hit this far from a target's body

var color := Color.WHITE
var damage := 120.0
var blast_radius := 4.0
var fuse := 1.5
var gun: Gun # to show hit markers
var seedburst: AbilityData = null # set when thrown with Seedburst equipped
var is_seed := false # one of the seeds from a Seedburst
var data: GrenadeData = null # the grenade it was thrown as (null for seeds and the ult)
var spore_patches: AbilityData = null # set when thrown with Spore Patches equipped
var _patches_left := 0
var _patch_cooldown := 0.0
var _spent := false
var _stuck := false # tripmine: stuck in place
var _arm_left := -1.0 # tripmine: seconds until armed (-1 = not stuck yet)
var _light: OmniLight3D
var explode_on_impact := false # Impact Grenades trait, and Root Bombs
var size_scale := 1.0 # Root Bombs are bigger
var thrown_from := Vector3.ZERO # where it was thrown from (walls face that way)


func _ready() -> void:
	if seedburst or is_seed:
		add_to_group(AbilityManager.SEED_GROUP) # keeps the Seedburst HUD lit
	var size := RADIUS * (0.65 if is_seed else 1.0) * size_scale
	continuous_cd = true # fast and small: don't tunnel through thin walls
	physics_material_override = PhysicsMaterial.new()
	physics_material_override.bounce = clampf((data.bounce if data else 0.35) + Loadout.bonus(get_tree(), "more_bounce"), 0.0, 1.0) # Bouncier Grenades trait
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
	_light = light

	if spore_patches:
		_patches_left = roundi(spore_patches.patch_max)
	if data and data.kind == "tripmine":
		contact_monitor = true
		max_contacts_reported = 4
		body_entered.connect(_on_body_entered)
		get_tree().create_timer(data.mine_life, false).timeout.connect(_fizzle)
		return # no fuse: it waits for an enemy
	if data and data.kind == "spike":
		contact_monitor = true
		max_contacts_reported = 4
		body_entered.connect(_on_body_entered)
		get_tree().create_timer(data.spike_life, false).timeout.connect(_fizzle)
		return # no fuse: it goes off when it touches a target
	if spore_patches or explode_on_impact:
		contact_monitor = true
		max_contacts_reported = 4
		body_entered.connect(_on_body_entered)
	get_tree().create_timer(fuse, false).timeout.connect(_explode) # false: the fuse pauses with the game


func _physics_process(delta: float) -> void:
	_patch_cooldown -= delta
	if data and data.kind == "spike" and not _spent:
		_seek(delta)
	if data and data.kind == "tripmine" and _stuck and not _spent:
		_update_mine(delta)


func _seek(delta: float) -> void:
	# The Poison Spike curves toward the nearest target in range, harder the closer it
	# gets, so it actually makes contact.
	var best: Node3D = null
	var best_distance := data.seek_radius
	var best_radius := 0.0
	for node in get_tree().get_nodes_in_group("targets"):
		var body: Dictionary = node.aim_parts()[-1] # the body
		var distance := global_position.distance_to(body.position)
		if distance < best_distance:
			best = node
			best_distance = distance
			best_radius = body.radius
	if best == null:
		return
	if best_distance <= best_radius + PROXIMITY:
		_spike_hit(best) # close enough: counts as touching it
		return
	var to: Vector3 = (best.aim_parts()[-1].position - global_position).normalized()
	var speed := linear_velocity.length()
	if speed < 0.5:
		return
	# Turn its flight toward the target (keeping its speed), faster the closer it gets.
	var heading := linear_velocity / speed
	var angle := heading.angle_to(to)
	if angle < 0.001:
		return
	var turn := deg_to_rad(data.seek_strength) * (1.0 + (1.0 - best_distance / data.seek_radius)) * delta
	linear_velocity = heading.slerp(to, minf(turn / angle, 1.0)) * speed


func _on_body_entered(body: Node) -> void:
	if _spent:
		return
	if data and data.kind == "spike" and Target.is_target(body) and not body.dead:
		_spike_hit(body)
		return
	if data and data.kind == "tripmine" and not _stuck:
		_stick()
		return
	if explode_on_impact and not body is Grenade:
		_explode.call_deferred() # after the patch below, if any

	if spore_patches and _patches_left > 0 and _patch_cooldown <= 0.0 and not Target.is_target(body):
		_patches_left -= 1
		_patch_cooldown = 0.3
		var patch := SporePatch.new()
		patch.radius = spore_patches.patch_radius
		patch.duration = spore_patches.patch_time
		DamageNumbers.world_root(self).add_child(patch)
		patch.global_position = global_position + Vector3.DOWN * RADIUS


func _share_shields(center: Vector3) -> void:
	# Shield Share (IronBark): explosions refill the shields of teammates caught in them.
	# Nothing is in the "teammates" group in the prototype yet, so this does nothing for now.
	var abilities := get_tree().get_first_node_in_group("ability_manager")
	var share: AbilityData = abilities.equipped("shield_share") if abilities else null
	if share == null:
		return
	for mate in get_tree().get_nodes_in_group("teammates"):
		if (mate as Node3D).global_position.distance_to(center) <= blast_radius + 0.5 and mate.has_method("restore_shield"):
			mate.restore_shield(share.shield_amount)


func _stick() -> void:
	# The Snare Mine plants itself where it first lands.
	_stuck = true
	set_deferred("freeze", true)
	linear_velocity = Vector3.ZERO
	_arm_left = data.arm_time


func _update_mine(delta: float) -> void:
	if _arm_left > 0.0:
		_arm_left -= delta
		_light.light_energy = 0.3
		return
	_light.light_energy = 1.0 + 1.0 * sin(Time.get_ticks_msec() * 0.015) # armed: blinks
	for node in get_tree().get_nodes_in_group("targets"):
		if (node as Node3D).global_position.distance_to(global_position) <= data.trigger_radius + 0.5:
			_explode()
			return


func _spike_hit(target: Node) -> void:
	# Direct contact: a hit, then poison.
	_spent = true
	var point := global_position
	var killed: bool = target.hit(damage, false, point)
	if gun:
		gun.hit_confirmed.emit(false, killed)
	if not killed:
		var potency := 1.0 + Loadout.bonus(get_tree(), "poison_damage") # Stronger Poison trait
		DamageOverTime.apply(target, data.poison_dps * potency, data.poison_time)
	blast_radius = 0.8
	_spawn_blast(point)
	queue_free()


func _fizzle() -> void:
	if _spent or not is_inside_tree():
		return
	_spent = true
	blast_radius = 0.6
	_spawn_blast(global_position)
	queue_free()


func _explode() -> void:
	if _spent:
		return
	_spent = true
	var center := global_position
	_spawn_blast(center)
	var space := get_world_3d().direct_space_state
	for node in get_tree().get_nodes_in_group("targets"):
		var target = node
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
		var killed: bool = target.hit(dealt, false, best_part.position)
		if gun:
			gun.hit_confirmed.emit(false, killed)
	if seedburst:
		_burst(center)
	_share_shields(center)
	if data and data.kind == "wall":
		BarkWall.raise(self, center, thrown_from, data.wall_width, data.wall_height,
			data.wall_time * (1.0 + Loadout.bonus(get_tree(), "wall_longer"))) # Longer Walls trait
	if data and data.kind == "blind":
		for enemy in get_tree().get_nodes_in_group("enemies"):
			if not enemy.dead and (enemy as Node3D).global_position.distance_to(center) <= blast_radius + 0.5:
				enemy.apply_blind(data.blind_time)
	if data and data.kind == "tripmine":
		# Slow every enemy caught in the blast.
		var slow_time := data.slow_time * (1.0 + Loadout.bonus(get_tree(), "slow_longer")) # Longer Snare trait
		for enemy in get_tree().get_nodes_in_group("enemies"):
			if not enemy.dead and (enemy as Node3D).global_position.distance_to(center) <= blast_radius + 0.5:
				enemy.apply_slow(data.slow_amount, slow_time)
	if data and data.kind == "pool":
		var pool := SporePool.new()
		pool.radius = blast_radius
		pool.dps = data.pool_dps * (1.0 + Loadout.bonus(get_tree(), "poison_damage")) # Stronger Poison trait
		pool.duration = data.pool_time
		pool.color = color
		pool.gun = gun
		DamageNumbers.world_root(self).add_child(pool)
		pool.global_position = center + Vector3.DOWN * RADIUS
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
