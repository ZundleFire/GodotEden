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
uniform float space_intensity : hint_range(0.0, 64.0) = 1.6;
uniform float space_overlay_intensity : hint_range(0.0, 64.0) = 1.6;
// Grading for the panoramas, applied after intensity (so in display-ish range) and before the tint.
// black point: subtracted first, to crush the faint noise floor a downscaled HDR panorama carries.
// gamma: power curve pivoting at 1.0 -- above 1 sinks the dim nebula wash while bright stars keep.
// contrast: power curve pivoting at mid-grey 0.18. saturation: 0 greyscale, above 1 more vivid.
// opacity: overall visibility of both slots, 0 hides them without unloading anything.
uniform float space_black_point : hint_range(0.0, 1.0) = 0.05;
uniform float space_gamma : hint_range(0.2, 4.0) = 1.6;
uniform float space_contrast : hint_range(0.2, 4.0) = 1.0;
uniform float space_saturation : hint_range(0.0, 3.0) = 1.0;
uniform float space_opacity : hint_range(0.0, 1.0) = 1.0;
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

// Samples an equirect panorama without a seam or a pole artifact.
//
// Both are the same underlying problem: dir_to_panorama_uv() has two coordinate singularities --
// u wraps at the meridian behind the camera, and BOTH u and v go singular at the poles, where
// longitude is undefined and a one-pixel screen step can swing u across the whole texture width
// for no real change in view direction. Differentiating the (u,v) mapping itself (dFdx/dFdy(uv),
// the obvious approach, and the seam-only fix this used to be) inherits both singularities: at the
// seam it shows up as a wrong-sign derivative (fixable by subtracting the nearest whole turn, as
// still done for the seam below), but at the poles the derivative is not off by a clean integer
// wrap -- it is genuinely unbounded -- and no such correction exists. The result was exactly the
// smeared, dragged-out streak this function now avoids.
//
// The fix: differentiate `dir` itself, not its (u,v) parametrisation. The direction vector has no
// singularity anywhere on the sphere -- poles included -- so its screen-space derivative is a
// stable measure of the true angular footprint of this pixel, which is what determines how much
// the sample should be blurred. That footprint is turned into an explicit LOD and sampled with
// textureLod, sidestepping derivative-based filtering (and its singularities) entirely.
vec3 sample_panorama(sampler2D tex, vec3 dir) {
	vec2 uv = dir_to_panorama_uv(dir);
	float angular_footprint = max(length(dFdx(dir)), length(dFdy(dir)));
	// The u-axis spans a full turn (2*PI) across the texture's width; a footprint that wide would
	// need the coarsest mip (LOD = log2(width)), one that covers a single texel needs the finest.
	float texels = float(textureSize(tex, 0).x);
	float lod = max(log2(max(angular_footprint * texels / 6.28318530718, 1e-6)), 0.0);
	return textureLod(tex, uv, lod).rgb;
}

// Stars. Cheap hash field on the view direction, so no texture and no seams.
uniform float star_intensity : hint_range(0.0, 4.0) = 1.0;
uniform float star_density : hint_range(0.0, 0.2) = 0.02;
uniform float star_scale : hint_range(50.0, 2000.0) = 400.0;
// Star radius as a fraction of its lattice cell. star_scale sets how many cells (so how far apart
// stars can be); this sets how big each one is drawn.
uniform float star_size : hint_range(0.02, 0.5) = 0.35;
// 0 = every star equally bright, 1 = strong spread between faint and bright stars.
uniform float star_brightness_variation : hint_range(0.0, 1.0) = 0.75;
// 0 = all white, 1 = full spread from blue-white to orange.
uniform float star_color_variation : hint_range(0.0, 1.0) = 0.3;
// Scintillation amount and rate. 0 disables it.
uniform float star_twinkle : hint_range(0.0, 1.0) = 0.0;
uniform float star_twinkle_speed : hint_range(0.0, 20.0) = 3.0;
// Mixed into the star hash, so a different seed gives a completely different (but repeatable)
// field instead of reshuffling it randomly every launch.
uniform float random_seed = 0.0;
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
// Sun 2 -- a second, independently coloured sun near the primary one (binary-star look), with its
// own disc/glow/corona. See EdenPlanetAtmosphere's Sun 2 group for the offset that positions it.
// ---------------------------------------------------------------------------------------
uniform bool sun2_enabled = false;
uniform vec3 sun2_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 sun2_tint : source_color = vec3(0.55, 0.70, 1.0);
uniform float sun2_angular_radius : hint_range(0.001, 0.2) = 0.012;
uniform float sun2_disc_brightness : hint_range(1.0, 200.0) = 30.0;
uniform float sun2_glow_intensity : hint_range(0.0, 8.0) = 0.5;
uniform float sun2_glow_size : hint_range(0.005, 1.0) = 0.1;
uniform float sun2_corona_intensity : hint_range(0.0, 16.0) = 1.2;

