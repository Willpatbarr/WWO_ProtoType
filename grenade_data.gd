class_name GrenadeData
extends Resource
# One grenade option. Each lives as a .tres file in res://data/grenades/.
# Only the menu/HUD data for now; throwing comes later.

@export var id := ""
@export var display_name := ""
@export var color := Color.WHITE
@export var order := 0 # position in the picker
@export var damage := 120.0 # at the center of the blast (half at the edge)
@export var blast_radius := 4.0 # meters; bigger blasts hit softer
# Adjustments on top of the global grenade settings (1.0 = same as global).
@export var throw_distance_mult := 1.0
@export var recharge_mult := 1.0
@export var fuse_mult := 1.0
@export var unlocked := true # unlock logic slots in later
