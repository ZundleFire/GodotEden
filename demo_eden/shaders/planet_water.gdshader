shader_type spatial;
render_mode blend_mix, diffuse_burley, specular_schlick_ggx, depth_draw_opaque;
// Backface culling deliberately left ON (i.e. cull_disabled is NOT set here, despite this
// looking like an obvious thing to want for a two-sided water surface): root-caused this
// session as the actual cause of severe screen-wide banding across every coarse-LOD water
// screenshot this shader ever produced. At a grazing view angle across a curved, alpha-blended
// water shell (looking near-tangent to the sphere, e.g. toward the horizon from just above the
// surface), cull_disabled meant every ray also composited the back faces of far-side/wrapped
// geometry it grazed through -- with thousands of coarse-LOD blocks in view at once, that's
// many stacked alpha layers per pixel, and the compositing result aliased into near-regular
// bands. Confirmed via elimination: neither wave motion, depth-texture sampling, per-vertex
// normal discontinuities at block boundaries, nor alpha blending itself reproduced the pattern
// in isolation -- only cull_disabled did. All the meshes here are one-sided, outward-facing
// heightfields (voxel water blocks and ocean-shell patches alike), so standard backface culling
// is both correct and the fix.

// Cheap procedural water: no textures (matches planet_land.gdshader's no-texture-assets
// constraint). Wave motion is a small 3-wave Gerstner stack (see Planetar UE5 plugin
// research -- its visible ocean waves are real-time Gerstner in the material's vertex
// shader too, not the FFT it also ships; FFT there only feeds buoyancy queries, which this
// project has no equivalent of). Displacement is applied along the vertex's local NORMAL
// (not world-up) since the water shell follows the planet's sphere curvature, not a flat
// plane. Foam is derived from wave steepness (crest sharpness), same technique as Planetar's
// Gerstner.ush. Fresnel edge brightening and DEPTH_TEXTURE-based shallow/deep tinting were
// already here from the perf pass. Shadow casting is disabled on the MeshInstance3D itself
// (see _build_water_material's caller in voxel_water_demo.gd) rather than here -- that's a
// per-instance flag, not a shader concern.

uniform sampler2D depth_tex : hint_depth_texture, filter_linear_mipmap;

// See fragment()'s flat_normal comment: this reconstruction fixes lighting seams between many
// independently-smoothed meshes (VoxelWaterSimulator's per-block water). A caller whose mesh
// already carries mathematically continuous per-vertex normals across every seam (ocean_shell.gd:
// the exact analytic sphere normal, identical by construction on shared leaf edges) does not have
// that problem, and the screen-space derivative itself becomes a liability instead -- at a grazing
// view angle over planet-scale world coordinates, dFdx/dFdy of world position is a noisy numerical
// approximation that reads as regular, tile-like banding across the whole visible surface. Off by
// default so a caller must opt in with a reason, not the reverse.
uniform bool use_derivative_normal = false;

uniform vec3 shallow_color : source_color = vec3(0.20, 0.55, 0.55);
uniform vec3 deep_color    : source_color = vec3(0.03, 0.12, 0.22);
uniform vec3 foam_color    : source_color = vec3(0.9, 0.95, 0.95);
uniform float depth_fade_distance : hint_range(0.1, 100.0) = 12.0;
uniform float wave_height  : hint_range(0.0, 1.0) = 0.18;
uniform float wave_speed   : hint_range(0.0, 4.0) = 0.6;
uniform float wave_steepness : hint_range(0.0, 1.0) = 0.35;
// Scales the 6.0/3.2/1.6 wavelength ratios below. MUST stay well above 2x the mesh's actual
// vertex spacing or the wave signal aliases into banding/moire (confirmed: the ocean shell's
// ~31-unit leaf vertex spacing with the old hardcoded wavelengths, tuned for ~1-unit near-field
// voxel water meshes, produced exactly this). Callers with coarser meshes must raise this via
// set_shader_parameter("wavelength_scale", ...) -- see ocean_shell.gd.
// Default of 20 (wavelengths ~32/64/120 units) is sized for near-field VoxelWaterSimulator
// meshes across its LOD0-3 tiers (voxel spacing from ~1 unit up to ~8x that at LOD3) -- the
// old implicit default of 1.0 (wavelengths 1.6/3.2/6.0) aliased even at LOD0, confirmed by
// visible striping in every screenshot this shader has ever produced.
uniform float wavelength_scale : hint_range(0.1, 200.0) = 20.0;
uniform float foam_strength : hint_range(0.0, 2.0) = 0.6;
uniform float roughness_value : hint_range(0.0, 1.0) = 0.05;
uniform float fresnel_power : hint_range(0.5, 8.0) = 4.0;
uniform float base_alpha : hint_range(0.0, 1.0) = 0.75;

