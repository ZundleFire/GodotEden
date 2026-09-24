shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

// Planet material for EdenPlanetGeneratorV4. Flat-color MIXEL4 blend (no texture assets) plus the generator's
// surface data (VoxelMesherTransvoxel.surface_data_enabled, CHANNEL_DATA6 at 32-bit in the terrain's VoxelFormat):
//   CUSTOM2.r erosion (0.5 untouched, lower = carved), .g ridge (0 gully .. 1 ridge), .b moisture, .a temperature
// Indices match EdenPlanetGeneratorV4::MAT_*: 0=grass 1=rock 2=snow 3=sand 4=dirt 5=moss 6=ocean_floor.
//
// Without surface data every value reads 0.5 (see v_surface below) and the climate-driven tinting goes flat --
// if this looks dull, check DATA6 is 32-bit first.

uniform int u_transition_mask;
uniform vec2 u_lod_fade = vec2(0.0, 0.0);

uniform float u_planet_radius = 40000.0;
// 1=erosion 2=ridge 3=moisture 4=temperature 5=world normal (unlit)
uniform int u_debug_mode = 0;

// Saturation around each material's own luminance; 1 = palette as-is
uniform float vibrance : hint_range(0.0, 2.0) = 1.18;
uniform float slope_rock_start : hint_range(0.0, 1.0) = 0.6;
uniform float slope_rock_end : hint_range(0.0, 1.0) = 0.38;
// Snow where temperature (0..1) drops below this, on flat enough ground
uniform float snow_temperature : hint_range(0.0, 1.0) = 0.22;
uniform float snow_blend : hint_range(0.001, 0.5) = 0.06;
uniform float gully_darkening : hint_range(0.0, 1.0) = 0.3;
uniform float ridge_highlight : hint_range(0.0, 1.0) = 0.25;
uniform float dryness_tint : hint_range(0.0, 1.0) = 0.6;

// Detail normals: two bands of procedural bump, faded out once a pixel spans more than a cell.
// Perturbs NORMAL directly in world space -- voxel meshes carry no tangents, so NORMAL_MAP would
// be undefined here.
uniform float detail_normal_strength : hint_range(0.0, 3.0) = 0.0;
uniform float detail_normal_scale = 6.0; // meters, fine band (coarse band is 5x this)

varying flat vec4 v_indices;
varying vec4 v_weights;
varying vec4 v_surface;
varying vec3 v_normal_world;
varying vec3 v_pos_world;

vec4 decode_8bit_vec4(float v) {
	uint i = floatBitsToUint(v);
	return vec4(float(i & 0xffu), float((i >> 8u) & 0xffu), float((i >> 16u) & 0xffu), float((i >> 24u) & 0xffu));
}

// Saturated but still physically sane albedos (real grass ~0.25, sand ~0.4, snow ~0.8): luminance stays in range,
// hue/chroma carry the vibrancy. vibrance pushes chroma further at runtime.
vec3 mat_albedo(int i) {
	if (i == 0) return vec3(0.15, 0.40, 0.13); // grass
	if (i == 1) return vec3(0.43, 0.41, 0.39); // rock
	if (i == 2) return vec3(0.84, 0.87, 0.93); // snow
	if (i == 3) return vec3(0.74, 0.60, 0.29); // sand
	if (i == 4) return vec3(0.35, 0.21, 0.09); // dirt
	if (i == 5) return vec3(0.11, 0.31, 0.17); // moss
	// Was (0.11, 0.20, 0.27) -- with no water surface rendered over it (no VoxelWaterSimulator
	// in this scene yet), that dark an unlit-looking flat albedo reads as a near-black sphere
	// below sea level from any distance, worse once gully_darkening below multiplies it further.
	// Brightened as a placeholder; darken it again once a real water layer sits on top, since a
	// seabed seen through actual water should be this dark.
	if (i == 6) return vec3(0.20, 0.30, 0.36); // ocean floor
	return vec3(1.0, 0.0, 1.0);
}

float mat_roughness(int i) {
	if (i == 2) return 0.42; // snow catches a little sheen
	if (i == 1) return 0.72;
	if (i == 3) return 0.78;
	if (i == 6) return 0.62;
	return 0.88;
}

bool is_vegetation(int i) {
	return i == 0 || i == 5;
}

