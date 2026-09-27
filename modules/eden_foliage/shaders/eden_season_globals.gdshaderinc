// Seasons for foliage shaders, from EdenAmbience's globals (declared in project.godot [shader_globals]).
#include "res://shaders/eden_season.gdshaderinc"

global uniform vec4 eden_calendar;
global uniform vec4 eden_calendar_axis;

// The season (see eden_season) at a world position; the planet centre comes from the weather globals
vec2 eden_season_here(vec3 world_pos, vec3 planet_centre) {
	return eden_season(world_pos - planet_centre, eden_calendar, eden_calendar_axis.xyz);
}
