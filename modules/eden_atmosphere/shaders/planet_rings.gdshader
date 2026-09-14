shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_never, unshaded, shadows_disabled, fog_disabled;

// Saturn-style planetary rings: a flat, tilted annulus banded by a deterministic 1D hash pattern,
// so no texture asset is required and the pattern reproduces exactly from ring_seed.
//
// Lighting and colour come from atmosphere_common.gdshaderinc, so rings redden with the sunset
// and darken through the planet's own shadow, the same as the sky and clouds.

#include "res://shaders/atmosphere_common.gdshaderinc"

uniform float ring_inner_radius : hint_range(1.01, 10.0) = 1.5;
uniform float ring_outer_radius : hint_range(1.02, 12.0) = 2.4;
uniform vec3 ring_normal = vec3(0.15, 0.97, 0.05);

uniform vec3 ring_tint : source_color = vec3(0.85, 0.80, 0.72);
uniform float ring_opacity : hint_range(0.0, 1.0) = 0.85;
uniform float ring_band_scale : hint_range(1.0, 400.0) = 40.0;
uniform float ring_band_contrast : hint_range(0.0, 1.0) = 0.6;
uniform float ring_band_variation : hint_range(0.0, 1.0) = 0.5;
uniform float ring_translucency : hint_range(0.0, 1.0) = 0.6;
uniform float ring_self_shadow : hint_range(0.0, 1.0) = 0.8;
uniform float ring_seed = 0.0;

varying vec3 v_world_pos;

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

float ring_hash(float x) {
	return fract(sin(x * 12.9898 + ring_seed * 37.71 + 1.0) * 43758.5453);
}

// Deterministic 1D banding across the ring's width: a value-noise style interpolation between
// hashed band cells, with ring_band_contrast controlling how hard each transition is -- 0 reads as
// a smooth painterly gradient, 1 as Saturn's crisp divisions and gaps.
float ring_bands(float t) {
	float scaled = t * max(ring_band_scale, 1.0);
	float cell = floor(scaled);
	float frac_part = fract(scaled);
	float h0 = ring_hash(cell);
	float h1 = ring_hash(cell + 1.0);
	float edge = mix(0.5, 0.015, ring_band_contrast);
	float w = smoothstep(0.5 - edge, 0.5 + edge, frac_part);
	return mix(h0, h1, w);
}

void fragment() {
	vec3 rel = v_world_pos - planet_center;
	float radius = length(rel);
	float r_in = planet_radius * ring_inner_radius;
	float r_out = planet_radius * max(ring_outer_radius, ring_inner_radius + 0.001);
	float t = clamp((radius - r_in) / max(r_out - r_in, 1.0), 0.0, 1.0);

	float band = ring_bands(t);
	float brightness = mix(1.0 - ring_band_variation, 1.0, band);
	// Soft taper at the inner/outer edge so the ring does not end in a razor-sharp geometric line.
	float edge_fade = smoothstep(0.0, 0.015, t) * smoothstep(0.0, 0.015, 1.0 - t);

	vec3 n = normalize(ring_normal);
	float ndl = abs(dot(n, sun_direction));
	// Backlit glow: brightest when the camera looks roughly along the sun direction through the
	// ring, the way real ice/dust rings glow when the sun sits behind them.
	vec3 view_dir = normalize(v_world_pos - CAMERA_POSITION_WORLD);
	float mu = max(dot(view_dir, sun_direction), 0.0);
	float back_glow = pow(mu, 6.0) * ring_translucency;

	float shadow = mix(1.0, planet_shadow_dir(rel, sun_direction), ring_self_shadow);
	vec3 lit = ring_tint * sun_tint * brightness * (ndl * 0.7 + 0.3 + back_glow) * shadow;

	ALBEDO = lit;
	ALPHA = ring_opacity * edge_fade * mix(0.4, 1.0, brightness);
}
