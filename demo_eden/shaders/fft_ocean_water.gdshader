shader_type spatial;
render_mode blend_mix, cull_back, diffuse_burley, specular_schlick_ggx, depth_draw_opaque;
// cull_back, not cull_disabled: see planet_water.gdshader's header comment -- backface culling on
// a curved, alpha-blended, multi-patch water shell is what avoids severe grazing-angle banding
// from compositing far-side/wrapped geometry the camera should never see through.

// Reads FFTOcean's (res://ocean/fft_ocean.gd) two output textures instead of computing waves
// procedurally: displacement_tex.xyz = world-scale (Dx, height, Dz) in the FFT patch's own local
// tangent frame, normal_tex.xyz = surface normal in that same local frame, normal_tex.w =
// Tessendorf's Jacobian "fold" factor (near 1 where the displacement map has folded a patch of
// surface onto itself -- a breaking wave in reality, foam by convention here). This REPLACES the
// old Gerstner + wave-steepness foam entirely: that foam accumulated independent of wave
// amplitude (confirmed this session -- it produced identical banding even with wave_height=0),
// because steepness came from a fixed 3-wave stack with no spatial variation across the whole
// planet. The Jacobian fold factor has neither problem: it is zero wherever the actual simulated
// surface isn't folding, and it varies with the real, per-texel FFT wave field.
//
// Each vertex builds its own local tangent frame from its sphere normal (tangent/bitangent
// perpendicular to it) and samples the FFT patch at the world position projected onto that local
// plane -- the same "displace along local normal, not world Z" idea planet_water.gdshader already
// uses to follow the planet's curvature, just with a full 2D tangent basis instead of one axis,
// since the FFT field needs two.

uniform sampler2D displacement_tex : filter_linear, repeat_enable;
uniform sampler2D normal_tex : filter_linear, repeat_enable;
uniform sampler2D depth_tex : hint_depth_texture, filter_linear_mipmap;
uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;

// Must match FFTOcean.patch_size (res://ocean/fft_ocean.gd) -- how many world units the sampled
// NxN grid tiles across before repeating.
uniform float patch_size : hint_range(10.0, 2000.0) = 200.0;

uniform vec3 shallow_color : source_color = vec3(0.20, 0.55, 0.55);
uniform vec3 deep_color : source_color = vec3(0.03, 0.12, 0.22);
uniform vec3 foam_color : source_color = vec3(0.92, 0.96, 0.97);
uniform float depth_fade_distance : hint_range(0.1, 100.0) = 12.0;

uniform float foam_threshold : hint_range(0.0, 1.0) = 0.25;
uniform float foam_strength : hint_range(0.0, 2.0) = 1.0;

uniform float roughness_value : hint_range(0.0, 1.0) = 0.03;
uniform float fresnel_power : hint_range(0.5, 8.0) = 4.0;
uniform float base_alpha : hint_range(0.0, 1.0) = 0.75;

// Screen-space refraction: offsets the background sample by the surface normal's horizontal
// component, so what you see "through" the water bends slightly with the waves instead of being
// a perfectly flat depth-tinted plane.
uniform float refraction_strength : hint_range(0.0, 0.1) = 0.025;

// Shallow-water translucency -- a cheap depth-driven glow standing in for true subsurface
// scattering, gated by sun_lit below so it (and caustics) only appear on the sunlit side of the
// planet -- both used to be unconditional, which read as the water "glowing" on the night side
// once real day/night terrain shading made the contrast obvious (screenshot-confirmed).
uniform vec3 translucency_color : source_color = vec3(0.35, 0.9, 0.55);
uniform float translucency_strength : hint_range(0.0, 4.0) = 1.0;

// Procedural caustics -- see planet_water.gdshader's own copy of this for the full rationale
// (a texture-free stand-in for a real caustic-texture-array pass, matching this project's
// no-texture-assets convention). Kept identical here so both water shaders read consistently.
uniform float caustics_intensity : hint_range(0.0, 4.0) = 0.6;
uniform float caustics_scale : hint_range(0.01, 2.0) = 0.15;
uniform float caustics_speed : hint_range(0.0, 2.0) = 0.35;
uniform vec3 caustics_color : source_color = vec3(0.6, 0.9, 1.0);

// Sun-facing gate for the two lighting-unaware effects above (translucency, caustics), pushed
// each frame from ocean_shell.gd via the sibling EdenPlanetAtmosphere's get_sun_direction().
uniform vec3 sun_direction = vec3(0.0, 0.0, 1.0);

