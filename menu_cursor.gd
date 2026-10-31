class_name MenuCursor
extends Control
# A Destiny-style menu cursor shared by the settings and character menus.
# - Left stick moves it (squared response: fine near center, fast at full tilt).
# - Cross clicks (and drags sliders, since it holds the click while held).
# - Right stick scrolls whatever scroll area is under it.
# - The mouse drives the same cursor; the OS pointer stays hidden while it's active.
# Add it as the last child of a menu so it draws on top, then call activate()/deactivate().

const SPEED := 1400.0 # pixels per second at full stick
const SCROLL_SPEED := 1100.0 # pixels per second at full right stick
const DEADZONE := 0.15

var pos := Vector2.ZERO


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	process_mode = Node.PROCESS_MODE_ALWAYS


func activate() -> void:
	Input.mouse_mode = Input.MOUSE_MODE_HIDDEN
	move_to(get_viewport().get_visible_rect().size * 0.5) # start centered


func deactivate() -> void:
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _active() -> bool:
	return is_visible_in_tree()


func _input(event: InputEvent) -> void:
	if not _active():
		return
	if event is InputEventMouseMotion:
		pos = event.position
		queue_redraw()
	elif event is InputEventJoypadButton and event.button_index == JOY_BUTTON_A:
		click(event.pressed)
		get_viewport().set_input_as_handled()


func _process(delta: float) -> void:
	if not _active():
		return
	var pad := _joypad()
	var stick := Vector2(Input.get_joy_axis(pad, JOY_AXIS_LEFT_X), Input.get_joy_axis(pad, JOY_AXIS_LEFT_Y))
	if stick.length() > DEADZONE:
		var strength := (stick.length() - DEADZONE) / (1.0 - DEADZONE)
		move_to(pos + stick.normalized() * strength * strength * SPEED * delta)
	var scroll := Input.get_joy_axis(pad, JOY_AXIS_RIGHT_Y)
	if absf(scroll) > DEADZONE:
		var area := _scroll_area_under_cursor()
		if area:
			area.scroll_vertical += int(scroll * SCROLL_SPEED * delta)


func move_to(to: Vector2) -> void:
	pos = to.clamp(Vector2.ZERO, get_viewport().get_visible_rect().size)
	# Tell the UI the "mouse" moved so hover highlights (and slider drags) follow.
	var motion := InputEventMouseMotion.new()
	motion.position = pos
	motion.global_position = pos
	Input.parse_input_event(motion)
	get_viewport().warp_mouse(pos) # keep the real (hidden) mouse in step
	queue_redraw()


func click(pressed: bool) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_LEFT
	event.pressed = pressed
	event.button_mask = MOUSE_BUTTON_MASK_LEFT if pressed else 0
	event.position = pos
	event.global_position = pos
	Input.parse_input_event(event)


func _scroll_area_under_cursor() -> ScrollContainer:
	var node: Node = get_viewport().gui_get_hovered_control()
	while node:
		if node is ScrollContainer:
			return node
		node = node.get_parent()
	return null


func _joypad() -> int:
	var pads := Input.get_connected_joypads()
	return pads[0] if pads.size() > 0 else 0


func _draw() -> void:
	# A ring with a dot.
	draw_circle(pos, 14.0, Color(1, 1, 1, 0.9), false, 2.5, true)
	draw_circle(pos, 3.0, Color(1, 1, 1, 0.9))