// Procedural caustics -- a cheap stand-in for the Texture2DArray-driven caustics in
// godotshaders.com's "Water Shader 3D (Godot 4.3)", the shader this feature was requested from.
// A real caustic texture array needs authored assets this project doesn't have (every other
// material here -- planet_land.gdshader, planet_v4.gdshader -- is deliberately texture-free for
// the same reason), so this is two overlapping animated sine-interference fields instead: cheap,
// seamless at any scale, and no asset dependency. Gated to shallow water only (see shallow_amount
// in fragment()) since caustics are a shallow/sunlit-seabed phenomenon in reality.
uniform float caustics_intensity : hint_range(0.0, 4.0) = 0.6;
uniform float caustics_scale : hint_range(0.01, 2.0) = 0.15;
uniform float caustics_speed : hint_range(0.0, 2.0) = 0.35;
uniform vec3 caustics_color : source_color = vec3(0.6, 0.9, 1.0);

varying vec3 v_pos_world;
varying float v_foam;
varying vec3 v_normal_perturb; // tangent-space-ish slope from the Gerstner stack, for NORMAL_MAP

// Small fixed 3-wave Gerstner stack. dir is normalized; wavelength/amp/speed_mul tune each
// wave's scale. Returns world-space displacement (xy = horizontal choppiness, z = vertical
// height) and accumulates crest steepness into out_steepness for foam.
vec3 gerstner_wave(vec2 pos_world_2d, vec2 dir, float wavelength, float amp, float speed_mul, float t, inout float out_steepness) {
	float k = TAU / max(wavelength, 0.001);
	// Deep-water dispersion relation: omega = sqrt(g*k) -- shorter waves (larger k) oscillate
	// FASTER, not slower. This was previously sqrt(9.8/k), backwards: it made short-wavelength
	// ripples crawl and long swells race, the opposite of real water. Fixed.
	float omega = sqrt(9.8 * k) * speed_mul;
	float f = k * dot(dir, pos_world_2d) - omega * t;
	float q = wave_steepness / max(k * amp, 0.001); // classic Gerstner steepness clamp
	out_steepness += max(-sin(f), 0.0);
	return vec3(q * amp * dir.x * cos(f), q * amp * dir.y * cos(f), amp * sin(f));
}

// Two sine-grid fields at different angles/scales/phases, multiplied together: where both are
// near a peak the product spikes brightly, elsewhere it stays near zero -- the same "overlapping
// wavefronts" look real caustics have, without a texture. smoothstep sharpens the spikes into
// thin bright threads instead of a soft sine blob.
float caustic_pattern(vec2 p, float t) {
	float a = sin(p.x * 1.0 + t) + sin(p.y * 1.1 - t * 0.8) + sin((p.x + p.y) * 0.6 + t * 1.3);
	float b = sin(p.x * 1.3 - t * 0.9) + sin(p.y * 0.9 + t * 1.15) + sin((p.x - p.y) * 0.8 - t * 0.6);
	return smoothstep(1.2, 2.6, a * b);
}