varying vec3 v_pos_world;
varying vec3 v_tangent;
varying vec3 v_bitangent;
varying vec3 v_normal_axis;
varying vec2 v_uv;

float caustic_pattern(vec2 p, float t) {
	float a = sin(p.x * 1.0 + t) + sin(p.y * 1.1 - t * 0.8) + sin((p.x + p.y) * 0.6 + t * 1.3);
	float b = sin(p.x * 1.3 - t * 0.9) + sin(p.y * 0.9 + t * 1.15) + sin((p.x - p.y) * 0.8 - t * 0.6);
	return smoothstep(1.2, 2.6, a * b);
}

void vertex() {
	vec3 world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 arbitrary_up = abs(world_normal.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(world_normal, arbitrary_up));
	vec3 bitangent = normalize(cross(world_normal, tangent));

	vec3 world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec2 uv = vec2(dot(world_pos, tangent), dot(world_pos, bitangent)) / patch_size;

	vec4 disp = textureLod(displacement_tex, uv, 0.0);
	vec3 offset = tangent * disp.x + world_normal * disp.y + bitangent * disp.z;
	// ocean_shell.gd's leaf MeshInstance3Ds carry no rotation/scale (global_position only), so the
	// model matrix's linear part is identity and a world-space offset can be added to VERTEX
	// (object space) directly -- documented rather than done "generally" via inverse(MODEL_MATRIX)
	// because that assumption breaks silently if a future caller ever rotates/scales this mesh.
	VERTEX += offset;

	v_pos_world = world_pos + offset;
	v_tangent = tangent;
	v_bitangent = bitangent;
	v_normal_axis = world_normal;
	v_uv = uv;
}

void fragment() {
	vec4 norm_sample = texture(normal_tex, v_uv);
	vec3 local_n = norm_sample.xyz;
	float fold = norm_sample.w;

	vec3 N = normalize(v_tangent * local_n.x + v_normal_axis * local_n.y + v_bitangent * local_n.z);
	NORMAL = N;

	float ndotv = clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0);
	float fresnel = pow(1.0 - ndotv, fresnel_power);

	float raw_depth = texture(depth_tex, SCREEN_UV).r;
	vec3 ndc = vec3(SCREEN_UV * 2.0 - 1.0, raw_depth);
	vec4 view_pos = INV_PROJECTION_MATRIX * vec4(ndc, 1.0);
	view_pos.xyz /= view_pos.w;
	float scene_depth = -view_pos.z;
	float water_depth = -VERTEX.z;
	float depth_diff = clamp((scene_depth - water_depth) / depth_fade_distance, 0.0, 1.0);
	float shallow_amount = 1.0 - depth_diff;

	// Refraction: bend the background sample by the wave normal's horizontal slope. Strength
	// tapers with depth_diff so it only ever pulls in the seabed's own nearby colour, not a wildly
	// wrong sample from open water many units away in deep water.
	vec2 refract_offset = N.xz * refraction_strength * shallow_amount;
	vec3 refracted = textureLod(screen_tex, clamp(SCREEN_UV + refract_offset, vec2(0.001), vec2(0.999)), 0.0).rgb;

	float sun_lit = max(dot(v_normal_axis, normalize(sun_direction)), 0.0);

	vec3 base_color = mix(shallow_color, deep_color, depth_diff);
	base_color = mix(base_color, refracted, shallow_amount * 0.45);
	base_color = mix(base_color, vec3(1.0), fresnel * 0.5);
	base_color += translucency_color * translucency_strength * shallow_amount * 0.15 * sun_lit;

	float foam_amt = clamp((fold - foam_threshold) / max(1.0 - foam_threshold, 0.001), 0.0, 1.0) * foam_strength;

	float caustic = caustic_pattern(v_pos_world.xz * caustics_scale, TIME * caustics_speed)
			* shallow_amount * (1.0 - fresnel) * (1.0 - foam_amt) * caustics_intensity * sun_lit;
	base_color += caustics_color * caustic;

	ALBEDO = mix(base_color, foam_color, foam_amt);
	ROUGHNESS = mix(roughness_value, 0.85, foam_amt);
	METALLIC = 0.02; // a slight sheen reads as reflective far better than a pure dielectric 0
	SPECULAR = 0.9;
	ALPHA = mix(base_alpha, 1.0, depth_diff * 0.4 + fresnel * 0.3 + foam_amt * 0.5);
}
