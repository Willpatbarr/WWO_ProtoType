extends CanvasLayer
# Character screen. DualSense touchpad (or I) opens and closes it.
# Left: three loadout tiles (Flora, Weapons, Frame Parts). Picking one expands it to
# fill the tile area; its Collapse button (or circle / Esc) goes back to all three.
# Right: a preview of the character (the mannequin body).
# Flora's expanded view: labeled rows (Flora / Abilities / Traits) in a scroll area.
# Traits wrap onto new lines, so the slot count can grow freely.
# The Grenade and Ability slots show what's equipped; clicking one opens a picker row
# under the Abilities row. Picking an ability that's in the other slot swaps them.
# Controller: the shared MenuCursor (left stick moves, cross clicks, right stick scrolls).
# Pauses the game while open.

@export var settings_menu: CanvasLayer
@export var player: CharacterBody3D
@export var loadout: Loadout
@export var mods: Modifiers

const TILES := ["Flora", "Weapons", "Frame Parts"]

var trait_slot_count := 4 # placeholder until the trait count is decided

var _tiles_box: VBoxContainer
var _expanded: PanelContainer
var _expanded_title: Label
var _expanded_scroll: ScrollContainer
var _expanded_content: VBoxContainer
var cursor: MenuCursor
var _preview_model: Node3D
var _picker := "" # which picker is open: "", "flora", "grenade", "ability0", "ability1" or "traits"


func _ready() -> void:
	layer = 11 # above the HUD and the settings menu
	process_mode = Node.PROCESS_MODE_ALWAYS # keep working while the game is paused
	_build()
	visible = false


func _style(color: Color, radius := 8, pad := 0) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = color
	box.set_corner_radius_all(radius)
	box.content_margin_left = pad
	box.content_margin_right = pad
	return box


func _build() -> void:
	var backdrop := ColorRect.new() # dims the game behind the menu
	backdrop.color = Color(0, 0, 0, 0.7)
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(backdrop)

	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(center)

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(1500, 820)
	panel.theme = Theme.new()
	panel.theme.default_font_size = 28
	panel.add_theme_stylebox_override("panel", _style(Color(0.1, 0.11, 0.13), 12))
	center.add_child(panel)

	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 28)
	panel.add_child(margin)

	var page := VBoxContainer.new()
	page.add_theme_constant_override("separation", 16)
	margin.add_child(page)

	var header := HBoxContainer.new()
	var title := Label.new()
	title.text = "Character"
	title.add_theme_font_size_override("font_size", 40)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var hint := Label.new()
	hint.text = "Touchpad / I to close  ·  ◯ / Esc to go back"
	hint.add_theme_font_size_override("font_size", 20)
	hint.modulate = Color(1, 1, 1, 0.6)
	header.add_child(hint)
	page.add_child(header)

	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 24)
	page.add_child(body)

	# Left: the tile area. Holds either the three tiles or one expanded tile.
	var tile_area := Control.new()
	tile_area.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	tile_area.size_flags_stretch_ratio = 1.4
	body.add_child(tile_area)

	_tiles_box = VBoxContainer.new()
	_tiles_box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_tiles_box.add_theme_constant_override("separation", 16)
	tile_area.add_child(_tiles_box)
	for tile_name in TILES:
		var tile := Button.new()
		tile.text = tile_name
		tile.alignment = HORIZONTAL_ALIGNMENT_LEFT
		tile.add_theme_font_size_override("font_size", 40)
		tile.size_flags_vertical = Control.SIZE_EXPAND_FILL
		tile.focus_mode = Control.FOCUS_NONE # the cursor drives everything, not focus
		tile.add_theme_stylebox_override("normal", _style(Color(0.18, 0.19, 0.23), 8, 36))
		tile.add_theme_stylebox_override("hover", _style(Color(0.26, 0.28, 0.33), 8, 36))
		tile.add_theme_stylebox_override("pressed", _style(Color(0.32, 0.34, 0.4), 8, 36))
		tile.pressed.connect(_expand.bind(tile_name))
		_tiles_box.add_child(tile)

	_expanded = PanelContainer.new()
	_expanded.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_expanded.visible = false
	_expanded.add_theme_stylebox_override("panel", _style(Color(0.18, 0.19, 0.23), 8))
	tile_area.add_child(_expanded)
	var expanded_margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		expanded_margin.add_theme_constant_override("margin_" + side, 28)
	_expanded.add_child(expanded_margin)
	var expanded_page := VBoxContainer.new()
	expanded_page.add_theme_constant_override("separation", 20)
	expanded_margin.add_child(expanded_page)
	var expanded_bar := HBoxContainer.new()
	expanded_page.add_child(expanded_bar)
	_expanded_title = Label.new()
	_expanded_title.add_theme_font_size_override("font_size", 40)
	_expanded_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	expanded_bar.add_child(_expanded_title)
	var collapse := Button.new()
	collapse.text = "Collapse ✕"
	collapse.focus_mode = Control.FOCUS_NONE
	collapse.pressed.connect(_collapse)
	expanded_bar.add_child(collapse)
	_expanded_scroll = ScrollContainer.new()
	_expanded_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_expanded_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	expanded_page.add_child(_expanded_scroll)
	_expanded_content = VBoxContainer.new()
	_expanded_content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_expanded_content.add_theme_constant_override("separation", 12)
	_expanded_scroll.add_child(_expanded_content)

	# Right: the character preview, rendered in its own little 3D world.
	body.add_child(_build_preview())

	cursor = MenuCursor.new() # last child, so it draws on top
	add_child(cursor)


