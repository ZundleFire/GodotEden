// Wind for foliage shaders, from EdenAmbience's globals (declared in project.godot [shader_globals]):
//   eden_wind      xyz = wind direction (world, at the camera), w = speed in m/s (gusts included)
//   eden_wind_flow x = how far the gust pattern has travelled (m, wraps at 4800 m), y = gustiness 0..1
// Without an EdenAmbience in the scene (both zero) plants sway in a light breeze of their own.

global uniform vec4 eden_wind;
global uniform vec4 eden_wind_flow;

// Model-space vertex offset for plant sway. `weight` is how much this vertex bends (0 at the root, 1 at the tip,
// usually height squared), `seed` 0..1 per instance, `strength` the lean in metres at 10 m/s, `flutter` the
// amplitude (m) of the quick per-instance shiver. The lean follows the wind; gust bands (16 m and 48 m long, a
// divisor of the 4800 m wrap) travel downwind across the field, so neighbouring plants bend together.
vec3 eden_wind_offset(mat4 model, float weight, float seed, float strength, float flutter, float time) {
	mat3 m = mat3(model);
	vec3 up = normalize(m[1]);
	vec3 dir = eden_wind.xyz;
	float speed = eden_wind.w;
	float flow = eden_wind_flow.x;
	float gustiness = eden_wind_flow.y;
	if (dot(dir, dir) < 0.25) {
		// No ambience: a slow breeze turning through the day
		float a = time * 0.01;
		dir = vec3(cos(a), 0.0, sin(a));
		speed = 3.0;
		flow = time * 3.0;
		gustiness = 0.5;
	}
	dir -= up * dot(dir, up);
	dir = dot(dir, dir) > 1e-6 ? normalize(dir) : normalize(cross(up, vec3(0.0, 0.0, 1.0)));
	vec3 side = cross(up, dir);
	vec3 p = model[3].xyz;
	float along = dot(p, dir) - flow;
	float across = dot(p, side);
	float g1 = 0.5 + 0.5 * sin(along * (6.2831853 / 16.0) + 1.5 * sin(across * 0.13));
	float g2 = 0.5 + 0.5 * sin(along * (6.2831853 / 48.0) + across * 0.05);
	float gust = mix(0.6, g1 * g2 * 2.2, clamp(gustiness, 0.0, 1.0));
	float k = clamp(speed / 10.0, 0.0, 1.6);
	float lean = strength * k * (0.35 + 0.65 * gust);
	float shiver = flutter * (0.3 + k) * sin(seed * 6.2831853 + time * (2.5 + 5.0 * k));
	vec3 w = (dir * (lean + 0.4 * shiver) + side * shiver) * weight;
	// Bending, not stretching: the tip comes down a little as it leans over
	w -= up * dot(w, w) * 0.6;
	// World metres to model space (rotation and uniform scale)
	return transpose(m) * w / max(dot(m[0], m[0]), 1e-6);
}