// ---------------------------------------------------------------------------------------
// Moon
// ---------------------------------------------------------------------------------------
uniform vec3 moon_direction = vec3(0.0, -1.0, 0.0);
uniform vec3 moon_tint : source_color = vec3(0.85, 0.90, 1.0);
uniform float moon_intensity : hint_range(0.0, 10.0) = 1.0;
uniform float moon_angular_radius : hint_range(0.001, 0.2) = 0.05;
uniform float moon_disc_brightness : hint_range(0.1, 200.0) = 2.5;
uniform float moon_limb_darkening : hint_range(0.0, 1.0) = 0.3;
// Grading of the moon's surface texture: contrast around mid-grey, saturation (0 greyscale).
uniform float moon_contrast : hint_range(0.0, 4.0) = 1.0;
uniform float moon_saturation : hint_range(0.0, 3.0) = 1.0;
// 1 = the disc is opaque and hides what is behind it. Lower lets stars and panorama show through.
uniform float moon_opacity : hint_range(0.0, 1.0) = 1.0;
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
// Moon B -- a second moon, on its own independent great-circle orbit (see EdenPlanetAtmosphere's
// Moon B group). Shares moon_geometry_generic() with the primary moon.
// ---------------------------------------------------------------------------------------
uniform bool moonb_enabled = false;
uniform vec3 moonb_direction = vec3(0.0, -1.0, 0.0);
uniform vec3 moonb_tint : source_color = vec3(1.0, 0.95, 0.9);
uniform float moonb_intensity : hint_range(0.0, 10.0) = 1.0;
uniform float moonb_angular_radius : hint_range(0.001, 0.2) = 0.018;
uniform float moonb_disc_brightness : hint_range(0.1, 200.0) = 2.5;
uniform float moonb_limb_darkening : hint_range(0.0, 1.0) = 0.3;
uniform float moonb_contrast : hint_range(0.0, 4.0) = 1.0;
uniform float moonb_saturation : hint_range(0.0, 3.0) = 1.0;
uniform float moonb_opacity : hint_range(0.0, 1.0) = 1.0;
uniform float moonb_daylight_fade : hint_range(0.0, 400.0) = 50.0;
uniform sampler2D moonb_texture : source_color, filter_linear_mipmap, repeat_enable, hint_default_white;
uniform bool moonb_texture_equirect = false;
uniform float moonb_texture_rotation = 0.0;
uniform float moonb_earthshine : hint_range(0.0, 0.5) = 0.03;
uniform vec3 moonb_orbit_normal = vec3(0.0, 1.0, 0.0);
uniform float moonb_phase_brightness = 1.0;
uniform float moonb_glow_intensity : hint_range(0.0, 8.0) = 0.4;
uniform float moonb_glow_size : hint_range(0.005, 1.0) = 0.07;
uniform float moonb_halo_intensity : hint_range(0.0, 4.0) = 0.0;

// ---------------------------------------------------------------------------------------
// Parent planet -- a large body fixed in the sky, for a "standing on a moon" view. Shaded like the
// moon (a real lit-sphere phase). The procedural look ports the noise/palette/pixel-snap technique
// from the "Animated Pixel Art Planet" shader (godotshaders.com) -- seamless rotated value noise
// thresholded into terrain bands, a second noise field for clouds with drop shadows, quantised
// (posterised) lighting and a pixel-grid snap -- adapted from that shader's flat billboard UVs onto
// this shader's real sphere projection (see moon_geometry_generic: `l`/`z` there are exactly that
// shader's `normalized_uv`/`w`). A texture, if assigned, overrides the procedural surface entirely.
// ---------------------------------------------------------------------------------------
uniform bool parent_planet_enabled = false;
uniform vec3 parent_planet_direction = vec3(0.0, 0.5, 0.866);
uniform float parent_planet_angular_radius : hint_range(0.01, 1.5) = 0.3;
uniform vec3 parent_planet_up = vec3(0.0, 1.0, 0.0);
uniform float parent_planet_brightness : hint_range(0.05, 20.0) = 1.6;
uniform float parent_planet_limb_darkening : hint_range(0.0, 1.0) = 0.25;
uniform float parent_planet_earthshine : hint_range(0.0, 0.5) = 0.02;
uniform sampler2D parent_planet_texture : source_color, filter_linear_mipmap, repeat_enable, hint_default_black;
uniform bool parent_planet_texture_enabled = false;
uniform float parent_planet_texture_rotation = 0.0;
uniform float parent_planet_glow_intensity : hint_range(0.0, 8.0) = 0.15;
uniform float parent_planet_glow_size : hint_range(0.005, 1.0) = 0.05;