void vertex() {
	vec3 pos_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float t = TIME * wave_speed;
	float steepness = 0.0;
	vec3 d = vec3(0.0);
	d += gerstner_wave(pos_world.xz, normalize(vec2(0.7, 0.3)), 6.0 * wavelength_scale, wave_height * 0.5, 1.0, t, steepness);
	d += gerstner_wave(pos_world.xz, normalize(vec2(-0.4, 0.9)), 3.2 * wavelength_scale, wave_height * 0.3, 1.4, t, steepness);
	d += gerstner_wave(pos_world.xz, normalize(vec2(0.2, -0.8)), 1.6 * wavelength_scale, wave_height * 0.15, 2.1, t, steepness);

	vec3 local_normal = normalize(NORMAL);
	VERTEX += local_normal * d.z + vec3(d.x, 0.0, d.y) * 0.3;
	v_pos_world = pos_world + local_normal * d.z;
	v_foam = clamp(steepness * foam_strength, 0.0, 1.0);
	// Horizontal (choppiness) displacement is a cheap proxy for local slope -- reused in
	// fragment() as a normal-map perturbation instead of a separate ad-hoc sine field.
	v_normal_perturb = vec3(-d.x, -d.y, 1.0);
}

void fragment() {
	// Root cause of the severe screen-wide banding seen with many coarse-LOD water blocks in
	// view at once: each block is its own SurfaceTool mesh with its own independently-computed
	// generate_normals() call, so per-vertex NORMAL doesn't match at shared block-boundary
	// edges (each block only sees its own local geometry when smoothing). At normal viewing
	// distances that's a faint seam; at a grazing angle across thousands of tiny blocks near
	// the horizon, the resulting lighting/Fresnel discontinuities at every block edge alias
	// into near-regular screen-space bands. Confirmed via elimination this session: the banding
	// persisted with wave motion, depth-tinting, and alpha blending all neutralized -- it only
	// went away with a single LOD tier (few enough blocks that grazing-angle seam density drops
	// to nothing). Fix: derive the lighting normal from screen-space derivatives of the actually
	// -rasterized world position instead of trusting the interpolated per-vertex NORMAL -- this
	// is continuous across any mesh/chunk boundary by construction, regardless of how many
	// separate SurfaceTool meshes make up the visible surface.
	if (use_derivative_normal) {
		vec3 flat_normal = normalize(cross(dFdx(v_pos_world), dFdy(v_pos_world)));
		if (dot(flat_normal, NORMAL) < 0.0) {
			flat_normal = -flat_normal;
		}
		NORMAL = flat_normal;
	}
	NORMAL_MAP = normalize(v_normal_perturb) * 0.5 + 0.5;
	NORMAL_MAP_DEPTH = 1.0;

	// Fresnel: more reflective/bright at grazing angles.
	float ndotv = clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0);
	float fresnel = pow(1.0 - ndotv, fresnel_power);

	// Depth-based shallow/deep tint using the scene depth buffer.
	float raw_depth = texture(depth_tex, SCREEN_UV).r;
	vec3 ndc = vec3(SCREEN_UV * 2.0 - 1.0, raw_depth);
	vec4 view_pos = INV_PROJECTION_MATRIX * vec4(ndc, 1.0);
	view_pos.xyz /= view_pos.w;
	float scene_depth = -view_pos.z;
	float water_depth = -VERTEX.z;
	float depth_diff = clamp((scene_depth - water_depth) / depth_fade_distance, 0.0, 1.0);

	vec3 base_color = mix(shallow_color, deep_color, depth_diff);
	base_color = mix(base_color, vec3(1.0), fresnel * 0.5);

	// Caustics only read as sunlit-seabed shimmer where the water is shallow enough to see the
	// bottom through (1.0 - depth_diff), not already whited out by foam, and not already looking
	// mostly at sky/reflection (1.0 - fresnel) -- the same three gates a real caustic pass would
	// need, just driven by values this shader already computes instead of a projected texture.
	float shallow_amount = 1.0 - depth_diff;
	float caustic = caustic_pattern(v_pos_world.xz * caustics_scale, TIME * caustics_speed)
			* shallow_amount * (1.0 - fresnel) * (1.0 - v_foam) * caustics_intensity;
	base_color += caustics_color * caustic;

	ALBEDO = mix(base_color, foam_color, v_foam);
	ROUGHNESS = mix(roughness_value, 0.9, v_foam);
	METALLIC = 0.0;
	SPECULAR = 0.6;
	ALPHA = mix(base_alpha, 1.0, depth_diff * 0.4 + fresnel * 0.3 + v_foam * 0.5);
}
