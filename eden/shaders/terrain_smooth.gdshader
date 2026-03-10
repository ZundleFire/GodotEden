shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

// ---------------------------------------------------------------------------
// TEXTURE ARRAY
// All terrain textures live in a single Texture2DArray.
// Slot layout (match your Texture2DArray import order):
//   0 = grass albedo
//   1 = rock  albedo
//   2 = snow  albedo
//   3 = sand  albedo
//   4 = dirt  albedo
//   5 = moss  albedo
//   (add more as needed, up to 16 with Mixel4 or 256 with Single)
// ---------------------------------------------------------------------------
uniform sampler2DArray u_texture_array         : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2DArray u_normal_array          : hint_normal,  filter_linear_mipmap_anisotropic;
uniform sampler2DArray u_roughness_array       :               filter_linear_mipmap_anisotropic;

// ---------------------------------------------------------------------------
// DETAIL NORMALMAP (virtual texturing — supplied automatically by the engine
// when VoxelLodTerrain has detail rendering enabled)
// ---------------------------------------------------------------------------
uniform sampler2D u_voxel_normalmap_atlas      : hint_normal,  filter_linear;
uniform sampler2D u_voxel_cell_lookup          :               filter_nearest;
uniform int       u_voxel_virtual_texture_tile_size = 16;
uniform float     u_voxel_cell_size            = 0.0;   // 0 = detail tex not ready
uniform int       u_voxel_block_size           = 16;
uniform vec4      u_voxel_virtual_texture_offset_scale = vec4(0.0, 0.0, 0.0, 1.0);

// ---------------------------------------------------------------------------
// TRANSVOXEL LOD stitching — supplied automatically by the engine
// ---------------------------------------------------------------------------
uniform int u_transition_mask;

// ---------------------------------------------------------------------------
// LOD FADING — supplied automatically by VoxelLodTerrain when lod_fade_duration > 0
// ---------------------------------------------------------------------------
uniform vec2 u_lod_fade = vec2(0.0, 0.0);

// ---------------------------------------------------------------------------
// MATERIAL / ENVIRONMENT PARAMETERS
// ---------------------------------------------------------------------------
uniform float uv_scale           : hint_range(0.001, 4.0)  = 0.25;   // world-space texture scale
uniform float normal_strength    : hint_range(0.0, 3.0)    = 1.0;
uniform float roughness_default  : hint_range(0.0, 1.0)    = 0.7;
uniform float ao_strength        : hint_range(0.0, 1.0)    = 0.5;

// Slope blending: angle (in dot-product) above which grass is replaced by rock
uniform float slope_rock_start   : hint_range(0.0, 1.0)    = 0.72;   // ~44 degrees
uniform float slope_rock_end     : hint_range(0.0, 1.0)    = 0.55;   // ~57 degrees

// Height-based snow cap
uniform float snow_height_start  : hint_range(-2000.0, 2000.0) = 120.0;
uniform float snow_height_blend  : hint_range(0.0, 50.0)       = 20.0;

// Varyings
varying vec4  v_indices;
varying vec4  v_weights;
varying vec3  v_normal_world;
varying vec3  v_pos_world;
varying vec3  v_pos_model;

// ---------------------------------------------------------------------------
// HELPERS
// ---------------------------------------------------------------------------

// Decode 4 bytes packed into a float (used for voxel CUSTOM1 data)
vec4 decode_8bit_vec4(float v) {
	uint i = floatBitsToUint(v);
	return vec4(
		float(i & uint(0xff)),
		float((i >> uint(8)) & uint(0xff)),
		float((i >> uint(16)) & uint(0xff)),
		float((i >> uint(24)) & uint(0xff))
	);
}

// Triplanar blend weights from world normal
vec3 triplanar_blend(vec3 n) {
	vec3 b = abs(n);
	b = max(b, vec3(0.00001));
	return b / (b.x + b.y + b.z);
}

// Sample a Texture2DArray with triplanar projection
vec4 triplanar_sample(sampler2DArray tex, vec3 world_pos, vec3 blend, float layer) {
	vec4 x = texture(tex, vec3(world_pos.yz, layer));
	vec4 y = texture(tex, vec3(world_pos.xz, layer));
	vec4 z = texture(tex, vec3(world_pos.xy, layer));
	return x * blend.x + y * blend.y + z * blend.z;
}

// Transvoxel vertex position correction for LOD seams
float get_transvoxel_secondary_factor(int idata) {
	int transition_mask = u_transition_mask & 0xff;
	int cell_border_mask   = (idata >> 0) & 63;
	int vertex_border_mask = (idata >> 8) & 63;
	int m = transition_mask & cell_border_mask;
	float t = float(m != 0);
	t *= float((vertex_border_mask & ~transition_mask) == 0);
	return t;
}

