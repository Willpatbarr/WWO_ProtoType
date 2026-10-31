class_name WeaponData
extends Resource
# One weapon: its model and every stat. Each lives as a .tres file in res://data/weapons/.
# The Gun reads the equipped weapon's values live, so the settings sliders apply instantly.

@export var id := ""
@export var display_name := ""
@export var archetype := "" # e.g. "Auto rifle", shown in menus
@export_file("*.tscn") var model := "" # built by tools/build_weapon_models.gd
@export var order := 0
@export var unlocked := true
# Where the model sits relative to the camera (its origin is the sight point).
@export var hip_position := Vector3(0.15, -0.12, -0.4)
@export var ads_position := Vector3(0.0, 0.0, -0.34)

@export_group("Aim down sights")
@export var zoom := 1.5 # magnification
@export var ads_time := 0.12 # seconds from hip to fully aimed
@export var ads_sensitivity := 1.0 # look-speed multiplier while aimed (on top of zoom scaling)
@export var ads_move_speed := 0.65 # movement multiplier while aimed
@export var ads_vignette := 0.35
@export var scope_overlay := 0.0 # 1 = black scope overlay while aimed (and the gun model hides)

@export_group("Shooting")
@export var semi_auto := 0.0 # 1 = one shot per trigger pull, 0 = hold to fire
@export var fire_rate := 600.0 # rounds per minute
@export var damage := 20.0 # per bullet (per pellet for shotguns)
@export var crit_multiplier := 1.6
@export var fire_range := 120.0 # meters; shots stop here
@export var pellets := 1.0 # bullets per shot (shotguns fire many)
@export var pellet_spread := 0.0 # degrees: cone the pellets scatter in
@export var hip_spread := 0.0 # degrees of random spread from the hip (none when fully aimed)
@export var recoil_up := 0.35 # degrees the view kicks up per shot
@export var recoil_side := 0.15
@export var gun_kick := 1.0 # how hard the model jolts back per shot
@export var magazine_size := 30.0
@export var reload_time := 2.0

@export_group("Handling")
@export var viewmodel_fov := 65.0
@export var sway := 1.0
@export var gun_bob := 1.0
@export var slide_tilt := 11.0 # degrees the muzzle points up while sliding

@export_group("Aim assist")
@export var aim_assistance := 70.0
@export var magnetism_angle := 2.5
@export var friction_strength := 0.45
@export var friction_angle := 3.7
@export var adhesion := 0.0
@export var assist_range := 40.0
@export var ads_boost := 1.4
