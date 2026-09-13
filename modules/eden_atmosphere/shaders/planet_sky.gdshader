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

// ---------------------------------------------------------------------------------------
// Deep space panorama
// ---------------------------------------------------------------------------------------
// Two equirectangular slots, so a starfield and a nebula can be layered independently and
// cross-faded when moving between systems. Both use Godot's own panorama convention (see
// dir_to_panorama_uv), so anything authored against PanoramaSkyMaterial drops straight in.
//
// Left empty, the procedural star field below is the fallback -- so the sky works with no art.
uniform sampler2D space_panorama : source_color, filter_linear_mipmap, repeat_enable, hint_default_black;
uniform bool space_panorama_enabled = false;
uniform sampler2D space_overlay : source_color, filter_linear_mipmap, repeat_enable, hint_default_black;
uniform bool space_overlay_enabled = false;

uniform vec3 space_tint : source_color = vec3(1.0, 1.0, 1.0);
// Defaults to 8: HDR space panoramas are authored at physical-ish levels (the supplied set
// averages ~0.023 luminance) and are simply invisible after tonemapping at 1.0.
uniform float space_intensity : hint_range(0.0, 64.0) = 8.0;
uniform float space_overlay_intensity : hint_range(0.0, 64.0) = 8.0;
// Same trick as star_daylight_fade: with no eye adaptation in the pipeline, a photographed
// starfield/nebula panorama would otherwise stay just as visible at noon as at midnight. 0
// disables fading for that slot; the two fade independently so an overlay nebula can vanish
// well before the base starfield does.
uniform float space_daylight_fade : hint_range(0.0, 400.0) = 60.0;
uniform float space_overlay_daylight_fade : hint_range(0.0, 400.0) = 60.0;

// The celestial sphere is fixed in space while the planet turns, so from the ground the stars
// must wheel overhead through the night. The controller drives this from the time of day; with
// it at zero the stars sit frozen while the sun moves, which reads as badly wrong at night.
uniform vec3 space_rotation_axis = vec3(0.0, 1.0, 0.0);
uniform float space_rotation_angle = 0.0;
// Independent of space_rotation_angle, so the overlay (e.g. a nebula) can sit at its own angle
// relative to the base starfield rather than being locked to the same orientation.
uniform float space_overlay_rotation_angle = 0.0;

// Samples an equirect panorama without a seam.
//
// The u coordinate wraps from 1 back to 0 down one meridian. Screen-space derivatives across
// that line come out near +/-1 instead of near zero, so the hardware picks the smallest mip and
// draws a blurred vertical stripe down the sky. Subtracting the nearest whole turn puts the
// derivative back on the correct small value while preserving its magnitude, which a plain
// fract() clamp would not.
vec3 sample_panorama(sampler2D tex, vec3 dir) {
	vec2 uv = dir_to_panorama_uv(dir);
	vec2 dx = dFdx(uv);
	vec2 dy = dFdy(uv);
	dx.x -= round(dx.x);
	dy.x -= round(dy.x);
	return textureGrad(tex, uv, dx, dy).rgb;
}

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

// ---------------------------------------------------------------------------------------
// Moon
// ---------------------------------------------------------------------------------------
uniform vec3 moon_direction = vec3(0.0, -1.0, 0.0);
uniform vec3 moon_tint : source_color = vec3(0.85, 0.90, 1.0);
uniform float moon_intensity : hint_range(0.0, 10.0) = 1.0;
uniform float moon_angular_radius : hint_range(0.001, 0.2) = 0.025;
uniform float moon_disc_brightness : hint_range(0.1, 200.0) = 2.5;
uniform float moon_limb_darkening : hint_range(0.0, 1.0) = 0.3;
uniform float moon_daylight_fade : hint_range(0.0, 400.0) = 50.0;

// Moon surface. A photo of the moon filling a square image, or -- with moon_texture_equirect --
// an equirectangular surface map wrapped on the sphere with its centre facing the planet.
// Defaults to white, so with no texture the moon is a plain sphere and phases still work.
uniform sampler2D moon_texture : source_color, filter_linear_mipmap, repeat_enable, hint_default_white;
uniform bool moon_texture_equirect = false;
uniform float moon_texture_rotation = 0.0;
// Sunlight bounced off the planet onto the moon's night side.
uniform float moon_earthshine : hint_range(0.0, 0.5) = 0.03;
// The moon's orbital plane normal. Used as its "up", so a tidally locked moon keeps a
// fixed attitude against its orbit instead of spinning as it crosses the sky.
uniform vec3 moon_orbit_normal = vec3(0.0, 1.0, 0.0);
// Phase-curved brightness pushed by EdenPlanetAtmosphere (new moon 0, full 1).
uniform float moon_phase_brightness = 1.0;

