extends CanvasLayer
# Hip-fire reticle, hit markers and ADS vignette.
# - The reticle fades out as you aim in, and turns red over a target.
# - Hit markers flash at the center: white for body hits, bigger and yellow for crits.
# - Optional aim assist debug view: magnetism cone (cyan), friction cone (yellow),
#   melee lunge angle (red), and a marker on whatever part friction is currently grabbing.
# - Ammo counter in the bottom right (red when low), with the weapon's name above it
#   and a reload progress bar under it.
# - Scoped weapons (sniper) get a black scope overlay once aimed in.
# - Health bar above whatever target is under the reticle.
# - A kill shows a bigger red hit marker.
# - Equipped grenade: a square in the bottom left. Full color when ready; gray with
#   a colored fill rising from the bottom while it recharges.
# - The two equipped flora abilities sit beside it:
#   passive = half-grayed; activated = like the grenade (plus a bright border while
#   its effect lasts); triggered = dark until set off, then lit.

@export var gun: Gun
@export var loadout: Loadout
@export var grenade_thrower: GrenadeThrower
@export var abilities: AbilityManager

const RETICLE_COLOR := Color(1, 1, 1, 0.9)
const RETICLE_ON_TARGET_COLOR := Color(1.0, 0.25, 0.2, 0.95)
const HIT_COLOR := Color(1, 1, 1)
const CRIT_COLOR := Color(1.0, 0.85, 0.2)
const HIT_MARKER_TIME := 0.18 # seconds a hit marker stays up
const KILL_MARKER_TIME := 0.4
const KILL_COLOR := Color(1.0, 0.25, 0.2)
const HEALTH_BAR_SIZE := Vector2(110, 10)
const HEALTH_BAR_HEIGHT := 2.25 # meters above a target's feet
const LOW_AMMO := 0.25 # counter turns red at or below this fraction of the magazine

var _reticle: Control
var _hit_marker: Control
var _assist_debug: Control
var _vignette: ColorRect
var _ammo_label: Label
var _weapon_label: Label
var _scope: ColorRect
var _reload_bar: ProgressBar
var _hit_time := 0.0
var _hit_critical := false
var _hit_killed := false
var _health_bar: Control
var _grenade_box: Control
var _grenade_label: Label
var _ability_boxes: Array[Control] = []
var _ability_labels: Array[Label] = []


func _ready() -> void:
	_vignette = ColorRect.new()
	_vignette.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_vignette.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_vignette.material = ShaderMaterial.new()
	_vignette.material.shader = preload("res://vignette.gdshader")
	add_child(_vignette)

	_scope = ColorRect.new()
	_scope.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_scope.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_scope.material = ShaderMaterial.new()
	_scope.material.shader = preload("res://scope.gdshader")
	add_child(_scope)

	_reticle = _center_control(_draw_reticle)
	_hit_marker = _center_control(_draw_hit_marker)
	_assist_debug = _center_control(_draw_assist_debug)
	_health_bar = _center_control(_draw_health_bar)
	gun.hit_confirmed.connect(_on_hit)
	_build_ammo_counter()
	_build_grenade_indicator()
	loadout.changed.connect(_update_grenade_indicator)
	_update_grenade_indicator()


func _build_grenade_indicator() -> void:
	# Bottom-left row: grenade, then ability 1 and ability 2.
	var row := HBoxContainer.new()
	row.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	row.grow_vertical = Control.GROW_DIRECTION_BEGIN
	row.position += Vector2(40, -40) # margin from the corner
	row.add_theme_constant_override("separation", 14)
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(row)
	var grenade_slot := _hud_slot(row, _draw_grenade_box)
	_grenade_box = grenade_slot[0]
	_grenade_label = grenade_slot[1]
	for slot in 2:
		var parts := _hud_slot(row, _draw_ability_box.bind(slot))
		_ability_boxes.append(parts[0])
		_ability_labels.append(parts[1])


func _hud_slot(row: HBoxContainer, draw_fn: Callable) -> Array:
	# A 64x64 square with a small name under it.
	var column := VBoxContainer.new()
	column.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(column)
	var box := Control.new()
	box.custom_minimum_size = Vector2(64, 64)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN # stay square, don't stretch to the label
	box.draw.connect(draw_fn)
	column.add_child(box)
	var label := Label.new()
	label.add_theme_font_size_override("font_size", 16)
	label.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.7))
	label.add_theme_constant_override("outline_size", 6)
	column.add_child(label)
	return [box, label]


func _update_grenade_indicator() -> void:
	if loadout.grenade:
		_grenade_label.text = loadout.grenade.display_name
	for slot in 2:
		var ability := loadout.ability(slot)
		_ability_labels[slot].text = ability.display_name if ability else ""
	_grenade_box.queue_redraw()


