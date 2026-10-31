class_name AbilityManager
extends Node
# Runs the two equipped flora abilities.
# - Overgrowth (passive): speed multiplier + extra jumps, read by the player.
# - Thornfist (active): Ability 1 / Ability 2 button activates it; melee damage is
#   multiplied while it lasts, then it recharges.
# - Seedburst (triggered): grenades burst into seeds (see Grenade). It counts as lit
#   while any Seedburst grenade or seed is still in the world.

const SEED_GROUP := "seedburst_grenades"

@export var loadout: Loadout

var _charge := {} # ability id -> 0..1 recharge progress (activated abilities)
var _active_left := {} # ability id -> seconds of effect left


func _process(delta: float) -> void:
	for slot in 2:
		var ability := loadout.ability(slot)
		if ability == null or ability.kind != "active":
			continue
		_charge[ability.id] = minf(charge(ability) + delta / maxf(ability.cooldown, 0.1), 1.0)
		_active_left[ability.id] = maxf(_active_left.get(ability.id, 0.0) - delta, 0.0)
		if Input.is_action_just_pressed("ability_%d" % (slot + 1)):
			activate(ability)


func activate(ability: AbilityData) -> bool:
	if ability.kind != "active" or charge(ability) < 1.0:
		return false
	_charge[ability.id] = 0.0
	_active_left[ability.id] = ability.duration
	return true


func charge(ability: AbilityData) -> float:
	return _charge.get(ability.id, 1.0)


func is_effect_active(ability: AbilityData) -> bool:
	match ability.kind:
		"active":
			return _active_left.get(ability.id, 0.0) > 0.0
		"triggered":
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


func melee_multiplier() -> float:
	var thornfist := equipped("thornfist")
	return thornfist.melee_damage_mult if thornfist and is_effect_active(thornfist) else 1.0


func seedburst() -> AbilityData:
	return equipped("seedburst")
