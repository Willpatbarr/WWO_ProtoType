class_name TraitData
extends Resource
# One flora trait. Each lives as a .tres in res://data/traits/ and belongs to a flora.
# Equip up to Loadout.TRAIT_SLOTS of your flora's traits in the character menu.
# A trait works one of two ways (or both):
# - `mods`: stat changes, added to Modifiers as source "trait:<id>" (like frame parts).
# - `effect` + `amount`: a named effect other code asks about through
#   Loadout.trait_bonus(effect), e.g. "fog_longer" 0.5 = spore fog lasts 50% longer.

@export var id := ""
@export var display_name := ""
@export_multiline var description := ""
@export var flora := "voltvine"
@export var order := 0
@export var unlocked := true
@export var mods: Array[StatModifier] = []
@export var effect := "" # fog_longer, no_radar, more_bounce, poison_damage, slow_longer
@export var amount := 0.0