func _build_preview() -> Control:
	var container := SubViewportContainer.new()
	container.stretch = true
	container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	container.mouse_filter = Control.MOUSE_FILTER_IGNORE

	var viewport := SubViewport.new()
	viewport.own_world_3d = true # don't render the level in here
	viewport.transparent_bg = true
	container.add_child(viewport)

	var camera := Camera3D.new()
	camera.position = Vector3(0, 0.1, 4.4)
	camera.fov = 45
	viewport.add_child(camera)

	var light := DirectionalLight3D.new()
	light.rotation = Vector3(deg_to_rad(-35), deg_to_rad(30), 0)
	viewport.add_child(light)

	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color = Color(0.5, 0.5, 0.55)
	viewport.add_child(environment)

	# The character: the same mannequin as the player's body (and the Soldier enemies).
	# Its feet sit where the old capsule's bottom was, so the framing stays the same.
	_preview_model = Node3D.new()
	viewport.add_child(_preview_model)
	var body := CharacterModel.new()
	body.position.y = -1.0
	_preview_model.add_child(body)
	body.follow_loadout(loadout) # each region shows its equipped frame part's color
	return container


func _expand(tile_name: String) -> void:
	_expanded_title.text = tile_name
	_picker = ""
	_rebuild_content()
	_expanded_scroll.scroll_vertical = 0
	_tiles_box.visible = false
	_expanded.visible = true


func _rebuild_content() -> void:
	for child in _expanded_content.get_children():
		_expanded_content.remove_child(child)
		child.queue_free()
	if _expanded_title.text == "Flora":
		_build_flora_rows()
	elif _expanded_title.text == "Weapons":
		_build_weapon_rows()
	elif _expanded_title.text == "Frame Parts":
		_build_frame_rows()


func _build_flora_rows() -> void:
	# Layout B: one labeled row per slot type. Slots are empty placeholders for now.
	var green := Color(0.16, 0.3, 0.2)
	var neutral := Color(0.24, 0.25, 0.3)
	var blue := Color(0.16, 0.22, 0.34)
	_add_row_label("Flora")
	_expanded_content.add_child(_make_flora_slot())
	if _picker == "flora":
		_add_flora_picker()
	_add_row_label("Abilities")
	var abilities := HBoxContainer.new()
	abilities.add_theme_constant_override("separation", 12)
	abilities.add_child(_make_grenade_slot())
	abilities.add_child(_make_ability_slot(0))
	abilities.add_child(_make_ability_slot(1))
	_expanded_content.add_child(abilities)
	if _picker == "grenade":
		_add_grenade_picker()
	elif _picker.begins_with("ability"):
		_add_ability_picker(int(_picker.trim_prefix("ability")))
	_add_row_label("Traits (%d of %d)" % [loadout.equipped_traits().size(), Loadout.TRAIT_SLOTS])
	var slots := HFlowContainer.new()
	slots.add_theme_constant_override("h_separation", 10)
	slots.add_theme_constant_override("v_separation", 10)
	var equipped := loadout.equipped_traits()
	for i in Loadout.TRAIT_SLOTS:
		var text := equipped[i].display_name if i < equipped.size() else "Empty"
		var slot := _make_slot(text, Vector2(170, 90), blue if i < equipped.size() else Color(0.2, 0.21, 0.25))
		slot.add_theme_font_size_override("font_size", 16)
		if i < equipped.size():
			_set_slot_border(slot, Color(0.55, 0.7, 1.0), 2)
			slot.tooltip_text = equipped[i].description
		slot.pressed.connect(_toggle_picker.bind("traits"))
		slots.add_child(slot)
	_expanded_content.add_child(slots)
	if _picker == "traits":
		_add_trait_picker()


