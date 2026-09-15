shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

// Planet material for EdenPlanetGeneratorV4. Same flat-color MIXEL4 blend as planet_land.gdshader, plus the
// generator's surface data (VoxelMesherTransvoxel.surface_data_enabled, CHANNEL_DATA6 at 32-bit):
//   CUSTOM2.r erosion (0.5 untouched, lower = carved), .g ridge (0 gully .. 1 ridge), .b moisture, .a temperature
// Indices match EdenPlanetGeneratorV4::MAT_*: 0=grass 1=rock 2=snow 3=sand 4=dirt 5=moss 6=ocean_floor.

uniform int u_transition_mask;
uniform vec2 u_lod_fade = vec2(0.0, 0.0);

uniform float u_planet_radius = 40000.0;
// 1=erosion 2=ridge 3=moisture 4=temperature (unlit grayscale)
uniform int u_debug_mode = 0;
uniform float slope_rock_start : hint_range(0.0, 1.0) = 0.6;
uniform float slope_rock_end : hint_range(0.0, 1.0) = 0.38;
// Snow where temperature (0..1) drops below this, on flat enough ground
uniform float snow_temperature : hint_range(0.0, 1.0) = 0.22;
uniform float snow_blend : hint_range(0.001, 0.5) = 0.06;
uniform float gully_darkening : hint_range(0.0, 1.0) = 0.35;
uniform float ridge_highlight : hint_range(0.0, 1.0) = 0.25;
uniform float dryness_tint : hint_range(0.0, 1.0) = 0.5;

varying flat vec4 v_indices;
varying vec4 v_weights;
varying vec4 v_surface;
varying vec3 v_normal_world;
varying vec3 v_pos_world;

vec4 decode_8bit_vec4(float v) {
	uint i = floatBitsToUint(v);
	return vec4(float(i & 0xffu), float((i >> 8u) & 0xffu), float((i >> 16u) & 0xffu), float((i >> 24u) & 0xffu));
}

vec3 mat_albedo(int i) {
	if (i == 0) return vec3(0.13, 0.40, 0.11); // grass
	if (i == 1) return vec3(0.33, 0.30, 0.28); // rock
	if (i == 2) return vec3(0.78, 0.80, 0.85); // snow
	if (i == 3) return vec3(0.62, 0.51, 0.23); // sand
	if (i == 4) return vec3(0.29, 0.17, 0.07); // dirt
	if (i == 5) return vec3(0.10, 0.35, 0.21); // moss
	if (i == 6) return vec3(0.14, 0.17, 0.24); // ocean floor
	return vec3(1.0, 0.0, 1.0);
}

float mat_roughness(int i) {
	if (i == 2) return 0.45;
	if (i == 1) return 0.75;
	if (i == 6) return 0.70;
	return 0.87;
}

bool is_vegetation(int i) {
	return i == 0 || i == 5;
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

	// Dry vegetation turns yellow-brown, cold vegetation desaturates
	vec3 dry = mix(albedo, vec3(0.42, 0.38, 0.16), dryness_tint * (1.0 - smoothstep(0.25, 0.6, moisture)));
	albedo = mix(albedo, dry, vegetation);
	albedo = mix(albedo, vec3(dot(albedo, vec3(0.33))), vegetation * 0.4 * (1.0 - smoothstep(0.15, 0.45, temperature)));

	vec3 true_up = normalize(v_pos_world);
	float up_dot = dot(n_world, true_up);
	float altitude = length(v_pos_world) - u_planet_radius;

	// Erosion features: gullies are darker, wetter sediment; ridges catch light as bare rock
	float gully = max(-ridge, 0.0) + max(0.5 - erosion, 0.0);
	albedo *= 1.0 - gully_darkening * clamp(gully, 0.0, 1.0);
	roughness = mix(roughness, 0.6, clamp(gully * moisture, 0.0, 1.0));
	float ridge_rock = ridge_highlight * max(ridge, 0.0);

	float rock_blend = max(1.0 - smoothstep(slope_rock_end, slope_rock_start, up_dot), ridge_rock);
	albedo = mix(albedo, mat_albedo(1) * (1.0 + ridge_rock * 0.5), rock_blend);
	roughness = mix(roughness, mat_roughness(1), rock_blend);

	float snow = (1.0 - smoothstep(snow_temperature - snow_blend, snow_temperature + snow_blend, temperature))
			* smoothstep(slope_rock_end, 1.0, up_dot) * step(0.0, altitude);
	albedo = mix(albedo, mat_albedo(2), snow);
	roughness = mix(roughness, mat_roughness(2), snow);

	vec2 detail_coord = v_pos_world.xz * 4.0;
	float detail_fade = 1.0 - smoothstep(0.5, 2.0, length(fwidth(detail_coord)));
	albedo *= 1.0 - 0.05 * detail_fade * get_hash(floor(detail_coord));

	if (u_debug_mode > 0) {
		float d = u_debug_mode == 1 ? erosion : u_debug_mode == 2 ? v_surface.g : u_debug_mode == 3 ? moisture : temperature;
		ALBEDO = vec3(0.0);
		EMISSION = vec3(d);
	} else {
		ALBEDO = albedo;
	}
	ROUGHNESS = roughness;
	METALLIC = 0.0;
	SPECULAR = 0.0;

	if (u_lod_fade.x > 0.0 || u_lod_fade.y > 0.0) {
		float fh = get_hash(SCREEN_UV);
		if (u_lod_fade.y > 0.5) {
			if (u_lod_fade.x < fh) { discard; }
		} else {
			if (u_lod_fade.x > fh) { discard; }
		}
	}
}
