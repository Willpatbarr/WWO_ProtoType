class_name UltManager
extends Node
# The ult (F, or L1 + R1 together on controller). Which one depends on the equipped flora (FloraData.ult):
# - blast (Starter): a massive grenade explosion centered on you (below).
# - veil (VeilSpore): a quick third-person flash as you release a big spore fog; for a
#   while after, you're invisible to enemies and hit much harder in melee whenever
#   you're inside spore fog (any spore fog). See _run_veil().
# - entrench (IronBark): the same quick flash as roots burst up around you; for a while
#   you're rooted in place (you can still look and shoot), can't be hurt, and your
#   grenade recharges much faster. See _entrench().
# When it fires, the camera glides out to third person behind you, your body plays a
# cast (hands up, an orb in your grenade's color grows between them, then thrown down),
# the blast goes off, and the camera glides back into first person.
# You can't move, look, shoot or take damage while it plays.
# Charges over time; the HUD reads `charge` (0..1).

signal started
signal finished

@export var player: CharacterBody3D
@export var loadout: Loadout

@export var charge_time := 60.0 # seconds from empty to full
@export var damage := 600.0 # at the center (half at the edge, like grenades)
@export var blast_radius := 12.0 # meters
@export var windup_time := 1.2 # seconds from pressing it to the blast
@export var hold_time := 1.0 # seconds to watch the blast before the camera returns
@export var camera_distance := 4.0 # meters behind you in third person
@export var camera_height := 1.0 # meters above your eyes in third person
@export var camera_side := 0.6 # meters to the right (over the shoulder)
@export var camera_blend := 0.4 # seconds for the camera to glide out or back

@export_group("Veil (VeilSpore)")
@export var veil_time := 12.0 # seconds you're veiled (invisible while in spore fog)
@export var veil_fog_radius := 7.0 # the big fog the ult releases
@export var veil_fog_time := 12.0 # how long that fog lasts
@export var veil_melee_mult := 2.5 # melee damage while veiled and in fog
@export var veil_camera_blend := 0.15 # the quick flash out to third person and back
@export var veil_hold := 0.35 # seconds in third person for the release

@export_group("Entrench (IronBark)")
@export var entrench_time := 10.0 # seconds rooted and invulnerable
@export var entrench_grenade_mult := 4.0 # grenade recharge speed while entrenched

const ENTER_ANIM := "Spell_Simple_Enter"
const CHARGE_ANIM := "Spell_Simple_Idle"
const CAST_ANIM := "Spell_Simple_Shoot"
const CAST_LEAD := 0.2 # the throw animation starts this long before the blast
const ORB_SIZE := 0.25 # radius of the orb when fully charged

var charge := 1.0 # start full so it's easy to test
var active := false

var _camera: Camera3D
var _orb: MeshInstance3D
var _orb_mat: StandardMaterial3D


func _ready() -> void:
	add_to_group("ult_manager")


func _process(delta: float) -> void:
	if not active and charge < 1.0:
		charge = minf(charge + delta / maxf(charge_time, 0.1), 1.0)
	if active and _orb:
		_place_orb()
	if Input.is_action_just_pressed("ult") or ControllerChord.consume(get_tree(), "ult"): # F, or L1 + R1
		activate()


func is_ready() -> bool:
	return charge >= 1.0


func can_activate() -> bool:
	return is_ready() and not active and player.process_mode != Node.PROCESS_MODE_DISABLED \
		and not player.mantling and not player.lunging


func activate() -> bool:
	if not can_activate():
		return false
	charge = 0.0
	active = true
	started.emit()
	_run()
	return true


func fill() -> void:
	# Test helper (settings menu button).
	charge = 1.0


# --- The sequence -----------------------------------------------------------------

func _run() -> void:
	if loadout.flora and loadout.flora.ult == "veil":
		await _run_quick(_release_veil)
		return
	if loadout.flora and loadout.flora.ult == "entrench":
		await _run_quick(_entrench)
		return
	var color := loadout.grenade.color if loadout.grenade else Color(1.0, 0.6, 0.2)
	player.ulting = true
	player.velocity = Vector3(0.0, minf(player.velocity.y, 0.0), 0.0)
	var gun: Node3D = player.gun
	gun.visible = false
	gun.process_mode = Node.PROCESS_MODE_DISABLED
	var model: CharacterModel = player.model
	model.set_shadow_only(false) # the whole body shows in third person
	model.play(ENTER_ANIM, 1.0, 0.1)
	_make_orb(color)

	# Camera: out to third person.
	_camera = Camera3D.new()
	_camera.fov = player.camera.fov
	DamageNumbers.world_root(player).add_child(_camera)
	var first_person: Transform3D = player.camera.global_transform
	_camera.global_transform = first_person
	_camera.current = true
	await _glide(first_person, _third_person_transform())

	# Charge, then throw.
	var enter_left := maxf(model.anim.current_animation_length - model.anim.current_animation_position, 0.0)
	await _wait(minf(enter_left, maxf(windup_time - camera_blend - CAST_LEAD, 0.0)))
	model.play(CHARGE_ANIM, 1.0, 0.1)
	await _wait(maxf(windup_time - camera_blend - CAST_LEAD - enter_left, 0.0))
	model.play(CAST_ANIM, 1.0, 0.1)
	await _wait(CAST_LEAD)
	_explode(color)
	await _wait(hold_time)

	# Camera: back into first person.
	model.play("Idle", 1.0, 0.3)
	await _glide(_camera.global_transform, player.camera.global_transform)
	player.camera.current = true
	_camera.queue_free()
	_camera = null
	model.set_shadow_only(true)
	gun.process_mode = Node.PROCESS_MODE_INHERIT
	gun.visible = true
	player.ulting = false
	player.reset_input_edges() # a button held through the ult doesn't fire on release
	active = false
	finished.emit()