float luma(vec3 c) {
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 saturate_color(vec3 c, float amount) {
	return clamp(mix(vec3(luma(c)), c, amount), vec3(0.0), vec3(1.0));
}

float hash13(vec3 p) {
	p = fract(p * 0.3183099 + 0.1);
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float value_noise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float n00 = mix(hash13(i), hash13(i + vec3(1.0, 0.0, 0.0)), f.x);
	float n10 = mix(hash13(i + vec3(0.0, 1.0, 0.0)), hash13(i + vec3(1.0, 1.0, 0.0)), f.x);
	float n01 = mix(hash13(i + vec3(0.0, 0.0, 1.0)), hash13(i + vec3(1.0, 0.0, 1.0)), f.x);
	float n11 = mix(hash13(i + vec3(0.0, 1.0, 1.0)), hash13(i + vec3(1.0, 1.0, 1.0)), f.x);
	return mix(mix(n00, n10, f.y), mix(n01, n11, f.y), f.z);
}

// Central-difference gradient of the bump field, projected onto the surface
vec3 perturb_normal(vec3 p, vec3 n, float scale, float strength) {
	float e = scale * 0.5;
	float h = value_noise(p / scale);
	vec3 g = vec3(
			value_noise((p + vec3(e, 0.0, 0.0)) / scale) - h,
			value_noise((p + vec3(0.0, e, 0.0)) / scale) - h,
			value_noise((p + vec3(0.0, 0.0, e)) / scale) - h);
	g -= n * dot(g, n);
	return normalize(n - g * strength);
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
	return pos * transition_cull;
}

float get_hash(vec2 c) {
	return fract(sin(dot(c.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void vertex() {
	VERTEX = get_transvoxel_position(VERTEX, CUSTOM0);
	v_pos_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_normal_world = (MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz;
	v_indices = decode_8bit_vec4(CUSTOM1.x);
	v_weights = decode_8bit_vec4(CUSTOM1.y) / 255.0;
	// Meshes without surface data read all zeros: fall back to neutral values
	v_surface = dot(CUSTOM2, CUSTOM2) == 0.0 ? vec4(0.5) : CUSTOM2;
}

void fragment() {
	vec3 n_world = normalize(v_normal_world);
	vec4 w = v_weights / (v_weights.x + v_weights.y + v_weights.z + v_weights.w + 0.00001);

	float erosion = v_surface.r;
	float ridge = v_surface.g * 2.0 - 1.0;
	float moisture = v_surface.b;
	float temperature = v_surface.a;

	vec3 albedo = vec3(0.0);
	float roughness = 0.0;
	float vegetation = 0.0;
	for (int k = 0; k < 4; ++k) {
		int mi = int(v_indices[k]);
		albedo += mat_albedo(mi) * w[k];
		roughness += mat_roughness(mi) * w[k];
		vegetation += is_vegetation(mi) ? w[k] : 0.0;
	}

	// Climate tinting: lush greens where wet, straw where dry, desaturated where cold
	vec3 lush = saturate_color(albedo * vec3(0.92, 1.08, 0.88), 1.12);
	vec3 dry = mix(albedo, vec3(0.52, 0.44, 0.18), dryness_tint);
	vec3 climate = mix(dry, lush, smoothstep(0.3, 0.7, moisture));
	albedo = mix(albedo, climate, vegetation);
	albedo = mix(albedo, saturate_color(albedo, 0.45), vegetation * (1.0 - smoothstep(0.12, 0.42, temperature)));

	vec3 true_up = normalize(v_pos_world);
	float up_dot = dot(n_world, true_up);
	float altitude = length(v_pos_world) - u_planet_radius;

	// Erosion features: gullies read as damp sediment, ridges as bare sunlit rock
	float gully = clamp(max(-ridge, 0.0) + max(0.5 - erosion, 0.0) * 1.5, 0.0, 1.0);
	albedo *= 1.0 - gully_darkening * gully;
	albedo = mix(albedo, albedo * vec3(0.94, 0.98, 1.02), gully * moisture);
	roughness = mix(roughness, 0.5, gully * moisture);
	float ridge_rock = ridge_highlight * max(ridge, 0.0);

	float rock_blend = max(1.0 - smoothstep(slope_rock_end, slope_rock_start, up_dot), ridge_rock);
	albedo = mix(albedo, mat_albedo(1) * (1.0 + ridge_rock * 0.6), rock_blend);
	roughness = mix(roughness, mat_roughness(1), rock_blend);

	float snow = (1.0 - smoothstep(snow_temperature - snow_blend, snow_temperature + snow_blend, temperature))
			* smoothstep(slope_rock_end, 1.0, up_dot) * step(0.0, altitude);
	albedo = mix(albedo, mat_albedo(2), snow);
	roughness = mix(roughness, mat_roughness(2), snow);

	albedo = saturate_color(albedo, vibrance);

	// Detail normals, faded by projected cell size so they never alias into far LODs
	float fine_fade = 1.0 - smoothstep(0.2, 0.8, length(fwidth(v_pos_world)) / detail_normal_scale);
	vec3 n_detail = n_world;
	if (detail_normal_strength > 0.0 && fine_fade > 0.01) {
		// Rock takes sharper bumps than soil, snow smooths them out
		float relief = detail_normal_strength * mix(1.0, 1.6, rock_blend) * (1.0 - 0.7 * snow);
		n_detail = perturb_normal(v_pos_world, n_detail, detail_normal_scale, relief * fine_fade);
		n_detail = perturb_normal(v_pos_world, n_detail, detail_normal_scale * 5.0, relief * 0.7);
		// Tiny albedo break-up along the same field keeps flat colors from banding
		albedo *= 1.0 - 0.07 * fine_fade * value_noise(v_pos_world / detail_normal_scale);
	} else {
		n_detail = perturb_normal(v_pos_world, n_detail, detail_normal_scale * 5.0, detail_normal_strength * 0.7);
	}
	// NORMAL is view space in fragment()
	NORMAL = normalize((VIEW_MATRIX * vec4(n_detail, 0.0)).xyz);

	if (u_debug_mode > 0) {
		vec3 d = u_debug_mode == 1 ? vec3(erosion)
				: u_debug_mode == 2 ? vec3(v_surface.g)
				: u_debug_mode == 3 ? vec3(moisture)
				: u_debug_mode == 4 ? vec3(temperature)
				: n_detail * 0.5 + 0.5;
		ALBEDO = vec3(0.0);
		EMISSION = d;
	} else {
		ALBEDO = albedo;
	}
	ROUGHNESS = roughness;
	METALLIC = 0.0;
	// Damp gullies and snow get a real highlight; dry soil stays matte
	SPECULAR = 0.3 * max(gully * moisture, snow * 0.4);

	if (u_lod_fade.x > 0.0 || u_lod_fade.y > 0.0) {
		float fh = get_hash(SCREEN_UV);
		if (u_lod_fade.y > 0.5) {
			if (u_lod_fade.x < fh) { discard; }
		} else {
			if (u_lod_fade.x > fh) { discard; }
		}
	}
}
