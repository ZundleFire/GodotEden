// Planet weather from EdenAmbience (modules/eden_ambience): an octahedral map around the planet
// centre. R precipitation, G snow cover, B ground wetness, A storm cloud; all 0..1.
// Shaders read it through global uniforms (declared in project.godot [shader_globals]). GPU-driven
// terrain can't bind global textures, so its shader defines EDEN_WEATHER_MATERIAL_UNIFORMS before
// including this and EdenAmbience sets the same three on the terrain's material instead.
// eden_weather_planet.w is 0 when no EdenAmbience runs.
#ifdef EDEN_WEATHER_MATERIAL_UNIFORMS
uniform sampler2D eden_weather_map : filter_linear, repeat_disable, hint_default_black;
uniform vec4 eden_weather_planet = vec4(0.0);
uniform vec4 eden_weather_params = vec4(0.0);
uniform vec4 eden_weather_snow = vec4(0.0);
#else
global uniform sampler2D eden_weather_map : filter_linear, repeat_disable;
global uniform vec4 eden_weather_planet; // xyz centre, w radius
global uniform vec4 eden_weather_params; // x snow depth at full cover (m), y wet darkening
global uniform vec4 eden_weather_snow; // rgb snow colour (a = 0: default)
#endif

// Octahedral mapping of a direction from the planet centre (needn't be normalized): no trig, since
// this runs per vertex over millions of grass blades. Matches EdenWeatherSim::dir_to_uv().
vec2 eden_weather_uv(vec3 d) {
	d /= abs(d.x) + abs(d.y) + abs(d.z);
	vec2 e = d.xz;
	if (d.y < 0.0) {
		e = (1.0 - abs(d.zx)) * vec2(d.x >= 0.0 ? 1.0 : -1.0, d.z >= 0.0 ? 1.0 : -1.0);
	}
	return e * 0.5 + 0.5;
}

// eden_weather_params.w: 0 while no snow or wet ground exists anywhere on the map -- skip the lookup
vec4 eden_weather(vec3 world_pos) {
	if (eden_weather_planet.w <= 0.0 || eden_weather_params.w < 0.5) {
		return vec4(0.0);
	}
	return textureLod(eden_weather_map, eden_weather_uv(world_pos - eden_weather_planet.xyz), 0.0);
}

// For things only drawn near the camera (grass, plants): eden_weather_params.z is 0 while there is no snow or
// wet ground within ~2 km, and most frames need no lookup at all (it cost ~2 ms over a meadow's grass)
vec4 eden_weather_near(vec3 world_pos) {
	if (eden_weather_params.z < 0.5) {
		return vec4(0.0);
	}
	return eden_weather(world_pos);
}

vec3 eden_weather_up(vec3 world_pos) {
	return normalize(world_pos - eden_weather_planet.xyz);
}

vec3 eden_snow_color() {
	return eden_weather_snow.a > 0.0 ? eden_weather_snow.rgb : vec3(0.74, 0.77, 0.82);
}
