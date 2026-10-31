// Draws the gun with its own fixed field of view (like Destiny's viewmodel),
// so zooming the world camera doesn't make the gun balloon in size.
// Also pulls the gun's depth toward the camera so it never clips into walls.
shader_type spatial;

uniform float fov_ratio = 1.0; // tan(camera half-FOV) / tan(gun half-FOV), set by gun.gd
instance uniform vec4 color : source_color = vec4(0.2, 0.2, 0.22, 1.0);
instance uniform float glow = 0.0;

void vertex() {
	mat4 proj = PROJECTION_MATRIX;
	proj[0][0] *= fov_ratio;
	proj[1][1] *= fov_ratio;
	POSITION = proj * MODELVIEW_MATRIX * vec4(VERTEX, 1.0);
}

void fragment() {
	ALBEDO = color.rgb;
	ROUGHNESS = 0.55;
	EMISSION = color.rgb * glow;
	// Squash the gun's depth toward the camera (reversed-Z: 1 = nearest) so it
	// always draws in front of the world instead of poking into walls.
	DEPTH = mix(FRAGCOORD.z, 1.0, 0.9);
}
