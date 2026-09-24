#version 450

// Crepuscular light rays ("god rays") for the sun and moon, at half resolution.
//
// Screen-space radial accumulation: each pixel marches toward the light's screen position,
// summing whatever bright sky lies along that line. Because it reads the FINAL image, anything
// dark in front of the light -- terrain, the silhouette of a cloud, a tree -- carves a shadow
// shaft out of the result for free. No shadow maps, no volumetric froxels.
//
// Why not Godot's volumetric fog for this: its froxel grid plus shadowed directional lights
// would give physically lit shafts, but it costs several milliseconds on a GTX 750 Ti and still
// cannot see the clouds, which are a transparent sky shell that casts no shadows. This pass
// sees clouds because it runs after transparents and reads their colour.
//
// Emission is restricted to the far plane (sky, plus the clouds drawn over it, which write no
// depth), to pixels brighter than a threshold, and to a radius around the light. Without those
// three limits a bright daylit terrain or the whole blue sky would start emitting rays.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// Sky-masked scene colour at this pass's resolution, from atmosphere_ray_emission.glsl
layout(set = 0, binding = 0) uniform sampler2D emission_tex;
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image2D rays_image;

#include "atmosphere_post_common.glsl"

vec3 emission(vec2 uv, vec2 light_uv, float threshold) {
	vec3 bright = max(textureLod(emission_tex, uv, 0.0).rgb - vec3(threshold), vec3(0.0));
	vec2 offset = (uv - light_uv) * vec2(p.screen.z, 1.0);
	float falloff = 1.0 - smoothstep(0.0, p.rays.w, length(offset));
	return bright * falloff;
}

vec3 march(vec2 uv, vec4 light_uv, vec4 light_col, float jitter) {
	// Per-frame constant, so this branch is coherent across the whole dispatch.
	if (light_uv.z <= 0.0) {
		return vec3(0.0);
	}
	int samples = max(int(p.rays.x), 1);
	vec2 delta = (light_uv.xy - uv) * (p.rays.y / float(samples));
	vec2 s = uv + delta * jitter;
	float decay = 1.0;
	vec3 sum = vec3(0.0);
	for (int i = 0; i < samples; i++) {
		sum += emission(s, light_uv.xy, light_uv.w) * decay;
		decay *= p.rays.z;
		s += delta;
	}
	return sum * (light_col.rgb * light_uv.z / float(samples));
}

void main() {
	ivec2 px = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(rays_image);
	if (any(greaterThanEqual(px, size))) {
		return;
	}
	vec2 uv = (vec2(px) + 0.5) / vec2(size);
	float jitter = ign(vec2(px));
	vec3 rays = march(uv, p.light0_uv, p.light0_col, jitter) + march(uv, p.light1_uv, p.light1_col, jitter);
	imageStore(rays_image, px, vec4(rays, 1.0));
}
