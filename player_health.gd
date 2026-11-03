class_name PlayerHealth
extends Node
# The player's health and shields, Destiny-style:
# - Damage hits the shield first, then health.
# - After `recharge_delay` seconds without damage, health refills, then the shield.
# - At 0 health you die, and respawn at the spawn point after `respawn_time`.
# - Falling below KILL_Y counts as a death too.
# Durability frame points raise max health and shields and shorten the recharge delay.

signal damaged(amount: float)
signal died
signal respawned

@export var player: CharacterBody3D
@export var mods: Modifiers
@export var spawn_point: Node3D # where you respawn; the player's start position if unset

@export var base_health := 100.0
@export var base_shield := 100.0
@export var recharge_delay := 4.0 # seconds without damage before regen starts
@export var health_regen := 40.0 # health per second
@export var shield_regen := 60.0 # shield per second (after health is full)
@export var respawn_time := 2.0 # seconds dead before respawning
@export var god_mode := 0.0 # 1 = take no damage (for testing)

const KILL_Y := -30.0

var health := 100.0
var shield := 100.0
var dead := false
var since_damage := 99.0 # seconds since the last hit
var _respawn_left := 0.0
var _spawn := Transform3D.IDENTITY


func _ready() -> void:
	_spawn = spawn_point.global_transform if spawn_point else player.global_transform
	health = max_health()
	shield = max_shield()


func max_health() -> float:
	return maxf(mods.apply("max_health", base_health), 1.0)


func max_shield() -> float:
	return maxf(mods.apply("max_shield", base_shield), 0.0)


func take_damage(amount: float, melee := false) -> void:
	# `melee`: a bite, punch or slam (Melee Guard cuts these after you land a melee hit).
	if dead or amount <= 0.0 or god_mode >= 0.5 or player.ulting or player.entrench_left > 0.0: # no damage during the ult or while entrenched
		return
	if melee:
		amount *= player.abilities.incoming_melee_multiplier()
	since_damage = 0.0
	var to_shield := minf(amount, shield)
	shield -= to_shield
	health -= amount - to_shield
	damaged.emit(amount)
	if health <= 0.0:
		_die()


func kill() -> void:
	if not dead:
		shield = 0.0
		health = 0.0
		_die()


func _die() -> void:
	health = 0.0
	dead = true
	_respawn_left = respawn_time
	player.process_mode = Node.PROCESS_MODE_DISABLED # no moving, shooting or throwing
	died.emit()


func respawn() -> void:
	dead = false
	player.global_transform = _spawn
	player.velocity = Vector3.ZERO
	player.slow_left = 0.0
	player.veil_left = 0.0
	player.entrench_left = 0.0
	player.get_node("Camera3D").rotation.x = 0.0
	health = max_health()
	shield = max_shield()
	since_damage = 99.0
	player.process_mode = Node.PROCESS_MODE_INHERIT
	respawned.emit()


func _physics_process(delta: float) -> void:
	if dead:
		_respawn_left -= delta
		if _respawn_left <= 0.0:
			respawn()
		return
	if player.global_position.y < KILL_Y:
		kill()
		return
	# Frame part changes can lower the maximums.
	health = minf(health, max_health())
	shield = minf(shield, max_shield())
	since_damage += delta
	if since_damage >= mods.apply("recharge_delay", recharge_delay):
		if health < max_health():
			health = minf(health + health_regen * delta, max_health())
		else:
			shield = minf(shield + mods.apply("shield_regen", shield_regen) * delta, max_shield())
