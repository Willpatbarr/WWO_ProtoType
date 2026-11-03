shader_type spatial;
// Colors the mannequin by body region (head, arms, core, legs). Each vertex takes the
// region of the bone that moves it most. `bind_region` maps skin bind index -> region.

uniform vec4 region_colors[4] : source_color; // 0 head, 1 arms, 2 core, 3 legs
uniform int bind_region[128];
uniform float flash = 0.0; // 0..1, blends toward white (hit flash)
uniform float roughness_value = 0.6;

varying flat vec3 tint;

void vertex() {
	int best = 0;
	float best_weight = -1.0;
	for (int i = 0; i < 4; i++) {
		if (BONE_WEIGHTS[i] > best_weight) {
			best_weight = BONE_WEIGHTS[i];
			best = int(BONE_INDICES[i]);
		}
	}
	tint = region_colors[bind_region[clamp(best, 0, 127)]].rgb;
}

void fragment() {
	ALBEDO = mix(tint, vec3(1.0), flash);
	ROUGHNESS = roughness_value;
}