vec3 get_transvoxel_position(vec3 vertex_pos, vec4 fdata) {
	int idata = floatBitsToInt(fdata.a);
	float secondary_factor = get_transvoxel_secondary_factor(idata);
	vec3 secondary_position = fdata.xyz;
	vec3 pos = mix(vertex_pos, secondary_position, secondary_factor);
	// Cull inactive transition vertices by collapsing them to a point
	int itransition = (idata >> 16) & 0xff;
	float transition_cull = float(itransition == 0 || (itransition & u_transition_mask) != 0);
	pos *= transition_cull;
	return pos;
}

// ---------------------------------------------------------------------------
// DETAIL NORMALMAP (virtual texturing)
// ---------------------------------------------------------------------------

// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
vec3 octahedron_decode(vec2 f) {
	f = f * 2.0 - 1.0;
	vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	float t = clamp(-n.z, 0.0, 1.0);
	n.x += n.x >= 0.0 ? -t : t;
	n.y += n.y >= 0.0 ? -t : t;
	return normalize(n);
}

vec2 pad_uv(vec2 uv, float amount) {
	return uv * (1.0 - 2.0 * amount) + vec2(amount);
}

vec3 get_voxel_detail_normal_model() {
	float cell_size        = u_voxel_cell_size;
	int   block_size       = u_voxel_block_size;
	int   tile_size        = u_voxel_virtual_texture_tile_size;

	vec3 cell_posf = v_pos_model / cell_size;
	cell_posf = cell_posf * u_voxel_virtual_texture_offset_scale.w
	          + u_voxel_virtual_texture_offset_scale.xyz;
	ivec3 cell_pos  = ivec3(floor(cell_posf));
	vec3  cell_frac = fract(cell_posf);

	int cell_index   = cell_pos.x + cell_pos.y * block_size + cell_pos.z * block_size * block_size;
	int lookup_sqri  = int(ceil(sqrt(float(block_size * block_size * block_size))));
	ivec2 lookup_pos = ivec2(cell_index % lookup_sqri, cell_index / lookup_sqri);

	vec2  lv  = texture(u_voxel_cell_lookup, (vec2(lookup_pos) + vec2(0.5)) / float(lookup_sqri)).rg;
	ivec2 lvi = ivec2(round(lv * 255.0));
	int tile_index     = lvi.r | ((lvi.g & 0x3f) << 8);
	int tile_direction = lvi.g >> 6;

	vec2 tile_texcoord = vec2(0.0);
	if (tile_direction == 0)      tile_texcoord = cell_frac.zy;
	else if (tile_direction == 1) tile_texcoord = cell_frac.xz;
	else                          tile_texcoord = cell_frac.xy;

	float padding     = 0.5 / float(tile_size);
	tile_texcoord     = pad_uv(tile_texcoord, padding);

	ivec2 atlas_size  = textureSize(u_voxel_normalmap_atlas, 0);
	int   tiles_per_row = atlas_size.x / tile_size;
	ivec2 tile_origin = ivec2(tile_index % tiles_per_row, tile_index / tiles_per_row) * tile_size;
	vec2  atlas_uv    = (vec2(tile_origin) + float(tile_size) * tile_texcoord) / vec2(atlas_size);

	vec3 enc = texture(u_voxel_normalmap_atlas, atlas_uv).rgb;
	// Using XYZ encoding (switch to octahedron_decode(enc.rg) if you bake with octahedral compression)
	return normalize(2.0 * enc - vec3(1.0));
}

vec3 get_voxel_detail_normal_view(vec3 geom_normal_view, mat4 model_to_view) {
	if (u_voxel_cell_size == 0.0) {
		return geom_normal_view; // detail tex not ready
	}
	vec3 n_model = get_voxel_detail_normal_model();
	vec3 n_view  = (model_to_view * vec4(n_model, 0.0)).xyz;
	// Fall back to geometry normal if the detail normal is degenerate
	return mix(geom_normal_view, n_view, dot(n_view, n_view));
}

