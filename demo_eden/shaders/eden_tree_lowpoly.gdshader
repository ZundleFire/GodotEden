shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

// Low-poly procedural trees from EdenTreeMultiMesh: flat part colours baked into vertex COLOR,
// mesh origin at the trunk base (+Y up).

uniform float u_wind_strength : hint_range(0.0, 1.0) = 0.25; // metres of sway at 10 m height
uniform float u_wind_speed : hint_range(0.0, 5.0) = 0.8;
// Camera distances (m), set by EdenFoliage: sway stops over this range
uniform float u_wind_fade_start = 60.0;
uniform float u_wind_fade_end = 90.0;

void vertex() {
	float wind = 1.0 - smoothstep(u_wind_fade_start, u_wind_fade_end, length(MODELVIEW_MATRIX[3].xyz));
	if (wind > 0.0) {
		// Quadratic in height: the base stays planted, the crown sways
		float h = max(VERTEX.y, 0.0) * 0.1;
		float phase = dot(MODEL_MATRIX[3].xyz, vec3(0.031, 0.023, 0.037)) + TIME * u_wind_speed;
		float bend = u_wind_strength * h * h * wind;
		VERTEX.x += sin(phase) * bend;
		VERTEX.z += sin(phase * 0.81 + 1.3) * bend * 0.6;
	}
}

void fragment() {
	ALBEDO = COLOR.rgb;
	ROUGHNESS = 0.9;
	SPECULAR = 0.2;
}
