class_name Loadout
extends Node
# What the player has equipped. Saved to user://loadout.json so it
# survives closing the menu and restarting the game.
# For now: the flora, the weapon, the grenade, two flora ability slots, and four frame
# part slots. The grenade and abilities always come from the equipped flora.

signal changed

const SAVE_PATH := "user://loadout.json"
const GRENADE_DIR := "res://data/grenades"
const ABILITY_DIR := "res://data/abilities"
const WEAPON_DIR := "res://data/weapons"
const FRAME_DIR := "res://data/frame_parts"
const FLORA_DIR := "res://data/flora"
const TRAIT_DIR := "res://data/traits"
const TRAIT_SLOTS := 4 # how many traits you can equip at once (placeholder until decided)
const FRAME_SLOTS := ["head", "arms", "legs", "core"]
const ABILITY_SLOTS := 2

var grenades: Array[GrenadeData] = [] # every grenade option, in picker order
var grenade: GrenadeData # the equipped one
var abilities: Array[AbilityData] = [] # every ability option, in picker order
var weapons: Array[WeaponData] = [] # every weapon, in picker order
var weapon: WeaponData # the equipped one
var frame_parts: Array[FramePartData] = [] # every frame part, in picker order
var floras: Array[FloraData] = [] # every flora, in picker order
var flora: FloraData # the equipped one
var traits: Array[TraitData] = [] # every trait, in picker order
var _equipped_traits: Array[TraitData] = [] # up to TRAIT_SLOTS, all from the equipped flora
var _equipped_frame := {} # slot -> FramePartData
var _equipped_abilities: Array[AbilityData] = [null, null] # one per slot


func _ready() -> void:
	grenades.assign(_load_all(GRENADE_DIR))
	abilities.assign(_load_all(ABILITY_DIR))
	weapons.assign(_load_all(WEAPON_DIR))
	frame_parts.assign(_load_all(FRAME_DIR))
	floras.assign(_load_all(FLORA_DIR))
	traits.assign(_load_all(TRAIT_DIR))
	add_to_group("loadout")
	_load()
	if flora == null and not floras.is_empty():
		flora = floras[0]
	_fit_flora() # drop a saved grenade/ability that isn't part of this flora
	for slot in FRAME_SLOTS: # default: the first part for each slot (the Standard one)
		if not _equipped_frame.has(slot):
			var options := frame_parts_for(slot)
			if not options.is_empty():
				_equipped_frame[slot] = options[0]
	if weapon == null and not weapons.is_empty():
		weapon = weapons[0]
	_fill_flora_slots()


func grenades_for_flora() -> Array[GrenadeData]:
	var options: Array[GrenadeData] = []
	options.assign(grenades.filter(func(g: GrenadeData) -> bool: return flora == null or g.flora == flora.id))
	return options


func abilities_for_flora() -> Array[AbilityData]:
	var options: Array[AbilityData] = []
	options.assign(abilities.filter(func(a: AbilityData) -> bool: return flora == null or a.flora == flora.id))
	return options


func traits_for_flora() -> Array[TraitData]:
	var options: Array[TraitData] = []
	options.assign(traits.filter(func(t: TraitData) -> bool: return flora == null or t.flora == flora.id))
	return options


func equipped_traits() -> Array[TraitData]:
	return _equipped_traits.duplicate()


func is_trait_equipped(choice: TraitData) -> bool:
	return _equipped_traits.has(choice)


func toggle_trait(choice: TraitData) -> bool:
	# Equips it if there's a free slot (and it's this flora's), or unequips it.
	# Returns false if it couldn't be equipped because every slot is full.
	if _equipped_traits.has(choice):
		_equipped_traits.erase(choice)
	elif _equipped_traits.size() >= TRAIT_SLOTS or (flora and choice.flora != flora.id):
		return false
	else:
		_equipped_traits.append(choice)
	_save()
	changed.emit()
	return true


func trait_bonus(effect: String) -> float:
	# Sum of `amount` for equipped traits with this effect (0 if none).
	var total := 0.0
	for equipped in _equipped_traits:
		if equipped.effect == effect:
			total += equipped.amount
	return total


static func bonus(tree: SceneTree, effect: String) -> float:
	# The same, for code that doesn't hold the Loadout (fog, grenades, pools).
	var loadout := tree.get_first_node_in_group("loadout") as Loadout
	return loadout.trait_bonus(effect) if loadout else 0.0


func equip_flora(choice: FloraData) -> void:
	# Switching flora swaps in that flora's first grenade and first two abilities, and
	# clears your traits (they belong to the old flora).
	if choice == flora:
		return
	flora = choice
	grenade = null
	_equipped_abilities = [null, null]
	_equipped_traits.clear()
	_fill_flora_slots()
	_save()
	changed.emit()


