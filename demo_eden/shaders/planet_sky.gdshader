shader_type sky;

// Planetary atmosphere sky. Renders Rayleigh + Mie single scattering from wherever the
// camera actually is, so the sky gradient, horizon reddening and sun colour all fall out of
// the camera's position on (or above) the planet rather than being authored per-time-of-day.
//
// Why a sky shader and not a mesh dome: Godot's sky shaders receive POSITION = the camera's
// world position (servers/rendering/shader_types.cpp, SHADER_SKY built-ins), which is the one
// input this whole model needs. It also renders into the radiance cubemap, so setting the
// Environment to ambient_light_source = AMBIENT_SOURCE_SKY makes terrain ambient light track
// the sky for free -- ground bounce goes warm at sunset with no extra work.
//
// Driven by planet_atmosphere.gd, which owns sun_direction and mirrors sun_transmittance()
// on the CPU to tint the DirectionalLight3D to match.

#include "res://shaders/atmosphere_common.gdshaderinc"

// Far-field ground disc. Terrain geometry only extends to the voxel view distance; past that
// the sky shader's own ground term is what fills the gap out to the true horizon, which on a
// 40km planet is much further than the meshed terrain reaches.
uniform vec3 ground_albedo : source_color = vec3(0.11, 0.10, 0.09);
// Lit ground is deliberately dim -- it is a backdrop behind real terrain, not a shading model.
uniform float ground_light_scale : hint_range(0.0, 2.0) = 0.15;

uniform vec3 space_color : source_color = vec3(0.0, 0.0, 0.0);

// Stars. Cheap hash field on the view direction, so no texture and no seams.
uniform float star_intensity : hint_range(0.0, 4.0) = 1.0;
uniform float star_density : hint_range(0.0, 0.2) = 0.02;
uniform float star_scale : hint_range(50.0, 2000.0) = 400.0;
// Stars are physically still there at noon; a real eye just cannot resolve them against a sky
// four orders of magnitude brighter. With no auto-exposure in the pipeline that has to be
// faked, or the daytime sky comes out visibly speckled. Higher = stars vanish sooner.
uniform float star_daylight_fade : hint_range(0.0, 400.0) = 80.0;

// Final linear-space multiplier. The demos tonemap with ACES, so this stays HDR.
uniform float exposure : hint_range(0.0, 8.0) = 1.0;

// Viewport size in pixels, used to turn SCREEN_UV into pixel coordinates for the dither.
// Must be a vec2: scaling both axes by one scalar makes the noise vary at different rates
// horizontally and vertically, which smears the dither into visible horizontal streaks
// instead of breaking up the bands. Sky shaders get no viewport-size built-in, so the
// controller feeds it.
uniform vec2 dither_resolution = vec2(1920.0, 1080.0);

// Limb darkening exponent for the sun disc -- 0 is a flat disc, higher dims the rim.
uniform float sun_limb_darkening : hint_range(0.0, 1.0) = 0.4;

// Brightness of the visible disc, as a multiple of sun_intensity. Deliberately decoupled:
// sun_intensity is calibrated so the SKY looks right, but a real solar disc is orders of
// magnitude brighter than the sky around it. Sharing one value makes the disc vanish into the
// horizon glow at sunset -- exactly when you most want to see it -- because the same low
// transmittance that reddens the sky also extinguishes the disc.
uniform float sun_disc_brightness : hint_range(1.0, 200.0) = 30.0;

float hash31(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.yzx + 33.33);
	return fract((p.x + p.y) * p.z);
}

// Point stars: one candidate per cell of a 3D lattice laid over the view direction, jittered
// inside its cell. Using the direction vector (not a UV) means no pole pinching and no seam.
float star_field(vec3 rd) {
	vec3 p = rd * star_scale;
	vec3 cell = floor(p);
	vec3 f = p - cell;

	float present = hash31(cell);
	// step() rather than an if, to keep the warp coherent.
	float alive = step(1.0 - star_density, present);

	vec3 jitter = vec3(hash31(cell + 11.0), hash31(cell + 23.0), hash31(cell + 37.0));
	float d = length(f - jitter);
	float point_mask = smoothstep(0.35, 0.0, d);

	// Vary brightness so the field does not read as a uniform grid.
	float brightness = 0.25 + 0.75 * hash31(cell + 59.0);
	return alive * point_mask * brightness;
}

void sky() {
	vec3 ro = POSITION - planet_center;
	vec3 rd = EYEDIR;

	float t_start;
	float t_end;
	bool hit_ground;
	bool in_atmosphere = atmosphere_ray_bounds(ro, rd, -1.0, t_start, t_end, hit_ground);

	vec3 inscatter = vec3(0.0);
	vec3 transmittance = vec3(1.0);
	if (in_atmosphere) {
		// Per-pixel dither on the sample positions. The radiance cubemap pass has no stable
		// screen space, so it takes plain midpoint sampling -- it only feeds ambient light,
		// where banding cannot be seen anyway.
		float jitter = AT_CUBEMAP_PASS
				? 0.5
				: interleaved_gradient_noise(SCREEN_UV * dither_resolution);
		atmosphere_scatter(ro, rd, t_start, t_end, jitter, inscatter, transmittance);
	}

	// Whatever sits behind the atmosphere shell along this ray.
	vec3 background;
	if (hit_ground) {
		vec3 surface = ro + rd * t_end;
		vec3 n = normalize(surface);
		float ndl = max(dot(n, sun_direction), 0.0);
		background = ground_albedo * ndl * sun_transmittance(surface) * sun_intensity * ground_light_scale;
	} else {
		background = space_color;

		// The cubemap pass feeds ambient light. Skipping the sun disc there keeps a single
		// very bright texel from blowing out the whole ambient term; the scattering integral
		// already carries the sun's contribution to ambient.
		if (!AT_CUBEMAP_PASS) {
			float sky_luminance = dot(inscatter, vec3(0.2126, 0.7152, 0.0722));
			float star_fade = 1.0 / (1.0 + sky_luminance * star_daylight_fade);
			background += star_field(rd) * star_intensity * star_fade;

			float mu = clamp(dot(rd, sun_direction), -1.0, 1.0);
			float angle = acos(mu);
			float disc = 1.0 - smoothstep(sun_angular_radius * 0.9, sun_angular_radius, angle);
			if (disc > 0.0) {
				// Limb darkening: normalised radius across the disc, dimmed toward the rim.
				float r = clamp(angle / max(sun_angular_radius, 1e-5), 0.0, 1.0);
				float limb = pow(max(1.0 - r * r, 0.0), sun_limb_darkening * 0.5);
				background += disc * limb * sun_tint * sun_intensity * sun_disc_brightness;
			}
		}
	}

	COLOR = (background * transmittance + inscatter) * exposure;
}
