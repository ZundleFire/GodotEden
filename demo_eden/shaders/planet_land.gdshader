shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

// Procedural MIXEL4 terrain material -- no texture assets (none exist yet, and texture
// sampling is the single biggest fragment-shader cost on weak GPUs like the target GTX 750
// Ti). Everything here is flat colors + cheap derivative-based micro-detail, structured the
// same way as eden/shaders/terrain_smooth.gdshader (slope/height overlay, Transvoxel LOD
// seam correction, LOD cross-fade) minus its Texture2DArray triplanar sampling and detail
// virtual-texture normal map, both of which cost real GPU time this hardware doesn't have to
// spare. Swap in terrain_smooth.gdshader wholesale once real textures exist -- the MIXEL4
// decode/blend math is identical, only the per-material lookup differs.
//
// Indices must match EdenPlanetGeneratorV1/V3's MAT_* enum: 0=grass 1=rock 2=snow 3=sand
// 4=dirt 5=moss 6=ocean_floor.

uniform int u_transition_mask;
uniform vec2 u_lod_fade = vec2(0.0, 0.0);

// u_planet_radius: this shader used to treat "up" as world +Y and "height" as raw world-space Y
// (copied from the flat-terrain terrain_smooth.gdshader it's modeled on, see file header) -- on
// a sphere that's only even approximately correct right at the north pole; everywhere else the
// real local up direction and altitude are radial, not axis-aligned. That mismatch made the
// slope/snow overlay fire almost at random relative to the real surface, washing biome color out
// under rock/snow across most of the planet regardless of the real MIXEL4 material underneath.
// Fixed below by deriving true_up/altitude from v_pos_world directly; u_planet_radius is the one
// value that still needs to come from outside (the terrain generator's planet_radius), so this
// works at any planet size, not just the ~40km this was tuned against.
uniform float u_planet_radius = 40000.0;
// Diagnostic only, defaults to the real render path -- 1=raw MIXEL4 albedo with no slope/height
// overlay, 2=raw blend weights as RGB, 3=up_dot (slope) as grayscale, 4=primary material index
// as grayscale. Set from a demo script to isolate where a visual artifact is actually coming
// from without editing the shader each time.
uniform int u_debug_mode = 0;
uniform float slope_rock_start : hint_range(0.0, 1.0) = 0.72; // ~44 degrees
uniform float slope_rock_end   : hint_range(0.0, 1.0) = 0.55; // ~57 degrees
uniform float snow_height_start : hint_range(-2000.0, 4000.0) = 900.0; // meters above sea level
uniform float snow_height_blend : hint_range(0.0, 400.0)      = 150.0;
uniform float detail_strength   : hint_range(0.0, 1.0)        = 0.35;

// flat is required on v_indices: these are packed integer material ids (MAT_* constants), not
// continuous data. Without `flat`, the GPU linearly interpolates them across each triangle like
// any other varying -- a no-op inside a biome where neighboring vertices share the same index,
// but at a biome border adjacent vertices carry different indices per slot, so interior
// fragments decode a blended-integer garbage index (e.g. 4.5 truncating to a bogus slot) instead
// of a real material -- the source of the per-fragment speckle seen only at biome borders
// (BIOME_MATERIAL_PLAN.md root cause #2). v_weights must stay smoothly interpolated -- that's
// the actual cross-fade.
varying flat vec4 v_indices;
varying vec4 v_weights;
varying vec3 v_normal_world;
varying vec3 v_pos_world;

vec4 decode_8bit_vec4(float v) {
	uint i = floatBitsToUint(v);
	return vec4(
		float(i & uint(0xff)),
		float((i >> uint(8)) & uint(0xff)),
		float((i >> uint(16)) & uint(0xff)),
		float((i >> uint(24)) & uint(0xff))
	);
}

