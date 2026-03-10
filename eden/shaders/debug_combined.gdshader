// debug_combined.gdshader
// Combined debug view: biome colours WITH elevation shading overlay.
// - Base colour = biome palette (same as planet_lowpoly but brighter)
// - Elevation darkens ocean depths and brightens mountain peaks
// - Latitude adds a subtle blue-white tint toward the poles
// - Full diffuse lighting from a sun direction
// Good for: verifying the whole pipeline looks sensible at a glance.
shader_type spatial;
render_mode unshaded, cull_back;

uniform float u_planet_radius  = 40000.0;
uniform float u_ambient        : hint_range(0.0, 1.0) = 0.50;
uniform vec3  u_sun_dir                               = vec3(0.4, 0.8, 0.3);
uniform float u_sun_strength   : hint_range(0.0, 2.0) = 0.65;
uniform float u_elev_contrast  : hint_range(0.0, 1.0) = 0.30;  // how much elevation modulates colour
uniform float u_lat_tint       : hint_range(0.0, 1.0) = 0.20;  // how much pole tint is applied

const vec3 BIOME_COLORS[15] = vec3[15](
	vec3(0.071, 0.380, 0.620),   //  0 OCEAN
	vec3(0.020, 0.100, 0.260),   //  1 DEEP_OCEAN
	vec3(0.910, 0.820, 0.520),   //  2 BEACH
	vec3(0.420, 0.490, 0.410),   //  3 TUNDRA
	vec3(0.940, 0.960, 0.980),   //  4 SNOW
	vec3(0.100, 0.310, 0.180),   //  5 TAIGA
	vec3(0.530, 0.560, 0.290),   //  6 SHRUBLAND
	vec3(0.310, 0.720, 0.180),   //  7 GRASSLAND
	vec3(0.080, 0.400, 0.180),   //  8 TEMPERATE_FOREST
	vec3(0.050, 0.520, 0.220),   //  9 TROPICAL_FOREST
	vec3(0.020, 0.380, 0.130),   // 10 RAINFOREST
	vec3(0.850, 0.740, 0.160),   // 11 SAVANNA
	vec3(0.870, 0.460, 0.130),   // 12 DESERT
	vec3(0.220, 0.110, 0.060),   // 13 SCORCHED
	vec3(0.520, 0.500, 0.490)    // 14 BARE
);

varying flat int  v_biome;
varying vec3      v_world_pos;

void vertex() {
	// CUSTOM1.x holds 4 packed uint8 biome indices (S4 single-texture format).
	// Byte 0 (bits 0-7) is the dominant index for this vertex.
	uint packed = floatBitsToUint(CUSTOM1.x);
	int raw = int(packed & uint(0xff));
	v_biome = clamp(raw, 0, 14);
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 dx = dFdx(v_world_pos);
	vec3 dy = dFdy(v_world_pos);
	vec3 face_normal = normalize(cross(dx, dy));
	float ndotl = max(0.0, dot(face_normal, normalize(u_sun_dir)));
	float light = u_ambient + u_sun_strength * ndotl;

	// Base biome colour
	vec3 color = BIOME_COLORS[v_biome];

	// Elevation modulation: darken oceans, brighten peaks slightly
	float dist     = length(v_world_pos);
	float elevation = dist - u_planet_radius;
	float elev_n   = clamp(elevation / 4000.0, -1.0, 1.0);  // -1=deep, 0=sea, 1=high
	color = mix(color, color * (1.0 + elev_n * 0.5), u_elev_contrast);

	// Latitude tint: abs(Y/radius) = 0 at equator, 1 at pole
	float lat_abs  = abs(v_world_pos.y) / max(dist, 1.0);
	vec3  pole_col = vec3(0.75, 0.85, 1.00);
	color = mix(color, pole_col, lat_abs * u_lat_tint);

	ALBEDO = clamp(color * light, vec3(0.0), vec3(1.0));
}
