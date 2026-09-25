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

vec4 eden_weather(vec3 world_pos) {
	if (eden_weather_planet.w <= 0.0) {
		return vec4(0.0);
	}
	return textureLod(eden_weather_map, eden_weather_uv(world_pos - eden_weather_planet.xyz), 0.0);
}

vec3 eden_weather_up(vec3 world_pos) {
	return normalize(world_pos - eden_weather_planet.xyz);
}

vec3 eden_snow_color() {
	return eden_weather_snow.a > 0.0 ? eden_weather_snow.rgb : vec3(0.74, 0.77, 0.82);
}