func _release_veil() -> void:
	SporeFog.spawn(player, player.global_position, veil_fog_radius, veil_fog_time, true)
	player.veil_left = veil_time


func _entrench() -> void:
	player.entrench_left = entrench_time
	var roots := EntrenchRoots.new()
	roots.duration = entrench_time
	player.add_child(roots)
	roots.position = Vector3.DOWN * 1.0 # at your feet


func _run_quick(release: Callable) -> void:
	# A super quick third-person beat: out, release the ult's effect, straight back in.
	player.ulting = true
	player.velocity = Vector3(0.0, minf(player.velocity.y, 0.0), 0.0)
	var gun: Node3D = player.gun
	gun.visible = false
	gun.process_mode = Node.PROCESS_MODE_DISABLED
	var model: CharacterModel = player.model
	model.set_shadow_only(false)
	model.play(CAST_ANIM, 1.6, 0.05)
	_camera = Camera3D.new()
	_camera.fov = player.camera.fov
	DamageNumbers.world_root(player).add_child(_camera)
	var first_person: Transform3D = player.camera.global_transform
	_camera.global_transform = first_person
	_camera.current = true
	var blend := camera_blend
	camera_blend = veil_camera_blend
	await _glide(first_person, _third_person_transform())
	release.call()
	await _wait(veil_hold)
	model.play("Idle", 1.0, 0.2)
	await _glide(_camera.global_transform, player.camera.global_transform)
	camera_blend = blend
	player.camera.current = true
	_camera.queue_free()
	_camera = null
	model.set_shadow_only(true)
	gun.process_mode = Node.PROCESS_MODE_INHERIT
	gun.visible = true
	player.ulting = false
	player.reset_input_edges()
	active = false
	finished.emit()


func _third_person_transform() -> Transform3D:
	# Behind and above you, over the right shoulder, looking at your chest. Pulled in if
	# a wall is in the way.
	var eyes: Vector3 = player.camera.global_position
	var back: Vector3 = player.global_basis.z
	var right: Vector3 = player.global_basis.x
	var wanted := eyes + back * camera_distance + Vector3.UP * camera_height + right * camera_side
	var query := PhysicsRayQueryParameters3D.create(eyes, wanted)
	query.exclude = [player.get_rid()]
	var hit := player.get_world_3d().direct_space_state.intersect_ray(query)
	if not hit.is_empty():
		wanted = hit.position + (eyes - wanted).normalized() * 0.3
	var look_at: Vector3 = player.global_position + Vector3.UP * 0.2
	return Transform3D(Basis.looking_at(look_at - wanted, Vector3.UP), wanted)


func _glide(from: Transform3D, to: Transform3D) -> void:
	var tween := create_tween().set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	tween.tween_method(func(t: float) -> void:
		_camera.global_transform = from.interpolate_with(to, t), 0.0, 1.0, maxf(camera_blend, 0.01))
	await tween.finished


func _wait(seconds: float) -> void:
	if seconds > 0.0:
		await get_tree().create_timer(seconds, false).timeout # false: pauses with the game


# --- The orb and the blast ----------------------------------------------------------

func _make_orb(color: Color) -> void:
	_orb = MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = 1.0
	sphere.height = 2.0
	_orb.mesh = sphere
	_orb_mat = StandardMaterial3D.new()
	_orb_mat.albedo_color = color
	_orb_mat.emission_enabled = true
	_orb_mat.emission = color
	_orb_mat.emission_energy_multiplier = 4.0
	_orb.material_override = _orb_mat
	_orb.scale = Vector3.ONE * 0.01
	var light := OmniLight3D.new()
	light.light_color = color
	light.light_energy = 2.0
	light.omni_range = 3.0
	_orb.add_child(light)
	DamageNumbers.world_root(player).add_child(_orb)
	_place_orb()
	var grow := _orb.create_tween()
	grow.tween_property(_orb, "scale", Vector3.ONE * ORB_SIZE, maxf(windup_time - CAST_LEAD, 0.1))


func _place_orb() -> void:
	# Between the hands, a little in front of them.
	var model: CharacterModel = player.model
	var skeleton := model.skeleton
	var left := (skeleton.global_transform * skeleton.get_bone_global_pose(skeleton.find_bone("hand_l"))).origin
	var right := (skeleton.global_transform * skeleton.get_bone_global_pose(skeleton.find_bone("hand_r"))).origin
	_orb.global_position = (left + right) * 0.5 - player.global_basis.z * 0.15


func _explode(color: Color) -> void:
	if _orb:
		_orb.queue_free()
		_orb = null
	# A grenade that goes off the moment it exists: same damage rules and blast look.
	var blast := Grenade.new()
	blast.color = color
	blast.damage = damage
	blast.blast_radius = blast_radius
	blast.fuse = 10.0
	blast.gun = player.gun
	blast.visible = false
	blast.freeze = true
	DamageNumbers.world_root(player).add_child(blast)
	blast.global_position = player.global_position + Vector3.UP * 0.2
	blast._explode()
