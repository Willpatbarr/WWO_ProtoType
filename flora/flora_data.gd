class_name FloraData
extends Resource
# One flora (a Destiny-style "subclass"). Each lives as a .tres in res://data/flora/.
# Grenades and abilities belong to a flora through their own `flora` id; picking a flora
# in the character menu switches you to that flora's grenades, abilities and ult.

@export var id := ""
@export var display_name := ""
@export_multiline var description := ""
@export var color := Color.WHITE
@export var order := 0
@export var unlocked := true
@export_enum("blast", "veil", "entrench") var ult := "blast" # which ult UltManager plays
@export var ult_name := ""
@export var graft_color := Color(0, 0, 0, 0) # if set, every body graft (grenade ball, ability markers) is this color