// Per-material albedo/roughness -- flat color, PBR-correct roughness per surface type
// instead of one shared constant (the placeholder shader's actual flatness came from this,
// not from lack of lighting: uniform ROUGHNESS=0.9 everywhere kills specular variation).
// Albedo = fraction of light REFLECTED, so these belong in a roughly physical range (real sand
// ~0.4, snow ~0.8, grass ~0.25). An earlier pass here pushed them up to 0.93-0.96 chasing
// "vibrancy", which is near-white before any lighting is applied at all -- under any sane light
// energy that clips straight to white and destroys the very color distinction it was meant to
// add. Saturation/hue below are kept (that's what actually reads as vibrant); only luminance is
// brought back into range, and exposure is matched to it in eden_foliage_demo.gd's
// _build_lighting().
vec3 mat_albedo(int i) {
	if (i == 0) return vec3(0.13, 0.40, 0.11); // grass
	if (i == 1) return vec3(0.33, 0.30, 0.28); // rock
	if (i == 2) return vec3(0.78, 0.80, 0.85); // snow (brightest, but still not clipping)
	if (i == 3) return vec3(0.62, 0.51, 0.23); // sand -- golden
	if (i == 4) return vec3(0.29, 0.17, 0.07); // dirt -- rich brown
	if (i == 5) return vec3(0.10, 0.35, 0.21); // moss -- teal-green, distinct from grass
	if (i == 6) return vec3(0.14, 0.17, 0.24); // ocean floor
	return vec3(1.0, 0.0, 1.0); // unknown -- obvious magenta
}

float mat_roughness(int i) {
	if (i == 0) return 0.85; // grass
	if (i == 1) return 0.75; // rock
	if (i == 2) return 0.45; // snow (slightly glossy)
	if (i == 3) return 0.80; // sand
	if (i == 4) return 0.90; // dirt
	if (i == 5) return 0.88; // moss
	if (i == 6) return 0.70; // ocean floor
	return 0.9;
}

float get_transvoxel_secondary_factor(int idata) {
	int transition_mask = u_transition_mask & 0xff;
	int cell_border_mask = idata & 63;
	int vertex_border_mask = (idata >> 8) & 63;
	int m = transition_mask & cell_border_mask;
	float t = float(m != 0);
	t *= float((vertex_border_mask & ~transition_mask) == 0);
	return t;
}

vec3 get_transvoxel_position(vec3 vertex_pos, vec4 fdata) {
	int idata = floatBitsToInt(fdata.a);
	float secondary_factor = get_transvoxel_secondary_factor(idata);
	vec3 pos = mix(vertex_pos, fdata.xyz, secondary_factor);
	int itransition = (idata >> 16) & 0xff;
	float transition_cull = float(itransition == 0 || (itransition & u_transition_mask) != 0);
	pos *= transition_cull;
	return pos;
}

