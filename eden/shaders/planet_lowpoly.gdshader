// planet_lowpoly.gdshader
// Flat-shaded lowpoly planet with biome index palette.
// Biome index arrives via COLOR.r (VoxelMesherTransvoxel texturing_mode=SINGLE_8_BIT).
//
// Index -> Biome:
//   0  OCEAN              deep ocean blue
//   1  DEEP_OCEAN         abyssal black-blue
//   2  BEACH              warm sand
//   3  TUNDRA             grey-green
//   4  SNOW               icy white
//   5  TAIGA              dark pine green
//   6  SHRUBLAND          dusty olive
//   7  GRASSLAND          vivid spring green
//   8  TEMPERATE_FOREST   rich dark green
//   9  TROPICAL_FOREST    lush saturated green
//  10  RAINFOREST         deep jungle
//  11  SAVANNA            golden straw
//  12  DESERT             burnt orange dune
//  13  SCORCHED           dark volcanic brown
//  14  BARE               grey mountain rock
shader_type spatial;
render_mode unshaded, cull_back;

uniform float u_lod_fade      : hint_range(0.0, 1.0) = 0.0;
uniform float u_ambient       : hint_range(0.0, 1.0) = 0.55;
uniform vec3  u_sun_dir                               = vec3(0.4, 0.8, 0.3);
uniform float u_sun_strength  : hint_range(0.0, 2.0) = 0.6;

// ── Biome colour palette ──────────────────────────────────────────────────────
// Richer, more saturated equatorial biomes; cool desaturated poles/altitude.
const vec3 BIOME_COLORS[15] = vec3[15](
	vec3(0.071, 0.380, 0.620),   //  0 OCEAN              mid blue
	vec3(0.020, 0.100, 0.260),   //  1 DEEP_OCEAN         abyss blue-black
	vec3(0.910, 0.820, 0.520),   //  2 BEACH              warm sand
	vec3(0.420, 0.490, 0.410),   //  3 TUNDRA             grey-green
	vec3(0.940, 0.960, 0.980),   //  4 SNOW               near-white blue
	vec3(0.100, 0.310, 0.180),   //  5 TAIGA              dark pine green
	vec3(0.530, 0.560, 0.290),   //  6 SHRUBLAND          dusty olive
	vec3(0.310, 0.720, 0.180),   //  7 GRASSLAND          vivid lime green
	vec3(0.080, 0.400, 0.180),   //  8 TEMPERATE_FOREST   rich forest
	vec3(0.050, 0.520, 0.220),   //  9 TROPICAL_FOREST    bright tropical green
	vec3(0.020, 0.380, 0.130),   // 10 RAINFOREST         deep saturated jungle
	vec3(0.850, 0.740, 0.160),   // 11 SAVANNA            golden straw yellow
	vec3(0.870, 0.460, 0.130),   // 12 DESERT             burnt sienna orange
	vec3(0.220, 0.110, 0.060),   // 13 SCORCHED           volcanic brown-black
	vec3(0.520, 0.500, 0.490)    // 14 BARE               mountain grey rock
);

varying flat int v_biome;
varying vec3     v_world_pos;

void vertex() {
	// CUSTOM1.x holds 4 packed uint8 biome indices (S4 single-texture format).
	// Byte 0 (bits 0-7) is the dominant index for this vertex.
	uint packed = floatBitsToUint(CUSTOM1.x);
	int biome_raw = int(packed & uint(0xff));
	v_biome = clamp(biome_raw, 0, 14);

	// NOTE: Do NOT apply CUSTOM0 as a vertex offset.
	// In VoxelMesherTransvoxel, CUSTOM0 carries smooth normals, NOT a position offset.
	// Applying it as VERTEX += CUSTOM0.xyz causes blocky/cube artefacts.

	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	// Flat shading: compute face normal from screen-space derivatives
	vec3 dx          = dFdx(v_world_pos);
	vec3 dy          = dFdy(v_world_pos);
	vec3 face_normal = normalize(cross(dx, dy));

	vec3  base_color = BIOME_COLORS[v_biome];
	float ndotl      = max(0.0, dot(face_normal, normalize(u_sun_dir)));
	vec3  lighting   = vec3(u_ambient) + vec3(u_sun_strength) * ndotl;
	vec3  final      = clamp(base_color * lighting, vec3(0.0), vec3(1.0));

	// LOD fade dithering
	if (u_lod_fade > 0.001) {
		ivec2 px        = ivec2(FRAGCOORD.xy) & ivec2(3);
		float threshold = float(px.x * 4 + px.y) / 16.0;
		if (u_lod_fade < threshold) { discard; }
	}

	ALBEDO = final;
}