// Pixel-art look: the whole projected disc is snapped to a grid this many "pixels" across before
// noise, terrain colour and lighting are evaluated, so continents and cloud edges read as chunky
// blocks instead of a smooth gradient. 0 disables the snap for a conventional smooth look.
uniform float parent_planet_pixel_scale : hint_range(0.0, 400.0) = 110.0;
// Terrain noise: primary (continent shapes) plus a finer secondary octave breaking up the coastline.
uniform int parent_planet_noise_cells : hint_range(2, 64) = 8;
uniform float parent_planet_noise_strength : hint_range(0.0, 4.0) = 1.4;
uniform int parent_planet_detail_cells : hint_range(2, 128) = 18;
uniform float parent_planet_detail_strength : hint_range(0.0, 2.0) = 0.35;
// Ocean/land split and how much of the land band goes to the higher (mountain/desert) tone.
uniform float parent_planet_land_fraction : hint_range(0.0, 1.0) = 0.42;
uniform float parent_planet_mountain_fraction : hint_range(0.0, 1.0) = 0.28;
uniform float parent_planet_ice_fraction : hint_range(0.0, 1.0) = 0.06;
uniform vec3 parent_planet_ocean_deep : source_color = vec3(0.05, 0.12, 0.28);
uniform vec3 parent_planet_ocean_shallow : source_color = vec3(0.09, 0.22, 0.38);
uniform vec3 parent_planet_land_low : source_color = vec3(0.22, 0.42, 0.20);
uniform vec3 parent_planet_land_high : source_color = vec3(0.62, 0.55, 0.38);
uniform vec3 parent_planet_ice_color : source_color = vec3(0.92, 0.94, 0.97);
// Rotation of the terrain noise domain, radians -- the planet's own spin, independent of the
// orbit direction it sits at in the sky.
uniform float parent_planet_rotation = 0.0;
// Clouds: their own noise field, drifting at its own rate, with a soft drop shadow on the terrain.
uniform float parent_planet_cloud_cover : hint_range(0.0, 1.0) = 0.4;
uniform int parent_planet_cloud_cells : hint_range(2, 64) = 9;
uniform int parent_planet_cloud_detail_cells : hint_range(2, 128) = 20;
uniform float parent_planet_cloud_detail_strength : hint_range(0.0, 2.0) = 0.4;
uniform float parent_planet_cloud_rotation = 0.6;
uniform vec3 parent_planet_cloud_color : source_color = vec3(0.98, 0.98, 1.0);
uniform float parent_planet_cloud_shadow : hint_range(0.0, 1.0) = 0.3;
// Posterised lighting: the lit fraction is rounded into this many flat bands, the way the source
// shader's "Quantised" shading mode reads as discrete pixel-art shading steps rather than a smooth
// gradient. 1 disables posterisation.
uniform int parent_planet_shade_bands : hint_range(1, 16) = 5;
// Thin glow rim at the limb -- the atmosphere seen edge-on, brightest grazing the silhouette.
uniform vec3 parent_planet_atmosphere_color : source_color = vec3(0.55, 0.75, 1.0);
uniform float parent_planet_atmosphere_power : hint_range(0.5, 16.0) = 4.0;
uniform float parent_planet_atmosphere_intensity : hint_range(0.0, 4.0) = 1.0;
// Mixed into the noise hash: a different seed gives a different (but repeatable) planet.
uniform float parent_planet_seed = 0.0;
// Accumulated rotation phase, advanced by EdenPlanetAtmosphere's parent_planet_band_speed.
uniform float parent_planet_band_time = 0.0;

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

