// Biome health for foliage shaders (EdenFoliage.biome_health_map): one texel per zone cell on a latitude/longitude
// grid (cell size u_health_cell radians; lat cells -rows/2 .. rows/2-1 from the south; lon cells from -PI), red =
// health 0..1. Unset: everything healthy. Living plants thin out and cool toward grey-green as health falls.

uniform sampler2D u_health_map : filter_linear, repeat_enable;
uniform bool u_health_enabled = false;
uniform float u_health_cell = 0.05;

float eden_health_at(vec3 planet_relative) {
	if (!u_health_enabled) {
		return 1.0;
	}
	vec3 n = normalize(planet_relative);
	float lat = asin(clamp(n.y, -1.0, 1.0));
	float lon = atan(n.x, n.z);
	vec2 size = vec2(textureSize(u_health_map, 0));
	vec2 uv = vec2((lon + 3.14159265) / u_health_cell / size.x, (lat / u_health_cell + size.y * 0.5) / size.y);
	return texture(u_health_map, uv).r;
}

// Share of a healthy stand still standing: all of it down to "Healthy" (0.65), a thinning stand through Stressed and
// Degraded, nearly nothing when Collapsing
float eden_health_keep(float health) {
	return smoothstep(0.05, 0.65, health);
}

// Colour temperature tracks health: thriving keeps its colour, collapsing goes cool grey-green
vec3 eden_health_tint(vec3 albedo, float health) {
	float grey = dot(albedo, vec3(0.3, 0.55, 0.15));
	vec3 cold = vec3(grey) * vec3(0.84, 0.93, 0.9);
	return mix(albedo, cold, (1.0 - smoothstep(0.15, 0.65, health)) * 0.75);
}
