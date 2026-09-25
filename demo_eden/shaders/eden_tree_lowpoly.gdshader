shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "res://shaders/eden_weather.gdshaderinc"

// Low-poly procedural trees from EdenTreeMultiMesh: flat part colours baked into vertex COLOR,
// mesh origin at the trunk base (+Y up).

uniform float u_wind_strength : hint_range(0.0, 1.0) = 0.25; // metres of sway at 10 m height
uniform float u_wind_speed : hint_range(0.0, 5.0) = 0.8;
// Camera distances (m), set by EdenFoliage: sway stops over this range
uniform float u_wind_fade_start = 60.0;
uniform float u_wind_fade_end = 90.0;
// Camera distance ring (m) where this item's instances draw (EdenFoliage far tiers): whole instances outside it
// collapse to a point, so neighbouring tiers hand over exactly at the ring edge instead of per terrain chunk
uniform vec2 u_ring = vec2(0.0, 1e9);

// Vertex colour with weather applied. Faces are flat (own vertices), so the snow cap worked out per
// vertex is per face, at no fragment cost.
varying vec3 v_albedo;

void vertex() {
	float dist = length(MODELVIEW_MATRIX[3].xyz);
	if (dist < u_ring.x || dist >= u_ring.y) {
		VERTEX = vec3(0.0);
	}
	vec4 weather = eden_weather(MODEL_MATRIX[3].xyz);
	v_albedo = COLOR.rgb * (1.0 - 0.75 * eden_weather_params.y * weather.b);
	if (weather.g > 0.0) {
		// Snow caps the upward-facing facets (branches, crowns, rock tops)
		float upness = smoothstep(0.25, 0.7, dot(normalize(mat3(MODEL_MATRIX) * NORMAL), eden_weather_up(MODEL_MATRIX[3].xyz)));
		v_albedo = mix(v_albedo, eden_snow_color(), smoothstep(0.05, 0.5, weather.g) * upness);
	}
	float wind = 1.0 - smoothstep(u_wind_fade_start, u_wind_fade_end, dist);
	if (wind > 0.0) {
		// Quadratic in height: the base stays planted, the crown sways
		float h = max(VERTEX.y, 0.0) * 0.1;
		float phase = dot(MODEL_MATRIX[3].xyz, vec3(0.031, 0.023, 0.037)) + TIME * u_wind_speed;
		float bend = u_wind_strength * h * h * wind;
		VERTEX.x += sin(phase) * bend;
		VERTEX.z += sin(phase * 0.81 + 1.3) * bend * 0.6;
	}
}

void fragment() {
	ALBEDO = v_albedo;
	ROUGHNESS = 0.9;
	SPECULAR = 0.2;
}
