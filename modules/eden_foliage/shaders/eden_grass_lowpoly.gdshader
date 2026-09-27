shader_type spatial;
render_mode cull_disabled, diffuse_burley, specular_schlick_ggx;

#include "res://shaders/eden_weather.gdshaderinc"
#include "res://shaders/eden_wind.gdshaderinc"
#include "res://shaders/eden_season_globals.gdshaderinc"
#include "res://shaders/eden_health.gdshaderinc"

// Low-poly grass tufts spawned by EdenFoliage (VoxelInstancer MultiMesh).
// Blades are single flat triangles; UV.y = 0 at the base, 1 at the tip.

uniform vec3 u_base_color = vec3(0.16, 0.33, 0.11); // near the terrain grass (0.22, 0.42, 0.16) so tufts read as grass, not dark specks
uniform vec3 u_tip_color = vec3(0.32, 0.54, 0.20);
// Per-tuft brightness spread, so a meadow reads as facets rather than a flat carpet.
uniform float u_color_variation : hint_range(0.0, 0.5) = 0.12;
// How far down the blade the base colour reaches: 0 = a full base-to-tip gradient, 1 = tip colour throughout
uniform float u_base_lift : hint_range(0.0, 1.0) = 0.35;
// Tip lean in metres at 10 m/s of wind (EdenAmbience's wind; a light breeze when there is none)
uniform float u_wind_strength : hint_range(0.0, 1.0) = 0.22;
// Light through the blades when the sun is behind them
uniform float u_translucency : hint_range(0.0, 1.0) = 0.15;
// How much blades keep their own facing in the lighting (0 = lit exactly like the ground): a little gives depth
uniform float u_blade_shading : hint_range(0.0, 1.0) = 0.3;
// Ambient light reaching the roots of a tuft, relative to the tips
uniform float u_root_occlusion : hint_range(0.0, 1.0) = 0.55;
// Camera distances (m), set by EdenFoliage. Wind stops over the first range; the tuft itself
// shrinks into the ground over the second, so the edge of the grass range doesn't pop.
uniform float u_wind_fade_start = 20.0;
uniform float u_wind_fade_end = 35.0;
uniform float u_fade_start = 45.0;
uniform float u_fade_end = 60.0;

// Final colour, weather included, worked out per vertex: blades are linear gradients, so interpolating
// it is exact and the (overdraw-heavy) fragment stage stays cheap
varying vec3 v_albedo;
// The ground's up in view space: blades are lit like the ground on both faces
varying vec3 v_up;
varying float v_h;

float hash11(float n) {
	return fract(sin(n * 12.9898) * 43758.5453);
}

void vertex() {
	float h = UV.y;
	float tuft = hash11(float(INSTANCE_ID));
	float shade = 1.0 + (tuft * 2.0 - 1.0) * u_color_variation;

	// Per-tuft camera distance, from the view-space origin (precise near the camera, unlike world
	// coordinates ~40 km out)
	float dist = length(MODELVIEW_MATRIX[3].xyz);
	VERTEX *= 1.0 - smoothstep(u_fade_start, u_fade_end, dist);
	// Poor biome health thins the grass (per tuft) and cools its colour
	float health = eden_health_at(MODEL_MATRIX[3].xyz - eden_weather_planet.xyz);
	VERTEX *= step(fract(sin(dot(MODEL_MATRIX[3].xyz, vec3(12.9898, 78.233, 37.719))) * 43758.5453), eden_health_keep(health));
	// Buried by snow: tufts sink as it piles up (the terrain rises by the same depth)
	vec4 weather = eden_weather_near(MODEL_MATRIX[3].xyz);
	VERTEX.y -= weather.g * eden_weather_params.x * 1.6;
	v_albedo = mix(u_base_color, u_tip_color, u_base_lift + (1.0 - u_base_lift) * h) * shade;
	// Seasons (EdenCalendar): a fresh flush in spring, browning in autumn, dormant straw and a little shorter in winter
	vec3 gs = eden_grass_state_tropical(eden_grass_state(eden_season_here(MODEL_MATRIX[3].xyz, eden_weather_planet.xyz)),
			eden_tropical_season(MODEL_MATRIX[3].xyz - eden_weather_planet.xyz, eden_calendar, eden_calendar_axis.xyz));
	v_albedo = eden_seasonal_green(v_albedo, gs);
	v_albedo = eden_health_tint(v_albedo, health);
	VERTEX.y *= 1.0 - 0.3 * gs.x;
	// (Lying snow, storms' and the season's, comes from the weather map below: tufts sink into it)
	v_albedo = mix(v_albedo, eden_snow_color(), weather.g * h) * (1.0 - 0.9 * eden_weather_params.y * weather.b);

	float wind_fade = 1.0 - smoothstep(u_wind_fade_start, u_wind_fade_end, dist);
	if (wind_fade > 0.0) {
		VERTEX += eden_wind_offset(MODEL_MATRIX, h * h, tuft, u_wind_strength, 0.035, TIME) * wind_fade;
	}
	v_up = normalize((MODELVIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
	v_h = h;
}

void fragment() {
	ALBEDO = v_albedo;
	// Set here, after the engine has turned back faces' normals toward the camera: mostly the ground's normal on
	// both faces (no black undersides), with a little of the blade's own facing for depth
	NORMAL = normalize(mix(v_up, NORMAL, u_blade_shading));
	AO = mix(u_root_occlusion, 1.0, v_h);
	AO_LIGHT_AFFECT = 0.25;
	BACKLIGHT = v_albedo * u_translucency;
	ROUGHNESS = 0.85;
	SPECULAR = 0.15;
}