// ---------------------------------------------------------------------------------------
// Glow and halos
// ---------------------------------------------------------------------------------------
uniform float sun_glow_intensity : hint_range(0.0, 8.0) = 0.6;
uniform float sun_glow_size : hint_range(0.005, 1.0) = 0.12;
uniform float sun_corona_intensity : hint_range(0.0, 16.0) = 1.5;
uniform float sun_halo_intensity : hint_range(0.0, 4.0) = 0.0;
uniform float moon_glow_intensity : hint_range(0.0, 8.0) = 0.5;
uniform float moon_glow_size : hint_range(0.005, 1.0) = 0.08;
uniform float moon_halo_intensity : hint_range(0.0, 4.0) = 0.05;
uniform float halo_radius = 0.3840;
uniform float halo_width = 0.018;

// 22-degree ice-crystal halo. Hexagonal plates refract red least, so the ring has a reddish inner
// edge and fades bluish outward. The offset is kept small (0.35 of the width): real halos are mostly
// white, and a wider split reads as a rainbow ring instead.
vec3 halo_ring(float angle) {
	float w = max(halo_width, 1e-4);
	float r = (angle - (halo_radius - w * 0.35)) / w;
	float g = (angle - halo_radius) / w;
	float b = (angle - (halo_radius + w * 0.35)) / w;
	return vec3(exp(-r * r), exp(-g * g), exp(-b * b));
}