func _fit_flora() -> void:
	if flora == null:
		return
	if grenade and grenade.flora != flora.id:
		grenade = null
	for slot in ABILITY_SLOTS:
		var equipped := _equipped_abilities[slot]
		if equipped and (equipped.flora != flora.id or not equipped.unlocked):
			_equipped_abilities[slot] = null
	_equipped_traits = _equipped_traits.filter(func(t: TraitData) -> bool: return t.flora == flora.id)


func _fill_flora_slots() -> void:
	var flora_grenades := grenades_for_flora()
	if grenade == null and not flora_grenades.is_empty():
		grenade = flora_grenades[0]
	# Fill any empty ability slot with the first of this flora's abilities not already equipped.
	for slot in ABILITY_SLOTS:
		if _equipped_abilities[slot] == null:
			for option in abilities_for_flora():
				if option.unlocked and not _equipped_abilities.has(option):
					_equipped_abilities[slot] = option
					break


func equip_grenade(choice: GrenadeData) -> void:
	grenade = choice
	_save()
	changed.emit()


func equip_weapon(choice: WeaponData) -> void:
	weapon = choice
	_save()
	changed.emit()


func cycle_weapon(step: int) -> void:
	var unlocked := weapons.filter(func(w: WeaponData) -> bool: return w.unlocked)
	if unlocked.size() > 1:
		equip_weapon(unlocked[posmod(unlocked.find(weapon) + step, unlocked.size())])


func frame_parts_for(slot: String) -> Array[FramePartData]:
	var options: Array[FramePartData] = []
	options.assign(frame_parts.filter(func(p: FramePartData) -> bool: return p.slot == slot))
	return options


func frame_part(slot: String) -> FramePartData:
	return _equipped_frame.get(slot)


func equip_frame_part(part: FramePartData) -> void:
	_equipped_frame[part.slot] = part
	_save()
	changed.emit()


func ability(slot: int) -> AbilityData:
	return _equipped_abilities[slot]


func ability_slot_of(choice: AbilityData) -> int:
	return _equipped_abilities.find(choice) # -1 if not equipped


func equip_ability(slot: int, choice: AbilityData) -> void:
	# If it's already in the other slot, the two swap places.
	var other := ability_slot_of(choice)
	if other != -1 and other != slot:
		_equipped_abilities[other] = _equipped_abilities[slot]
	_equipped_abilities[slot] = choice
	_save()
	changed.emit()


func _load_all(dir: String) -> Array[Resource]:
	var found: Array[Resource] = []
	for file in DirAccess.get_files_at(dir):
		file = file.trim_suffix(".remap") # exported builds rename resources
		if file.ends_with(".tres"):
			var data := load(dir.path_join(file))
			if data:
				found.append(data)
	found.sort_custom(func(a: Resource, b: Resource) -> bool: return a.order < b.order)
	return found


func _find(list: Array, id: String) -> Resource:
	for item in list:
		if item.id == id:
			return item
	return null


func _save() -> void:
	var file := FileAccess.open(SAVE_PATH, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify({
			"flora": flora.id if flora else "",
			"traits": _equipped_traits.map(func(t: TraitData) -> String: return t.id),
			"weapon": weapon.id if weapon else "",
			"grenade": grenade.id if grenade else "",
			"abilities": _equipped_abilities.map(func(a: AbilityData) -> String: return a.id if a else ""),
			"frame": _frame_ids(),
		}, "\t"))


func _frame_ids() -> Dictionary:
	var ids := {}
	for slot in _equipped_frame:
		ids[slot] = _equipped_frame[slot].id
	return ids


func _load() -> void:
	if not FileAccess.file_exists(SAVE_PATH):
		return
	var data = JSON.parse_string(FileAccess.get_file_as_string(SAVE_PATH))
	if not data is Dictionary:
		return
	if data.has("flora"):
		flora = _find(floras, str(data.flora))
	if data.has("traits") and data.traits is Array:
		for trait_id in data.traits:
			var found := _find(traits, str(trait_id)) as TraitData
			if found and not _equipped_traits.has(found) and _equipped_traits.size() < TRAIT_SLOTS:
				_equipped_traits.append(found)
	if data.has("grenade"):
		grenade = _find(grenades, str(data.grenade))
	if data.has("weapon"):
		weapon = _find(weapons, str(data.weapon))
	if data.has("frame") and data.frame is Dictionary:
		for slot in data.frame:
			var part := _find(frame_parts, str(data.frame[slot])) as FramePartData
			if part and part.slot == slot:
				_equipped_frame[slot] = part
	if data.has("abilities") and data.abilities is Array:
		for slot in mini(data.abilities.size(), ABILITY_SLOTS):
			var found := _find(abilities, str(data.abilities[slot]))
			if found and not _equipped_abilities.has(found):
				_equipped_abilities[slot] = found
