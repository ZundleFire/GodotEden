#version 450

// Light-ray emission, at the rays' quarter resolution: the scene colour where the far plane shows
// (sky, and the clouds drawn over it, which write no depth). atmosphere_rays.glsl marches over this
// instead of the full-resolution colour and depth: 1 fetch per step from a texture small enough to
// stay in cache, where it used to be 2 scattered full-resolution fetches -- at 128 steps that march
// was ~2.5 ms of a 1080p frame on a GTX 750 Ti. The brightness threshold and the radius falloff
// differ per light, so they are still applied in the march.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D color_tex;
layout(set = 0, binding = 1) uniform sampler2D depth_tex;
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image2D emission_image;

#include "atmosphere_post_common.glsl"

void main() {
	ivec2 px = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(emission_image);
	if (any(greaterThanEqual(px, size))) {
		return;
	}
	vec2 uv = (vec2(px) + 0.5) / vec2(size);
	// Reverse-Z depth: the far plane is 0.
	float sky = step(textureLod(depth_tex, uv, 0.0).r, 1e-6);
	imageStore(emission_image, px, vec4(textureLod(color_tex, uv, 0.0).rgb * sky, 1.0));
}
