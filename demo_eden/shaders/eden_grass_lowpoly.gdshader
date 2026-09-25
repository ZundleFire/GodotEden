shader_type spatial;
render_mode cull_disabled, diffuse_burley, specular_schlick_ggx;

#include "res://shaders/eden_weather.gdshaderinc"

// Low-poly grass tufts spawned by EdenFoliage (VoxelInstancer MultiMesh).
// Blades are single flat triangles; UV.y = 0 at the base, 1 at the tip.

uniform vec3 u_base_color = vec3(0.16, 0.33, 0.11); // near the terrain grass (0.22, 0.42, 0.16) so tufts read as grass, not dark specks
uniform vec3 u_tip_color = vec3(0.32, 0.54, 0.20);
// Per-tuft brightness spread, so a meadow reads as facets rather than a flat carpet.
uniform float u_color_variation : hint_range(0.0, 0.5) = 0.15;
uniform float u_wind_strength : hint_range(0.0, 1.0) = 0.12;
uniform float u_wind_speed : hint_range(0.0, 5.0) = 1.3;
// Camera distances (m), set by EdenFoliage. Wind stops over the first range; the tuft itself
// shrinks into the ground over the second, so the edge of the grass range doesn't pop.
uniform float u_wind_fade_start = 20.0;
uniform float u_wind_fade_end = 35.0;
uniform float u_fade_start = 45.0;
uniform float u_fade_end = 60.0;

// Final colour, weather included, worked out per vertex: blades are linear gradients, so interpolating
// it is exact and the (overdraw-heavy) fragment stage stays as cheap as it was before weather
varying vec3 v_albedo;

float hash11(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
}

void vertex() {
	float v_height = UV.y;
	float v_shade = 1.0 + (hash11(float(INSTANCE_ID)) * 2.0 - 1.0) * u_color_variation;

	// Per-tuft camera distance, from the view-space origin (precise near the camera, unlike world
	// coordinates ~40 km out)
	float dist = length(MODELVIEW_MATRIX[3].xyz);
	VERTEX *= 1.0 - smoothstep(u_fade_start, u_fade_end, dist);
	// Buried by snow: tufts sink as it piles up (the terrain rises by the same depth)
	vec4 weather = eden_weather(MODEL_MATRIX[3].xyz);
	VERTEX.y -= weather.g * eden_weather_params.x * 1.6;
	v_albedo = mix(u_base_color, u_tip_color, v_height) * v_shade;
	v_albedo = mix(v_albedo, eden_snow_color(), weather.g * v_height) * (1.0 - 0.9 * eden_weather_params.y * weather.b);

	// Tip-weighted sway in the tuft's local frame. Phase from world position is fine at planet
	// scale: it only needs to vary slowly between neighbouring tufts.
	float wind = 1.0 - smoothstep(u_wind_fade_start, u_wind_fade_end, dist);
	if (wind > 0.0) {
		vec3 p = MODEL_MATRIX[3].xyz;
		float phase = dot(p, vec3(0.071, 0.053, 0.089)) + TIME * u_wind_speed;
		float bend = u_wind_strength * v_height * v_height * wind;
		VERTEX.x += sin(phase) * bend;
		VERTEX.z += sin(phase * 0.73 + 1.7) * bend * 0.6;
	}

	// Light blades with the ground normal, not their own: flat triangles seen edge-on or from
	// behind would otherwise flicker black. Matches the terrain's facet shading.
	NORMAL = vec3(0.0, 1.0, 0.0);
}

void fragment() {
	ALBEDO = v_albedo;
	ROUGHNESS = 0.9;
	SPECULAR = 0.15;
}
