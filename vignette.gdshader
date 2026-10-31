// Darkens the screen edges while aiming down sights.
shader_type canvas_item;

uniform float strength = 0.0;

void fragment() {
	vec2 uv = UV - 0.5;
	uv.x *= SCREEN_PIXEL_SIZE.y / SCREEN_PIXEL_SIZE.x; // keep it round on wide screens
	COLOR = vec4(0.0, 0.0, 0.0, smoothstep(0.3, 0.9, length(uv)) * strength);
}