// Moon disc geometry for view ray rd.
//   l        : position on the disc, limb at radius 1 (clamped inside for the AA fringe)
//   z        : sphere height at l, i.e. cosine of the angle between normal and viewer
//   n        : world-space surface normal
//   coverage : antialiased disc mask
//   uv       : texture coordinate for the current texture mode
void moon_geometry(vec3 rd, out vec2 l, out float z, out vec3 n, out float coverage, out vec2 uv) {
	vec3 m = moon_direction;
	vec3 up_ref = abs(dot(moon_orbit_normal, m)) > 0.99 ? vec3(1.0, 0.0, 0.0) : moon_orbit_normal;
	// cross(m, up) rather than cross(up, m): the other order puts "right" on the left of the
	// screen and mirrors the texture.
	vec3 right = normalize(cross(m, up_ref));
	vec3 up = cross(right, m);

	// Gnomonic projection onto the tangent plane, scaled so the limb lands at radius 1.
	float cos_v = dot(rd, m);
	vec2 local = vec2(dot(rd, right), dot(rd, up)) / (max(cos_v, 1e-4) * tan(moon_angular_radius));
	float r = length(local);
	float aa = max(fwidth(r), 1e-4);
	coverage = cos_v > 0.0 ? 1.0 - smoothstep(1.0 - aa, 1.0, r) : 0.0;

	l = local / max(r, 1.0);
	z = sqrt(max(1.0 - dot(l, l), 0.0));
	n = normalize(right * l.x + up * l.y - m * z);

	float c = cos(moon_texture_rotation);
	float s = sin(moon_texture_rotation);
	if (moon_texture_equirect) {
		// Moon-local direction: x right, y up, z toward the planet. Longitude offset rotates
		// about y. The near side lands mid-map and the wrap seam sits on the far side, which a
		// tidally locked moon never shows.
		vec3 d = vec3(l.x, l.y, z);
		d = vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
		uv = vec2(0.5 + atan(d.x, d.z) / 6.28318530718, acos(clamp(d.y, -1.0, 1.0)) / 3.14159265359);
	} else {
		vec2 q = vec2(c * l.x - s * l.y, s * l.x + c * l.y);
		uv = vec2(q.x, -q.y) * 0.5 + 0.5;
	}
}

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

	// Moon geometry, computed HERE rather than inside the `!hit_ground` branch below. Its texture
	// lookup needs screen-space derivatives, and derivatives are undefined inside control flow
	// that varies per pixel -- down there they would pick garbage mip levels at the disc edge.
	// AT_CUBEMAP_PASS is constant for the whole pass, so gating on it is safe.
	vec2 moon_l = vec2(0.0);
	float moon_z = 0.0;
	vec3 moon_n = vec3(0.0);
	float moon_coverage = 0.0;
	vec2 moon_uv = vec2(0.0);
	vec2 moon_uv_dx = vec2(0.0);
	vec2 moon_uv_dy = vec2(0.0);
	if (!AT_CUBEMAP_PASS) {
		moon_geometry(rd, moon_l, moon_z, moon_n, moon_coverage, moon_uv);
		moon_uv_dx = dFdx(moon_uv);
		moon_uv_dy = dFdy(moon_uv);
		// Equirect mode wraps u on the far side; keep a wrap from reading as a huge derivative.
		moon_uv_dx.x -= round(moon_uv_dx.x);
		moon_uv_dy.x -= round(moon_uv_dy.x);
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

		// Drives both the panorama fade below and the star fade further down -- computed once
		// here since inscatter is already available regardless of AT_CUBEMAP_PASS.
		float sky_luminance = dot(inscatter, vec3(0.2126, 0.7152, 0.0722));

		// The cubemap pass feeds ambient light. Skipping the sun disc there keeps a single
		// very bright texel from blowing out the whole ambient term; the scattering integral
		// already carries the sun's contribution to ambient.
		// Deep space. Included in the cubemap pass, unlike the sun disc and the procedural
		// stars: a bright nebula is a broad, low-frequency source that genuinely should tint
		// ambient light at night, and it cannot blow out a single texel the way the disc can.
		// Both slots fade with sky_luminance for the same reason the stars do below: without it
		// a photographed panorama reads as a wash of colour over the daylit sky.
		if (space_panorama_enabled || space_overlay_enabled) {
			vec3 space = vec3(0.0);
			if (space_panorama_enabled) {
				// Rotating the sampling direction by the negative angle makes the sky itself
				// appear to rotate the positive way, i.e. with the sun.
				vec3 sky_dir = rotate_about(rd, normalize(space_rotation_axis), -space_rotation_angle);
				float fade = 1.0 / (1.0 + sky_luminance * space_daylight_fade);
				space += sample_panorama(space_panorama, sky_dir) * space_intensity * fade;
			}
			if (space_overlay_enabled) {
				vec3 overlay_dir = rotate_about(rd, normalize(space_rotation_axis), -space_overlay_rotation_angle);
				float overlay_fade = 1.0 / (1.0 + sky_luminance * space_overlay_daylight_fade);
				space += sample_panorama(space_overlay, overlay_dir) * space_overlay_intensity * overlay_fade;
			}
			background += space * space_tint;
		}

		if (!AT_CUBEMAP_PASS) {
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

			// Sun glow, corona and halo. In `background`, so they are extinguished and reddened
			// along the view ray exactly like the disc. Scaled by the planet's shadow so a sun below
			// the horizon cannot shine a glow up through the ground.
			float sun_vis = planet_shadow(ro);
			float sun_glow = exp(-angle / max(sun_glow_size, 1e-4)) * sun_glow_intensity;
			float corona = exp(-max(angle - sun_angular_radius, 0.0) / max(sun_angular_radius * 0.75, 1e-5)) * sun_corona_intensity;
			background += sun_tint * ((sun_glow + corona) * sun_vis);
			if (sun_halo_intensity > 0.0) {
				background += halo_ring(angle) * (sun_halo_intensity * sun_vis);
			}

			// Moon. Shaded as a lit sphere against the real sun direction, so the phase -- crescent,
			// quarter, gibbous, full -- falls out of the lighting instead of needing phase textures,
			// and it is always consistent with where the sun actually is.
			if (moon_coverage > 0.0) {
				vec3 albedo = textureGrad(moon_texture, moon_uv, moon_uv_dx, moon_uv_dy).rgb;
				float mu0 = max(dot(moon_n, sun_direction), 0.0);
				// Lommel-Seeliger rather than Lambert. Real regolith back-scatters, which is why a
				// full moon looks like a flat, evenly bright disc instead of a shaded ball. Lambert
				// would darken the full moon toward its limb like a billiard ball.
				float moon_reflect = min(2.0 * mu0 / max(mu0 + moon_z, 1e-4), 1.5);
				float limb = pow(max(moon_z, 1e-4), moon_limb_darkening);
				// Outside the atmosphere, so lit by raw sunlight, not by sun_transmittance().
				vec3 incident = sun_tint * moon_reflect + vec3(moon_earthshine);
				float moon_fade = 1.0 / (1.0 + sky_luminance * moon_daylight_fade);
				vec3 moon_col = albedo * moon_tint * incident * limb * moon_intensity * moon_disc_brightness * moon_fade;
				// mix, not add: the moon is opaque and hides the stars, panorama and even the sun disc
				// behind it. Its dark side therefore shows black here and becomes plain sky once the
				// inscatter is added on top -- exactly how the daytime moon looks.
				background = mix(background, moon_col, moon_coverage);
			}

			// Moon glow and halo. Scaled by phase -- a new moon has neither -- and by the same daylight
			// fade as the disc.
			if (moon_glow_intensity > 0.0 || moon_halo_intensity > 0.0) {
				float moon_angle = acos(clamp(dot(rd, moon_direction), -1.0, 1.0));
				float moon_vis = planet_shadow_dir(ro, moon_direction) * moon_phase_brightness
						/ (1.0 + sky_luminance * moon_daylight_fade);
				float moon_glow = exp(-moon_angle / max(moon_glow_size, 1e-4)) * moon_glow_intensity;
				background += moon_tint * (moon_glow * moon_vis);
				background += halo_ring(moon_angle) * (moon_halo_intensity * moon_vis);
			}
		}
	}

	COLOR = (background * transmittance + inscatter) * exposure;
}