float get_hash(vec2 c) {
	return fract(sin(dot(c.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void vertex() {
	// u_debug_mode == 7: bypass Transvoxel's LOD-seam vertex correction entirely (raw VERTEX).
	// If get_transvoxel_position() -- specifically its `pos *= transition_cull` step, which
	// zeroes a vertex's position outright when u_transition_mask doesn't recognize its cell as a
	// transition cell -- isn't being fed the correct per-chunk u_transition_mask, this collapses
	// affected vertices to the origin and can create degenerate/overlapping geometry whose only
	// visible symptom might be depth-fighting-driven lighting noise, not a wildly obvious hole.
	if (u_debug_mode == 7) {
		v_pos_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	} else {
		VERTEX = get_transvoxel_position(VERTEX, CUSTOM0);
		v_pos_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	}
	v_normal_world = (MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz;
	v_indices = decode_8bit_vec4(CUSTOM1.x);
	v_weights = decode_8bit_vec4(CUSTOM1.y) / 255.0;
}

void fragment() {
	vec3 n_world = normalize(v_normal_world);
	vec4 w = v_weights;
	w /= (w.x + w.y + w.z + w.w + 0.00001);

	vec3 albedo = mat_albedo(int(v_indices.x)) * w.x
			+ mat_albedo(int(v_indices.y)) * w.y
			+ mat_albedo(int(v_indices.z)) * w.z
			+ mat_albedo(int(v_indices.w)) * w.w;
	float roughness = mat_roughness(int(v_indices.x)) * w.x
			+ mat_roughness(int(v_indices.y)) * w.y
			+ mat_roughness(int(v_indices.z)) * w.z
			+ mat_roughness(int(v_indices.w)) * w.w;

	// Procedural slope/height overlay -- same idea as terrain_smooth.gdshader, flat color
	// instead of a texture sample. true_up/altitude are radial (sphere-correct), not world +Y --
	// see u_planet_radius's comment above.
	vec3 true_up = normalize(v_pos_world);
	float altitude = length(v_pos_world) - u_planet_radius;
	float up_dot = dot(n_world, true_up);
	float rock_blend = 1.0 - smoothstep(slope_rock_end, slope_rock_start, up_dot);
	float snow_blend = smoothstep(snow_height_start, snow_height_start + snow_height_blend, altitude)
			* smoothstep(slope_rock_start, 1.0, up_dot);
	if (u_debug_mode != 1) {
		albedo = mix(albedo, mat_albedo(1), rock_blend);
		roughness = mix(roughness, mat_roughness(1), rock_blend);
		albedo = mix(albedo, mat_albedo(2), snow_blend);
		roughness = mix(roughness, mat_roughness(2), snow_blend);
	}

	vec3 debug_emission = vec3(0.0);
	bool debug_unlit = false;
	if (u_debug_mode == 2) {
		debug_emission = w.xyz;
		debug_unlit = true;
	} else if (u_debug_mode == 3) {
		debug_emission = vec3(up_dot);
		debug_unlit = true;
	} else if (u_debug_mode == 4) {
		// Unlit (EMISSION, not ALBEDO+lighting) so ambient/directional light can't tint what
		// should be a pure grayscale readout of the raw material index -- a prior attempt at
		// this same debug mode via ALBEDO looked greenish/tinted and was hard to read because of
		// exactly that confound.
		debug_emission = vec3(float(int(v_indices.x)) / 6.0);
		debug_unlit = true;
	} else if (u_debug_mode == 8) {
		// Raw normal XYZ as RGB (unlit), not compressed into a single dot-product scalar like
		// mode 3 -- up_dot near 1.0 (flat ground) compresses small normal variations into a
		// narrow range that's hard to see in an 8-bit screenshot even if the underlying
		// lighting math is sensitive to them. This is a more sensitive readout of the same data.
		debug_emission = n_world * 0.5 + 0.5;
		debug_unlit = true;
	} else if (u_debug_mode == 9) {
		// Amplified deviation of n_world from the expected smooth "true_up" direction --
		// multiplies a tiny difference way up so even sub-degree normal noise becomes visible.
		debug_emission = abs(n_world - true_up) * 40.0;
		debug_unlit = true;
	}

	// Cheap per-fragment micro-detail: derivative-based fake bump from a single hashed value
	// (no noise texture, no extra samples) -- breaks up the otherwise perfectly flat color
	// fields without paying for real detail normal mapping.
	//
	// detail_fade: root-caused this session as the source of screen-wide vertical banding
	// visible even with terrain alone (no water) at long view distances. get_hash's floor()
	// makes h piecewise-constant per detail cell, so dFdx/dFdy(h) is 0 almost everywhere and
	// spikes at cell boundaries -- fine up close where a cell covers many pixels, but once the
	// camera is far enough that a single screen pixel spans multiple 0.25-unit cells (the
	// common case at planetary view distances), this hashed detail is unfiltered high-frequency
	// noise with no mipmapping, and it aliases. fwidth() of the (unfloored) detail coordinate is
	// the standard measure of "how many cells does this pixel span" -- fade the whole effect out
	// once that exceeds roughly one cell per pixel, the same way texture minification would.
	vec2 detail_coord = v_pos_world.xz * 4.0;
	float detail_fade = 1.0 - smoothstep(0.5, 2.0, length(fwidth(detail_coord)));
	float h = get_hash(floor(detail_coord));
	vec3 detail_n = normalize(vec3(dFdx(h), dFdy(h), 1.0));
	NORMAL_MAP = mix(vec3(0.5, 0.5, 1.0), detail_n * 0.5 + 0.5, detail_strength * detail_fade);
	NORMAL_MAP_DEPTH = 0.6;
	albedo *= 1.0 - detail_strength * detail_fade * 0.06 * h;

	ALBEDO = debug_unlit ? vec3(0.0) : albedo;
	EMISSION = debug_emission;
	ROUGHNESS = roughness;
	METALLIC = 0.0;
	// Matte terrain (grass/dirt/moss/rock) has no real specular highlight to render; zeroing
	// this is a cheap, physically-reasonable simplification. (Tested in isolation while chasing
	// the checkerboard bug below -- didn't turn out to be the cause, but there's no reason to
	// pay for GGX specular on flat-colored dirt either way.)
	SPECULAR = 0.0;

	if ((u_lod_fade.x > 0.0 || u_lod_fade.y > 0.0) && u_debug_mode != 5) {
		float fh = get_hash(SCREEN_UV);
		if (u_lod_fade.y > 0.5) {
			if (u_lod_fade.x < fh) { discard; }
		} else {
			if (u_lod_fade.x > fh) { discard; }
		}
	}
}
