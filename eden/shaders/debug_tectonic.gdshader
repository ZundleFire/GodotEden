// debug_tectonic.gdshader
// Shows tectonic plate elevation as a heatmap.
// The SDF value stored in CUSTOM0.x encodes elevation relative to sea level.
// We derive elevation from the world-space distance to the planet centre,
// compared to the PLANET_RADIUS constant. The biome index also lets us
// distinguish deep ocean / ocean / land / mountain bands.
//
// Red   = high mountains (>3000 above surface)
// White = snow peaks
// Tan   = lowlands / plains
// Blue  = shallow ocean
// Dark  = deep ocean
shader_type spatial;
render_mode unshaded, cull_back;

uniform float u_planet_radius = 40000.0;
uniform float u_ambient : hint_range(0.0, 1.0) = 0.5;
uniform vec3  u_sun_dir = vec3(0.4, 0.8, 0.3);
uniform float u_sun_strength : hint_range(0.0, 2.0) = 0.6;

varying vec3 v_world_pos;
varying flat float v_elevation;

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	// Elevation = distance from planet centre minus planet radius
	v_elevation = length(v_world_pos) - u_planet_radius;
}

// Map a value from [in_min, in_max] to [0, 1]
float remap01(float v, float lo, float hi) {
	return clamp((v - lo) / (hi - lo), 0.0, 1.0);
}

void fragment() {
	vec3 dx = dFdx(v_world_pos);
	vec3 dy = dFdy(v_world_pos);
	vec3 face_normal = normalize(cross(dx, dy));
	float ndotl = max(0.0, dot(face_normal, normalize(u_sun_dir)));
	float light = u_ambient + u_sun_strength * ndotl;

	vec3 color;
	float e = v_elevation;

	// Deep ocean: dark navy
	if (e < -2000.0) {
		color = mix(vec3(0.01, 0.04, 0.18), vec3(0.05, 0.15, 0.40), remap01(e, -4000.0, -2000.0));
	}
	// Shallow ocean: blue -> cyan
	else if (e < 0.0) {
		color = mix(vec3(0.05, 0.15, 0.40), vec3(0.20, 0.55, 0.80), remap01(e, -2000.0, 0.0));
	}
	// Beach / coast: sand
	else if (e < 200.0) {
		color = mix(vec3(0.80, 0.75, 0.50), vec3(0.55, 0.65, 0.35), remap01(e, 0.0, 200.0));
	}
	// Lowlands: green -> yellow
	else if (e < 1500.0) {
		color = mix(vec3(0.30, 0.62, 0.20), vec3(0.75, 0.72, 0.30), remap01(e, 200.0, 1500.0));
	}
	// Highlands: yellow -> orange-red
	else if (e < 3500.0) {
		color = mix(vec3(0.75, 0.55, 0.20), vec3(0.80, 0.20, 0.10), remap01(e, 1500.0, 3500.0));
	}
	// Peaks: red -> white (snow)
	else {
		color = mix(vec3(0.80, 0.15, 0.10), vec3(0.95, 0.95, 1.00), remap01(e, 3500.0, 5000.0));
	}

	ALBEDO = clamp(color * light, vec3(0.0), vec3(1.0));
}