func _draw_charge_box(box: Control, color: Color, charge: float) -> void:
	# Full color when charged; gray with the color filling up from the bottom otherwise.
	var rect := Rect2(Vector2.ZERO, box.size)
	if charge >= 1.0:
		box.draw_rect(rect, color)
		box.draw_rect(rect, Color(1, 1, 1, 0.85), false, 3.0)
		return
	box.draw_rect(rect, Color(0.25, 0.25, 0.27, 0.9))
	var fill_height := rect.size.y * charge
	box.draw_rect(Rect2(rect.position.x, rect.end.y - fill_height, rect.size.x, fill_height), color.darkened(0.35))
	box.draw_rect(rect, Color(0.6, 0.6, 0.6, 0.7), false, 2.0)


func _draw_grenade_box() -> void:
	if loadout.grenade:
		_draw_charge_box(_grenade_box, loadout.grenade.color, grenade_thrower.charge)


func _draw_ability_box(slot: int) -> void:
	var ability := loadout.ability(slot)
	if ability == null:
		return
	var box := _ability_boxes[slot]
	var rect := Rect2(Vector2.ZERO, box.size)
	match ability.kind:
		"passive":
			# Always on: halfway between a ready and an empty ability.
			box.draw_rect(rect, ability.color.lerp(Color(0.25, 0.25, 0.27), 0.5))
			box.draw_rect(rect, Color(0.75, 0.75, 0.75, 0.75), false, 2.0)
		"active":
			_draw_charge_box(box, ability.color, abilities.charge(ability))
			if abilities.is_effect_active(ability):
				# The boost is running: a bright pulsing border.
				var pulse := 0.6 + 0.4 * sin(Time.get_ticks_msec() * 0.012)
				box.draw_rect(rect.grow(3.0), Color(ability.color.lightened(0.5), pulse), false, 4.0)
		"triggered":
			if abilities.is_effect_active(ability):
				box.draw_rect(rect, ability.color)
				box.draw_rect(rect, Color(1, 1, 1, 0.85), false, 3.0)
			else:
				box.draw_rect(rect, Color(0.17, 0.17, 0.19, 0.9))
				box.draw_rect(rect, Color(ability.color, 0.45), false, 2.0)


func _build_ammo_counter() -> void:
	var box := VBoxContainer.new()
	box.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
	box.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	box.grow_vertical = Control.GROW_DIRECTION_BEGIN
	box.position -= Vector2(40, 40) # margin from the corner
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(box)

	_weapon_label = Label.new()
	_weapon_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_weapon_label.add_theme_font_size_override("font_size", 20)
	_weapon_label.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.7))
	_weapon_label.add_theme_constant_override("outline_size", 6)
	box.add_child(_weapon_label)

	_ammo_label = Label.new()
	_ammo_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_ammo_label.add_theme_font_size_override("font_size", 44)
	_ammo_label.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.7))
	_ammo_label.add_theme_constant_override("outline_size", 8)
	box.add_child(_ammo_label)

	_reload_bar = ProgressBar.new()
	_reload_bar.custom_minimum_size = Vector2(160, 8)
	_reload_bar.show_percentage = false
	_reload_bar.max_value = 1.0
	box.add_child(_reload_bar)


func _center_control(draw_fn: Callable) -> Control:
	var c := Control.new() # zero-size control sitting at screen center
	c.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	c.mouse_filter = Control.MOUSE_FILTER_IGNORE
	c.draw.connect(draw_fn)
	add_child(c)
	return c


func _draw_reticle() -> void:
	# Drawn in white; modulate tints it (white or red) and fades it.
	for dir in [Vector2.UP, Vector2.DOWN, Vector2.LEFT, Vector2.RIGHT]:
		_reticle.draw_line(dir * 7, dir * 16, Color.WHITE, 2.0, true)
	_reticle.draw_circle(Vector2.ZERO, 1.5, Color.WHITE)


func _draw_hit_marker() -> void:
	# Four diagonal ticks around the center, like an X with a gap.
	var size := 1.8 if _hit_killed else (1.4 if _hit_critical else 1.0)
	var width := 3.5 if _hit_killed else (3.0 if _hit_critical else 2.0)
	for dir in [Vector2(1, 1), Vector2(1, -1), Vector2(-1, 1), Vector2(-1, -1)]:
		var d: Vector2 = dir.normalized()
		_hit_marker.draw_line(d * 9 * size, d * 19 * size, Color.WHITE, width, true)


func _cone_radius_px(angle_deg: float, scaled := true) -> float:
	# How many pixels from center a cone of this angle reaches on screen.
	var half_width := get_viewport().get_visible_rect().size.x * 0.5
	var angle := deg_to_rad(angle_deg) * (gun.aim_assist.cone_scale() if scaled else 1.0)
	return half_width * tan(angle) / tan(deg_to_rad(gun.camera.fov) * 0.5)