// Hash for LOD fade dithering
float get_hash(vec2 c) {
	return fract(sin(dot(c.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

// ---------------------------------------------------------------------------
// VERTEX
// ---------------------------------------------------------------------------
void vertex() {
	// Transvoxel LOD seam correction (uses CUSTOM0)
	VERTEX = get_transvoxel_position(VERTEX, CUSTOM0);

	// Pass data to fragment shader
	v_pos_model   = VERTEX;
	v_pos_world   = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_normal_world = (MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz;

	// Decode voxel texture indices and weights from CUSTOM1 (Mixel4 / Single format)
	v_indices = decode_8bit_vec4(CUSTOM1.x);
	v_weights = decode_8bit_vec4(CUSTOM1.y) / 255.0;
}

// ---------------------------------------------------------------------------
// FRAGMENT
// ---------------------------------------------------------------------------
void fragment() {
	// --- Triplanar setup ---
	vec3 n_world = normalize(v_normal_world);
	vec3 blend   = triplanar_blend(n_world);
	vec3 uv_pos  = v_pos_world * uv_scale;

	// --- Normalized weights (Mixel4) ---
	vec4 w = v_weights;
	w /= (w.x + w.y + w.z + w.w + 0.00001);

	// --- Sample the 4 blending slots from the texture array ---
	vec3 alb0 = triplanar_sample(u_texture_array, uv_pos, blend, v_indices.x).rgb;
	vec3 alb1 = triplanar_sample(u_texture_array, uv_pos, blend, v_indices.y).rgb;
	vec3 alb2 = triplanar_sample(u_texture_array, uv_pos, blend, v_indices.z).rgb;
	vec3 alb3 = triplanar_sample(u_texture_array, uv_pos, blend, v_indices.w).rgb;

	vec4 nrm0 = triplanar_sample(u_normal_array, uv_pos, blend, v_indices.x);
	vec4 nrm1 = triplanar_sample(u_normal_array, uv_pos, blend, v_indices.y);
	vec4 nrm2 = triplanar_sample(u_normal_array, uv_pos, blend, v_indices.z);
	vec4 nrm3 = triplanar_sample(u_normal_array, uv_pos, blend, v_indices.w);

	vec4 rgh0 = triplanar_sample(u_roughness_array, uv_pos, blend, v_indices.x);
	vec4 rgh1 = triplanar_sample(u_roughness_array, uv_pos, blend, v_indices.y);
	vec4 rgh2 = triplanar_sample(u_roughness_array, uv_pos, blend, v_indices.z);
	vec4 rgh3 = triplanar_sample(u_roughness_array, uv_pos, blend, v_indices.w);

	// --- Blend by Mixel4 weights ---
	vec3 albedo   = alb0 * w.r + alb1 * w.g + alb2 * w.b + alb3 * w.a;
	vec3 nrm_data = nrm0.rgb * w.r + nrm1.rgb * w.g + nrm2.rgb * w.b + nrm3.rgb * w.a;
	float roughness = (rgh0.r * w.r + rgh1.r * w.g + rgh2.r * w.b + rgh3.r * w.a);
	roughness = mix(roughness_default, roughness, 0.8);

	// --- Procedural slope/height overlay ---
	// These override the Mixel4 blend for areas where the generator didn't
	// set explicit material indices (e.g. if using a simple SDF generator
	// with no material output nodes). Remove or gate these if your generator
	// drives Mixel4 weights fully.

	// Slope: dot of world normal with up direction
	float up_dot = dot(n_world, vec3(0.0, 1.0, 0.0));

	// Rock on steep slopes (index 1)
	float rock_blend = 1.0 - smoothstep(slope_rock_end, slope_rock_start, up_dot);

	// Snow on high, flat areas (index 2)
	float height = v_pos_world.y;
	float snow_blend = smoothstep(snow_height_start, snow_height_start + snow_height_blend, height)
	                 * smoothstep(slope_rock_start, 1.0, up_dot); // flat only

	// Sample rock and snow with triplanar
	vec3 alb_rock = triplanar_sample(u_texture_array, uv_pos, blend, 1.0).rgb;
	vec3 alb_snow = triplanar_sample(u_texture_array, uv_pos, blend, 2.0).rgb;
	vec4 nrm_rock = triplanar_sample(u_normal_array,  uv_pos, blend, 1.0);
	vec4 nrm_snow = triplanar_sample(u_normal_array,  uv_pos, blend, 2.0);

	// Blend procedural overlays on top of voxel-driven albedo
	albedo   = mix(albedo,   alb_rock, rock_blend);
	nrm_data = mix(nrm_data, nrm_rock.rgb, rock_blend);
	albedo   = mix(albedo,   alb_snow, snow_blend);
	nrm_data = mix(nrm_data, nrm_snow.rgb, snow_blend);

	// --- Normal map tangent-space decode ---
	vec3 nrm_ts = normalize(nrm_data * 2.0 - vec3(1.0));
	nrm_ts.xy  *= normal_strength;

	// --- Virtual detail normalmap (applied on top) ---
	NORMAL = get_voxel_detail_normal_view(NORMAL, VIEW_MATRIX * MODEL_MATRIX);

	// --- AO (simple height-based ambient occlusion approximation) ---
	// Replace with proper AO map if you bake one into the alpha channel of roughness
	float ao = 1.0;

	// --- Write outputs ---
	ALBEDO    = albedo;
	ROUGHNESS = roughness;
	METALLIC  = 0.0;
	AO        = ao;
	AO_LIGHT_AFFECT = ao_strength;
	NORMAL_MAP        = nrm_ts;
	NORMAL_MAP_DEPTH  = normal_strength;

	// --- LOD cross-fade (must be last) ---
	if (u_lod_fade.x > 0.0 || u_lod_fade.y > 0.0) {
		float h = get_hash(SCREEN_UV);
		if (u_lod_fade.y > 0.5) {
			// Fading in
			if (u_lod_fade.x < h) { discard; }
		} else {
			// Fading out
			if (u_lod_fade.x > h) { discard; }
		}
	}
}
