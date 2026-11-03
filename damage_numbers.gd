class_name DamageNumbers
# Floating damage numbers: white for body hits, yellow for crits. Shared by every
# shootable thing (target dummies and enemies).

const CRIT_COLOR := Color(1.0, 0.85, 0.2)


static func spawn(from: Node, damage: float, critical: bool, point: Vector3, color := Color(0, 0, 0, 0)) -> void:
	# `color` overrides the usual white/yellow (e.g. blue for shield damage).
	show_text(from, str(roundi(damage)), critical, point, color)


static func show_text(from: Node, text: String, critical: bool, point: Vector3, color := Color(0, 0, 0, 0)) -> void:
	# Any floating word, like "BLINDED".
	var label := Label3D.new()
	label.text = text
	label.modulate = color if color.a > 0.0 else (CRIT_COLOR if critical else Color.WHITE)
	label.outline_modulate = Color(0, 0, 0, 0.8)
	label.font_size = 48 if critical else 36
	label.outline_size = 8
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	label.fixed_size = true # same size on screen no matter how far away
	label.pixel_size = 0.0006
	world_root(from).add_child(label)
	label.global_position = point + Vector3(randf_range(-0.15, 0.15), 0.2, 0.0)
	var tween := label.create_tween()
	tween.set_parallel(true)
	tween.tween_property(label, "global_position", label.global_position + Vector3.UP * 0.6, 0.7)
	tween.tween_property(label, "modulate:a", 0.0, 0.7).set_delay(0.25)
	tween.chain().tween_callback(label.queue_free)


static func world_root(from: Node) -> Node:
	# Where to put spawned effects: the running level (or the tree root as a fallback).
	var tree := from.get_tree()
	return tree.current_scene if tree.current_scene else tree.root
