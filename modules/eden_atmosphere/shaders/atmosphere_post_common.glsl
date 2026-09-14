// Shared by atmosphere_rays.glsl and atmosphere_fog_composite.glsl, inlined into both by
// gen_shader_header.py. Both passes bind the same parameter block at binding 3.
//
// std140 with vec4/mat4 members only: every member is exactly 16 floats or 4, with no implicit
// padding, so EdenAtmospherePostEffect can fill it as one flat float array and the layout cannot
// silently drift out of alignment with the C++ side.

layout(set = 0, binding = 3, std140) uniform Params {
	mat4 inv_projection; // depth-corrected (reverse-Z, flip-Y) projection, inverted
	mat4 cam_transform;  // world-from-view
	vec4 planet;         // xyz planet centre (world), w radius
	vec4 fog_a;          // x density per metre at the base, y scale height (m), z base altitude (m), w enabled
	vec4 fog_b;          // x sky affect, y anisotropy g, z directional scatter blend, w top of fog layer above base (m)
	vec4 fog_albedo;     // rgb
	vec4 fog_ambient;    // rgb ambient light on the fog
	vec4 sun_dir;        // xyz unit vector toward the sun
	vec4 sun_col;        // rgb sunlight reaching the fog, already extinguished by the atmosphere
	vec4 moon_dir;
	vec4 moon_col;
	vec4 screen;         // x width, y height, z aspect (w/h), w rays enabled
	vec4 light0_uv;      // sun:  xy screen uv, z weight (0 = skip), w brightness threshold
	vec4 light0_col;     // rgb tint * intensity
	vec4 light1_uv;      // moon: same layout
	vec4 light1_col;
	vec4 rays;           // x samples, y density (fraction of the way to the light), z decay, w emissive radius (uv)
	vec4 ray_style;      // x sky boost (extra ray weight over fog/sky, scaled by local fog amount)
} p;

// Interleaved gradient noise: a static per-pixel dither that turns fixed-step sampling bands
// into fine noise without temporal shimmer.
float ign(vec2 px) {
	return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
}

// See atmosphere_common.gdshaderinc's clamp_planet_relative() -- same guard, mirrored here since
// this compute shader does not share that include. Keeps the fog/rays march out of the same
// exotic-banding failure mode when the camera sits deep inside (or at) the planet centre.
vec3 clamp_planet_relative(vec3 ro, float planet_radius) {
	float len = length(ro);
	float floor_r = planet_radius + 1.0;
	if (len >= floor_r) {
		return ro;
	}
	vec3 dir = len > 1e-4 ? ro / len : vec3(0.0, 1.0, 0.0);
	return dir * floor_r;
}
