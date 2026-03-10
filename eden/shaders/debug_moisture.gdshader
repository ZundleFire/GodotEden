// debug_moisture.gdshader
// Visualises the moisture/temperature bands derived from world position.
// Since moisture is baked into biome index (not a separate channel),
// we reconstruct it approximately from the biome itself:
//   - Wet equatorial biomes (rainforest, tropical) = saturated blue-green
//   - Dry biomes (desert, savanna) = orange-yellow
//   - Cold biomes = blue-white
//   - Ocean = grey-blue
//
// This shader also shows latitude as a secondary overlay (poles = faint blue tint).
shader_type spatial;
render_mode unshaded, cull_back;

uniform float u_planet_radius = 40000.0;
uniform float u_ambient : hint_range(0.0, 1.0) = 0.5;
uniform vec3  u_sun_dir = vec3(0.4, 0.8, 0.3);
uniform float u_sun_strength : hint_range(0.0, 2.0) = 0.6;

// Moisture level per biome (0=arid, 1=wet).  Matches biome index 0-14.
// Used to reconstruct an approximate moisture visualisation from the stored index.
const float BIOME_MOISTURE[15] = float[15](
	0.9,  //  0 OCEAN
	1.0,  //  1 DEEP_OCEAN
	0.4,  //  2 BEACH
	0.3,  //  3 TUNDRA
	0.5,  //  4 SNOW
	0.6,  //  5 TAIGA
	0.35, //  6 SHRUBLAND
	0.55, //  7 GRASSLAND
	0.70, //  8 TEMPERATE_FOREST
	0.80, //  9 TROPICAL_FOREST
	0.95, // 10 RAINFOREST
	0.30, // 11 SAVANNA
	0.05, // 12 DESERT
	0.02, // 13 SCORCHED
	0.15  // 14 BARE
);

// Temperature proxy per biome (0=freezing, 1=hot).
const float BIOME_TEMP[15] = float[15](
	0.50, //  0 OCEAN
	0.40, //  1 DEEP_OCEAN
	0.70, //  2 BEACH
	0.10, //  3 TUNDRA
	0.00, //  4 SNOW
	0.20, //  5 TAIGA
	0.45, //  6 SHRUBLAND
	0.50, //  7 GRASSLAND
	0.45, //  8 TEMPERATE_FOREST
	0.80, //  9 TROPICAL_FOREST
	0.90, // 10 RAINFOREST
	0.75, // 11 SAVANNA
	0.95, // 12 DESERT
	1.00, // 13 SCORCHED
	0.15  // 14 BARE
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

	float moisture = BIOME_MOISTURE[v_biome];
	float temp     = BIOME_TEMP[v_biome];

	// Dry-wet axis:  orange (dry) -> teal (wet)
	vec3 dry_col = vec3(0.95, 0.55, 0.10);
	vec3 wet_col = vec3(0.05, 0.65, 0.70);
	vec3 moi_col = mix(dry_col, wet_col, moisture);

	// Cold-hot axis tint: blue (cold) -> red (hot)
	vec3 cold_tint = vec3(0.60, 0.75, 1.00);
	vec3 hot_tint  = vec3(1.00, 0.65, 0.40);
	vec3 temp_tint = mix(cold_tint, hot_tint, temp);

	vec3 color = mix(moi_col, temp_tint, 0.35);  // 65% moisture, 35% temperature

	ALBEDO = clamp(color * light, vec3(0.0), vec3(1.0));
}
