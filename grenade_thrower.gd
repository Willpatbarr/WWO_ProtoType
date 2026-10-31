class_name GrenadeThrower
extends Node
# Throws the equipped grenade (L1 / G) and handles its recharge.
# The HUD reads `charge` (0..1) to show recharge progress.
# Throw distance, recharge and fuse are global base values; each grenade can
# scale them with its own multipliers (see GrenadeData).

# --- Global base values (the settings menu edits these live) ---
@export var throw_distance := 18.0 # meters it travels on flat ground when you look level
@export var recharge_time := 8.0 # seconds from thrown to ready again
@export var detonation_time := 1.5 # seconds of fuse after the throw
# Damage and blast radius come from the equipped grenade's data file.

const LAUNCH_ANGLE := 20.0 # degrees above where you're looking
const HAND_OFFSET := Vector3(0.2, -0.25, -0.5) # where it leaves your hand, relative to the camera

@export var player: CharacterBody3D
@export var loadout: Loadout
@export var abilities: AbilityManager
@export var mods: Modifiers

var charge := 1.0 # 1 = ready


func _process(delta: float) -> void:
	if charge < 1.0:
		charge = minf(charge + delta / maxf(effective_recharge_time(), 0.1), 1.0)
	if Input.is_action_just_pressed("grenade"):
		throw()


func effective_throw_distance(grenade: GrenadeData = null) -> float:
	grenade = grenade if grenade else loadout.grenade
	return mods.apply("throw_distance", throw_distance * (grenade.throw_distance_mult if grenade else 1.0))


func effective_recharge_time(grenade: GrenadeData = null) -> float:
	grenade = grenade if grenade else loadout.grenade
	return recharge_time * (grenade.recharge_mult if grenade else 1.0)


func effective_fuse(grenade: GrenadeData = null) -> float:
	grenade = grenade if grenade else loadout.grenade
	return detonation_time * (grenade.fuse_mult if grenade else 1.0)


func is_ready() -> bool:
	return charge >= 1.0


func throw() -> bool:
	var gun: Gun = player.gun
	if not is_ready() or gun.meleeing or player.mantling or loadout.grenade == null:
		return false
	charge = 0.0
	gun.cancel_reload() # throwing cancels a reload, like Destiny
	gun.start_throw_pose()

	var camera: Camera3D = player.camera
	# Aim a bit above where you're looking, fast enough to land throw_distance
	# away on flat ground when you look level.
	var direction := (-camera.global_basis.z).rotated(camera.global_basis.x, deg_to_rad(LAUNCH_ANGLE)).normalized()
	var gravity: float = ProjectSettings.get_setting("physics/3d/default_gravity")
	var speed := sqrt(effective_throw_distance() * gravity / sin(deg_to_rad(2.0 * LAUNCH_ANGLE)))

	var grenade := Grenade.new()
	grenade.color = loadout.grenade.color
	grenade.damage = loadout.grenade.damage
	grenade.blast_radius = loadout.grenade.blast_radius
	grenade.fuse = effective_fuse()
	grenade.gun = gun
	grenade.seedburst = abilities.seedburst() # null unless Seedburst is equipped
	var root := get_tree().current_scene if get_tree().current_scene else get_tree().root
	root.add_child(grenade)
	grenade.global_position = camera.global_transform * HAND_OFFSET
	grenade.add_collision_exception_with(player) # don't bounce off yourself on the way out
	grenade.linear_velocity = direction * speed
	return true