func _build_frame_rows() -> void:
	# One row per frame slot; each option lists its stat changes. Click to equip.
	for slot in Loadout.FRAME_SLOTS:
		_add_row_label(slot.capitalize())
		var row := HFlowContainer.new()
		row.add_theme_constant_override("h_separation", 10)
		row.add_theme_constant_override("v_separation", 10)
		for part in loadout.frame_parts_for(slot):
			if not part.unlocked:
				continue
			var equipped := part == loadout.frame_part(slot)
			var button := _make_slot(part.display_name + ("  ✓" if equipped else "") + "\n" + part.summary(), Vector2(200, 120), Color(0.2, 0.22, 0.27))
			button.add_theme_font_size_override("font_size", 15)
			if equipped:
				_set_slot_border(button, Color(0.9, 0.9, 0.95), 4)
			button.pressed.connect(func() -> void:
				loadout.equip_frame_part(part)
				_refresh())
			row.add_child(button)
		_expanded_content.add_child(row)
	_add_row_label("Net effect")
	var net := Label.new()
	var lines := mods.group_summary()
	net.text = "\n".join(lines) if not lines.is_empty() else "No changes (all Standard parts)"
	net.add_theme_font_size_override("font_size", 18)
	_expanded_content.add_child(net)


func _build_weapon_rows() -> void:
	# The equipped weapon, then every weapon to pick from.
	var equipped := loadout.weapon
	_add_row_label("Equipped")
	var current := _make_slot(equipped.display_name + "\n" + equipped.archetype, Vector2(320, 110), Color(0.24, 0.25, 0.3))
	_set_slot_border(current, Color(0.85, 0.85, 0.9), 3)
	_expanded_content.add_child(current)
	_add_row_label("Choose a weapon")
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 12)
	grid.add_theme_constant_override("v_separation", 12)
	for option in loadout.weapons:
		if not option.unlocked:
			continue
		var is_equipped := option == loadout.weapon
		var fire := "semi" if option.semi_auto >= 0.5 else "auto"
		var dmg := "%d×%d dmg" % [roundi(option.pellets), roundi(option.damage)] if option.pellets > 1.0 else "%d dmg" % roundi(option.damage)
		var stats := "%d rpm %s · %s · %d rds" % [roundi(option.fire_rate), fire, dmg, roundi(option.magazine_size)]
		var button := _make_slot(option.display_name + ("  ✓" if is_equipped else "") + "\n" + stats, Vector2(330, 90), Color(0.2, 0.21, 0.26))
		if is_equipped:
			_set_slot_border(button, Color(0.9, 0.9, 0.95), 4)
		button.pressed.connect(func() -> void:
			loadout.equip_weapon(option)
			_refresh())
		grid.add_child(button)
	_expanded_content.add_child(grid)


func _make_flora_slot() -> Button:
	var equipped := loadout.flora
	var text := "Flora\n" + (equipped.display_name if equipped else "None")
	if equipped and equipped.ult_name != "":
		text += "\nUlt: " + equipped.ult_name
	var color := equipped.color if equipped else Color(0.4, 0.85, 0.3)
	var slot := _make_slot(text, Vector2(300, 120), color.darkened(0.6))
	slot.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	_set_slot_border(slot, color, 3)
	slot.pressed.connect(_toggle_picker.bind("flora"))
	return slot


