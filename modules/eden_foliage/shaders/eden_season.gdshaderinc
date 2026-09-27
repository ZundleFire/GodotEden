// Seasons for planet shaders (EdenCalendar -> EdenAmbience):
//   calendar: x = the year's phase (0 = northern spring equinox, 0.25 northern summer solstice ...), y = seasonal
//   strength, z = tropical wet/dry season strength, w = 1 when a calendar runs (0: no seasons, always summer)
//   axis: xyz = the planet's spin axis
// Each place has its own season: the southern hemisphere is half a year off the northern, and near the equator
// seasons are barely felt. The terrain passes these as material uniforms; foliage includes eden_season_globals.

// x = the local season's phase (0 spring equinox, 0.25 midsummer, 0.5 autumn equinox, 0.75 midwinter),
// y = how strongly seasons are felt here (0 at the equator .. 1 toward the poles)
vec2 eden_season(vec3 dir_from_centre, vec4 calendar, vec3 axis) {
	if (calendar.w < 0.5) {
		return vec2(0.3, 0.0);
	}
	float s = dot(normalize(dir_from_centre), normalize(axis));
	float phase = fract(calendar.x + (s < 0.0 ? 0.5 : 0.0));
	return vec2(phase, smoothstep(0.08, 0.5, abs(s)));
}

// The season's change to the climate temperature (0..1 units, 0.3 = freezing) at a direction: the same as the
// weather's (EdenWeatherSim::season_offset), so ground and weather agree on when it freezes
float eden_season_offset(vec3 dir_from_centre, vec4 calendar, vec3 axis) {
	if (calendar.w < 0.5) {
		return 0.0;
	}
	float s = dot(normalize(dir_from_centre), normalize(axis));
	float phase = calendar.x + (s < 0.0 ? 0.5 : 0.0);
	float a = clamp((abs(s) - 0.05) / 0.85, 0.0, 1.0);
	return calendar.y * a * a * (3.0 - 2.0 * a) * sin(6.2831853 * (phase - 0.08));
}

// The climate's annual mean temperature (0..1, 0.3 = freezing) estimated from latitude and height alone: a fit to
// the V4 generator (within ~0.03 per latitude band). For where the real value isn't to hand: far terrain sectors,
// grass tufts.
float eden_estimated_temperature(vec3 dir_from_centre, vec3 axis, float height_above_sea) {
	float s = dot(normalize(dir_from_centre), normalize(axis));
	return clamp(1.02 - s * s - 0.00029 * max(height_above_sea, 0.0), 0.0, 1.0);
}

// Lying winter snow for today's temperature (the climate plus the season): from about -3 °C, full by -6 °C
float eden_winter_snow(float temperature_today) {
	return 1.0 - smoothstep(0.2, 0.26, temperature_today);
}

// Tropical wet and dry seasons (EdenWeatherSim::wet_season_offset): -1 the depth of the dry season .. +1 the height
// of the wet one, zero on the equator and outside the tropics
float eden_tropical_season(vec3 dir_from_centre, vec4 calendar, vec3 axis) {
	if (calendar.w < 0.5) {
		return 0.0;
	}
	float s = dot(normalize(dir_from_centre), normalize(axis));
	float a = abs(s);
	float tropics = smoothstep(0.03, 0.2, a) * (1.0 - smoothstep(0.38, 0.52, a));
	float phase = calendar.x + (s < 0.0 ? 0.5 : 0.0);
	return tropics * clamp(calendar.z / 0.25, 0.0, 1.5) * sin(6.2831853 * (phase - 0.1));
}

// eden_grass_state with the tropics' seasons folded in: browning through the dry season, a green flush in the wet
vec3 eden_grass_state_tropical(vec3 grass_state, float tropical) {
	return vec3(grass_state.x, max(grass_state.y, max(-tropical, 0.0) * 0.8), max(grass_state.z, max(tropical, 0.0) * 0.6));
}

// Deciduous leaves: x = autumn colouring 0..1, y = leaves fallen 0..1, z = young spring green 0..1
vec3 eden_leaf_state(vec2 season) {
	float w = season.x;
	float autumn = smoothstep(0.5, 0.62, w) * (1.0 - smoothstep(0.8, 0.9, w));
	// Falling through late autumn, bare all winter, budding again from the spring equinox
	float fallen = w > 0.62 ? smoothstep(0.62, 0.77, w) : 1.0 - smoothstep(0.0, 0.1, w);
	float young = smoothstep(0.0, 0.08, w) * (1.0 - smoothstep(0.14, 0.28, w));
	return vec3(autumn, fallen, young) * season.y;
}

// Grass and ground plants: x = dormant (straw-coloured, winter) 0..1, y = autumn browning 0..1, z = spring flush 0..1
vec3 eden_grass_state(vec2 season) {
	float w = season.x;
	float dormant = w > 0.5 ? smoothstep(0.66, 0.8, w) : 1.0 - smoothstep(0.02, 0.14, w);
	float browning = smoothstep(0.52, 0.68, w) * (1.0 - smoothstep(0.85, 0.95, w));
	float flush = smoothstep(0.05, 0.12, w) * (1.0 - smoothstep(0.2, 0.35, w));
	return vec3(dormant, browning, flush) * season.y;
}

// A living plant colour through the seasons: fresh in spring, browning in autumn, dull straw in winter
vec3 eden_seasonal_green(vec3 green, vec3 grass_state) {
	vec3 straw = vec3(0.52, 0.45, 0.28) * (0.6 + 0.4 * dot(green, vec3(0.3, 0.6, 0.1)) / 0.35);
	vec3 c = mix(green, green * vec3(1.05, 1.12, 0.85) + vec3(0.02, 0.04, 0.0), grass_state.z);
	c = mix(c, mix(green, straw, 0.6) * vec3(1.15, 0.95, 0.7), grass_state.y * 0.8);
	return mix(c, straw * 0.85, grass_state.x * 0.85);
}
