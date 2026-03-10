// debug_biome_flat.gdshader
// Pure flat biome index colours with NO lighting — maximum contrast for debugging.
// Each biome gets a highly distinct, saturated colour so you can immediately
// see if the biome select cascade is working correctly.
//
// Useful to verify: are ocean/land borders correct? do deserts appear at equator?
// do snow caps appear at poles?  are deep/shallow ocean separated?
shader_type spatial;
render_mode unshaded, cull_back;

// High-contrast debug palette — very distinct per biome
const vec3 BIOME_DEBUG[15] = vec3[15](
	vec3(0.00, 0.40, 1.00),   //  0 OCEAN              pure blue
	vec3(0.00, 0.05, 0.50),   //  1 DEEP_OCEAN         near-black blue
	vec3(1.00, 0.95, 0.40),   //  2 BEACH              bright yellow
	vec3(0.45, 0.55, 0.45),   //  3 TUNDRA             muted sage
	vec3(1.00, 1.00, 1.00),   //  4 SNOW               pure white
	vec3(0.00, 0.35, 0.10),   //  5 TAIGA              deep forest green
	vec3(0.60, 0.55, 0.10),   //  6 SHRUBLAND          olive-gold
	vec3(0.20, 1.00, 0.10),   //  7 GRASSLAND          neon lime
	vec3(0.00, 0.55, 0.25),   //  8 TEMPERATE_FOREST   dark green
	vec3(0.00, 0.90, 0.50),   //  9 TROPICAL_FOREST    bright teal-green
	vec3(0.00, 0.60, 0.30),   // 10 RAINFOREST         deep cyan-green
	vec3(1.00, 0.80, 0.00),   // 11 SAVANNA            vivid amber
	vec3(1.00, 0.35, 0.00),   // 12 DESERT             vivid orange-red
	vec3(0.25, 0.05, 0.05),   // 13 SCORCHED           very dark red
	vec3(0.60, 0.60, 0.65)    // 14 BARE               light grey
);

varying flat int v_biome;

void vertex() {
	// CUSTOM1.x holds 4 packed uint8 biome indices (S4 single-texture format).
	// Byte 0 (bits 0-7) is the dominant index for this vertex.
	uint packed = floatBitsToUint(CUSTOM1.x);
	int raw = int(packed & uint(0xff));
	v_biome = clamp(raw, 0, 14);
}

void fragment() {
	ALBEDO = BIOME_DEBUG[v_biome];
}