func _add_flora_picker() -> void:
	_add_row_label("Choose a flora (switches your grenade, abilities and ult)")
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	for option in loadout.floras:
		if not option.unlocked:
			continue
		var equipped := option == loadout.flora
		var button := _make_slot(option.display_name + ("  ✓" if equipped else "") + "\nUlt: " + option.ult_name, Vector2(240, 90), option.color.darkened(0.5))
		button.tooltip_text = option.description
		_set_slot_border(button, option.color.lightened(0.3) if equipped else option.color, 5 if equipped else 2)
		button.pressed.connect(func() -> void:
			loadout.equip_flora(option)
			_picker = ""
			_refresh())
		row.add_child(button)
	_expanded_content.add_child(row)
	if loadout.flora:
		var hint := Label.new()
		hint.text = loadout.flora.description
		hint.add_theme_font_size_override("font_size", 18)
		hint.modulate = Color(1, 1, 1, 0.6)
		hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		_expanded_content.add_child(hint)


func _add_trait_picker() -> void:
	# Every trait of the equipped flora; click to equip or unequip.
	_add_row_label("Choose traits (click to equip / unequip, up to %d)" % Loadout.TRAIT_SLOTS)
	var row := HFlowContainer.new()
	row.add_theme_constant_override("h_separation", 10)
	row.add_theme_constant_override("v_separation", 10)
	var full := loadout.equipped_traits().size() >= Loadout.TRAIT_SLOTS
	for option in loadout.traits_for_flora():
		if not option.unlocked:
			continue
		var on := loadout.is_trait_equipped(option)
		var button := _make_slot(option.display_name + ("  ✓" if on else "") + "\n" + option.description, Vector2(250, 110), Color(0.16, 0.22, 0.34) if on else Color(0.2, 0.21, 0.26))
		button.add_theme_font_size_override("font_size", 14)
		button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		_set_slot_border(button, Color(0.55, 0.7, 1.0) if on else Color(0.4, 0.42, 0.5), 4 if on else 1)
		if full and not on:
			button.modulate = Color(1, 1, 1, 0.5) # every slot is taken
		button.pressed.connect(func() -> void:
			loadout.toggle_trait(option)
			_refresh())
		row.add_child(button)
	_expanded_content.add_child(row)


func _make_grenade_slot() -> Button:
	var equipped := loadout.grenade
	var slot := _make_slot("Grenade\n" + equipped.display_name, Vector2(220, 110), equipped.color.darkened(0.6))
	_set_slot_border(slot, equipped.color, 3)
	slot.pressed.connect(_toggle_picker.bind("grenade"))
	return slot


func _toggle_picker(which: String) -> void:
	_picker = "" if _picker == which else which
	_refresh()


func _make_ability_slot(slot: int) -> Button:
	var equipped := loadout.ability(slot)
	var title := "Ability %d" % (slot + 1)
	if equipped == null:
		return _make_slot(title, Vector2(220, 110), Color(0.24, 0.25, 0.3))
	var button := _make_slot(title + "\n" + equipped.display_name, Vector2(220, 110), equipped.color.darkened(0.6))
	_set_slot_border(button, equipped.color, 3)
	button.pressed.connect(_toggle_picker.bind("ability%d" % slot))
	return button


func _add_ability_picker(slot: int) -> void:
	_add_row_label("Choose ability %d" % (slot + 1))
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	for option in loadout.abilities_for_flora():
		if not option.unlocked:
			continue
		var in_slot := loadout.ability_slot_of(option)
		var tag := ""
		if in_slot == slot:
			tag = "  ✓"
		elif in_slot != -1:
			tag = "  (slot %d)" % (in_slot + 1)
		var text := option.display_name + tag + "\n" + option.kind.capitalize()
		var button := _make_slot(text, Vector2(200, 90), option.color.darkened(0.5))
		button.tooltip_text = option.description
		_set_slot_border(button, option.color.lightened(0.3) if in_slot == slot else option.color, 5 if in_slot == slot else 2)
		button.pressed.connect(func() -> void:
			loadout.equip_ability(slot, option)
			_picker = ""
			_refresh())
		row.add_child(button)
	_expanded_content.add_child(row)
	var hint := Label.new()
	hint.text = _describe_abilities()
	hint.add_theme_font_size_override("font_size", 18)
	hint.modulate = Color(1, 1, 1, 0.6)
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_expanded_content.add_child(hint)


