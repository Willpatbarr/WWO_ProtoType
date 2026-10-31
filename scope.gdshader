// Sniper scope overlay: black outside a circle, thin crosshairs inside, red center dot.
shader_type canvas_item;

uniform float radius = 0.44; // fraction of screen height
uniform float strength = 0.0; // 0 = off, 1 = fully up

void fragment() {
	vec2 p = UV - 0.5;
	p.x *= SCREEN_PIXEL_SIZE.y / SCREEN_PIXEL_SIZE.x; // keep it round on wide screens
	float d = length(p);
	float outside = smoothstep(radius, radius + 0.006, d);
	float lines = max(step(abs(p.x), 0.0011), step(abs(p.y), 0.0011)) * step(0.025, d) * (1.0 - outside);
	float dot_mask = 1.0 - smoothstep(0.0028, 0.0038, d);
	vec3 color = mix(vec3(0.0), vec3(1.0, 0.15, 0.1), dot_mask);
	COLOR = vec4(color, max(max(outside, lines * 0.9), dot_mask) * strength);
}
