#version 450

// Planetary exponential height fog, plus the light-ray composite, at full resolution.
//
// Density falls off exponentially with ALTITUDE ABOVE THE PLANET -- length(p) - radius -- not
// with world Y. Godot's built-in height fog is planar; on a sphere that turns into a vertical
// wall at the equator and an upside-down layer at the far pole. Measuring altitude radially is
// the whole difference between "height fog" and "height fog that works on a planet".
//
// Runs after transparents on the resolved colour, so clouds are fogged too. Clouds write no
// depth, so a cloud pixel integrates fog out to whatever lies behind it; the error is only the
// fog BEYOND the cloud, which sits at cloud altitude where the density is effectively zero.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color_image;
layout(set = 0, binding = 1) uniform sampler2D depth_tex;
layout(set = 0, binding = 2) uniform sampler2D rays_tex;

#include "atmosphere_post_common.glsl"

vec2 ray_sphere(vec3 ro, vec3 rd, float radius) {
	float b = dot(ro, rd);
	float c = dot(ro, ro) - radius * radius;
	float d = b * b - c;
	if (d < 0.0) {
		return vec2(1.0, -1.0);
	}
	d = sqrt(d);
	return vec2(-b - d, -b + d);
}

float fog_density_at(vec3 pos) {
	float h = length(pos) - (p.planet.w + p.fog_a.z);
	// Uniform below the base altitude, exponential above it.
	return exp(-max(h, 0.0) / max(p.fog_a.y, 1.0));
}

// Optical depth along ro + rd * [0, scene_dist], planet-relative.
float fog_optical_depth(vec3 ro, vec3 rd, float scene_dist) {
	// Clip the integration to the fog layer first. Without this, a camera in orbit spreads all its
	// samples across tens of kilometres of empty space and only the last one lands in the fog.
	float top = p.planet.w + p.fog_a.z + p.fog_b.w;
	vec2 shell = ray_sphere(ro, rd, top);
	if (shell.y <= shell.x || shell.y <= 0.0) {
		return 0.0;
	}
	float t0 = max(shell.x, 0.0);
	float t1 = min(shell.y, scene_dist);
	if (t1 <= t0) {
		return 0.0;
	}

	// Composite Simpson over 4 intervals, sampling the true spherical altitude. Several samples
	// are needed rather than an endpoint formula because a horizontal ray over a sphere is a
	// chord: its middle dips below both ends by L^2 / 8R, which on a 40km planet is ~80m over 5km
	// -- 4 intervals (5 fog_density_at() calls, each an exp()) already resolves that curvature to
	// well under Simpson's own error term for a monotonic exponential falloff; this pass runs at
	// full screen resolution every frame, so halving the sample count from 8 intervals (9 calls)
	// is a real, low-risk saving.
	const int N = 4;
	float h = (t1 - t0) / float(N);
	float sum = fog_density_at(ro + rd * t0) + fog_density_at(ro + rd * t1);
	for (int i = 1; i < N; i++) {
		float w = (i % 2 == 1) ? 4.0 : 2.0;
		sum += w * fog_density_at(ro + rd * (t0 + h * float(i)));
	}
	return sum * h / 3.0;
}

// Henyey-Greenstein normalised so that isotropic scattering is exactly 1.
float phase_hg(float mu, float g) {
	float g2 = g * g;
	return (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * mu, 1e-4), 1.5);
}

void main() {
	ivec2 px = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(color_image);
	if (any(greaterThanEqual(px, size))) {
		return;
	}
	vec2 uv = (vec2(px) + 0.5) / vec2(size);
	vec4 color = imageLoad(color_image, px);
	vec3 result = color.rgb;
	// Set inside the fog block below when fog is enabled; stays 0 (no boost) otherwise.
	float fog_amount_for_rays = 0.0;

	if (p.fog_a.w > 0.5) {
		float depth = texelFetch(depth_tex, px, 0).r;
		bool sky = depth <= 1e-6; // reverse-Z far plane

		vec2 ndc = uv * 2.0 - 1.0;
		// Direction from the near plane (reverse-Z: 1). Works for sky pixels too, where the far
		// plane is at infinity and cannot be unprojected.
		vec4 v_near = p.inv_projection * vec4(ndc, 1.0, 1.0);
		vec3 view_dir = normalize(v_near.xyz / v_near.w);

		float scene_dist = 1.0e9;
		if (!sky) {
			vec4 v_pos = p.inv_projection * vec4(ndc, depth, 1.0);
			scene_dist = length(v_pos.xyz / v_pos.w);
		}

		vec3 rd = normalize(mat3(p.cam_transform) * view_dir);
		vec3 ro = clamp_planet_relative(p.cam_transform[3].xyz - p.planet.xyz, p.planet.w);

		if (sky) {
			// The sky shader draws its own analytic ground past the meshed terrain; stop there.
			vec2 ground = ray_sphere(ro, rd, p.planet.w);
			if (ground.y > ground.x && ground.x > 0.0) {
				scene_dist = ground.x;
			}
		}

		float tau = fog_optical_depth(ro, rd, scene_dist) * p.fog_a.x;
		float amount = 1.0 - exp(-tau);
		// Remembered before the sky-only scale-down just below, so the ray boost reflects the real
		// depth of haze along this ray rather than the halved value used to avoid double-scattering
		// the sky itself.
		fog_amount_for_rays = amount;
		if (sky) {
			// The atmosphere already scatters light over the sky; full fog on top would double
			// it. Partial affect keeps the horizon seam between fogged terrain and sky invisible.
			amount *= p.fog_b.x;
		}

		// Fog is lit by ambient plus sun and moon, with a forward-scattering lobe blended in so the
		// fog glows toward the light -- the gold haze under a low sun.
		float g = p.fog_b.y;
		float ph_sun = mix(1.0, phase_hg(dot(rd, p.sun_dir.xyz), g), p.fog_b.z);
		float ph_moon = mix(1.0, phase_hg(dot(rd, p.moon_dir.xyz), g), p.fog_b.z);
		vec3 light = p.fog_ambient.rgb + p.sun_col.rgb * ph_sun + p.moon_col.rgb * ph_moon;

		result = mix(result, p.fog_albedo.rgb * light, amount);
	}

	if (p.screen.w > 0.5) {
		// Boost rays where they cross fog/haze, proportional to how much fog is actually there -- a
		// light shaft slicing through mist, or the atmosphere glowing along the sun's direction, rather
		// than a flat brightening of clear sky.
		float sky_boost = 1.0 + fog_amount_for_rays * p.ray_style.x;
		// The rays are half resolution with a per-pixel jitter. One bilinear tap shows that jitter as
		// a fine stipple over everything the shafts cross; four taps offset inside the footprint
		// average it away for three extra fetches.
		// 1.5 ray-texels, not 0.75: at quarter resolution each jitter cell spans 4 screen pixels, and a
		// footprint sized for half res left a visible diamond pattern inside the shafts.
		vec2 texel = 1.5 / vec2(textureSize(rays_tex, 0));
		vec3 rays = textureLod(rays_tex, uv + vec2(-texel.x, -texel.y), 0.0).rgb
				+ textureLod(rays_tex, uv + vec2(texel.x, -texel.y), 0.0).rgb
				+ textureLod(rays_tex, uv + vec2(-texel.x, texel.y), 0.0).rgb
				+ textureLod(rays_tex, uv + vec2(texel.x, texel.y), 0.0).rgb;
		result += rays * (0.25 * sky_boost);
	}

	imageStore(color_image, px, vec4(result, color.a));
}