func _describe_abilities() -> String:
	var lines := []
	for option in loadout.abilities_for_flora():
		lines.append("%s: %s" % [option.display_name, option.description])
	return "\n".join(lines)


func _add_grenade_picker() -> void:
	_add_row_label("Choose a grenade")
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	for option in loadout.grenades_for_flora():
		if not option.unlocked:
			continue
		var equipped := option == loadout.grenade
		var stats := "%s · %d dmg · %s m" % [option.kind.capitalize(), roundi(option.damage), str(option.blast_radius)]
		var button := _make_slot(option.display_name + ("  ✓" if equipped else "") + "\n" + stats, Vector2(200, 90), option.color.darkened(0.45))
		_set_slot_border(button, option.color.lightened(0.3) if equipped else option.color, 5 if equipped else 2)
		button.pressed.connect(func() -> void:
			loadout.equip_grenade(option)
			_picker = ""
			_refresh())
		row.add_child(button)
	_expanded_content.add_child(row)


func _set_slot_border(slot: Button, color: Color, width: int) -> void:
	for state in ["normal", "hover", "pressed"]:
		var box := slot.get_theme_stylebox(state).duplicate() as StyleBoxFlat
		box.border_color = color
		box.set_border_width_all(width)
		slot.add_theme_stylebox_override(state, box)


func _refresh() -> void:
	# Rebuild the expanded tile in place, keeping the scroll position.
	var scroll := _expanded_scroll.scroll_vertical
	_rebuild_content()
	await get_tree().process_frame
	_expanded_scroll.scroll_vertical = scroll


func _add_row_label(text: String) -> void:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", 22)
	label.modulate = Color(1, 1, 1, 0.6)
	_expanded_content.add_child(label)


func _add_slot_row(names: Array, slot_size: Vector2, color: Color, wraps: bool) -> void:
	# Traits use a flow container so extra slots wrap onto new lines.
	var row: Container = HFlowContainer.new() if wraps else HBoxContainer.new()
	row.add_theme_constant_override("h_separation" if wraps else "separation", 12)
	if wraps:
		row.add_theme_constant_override("v_separation", 12)
	for slot_name in names:
		row.add_child(_make_slot(slot_name, slot_size, color))
	_expanded_content.add_child(row)


func _make_slot(slot_name: String, slot_size: Vector2, color: Color) -> Button:
	var slot := Button.new()
	slot.text = slot_name
	slot.custom_minimum_size = slot_size
	slot.focus_mode = Control.FOCUS_NONE
	slot.add_theme_font_size_override("font_size", 20)
	for state in ["normal", "hover", "pressed"]:
		var box := _style(color.lightened(0.12 if state == "hover" else (0.2 if state == "pressed" else 0.0)), 8)
		box.set_border_width_all(2)
		box.border_color = color.lightened(0.35)
		slot.add_theme_stylebox_override(state, box)
	return slot


func _collapse() -> void:
	_picker = ""
	_expanded.visible = false
	_tiles_box.visible = true


func is_expanded() -> bool:
	return _expanded.visible


func _input(event: InputEvent) -> void:
	# This node sits after the settings menu in the scene, so it sees input first
	# and can swallow Esc / circle while it's open.
	if settings_menu.visible:
		return # one menu at a time
	if event.is_action_pressed("character_menu"):
		toggle()
		get_viewport().set_input_as_handled()
		return
	if not visible:
		return
	if event.is_action_pressed("menu_back") or event.is_action_pressed("menu"):
		if _picker != "":
			_picker = "" # back out one level at a time
			_refresh()
		elif is_expanded():
			_collapse()
		else:
			toggle()
		get_viewport().set_input_as_handled()


func _process(delta: float) -> void:
	if visible:
		_preview_model.rotate_y(0.6 * delta)


func toggle() -> void:
	visible = not visible
	get_tree().paused = visible
	if visible:
		cursor.activate()
	else:
		cursor.deactivate()
		player.reset_input_edges() # circle is also crouch: don't count the closing press