// Disc geometry for view ray rd, shared by both moons.
//   m, orbit_normal, angular_radius, tex_rotation, equirect : the body's own uniforms
//   l        : position on the disc, limb at radius 1 (clamped inside for the AA fringe)
//   z        : sphere height at l, i.e. cosine of the angle between normal and viewer
//   n        : world-space surface normal
//   coverage : antialiased disc mask
//   uv       : texture coordinate for the current texture mode
void moon_geometry_generic(vec3 rd, vec3 m, vec3 orbit_normal, float angular_radius, float tex_rotation,
		bool equirect, out vec2 l, out float z, out vec3 n, out float coverage, out vec2 uv) {
	vec3 up_ref = abs(dot(orbit_normal, m)) > 0.99 ? vec3(1.0, 0.0, 0.0) : orbit_normal;
	// cross(m, up) rather than cross(up, m): the other order puts "right" on the left of the
	// screen and mirrors the texture.
	vec3 right = normalize(cross(m, up_ref));
	vec3 up = cross(right, m);

	// Gnomonic projection onto the tangent plane, scaled so the limb lands at radius 1.
	float cos_v = dot(rd, m);
	vec2 local = vec2(dot(rd, right), dot(rd, up)) / (max(cos_v, 1e-4) * tan(angular_radius));
	float r = length(local);
	float aa = max(fwidth(r), 1e-4);
	coverage = cos_v > 0.0 ? 1.0 - smoothstep(1.0 - aa, 1.0, r) : 0.0;

	l = local / max(r, 1.0);
	z = sqrt(max(1.0 - dot(l, l), 0.0));
	n = normalize(right * l.x + up * l.y - m * z);

	float c = cos(tex_rotation);
	float s = sin(tex_rotation);
	if (equirect) {
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

void moon_geometry(vec3 rd, out vec2 l, out float z, out vec3 n, out float coverage, out vec2 uv) {
	moon_geometry_generic(rd, moon_direction, moon_orbit_normal, moon_angular_radius, moon_texture_rotation, moon_texture_equirect, l, z, n, coverage, uv);
}

float hash31(vec3 p) {
	p = fract((p + random_seed * 17.0) * 0.1031);
	p += dot(p, p.yzx + 33.33);
	return fract((p.x + p.y) * p.z);
}

// Point stars: one candidate per cell of a 3D lattice laid over the view direction, jittered
// inside its cell. Using the direction vector (not a UV) means no pole pinching and no seam.
vec3 star_field(vec3 rd, float sun_proximity, float moon_proximity) {
	if (sun_proximity <= 0.0 && moon_proximity <= 0.0) {
		return vec3(0.0);
	}
	vec3 p = rd * star_scale;
	vec3 cell = floor(p);
	vec3 f = p - cell;

	float present = hash31(cell);
	// step() rather than an if, to keep the warp coherent.
	float alive = step(1.0 - star_density, present);

	vec3 jitter = vec3(hash31(cell + 11.0), hash31(cell + 23.0), hash31(cell + 37.0));
	float d = length(f - jitter);
	float point_mask = smoothstep(star_size, 0.0, d);

	// Vary brightness so the field does not read as a uniform grid.
	float brightness = 1.0 - star_brightness_variation * hash31(cell + 59.0);
	if (star_twinkle > 0.0) {
		float phase = hash31(cell + 71.0) * 6.2832;
		brightness *= 1.0 - star_twinkle * (0.5 + 0.5 * sin(TIME * star_twinkle_speed * (0.6 + hash31(cell + 83.0)) + phase));
	}
	// Blackbody-ish tint: blue-white through white to orange.
	float temperature = hash31(cell + 97.0) * 2.0 - 1.0;
	vec3 tint = temperature > 0.0 ? mix(vec3(1.0), vec3(1.0, 0.72, 0.45), temperature) : mix(vec3(1.0), vec3(0.7, 0.8, 1.0), -temperature);
	return mix(vec3(1.0), tint, star_color_variation) * (alive * point_mask * brightness * sun_proximity * moon_proximity);
}

// ---------------------------------------------------------------------------------------
// Parent planet: pixel-art procedural surface
//
// Ported from the "Animated Pixel Art Planet" shader (godotshaders.com): seamless rotated 3D value
// noise (random/seamless_noise/generate_rotation_matrix below are that shader's, essentially
// unchanged), terrain read off palette thresholds, a second noise field for clouds with a soft drop
// shadow, and posterised lighting. The one structural change is the input: that shader is a flat
// canvas_item billboard with its own calculate_z() from a UV rect, whereas here the disc position
// (`l`, `z`) already comes from moon_geometry_generic's real sphere/camera projection, so `l` takes
// the place of its `normalized_uv` and `z` the place of its `w` throughout.
// ---------------------------------------------------------------------------------------
vec3 pp_modulo(vec3 a, vec3 b) {
	return mod(mod(a, b) + b, b);
}

vec3 pp_random(vec3 v) {
	vec3 r = vec3(dot(v, vec3(127.1, 311.7, 201.9) + parent_planet_seed),
			dot(v, vec3(269.5, 183.3, 367.7) + parent_planet_seed),
			dot(v, vec3(245.1, 367.7, 105.6) + parent_planet_seed));
	return -1.0 + 2.0 * fract(sin(r) * 43758.5453123);
}

mat3 pp_rotation(float theta) {
	// Rotation about the (1,1,0)/sqrt(2) axis: rotating the NOISE DOMAIN about a tilted axis (rather
	// than about a coordinate axis) is what makes it read as the sphere spinning on screen instead
	// of the pattern sliding sideways through it -- see the source shader's own comment on this.
	float c = cos(theta), s = sin(theta);
	return mat3(
			vec3(0.5 * (1.0 - c) + c, 0.5 * (1.0 - c), -0.70710678 * s),
			vec3(0.5 * (1.0 - c), 0.5 * (1.0 - c) + c, 0.70710678 * s),
			vec3(0.70710678 * s, -0.70710678 * s, c));
}

// uvw = (disc position, height) rotated into the spinning noise domain, split into a lattice of
// `cells` cells per axis and trilinearly blended between each cell's own random gradient-ish value.
float pp_seamless_noise(vec2 l, float z, vec3 period, mat3 rot, int cells) {
	vec3 uvw = (rot * vec3(l, z)) * float(cells);
	vec3 lo = floor(uvw);
	vec3 hi = ceil(uvw);
	vec3 f = fract(uvw);
	lo = pp_modulo(lo, period);
	hi = pp_modulo(hi, period);
	vec3 blur = smoothstep(0.3, 1.0, f);

	vec3 p000 = pp_random(vec3(lo.x, lo.y, lo.z));
	vec3 p100 = pp_random(vec3(hi.x, lo.y, lo.z));
	vec3 p010 = pp_random(vec3(lo.x, hi.y, lo.z));
	vec3 p110 = pp_random(vec3(hi.x, hi.y, lo.z));
	vec3 p001 = pp_random(vec3(lo.x, lo.y, hi.z));
	vec3 p101 = pp_random(vec3(hi.x, lo.y, hi.z));
	vec3 p011 = pp_random(vec3(lo.x, hi.y, hi.z));
	vec3 p111 = pp_random(vec3(hi.x, hi.y, hi.z));

	return mix(mix(mix(dot(p000, f - vec3(0, 0, 0)), dot(p100, f - vec3(1, 0, 0)), blur.x),
						 mix(dot(p010, f - vec3(0, 1, 0)), dot(p110, f - vec3(1, 1, 0)), blur.x), blur.y),
				   mix(mix(dot(p001, f - vec3(0, 0, 1)), dot(p101, f - vec3(1, 0, 1)), blur.x),
						   mix(dot(p011, f - vec3(0, 1, 1)), dot(p111, f - vec3(1, 1, 1)), blur.x), blur.y),
				   blur.z) *
					0.8 +
			0.5;
}

// Snaps a disc position to parent_planet_pixel_scale "pixels" across the disc, and returns the
// matching height so downstream code (noise, lighting) stays on the sphere. This is the whole pixel
// -art look: everything below samples the snapped position, so terrain, clouds and light all read
// as flat blocks instead of a smooth gradient.
void pp_pixel_snap(vec2 l, float z, out vec2 out_l, out float out_z) {
	if (parent_planet_pixel_scale <= 1.0) {
		out_l = l;
		out_z = z;
		return;
	}
	float half_px = parent_planet_pixel_scale * 0.5;
	out_l = round(l * half_px) / half_px;
	out_z = sqrt(max(1.0 - dot(out_l, out_l), 0.0));
}

const vec3 PP_NOISE_PERIOD = vec3(9.5, 9.5, 9.5);

// Terrain colour at a (pixel-snapped) disc position, from two octaves of seamless noise read
// against fixed ocean/land/mountain/ice thresholds.
vec3 pp_surface(vec2 l, float z, mat3 rot) {
	float n = pp_seamless_noise(l, z, PP_NOISE_PERIOD, rot, parent_planet_noise_cells);
	n = clamp((n - 0.5) * parent_planet_noise_strength + 0.5, 0.0, 1.0);
	float detail = pp_seamless_noise(l, z, PP_NOISE_PERIOD, rot, parent_planet_detail_cells);
	n = clamp(n + (detail - 0.5) * parent_planet_detail_strength, 0.0, 1.0);

	float ocean_split = 1.0 - parent_planet_land_fraction;
	float mountain_split = ocean_split + (1.0 - ocean_split) * (1.0 - parent_planet_mountain_fraction);
	float ice_split = 1.0 - parent_planet_ice_fraction;

	vec3 col = mix(parent_planet_ocean_deep, parent_planet_ocean_shallow, smoothstep(0.0, ocean_split, n));
	col = mix(col, parent_planet_land_low, step(ocean_split, n));
	col = mix(col, mix(parent_planet_land_low, parent_planet_land_high, smoothstep(ocean_split, mountain_split, n)), step(ocean_split, n));
	col = mix(col, parent_planet_ice_color, step(ice_split, n));
	return col;
}

// Cloud coverage [0,1] and shadow strength at a disc position, from its own noise field rotated at
// its own rate so clouds drift relative to the terrain beneath them.
float pp_clouds(vec2 l, float z, mat3 rot) {
	if (parent_planet_cloud_cover <= 0.0) {
		return 0.0;
	}
	mat3 cloud_rot = pp_rotation(parent_planet_band_time * parent_planet_cloud_rotation) * rot;
	float n = pp_seamless_noise(l, z, PP_NOISE_PERIOD, cloud_rot, parent_planet_cloud_cells);
	float detail = pp_seamless_noise(l, z, PP_NOISE_PERIOD, cloud_rot, parent_planet_cloud_detail_cells);
	n = clamp(n + (detail - 0.5) * parent_planet_cloud_detail_strength, 0.0, 1.0);
	float cutoff = 1.0 - parent_planet_cloud_cover;
	return smoothstep(cutoff, cutoff + 0.05, n);
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
	vec2 moonb_l = vec2(0.0);
	float moonb_z = 0.0;
	vec3 moonb_n = vec3(0.0);
	float moonb_coverage = 0.0;
	vec2 moonb_uv = vec2(0.0);
	vec2 moonb_uv_dx = vec2(0.0);
	vec2 moonb_uv_dy = vec2(0.0);
	if (!AT_CUBEMAP_PASS) {
		moon_geometry(rd, moon_l, moon_z, moon_n, moon_coverage, moon_uv);
		moon_uv_dx = dFdx(moon_uv);
		moon_uv_dy = dFdy(moon_uv);
		// Equirect mode wraps u on the far side; keep a wrap from reading as a huge derivative.
		moon_uv_dx.x -= round(moon_uv_dx.x);
		moon_uv_dy.x -= round(moon_uv_dy.x);

		if (moonb_enabled) {
			moon_geometry_generic(rd, moonb_direction, moonb_orbit_normal, moonb_angular_radius,
					moonb_texture_rotation, moonb_texture_equirect, moonb_l, moonb_z, moonb_n, moonb_coverage, moonb_uv);
			moonb_uv_dx = dFdx(moonb_uv);
			moonb_uv_dy = dFdy(moonb_uv);
			moonb_uv_dx.x -= round(moonb_uv_dx.x);
			moonb_uv_dy.x -= round(moonb_uv_dy.x);
		}
	}

	vec2 parent_l = vec2(0.0);
	float parent_z = 0.0;
	vec3 parent_n = vec3(0.0);
	float parent_coverage = 0.0;
	vec2 parent_uv = vec2(0.0);
	vec2 parent_uv_dx = vec2(0.0);
	vec2 parent_uv_dy = vec2(0.0);
	if (!AT_CUBEMAP_PASS && parent_planet_enabled) {
		moon_geometry_generic(rd, normalize(parent_planet_direction), parent_planet_up, parent_planet_angular_radius,
				parent_planet_texture_rotation, true, parent_l, parent_z, parent_n, parent_coverage, parent_uv);
		parent_uv_dx = dFdx(parent_uv);
		parent_uv_dy = dFdy(parent_uv);
		parent_uv_dx.x -= round(parent_uv_dx.x);
		parent_uv_dy.x -= round(parent_uv_dy.x);
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
		// 1 well inside the atmosphere, fading to 0 above it -- ice-crystal halos have nothing to
		// refract through once the camera leaves the air.
		float in_atmosphere_factor = 1.0 - smoothstep(0.3, 1.0, altitude_of(ro) / max(atmosphere_height, 1.0));

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
			space = max(space - space_black_point, vec3(0.0));
			space = pow(space, vec3(space_gamma));
			space = 0.18 * pow(space / 0.18, vec3(space_contrast));
			space = max(mix(vec3(dot(space, vec3(0.2126, 0.7152, 0.0722))), space, space_saturation), vec3(0.0));
			background += space * space_tint * space_opacity;
		}

		if (!AT_CUBEMAP_PASS) {
			float star_fade = 1.0 / (1.0 + sky_luminance * star_daylight_fade);
			float mu = clamp(dot(rd, sun_direction), -1.0, 1.0);
			float angle = acos(mu);
			// Suppressed within a few glow-radii of either disc: real stars vanish long before a
			// point that close to the sun or moon would, and it hides the lattice's own periodicity
			// from showing through the corona as a grid of unsuppressed sprites.
			float sun_proximity = smoothstep(sun_glow_size * 1.5, sun_glow_size * 6.0, angle);
			float moon_angle_for_stars = acos(clamp(dot(rd, moon_direction), -1.0, 1.0));
			float moon_proximity = smoothstep(moon_glow_size * 1.5, moon_glow_size * 6.0, moon_angle_for_stars);
			background += star_field(rd, sun_proximity, moon_proximity) * star_intensity * star_fade;
			float disc_aa = max(fwidth(angle), sun_angular_radius * 0.02);
			float disc = 1.0 - smoothstep(sun_angular_radius - disc_aa, sun_angular_radius + disc_aa, angle);
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
				background += halo_ring(angle) * (sun_halo_intensity * sun_vis * in_atmosphere_factor);
			}

			// Sun 2: a second, independently coloured sun near the primary one. Same disc/glow/corona
			// treatment, entirely additive, in its own tint. Does not add a second scattering term to
			// the sky itself (see sun2_affects_scattering on the atmosphere node) -- this alone already
			// gives a real binary-star look for a fraction of the cost.
			if (sun2_enabled) {
				float angle2 = acos(clamp(dot(rd, sun2_direction), -1.0, 1.0));
				float disc2_aa = max(fwidth(angle2), sun2_angular_radius * 0.02);
				float disc2 = 1.0 - smoothstep(sun2_angular_radius - disc2_aa, sun2_angular_radius + disc2_aa, angle2);
				if (disc2 > 0.0) {
					float r2 = clamp(angle2 / max(sun2_angular_radius, 1e-5), 0.0, 1.0);
					float limb2 = pow(max(1.0 - r2 * r2, 0.0), sun_limb_darkening * 0.5);
					background += disc2 * limb2 * sun2_tint * sun2_disc_brightness;
				}
				float sun2_vis = planet_shadow_dir(ro, sun2_direction);
				float sun2_glow = exp(-angle2 / max(sun2_glow_size, 1e-4)) * sun2_glow_intensity;
				float corona2 = exp(-max(angle2 - sun2_angular_radius, 0.0) / max(sun2_angular_radius * 0.75, 1e-5)) * sun2_corona_intensity;
				background += sun2_tint * ((sun2_glow + corona2) * sun2_vis);
			}

			// Moon. Shaded as a lit sphere against the real sun direction, so the phase -- crescent,
			// quarter, gibbous, full -- falls out of the lighting instead of needing phase textures,
			// and it is always consistent with where the sun actually is.
			if (moon_coverage > 0.0) {
				vec3 albedo = textureGrad(moon_texture, moon_uv, moon_uv_dx, moon_uv_dy).rgb;
				albedo = max((albedo - 0.5) * moon_contrast + 0.5, vec3(0.0));
				albedo = max(mix(vec3(dot(albedo, vec3(0.2126, 0.7152, 0.0722))), albedo, moon_saturation), vec3(0.0));
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
				background = mix(background, moon_col, moon_coverage * moon_opacity);
			}

			// Moon glow and halo. Scaled by phase -- a new moon has neither -- and by the same daylight
			// fade as the disc.
			if (moon_glow_intensity > 0.0 || moon_halo_intensity > 0.0) {
				float moon_angle = acos(clamp(dot(rd, moon_direction), -1.0, 1.0));
				float moon_vis = planet_shadow_dir(ro, moon_direction) * moon_phase_brightness
						/ (1.0 + sky_luminance * moon_daylight_fade);
				float moon_glow = exp(-moon_angle / max(moon_glow_size, 1e-4)) * moon_glow_intensity;
				background += moon_tint * (moon_glow * moon_vis);
				background += halo_ring(moon_angle) * (moon_halo_intensity * moon_vis * in_atmosphere_factor);
			}

			// Moon B: a second moon, on its own orbit. Same shading model as the primary moon.
			if (moonb_enabled) {
				if (moonb_coverage > 0.0) {
					vec3 albedo = textureGrad(moonb_texture, moonb_uv, moonb_uv_dx, moonb_uv_dy).rgb;
					albedo = max((albedo - 0.5) * moonb_contrast + 0.5, vec3(0.0));
					albedo = max(mix(vec3(dot(albedo, vec3(0.2126, 0.7152, 0.0722))), albedo, moonb_saturation), vec3(0.0));
					float mu0b = max(dot(moonb_n, sun_direction), 0.0);
					float moonb_reflect = min(2.0 * mu0b / max(mu0b + moonb_z, 1e-4), 1.5);
					float limbb = pow(max(moonb_z, 1e-4), moonb_limb_darkening);
					vec3 incidentb = sun_tint * moonb_reflect + vec3(moonb_earthshine);
					float moonb_fade = 1.0 / (1.0 + sky_luminance * moonb_daylight_fade);
					vec3 moonb_col = albedo * moonb_tint * incidentb * limbb * moonb_intensity * moonb_disc_brightness * moonb_fade;
					background = mix(background, moonb_col, moonb_coverage * moonb_opacity);
				}
				if (moonb_glow_intensity > 0.0 || moonb_halo_intensity > 0.0) {
					float moonb_angle = acos(clamp(dot(rd, moonb_direction), -1.0, 1.0));
					float moonb_vis = planet_shadow_dir(ro, moonb_direction) * moonb_phase_brightness
							/ (1.0 + sky_luminance * moonb_daylight_fade);
					float moonb_glow = exp(-moonb_angle / max(moonb_glow_size, 1e-4)) * moonb_glow_intensity;
					background += moonb_tint * (moonb_glow * moonb_vis);
					background += halo_ring(moonb_angle) * (moonb_halo_intensity * moonb_vis * in_atmosphere_factor);
				}
			}

			// Parent planet: a large body fixed in the sky. Same real lit-sphere phase as the moons,
			// with either its own texture or the pixel-art procedural surface above.
			if (parent_planet_enabled && parent_coverage > 0.0) {
				vec3 col;
				float ndl = max(dot(parent_n, sun_direction), 0.0);
				if (parent_planet_texture_enabled) {
					col = textureGrad(parent_planet_texture, parent_uv, parent_uv_dx, parent_uv_dy).rgb;
				} else {
					vec2 pl;
					float pz;
					pp_pixel_snap(parent_l, parent_z, pl, pz);
					mat3 rot = pp_rotation(parent_planet_rotation + parent_planet_band_time);
					vec3 terrain = pp_surface(pl, pz, rot);
					float cloud = pp_clouds(pl, pz, rot);
					// Drop shadow: darken the terrain wherever cloud sits over it, same as the source
					// shader's separate shadow pass, before the (fully opaque) cloud colour goes on top.
					terrain *= 1.0 - cloud * parent_planet_cloud_shadow;
					col = mix(terrain, parent_planet_cloud_color, cloud);
				}
				float parent_reflect = min(2.0 * ndl / max(ndl + parent_z, 1e-4), 1.5);
				float light = parent_planet_shade_bands > 1
						? floor(parent_reflect * float(parent_planet_shade_bands) + 0.5) / float(parent_planet_shade_bands)
						: parent_reflect;
				float limbp = pow(max(parent_z, 1e-4), parent_planet_limb_darkening);
				vec3 incidentp = sun_tint * light + vec3(parent_planet_earthshine);
				vec3 parent_col = col * incidentp * limbp * parent_planet_brightness;
				// Thin atmosphere rim at the limb, brightest grazing the silhouette -- the blue edge
				// glow that reads as "this has air" even in a stylised render.
				float rim = pow(1.0 - parent_z, parent_planet_atmosphere_power) * parent_planet_atmosphere_intensity;
				parent_col += parent_planet_atmosphere_color * (rim * max(light, 0.15));
				background = mix(background, parent_col, parent_coverage);
			}
			if (parent_planet_enabled && parent_planet_glow_intensity > 0.0) {
				float parent_angle = acos(clamp(dot(rd, normalize(parent_planet_direction)), -1.0, 1.0));
				float parent_vis = planet_shadow_dir(ro, normalize(parent_planet_direction)) / (1.0 + sky_luminance * 50.0);
				float parent_glow = exp(-parent_angle / max(parent_planet_glow_size, 1e-4)) * parent_planet_glow_intensity;
				background += parent_planet_atmosphere_color * (parent_glow * parent_vis);
			}
		}
	}

	COLOR = (background * transmittance + inscatter) * exposure;
}
