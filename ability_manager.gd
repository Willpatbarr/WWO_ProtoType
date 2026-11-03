class_name AbilityManager
extends Node
# Runs the two equipped flora abilities.
# - Overgrowth (passive): speed multiplier + extra jumps, read by the player.
# - Thornfist (active): Ability 1 / Ability 2 button activates it; melee damage is
#   multiplied while it lasts, then it recharges.
# - Seedburst (triggered): grenades burst into seeds (see Grenade). It counts as lit
#   while any Seedburst grenade or seed is still in the world.
# - Melee Guard (triggered, VoltVine): landing a melee hit cuts the melee damage you
#   take for a few seconds. Lit while it's running.
# VeilSpore:
# - Spore Fog Damage (passive): spore fogs hurt Swarm and Soldiers (see SporeFog).
# - Spore Dash (active): launches you backwards and leaves a thick spore fog.
# - Spore Patches (triggered): grenades sprout fog-emitting patches where they touch
#   down (see Grenade). Lit while any patch is alive.

const SEED_GROUP := "seedburst_grenades"

@export var loadout: Loadout

var _charge := {} # ability id -> 0..1 recharge progress (activated abilities)
var _active_left := {} # ability id -> seconds of effect left
var _guard_left := 0.0 # Melee Guard time left


func _process(delta: float) -> void:
	_guard_left = maxf(_guard_left - delta, 0.0)
	for slot in 2:
		var ability := loadout.ability(slot)
		if ability == null or ability.kind != "active":
			continue
		_charge[ability.id] = minf(charge(ability) + delta / maxf(ability.cooldown, 0.1), 1.0)
		_active_left[ability.id] = maxf(_active_left.get(ability.id, 0.0) - delta, 0.0)
		if Input.is_action_just_pressed("ability_%d" % (slot + 1)) and not _player_busy():
			activate(ability)


func _player_busy() -> bool:
	var player := get_tree().get_first_node_in_group("player")
	return player != null and player.ulting


func _ready() -> void:
	add_to_group("ability_manager")


func activate(ability: AbilityData) -> bool:
	if ability.kind != "active" or charge(ability) < 1.0:
		return false
	_charge[ability.id] = 0.0
	_active_left[ability.id] = ability.duration
	if ability.id == "spore_dash":
		_spore_dash(ability)
	if ability.id == "root_bombs":
		_root_bombs(ability)
	return true


func charge(ability: AbilityData) -> float:
	return _charge.get(ability.id, 1.0)


func is_effect_active(ability: AbilityData) -> bool:
	match ability.kind:
		"active":
			return _active_left.get(ability.id, 0.0) > 0.0
		"triggered":
			if ability.id == "melee_guard":
				return _guard_left > 0.0
			if ability.id == "spore_patches":
				return not get_tree().get_nodes_in_group(SporePatch.GROUP).is_empty()
			return not get_tree().get_nodes_in_group(SEED_GROUP).is_empty()
	return true # passives are always on


func equipped(id: String) -> AbilityData:
	for slot in 2:
		var ability := loadout.ability(slot)
		if ability and ability.id == id:
			return ability
	return null


# --- Effects, read by the rest of the game ---

func speed_multiplier() -> float:
	var overgrowth := equipped("overgrowth")
	return overgrowth.speed_mult if overgrowth else 1.0


func extra_jumps() -> int:
	var overgrowth := equipped("overgrowth")
	return roundi(overgrowth.extra_jumps) if overgrowth else 0


func on_melee_landed() -> void:
	var guard := equipped("melee_guard")
	if guard:
		_guard_left = guard.duration


func incoming_melee_multiplier() -> float:
	var guard := equipped("melee_guard")
	return 1.0 - guard.guard_reduction if guard and _guard_left > 0.0 else 1.0


func extra_charges() -> float:
	var extra := equipped("extra_grenade")
	return extra.extra_charges if extra else 0.0


func _root_bombs(ability: AbilityData) -> void:
	# Two big bombs, thrown a little left and right of where you're looking, that blow up
	# the moment they hit something.
	var player := get_tree().get_first_node_in_group("player") as CharacterBody3D
	if player == null:
		return
	var camera: Camera3D = player.camera
	var bigger := 1.0 + loadout.trait_bonus("blast_bigger") # Bigger Explosions trait
	var thrown: Array[Grenade] = []
	for side in [-1.0, 1.0]:
		var bomb := Grenade.new()
		bomb.color = ability.color
		bomb.damage = ability.bomb_damage
		bomb.blast_radius = ability.bomb_radius * bigger
		bomb.fuse = 6.0 # backup, in case it never hits anything
		bomb.size_scale = 2.0
		bomb.explode_on_impact = true
		bomb.gun = player.gun
		bomb.thrown_from = player.global_position
		DamageNumbers.world_root(player).add_child(bomb)
		bomb.global_position = camera.global_transform * Vector3(0.35 * side, -0.2, -0.6)
		bomb.add_collision_exception_with(player)
		for other in thrown:
			bomb.add_collision_exception_with(other) # the pair never hit each other
		thrown.append(bomb)
		# Fan out slightly: the left bomb a little left, the right one a little right.
		var direction := (-camera.global_basis.z).rotated(Vector3.UP, deg_to_rad(-1.5 * side)).rotated(camera.global_basis.x, deg_to_rad(2.0))
		bomb.linear_velocity = direction.normalized() * ability.bomb_speed


func spore_patches() -> AbilityData:
	return equipped("spore_patches")


func _spore_dash(ability: AbilityData) -> void:
	# Launch backwards (and a little up, so you keep the speed through the air) and leave
	# a thick fog where you were standing.
	var player := get_tree().get_first_node_in_group("player") as CharacterBody3D
	if player == null:
		return
	SporeFog.spawn(player, player.global_position, ability.dash_fog_radius, ability.dash_fog_time, true)
	var back := player.global_basis.z
	back.y = 0.0
	back = back.normalized()
	player.launch(back * ability.dash_speed + Vector3.UP * ability.dash_up)


func melee_multiplier() -> float:
	var thornfist := equipped("thornfist")
	return thornfist.melee_damage_mult if thornfist and is_effect_active(thornfist) else 1.0


func seedburst() -> AbilityData:
	return equipped("seedburst")