func _draw_assist_debug() -> void:
	var assist := gun.aim_assist
	if assist.show_debug < 0.5:
		return
	# Cones as drawn here are at full strength (inside assist range).
	_assist_debug.draw_arc(Vector2.ZERO, _cone_radius_px(assist.magnetism_angle), 0, TAU, 48, Color(0.3, 0.9, 1.0, 0.8), 1.5, true)
	_assist_debug.draw_arc(Vector2.ZERO, _cone_radius_px(assist.friction_angle), 0, TAU, 48, Color(1.0, 0.85, 0.2, 0.8), 1.5, true)
	# Melee lunge angle (red). Not scaled by aim assist.
	_assist_debug.draw_arc(Vector2.ZERO, _cone_radius_px(gun.lunge_angle, false), 0, TAU, 64, Color(1.0, 0.3, 0.3, 0.7), 1.5, true)
	if not assist.friction_part.is_empty():
		var screen := gun.camera.unproject_position(assist.friction_part.position)
		var center := get_viewport().get_visible_rect().size * 0.5
		_assist_debug.draw_circle(screen - center, 6.0, Color(1.0, 0.85, 0.2, 0.9), false, 2.0)
	var device := "controller" if assist.using_controller else "mouse"
	var text := "Aim assist: %s (strength %d%%)  range %.0f m" % [device, roundi(assist.cone_scale() * 100.0), assist.effective_range()]
	_assist_debug.draw_string(ThemeDB.fallback_font, Vector2(-220, 120), text, HORIZONTAL_ALIGNMENT_CENTER, 440, 18)


func _on_hit(critical: bool, killed: bool) -> void:
	_hit_time = KILL_MARKER_TIME if killed else HIT_MARKER_TIME
	_hit_critical = critical
	_hit_killed = killed
	_hit_marker.queue_redraw()


func _draw_health_bar() -> void:
	var target := gun.reticle_target
	if target == null:
		return
	var top := target.global_position + Vector3.UP * HEALTH_BAR_HEIGHT
	if gun.camera.is_position_behind(top):
		return
	var center := get_viewport().get_visible_rect().size * 0.5
	var at := gun.camera.unproject_position(top) - center # this control sits at screen center
	var rect := Rect2(at - Vector2(HEALTH_BAR_SIZE.x * 0.5, HEALTH_BAR_SIZE.y), HEALTH_BAR_SIZE)
	var fraction := clampf(target.health / target.max_health, 0.0, 1.0)
	_health_bar.draw_rect(rect.grow(2.0), Color(0, 0, 0, 0.6))
	_health_bar.draw_rect(Rect2(rect.position, Vector2(rect.size.x * fraction, rect.size.y)), Color(0.9, 0.2, 0.15))
	var text := "%d" % ceili(target.health)
	_health_bar.draw_string(ThemeDB.fallback_font, rect.position + Vector2(0, -6), text, HORIZONTAL_ALIGNMENT_CENTER, rect.size.x, 16)


func _process(delta: float) -> void:
	var aimed: float = gun.ads_amount
	var color := RETICLE_ON_TARGET_COLOR if gun.target_under_reticle else RETICLE_COLOR
	color.a *= clampf(1.0 - aimed * 2.5, 0.0, 1.0) # gone by 40% aimed, like Destiny
	_reticle.modulate = color

	_hit_time -= delta
	var marker_color := KILL_COLOR if _hit_killed else (CRIT_COLOR if _hit_critical else HIT_COLOR)
	marker_color.a = clampf(_hit_time / (KILL_MARKER_TIME if _hit_killed else HIT_MARKER_TIME), 0.0, 1.0)
	_hit_marker.modulate = marker_color

	_vignette.material.set_shader_parameter("strength", gun.ads_vignette * aimed)
	_assist_debug.queue_redraw()
	_health_bar.queue_redraw()
	_grenade_box.queue_redraw()
	for box in _ability_boxes:
		box.queue_redraw()

	_weapon_label.text = gun.weapon.display_name
	var scoped := gun.weapon.scope_overlay >= 0.5
	_scope.material.set_shader_parameter("strength", smoothstep(0.8, 1.0, aimed) if scoped else 0.0)
	var mag := int(gun.magazine_size)
	_ammo_label.text = "RELOADING" if gun.reloading and gun.ammo < mag else "%d / %d" % [gun.ammo, mag]
	var low := gun.ammo <= int(mag * LOW_AMMO)
	_ammo_label.modulate = Color(1.0, 0.3, 0.25) if low and not gun.reloading else Color.WHITE
	_reload_bar.visible = gun.reloading
	_reload_bar.value = gun.reload_progress
