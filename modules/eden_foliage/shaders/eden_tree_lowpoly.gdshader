shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

#include "res://shaders/eden_weather.gdshaderinc"
#include "res://shaders/eden_wind.gdshaderinc"
#include "res://shaders/eden_season_globals.gdshaderinc"
#include "res://shaders/eden_health.gdshaderinc"

// Low-poly procedural trees from EdenTreeMultiMesh: flat part colours baked into vertex COLOR,
// mesh origin at the trunk base (+Y up).

uniform float u_wind_strength : hint_range(0.0, 1.0) = 0.25; // metres of lean at 10 m height in a 10 m/s wind
uniform float u_wind_speed : hint_range(0.0, 5.0) = 0.8;
// Camera distances (m), set by EdenFoliage: sway stops over this range
uniform float u_wind_fade_start = 60.0;
uniform float u_wind_fade_end = 90.0;
// Camera distance ring (m) where this item's instances draw (EdenFoliage far tiers): whole instances outside it
// collapse to a point, so neighbouring tiers hand over exactly at the ring edge instead of per terrain chunk
uniform vec2 u_ring = vec2(0.0, 1e9);
// Leaves follow the seasons (EdenCalendar): turn in autumn, fall, regrow in spring. Off for evergreens and rocks.
uniform bool u_deciduous = false;
// Plants (thin out and cool with poor biome health); off for rocks and wood
uniform bool u_living = true;
// Leaves a bare tree keeps (dead, brown), so a winter crown isn't just a trunk
uniform float u_winter_leaves : hint_range(0.0, 1.0) = 0.12;

// Vertex colour with weather applied. Faces are flat (own vertices), so the snow cap worked out per
// vertex is per face, at no fragment cost.
varying vec3 v_albedo;
// Per face: a random number for leaf faces (which fall when it is below the tree's leaf loss), 2 for the rest
varying flat float v_leaf;
varying flat float v_fallen;

float hash13(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yzx + 33.33);
	return fract((p.x + p.y) * p.z);
}

void vertex() {
	float dist = length(MODELVIEW_MATRIX[3].xyz);
	if (dist < u_ring.x || dist >= u_ring.y) {
		VERTEX = vec3(0.0);
	}
	float health = u_living ? eden_health_at(MODEL_MATRIX[3].xyz - eden_weather_planet.xyz) : 1.0;
	if (hash13(MODEL_MATRIX[3].xyz + vec3(0.37)) > eden_health_keep(health)) {
		VERTEX = vec3(0.0); // this plant died back with the biome
	}
	vec4 weather = eden_weather(MODEL_MATRIX[3].xyz);
	v_albedo = COLOR.rgb;
	v_leaf = 2.0;
	v_fallen = 0.0;
	// Leaves by colour: green or yellow-green foliage, not bark (brown, where red leads) or birch bark (grey-white)
	bool leaf = COLOR.g > COLOR.b + 0.08 && COLOR.r < COLOR.g * 1.1;
	if (u_deciduous && leaf && eden_calendar.w > 0.5) {
		float tree = hash13(MODEL_MATRIX[3].xyz);
		// Each tree a little ahead or behind the others
		vec2 season = eden_season_here(MODEL_MATRIX[3].xyz, eden_weather_planet.xyz);
		season.x = fract(season.x + (tree - 0.5) * 0.05);
		vec3 ls = eden_leaf_state(season);
		float face = hash13(VERTEX * 7.31 + tree * 17.0);
		vec3 hue = tree < 0.35 ? vec3(0.86, 0.42, 0.1) : (tree < 0.6 ? vec3(0.72, 0.18, 0.08) : vec3(0.9, 0.72, 0.16));
		v_albedo = mix(v_albedo, hue * (0.8 + 0.4 * face), ls.x);
		v_albedo = mix(v_albedo, v_albedo * vec3(1.05, 1.15, 0.8) + vec3(0.03, 0.06, 0.0), ls.z);
		// What's left on a bare tree is dead and brown
		v_albedo = mix(v_albedo, vec3(0.36, 0.26, 0.16) * (0.8 + 0.4 * face), smoothstep(0.7, 1.0, ls.y));
		v_leaf = face;
		v_fallen = ls.y * (1.0 - u_winter_leaves);
	}
	// Fruit (vertex alpha 0.5): away from the tropics it ripens from early summer and is gone by mid autumn (none in
	// winter or spring); in the tropics, where summer and winter are barely felt, it follows the wet season instead
	if (u_deciduous && COLOR.a < 0.75 && eden_calendar.w > 0.5) {
		vec3 rel = MODEL_MATRIX[3].xyz - eden_weather_planet.xyz;
		vec2 season = eden_season_here(MODEL_MATRIX[3].xyz, eden_weather_planet.xyz);
		float temperate = smoothstep(0.26, 0.34, season.x) * (1.0 - smoothstep(0.58, 0.68, season.x));
		float tropical = smoothstep(-0.3, 0.2, eden_tropical_season(rel, eden_calendar, eden_calendar_axis.xyz));
		float on = mix(tropical, temperate, smoothstep(0.1, 0.4, season.y));
		v_leaf = hash13(VERTEX * 5.17 + MODEL_MATRIX[3].xyz * 0.01);
		v_fallen = 1.0 - on;
	}
	v_albedo = eden_health_tint(v_albedo, health);
	v_albedo *= 1.0 - 0.75 * eden_weather_params.y * weather.b;
	if (weather.g > 0.0) {
		// Snow caps the upward-facing facets (branches, crowns, rock tops)
		float upness = smoothstep(0.25, 0.7, dot(normalize(mat3(MODEL_MATRIX) * NORMAL), eden_weather_up(MODEL_MATRIX[3].xyz)));
		v_albedo = mix(v_albedo, eden_snow_color(), smoothstep(0.05, 0.5, weather.g) * upness);
	}
	float wind = 1.0 - smoothstep(u_wind_fade_start, u_wind_fade_end, dist);
	if (wind > 0.0) {
		// Quadratic in height: the base stays planted, the crown leans with the wind (EdenAmbience's direction and
		// gusts) and shivers a little
		float h = max(VERTEX.y, 0.0) * 0.1;
		float seed = fract(dot(MODEL_MATRIX[3].xyz, vec3(0.031, 0.023, 0.037)));
		VERTEX += eden_wind_offset(MODEL_MATRIX, h * h * wind, seed, u_wind_strength, u_wind_strength * 0.25, TIME * u_wind_speed);
	}
}

// Within this distance of the camera the tree dithers out (a third-person camera pushed into a crown or trunk)
uniform float u_near_fade = 1.6;

void fragment() {
	float near = -VERTEX.z / u_near_fade;
	if (near < 1.0) {
		// 4x4 ordered dither: screen-door transparency, no sorting
		const float BAYER[16] = { 0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0 };
		ivec2 f = ivec2(FRAGCOORD.xy) & ivec2(3);
		if (near < (BAYER[f.y * 4 + f.x] + 0.5) / 16.0) {
			discard;
		}
	}
	if (v_leaf < v_fallen) {
		discard; // a fallen leaf
	}
	ALBEDO = v_albedo;
	ROUGHNESS = 0.9;
	SPECULAR = 0.2;
}
