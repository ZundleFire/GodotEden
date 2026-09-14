// Shared planetary atmosphere math -- included by planet_sky.gdshader,
// planet_clouds.gdshader and (eventually) the terrain aerial-perspective pass, so all
// three agree on exactly one scattering model. Everything here works in PLANET-RELATIVE
// space: callers subtract planet_center before calling, and the planet is assumed to sit
// at world origin by default (matching ocean_shell.gd's planet_center = Vector3.ZERO).
//
// Model: single-scattering Rayleigh + Mie with exponential density falloff, integrated
// by raymarching the view ray and, at each view sample, a short march toward the sun.
// This is the standard Nishita/O'Neil formulation trimmed to a fixed low step count --
// see SUN_STEPS / VIEW_STEPS below for the GTX 750 Ti budget.
//
// ponytail: single scattering only, no multiple-scattering term and no precomputed LUT.
// Sunsets lose a little of the deep-purple bounce light real skies have. Upgrade path if
// that matters: bake a 2D transmittance LUT (altitude x sun-zenith) once into a viewport
// texture and replace optical_depth_to_sun() with a texture fetch -- that also buys back
// the SUN_STEPS inner loop, which is the single biggest cost here.

// ---------------------------------------------------------------------------------------
// Planet / atmosphere geometry
// ---------------------------------------------------------------------------------------
// Planet-relative space is keyed off planet_center rather than assuming the planet sits at
// the world origin. It does today in every demo, but EDEN_SYSTEMS_REFERENCE.md 9 specs a
// reference-body floating-origin system; when that lands, the controller feeds the active
// body's transform in here and none of this math changes.
uniform vec3 planet_center = vec3(0.0, 0.0, 0.0);
// Matches EdenPlanetGeneratorV3's own default radius.
uniform float planet_radius = 40000.0;
// Thickness of the atmosphere shell above sea level. Rays are only integrated inside
// [planet_radius, planet_radius + atmosphere_height].
uniform float atmosphere_height : hint_range(500.0, 20000.0) = 4000.0;

// Guards against a planet-relative camera position that sits deep inside (or at) the planet
// centre -- every distance-from-centre computation below assumes "altitude" is a small number
// near planet_radius, not a wild negative one. That only happens by mistake -- a camera parked
// at the world origin before anything has positioned it relative to the planet (an editor
// viewport's default camera is exactly this case, now that the node can appear there with no
// scene setup at all) -- but when it does, the scattering integral sees an "altitude" of tens of
// thousands of metres below ground and produces exotic banding instead of a sky. Clamping the
// distance to at least the surface mirrors what EdenPlanetAtmosphere::_camera_planet_relative()
// already does on the CPU side for the exact same reason.
vec3 clamp_planet_relative(vec3 ro) {
	float len = length(ro);
	float floor_r = planet_radius + 1.0;
	if (len >= floor_r) {
		return ro;
	}
	vec3 dir = len > 1e-4 ? ro / len : vec3(0.0, 1.0, 0.0);
	return dir * floor_r;
}

// Exponential density scale heights: density(h) = exp(-h / scale_height).
// Earth's are 8000m (Rayleigh) / 1200m (Mie); these are compressed for a 20km planet so
// the visible gradient fits inside a shell you can actually fly through.
uniform float rayleigh_scale_height : hint_range(50.0, 8000.0) = 800.0;
uniform float mie_scale_height : hint_range(10.0, 2000.0) = 120.0;

// ---------------------------------------------------------------------------------------
// Scattering strength
// ---------------------------------------------------------------------------------------
// IMPORTANT: these are multipliers on EARTH-EQUIVALENT scattering, not raw coefficients.
//
// Earth's published beta values assume Earth's 8000m scale height. Dropping the scale
// height to 800m without touching beta cuts the total vertical optical depth by 10x and
// leaves you with a near-invisible washed-out sky -- this is the trap in most "scale it
// down for a small planet" write-ups. beta_rayleigh()/beta_mie() below therefore rescale
// by (earth_scale_height / our_scale_height), so 1.0 reproduces an Earth-like sky at ANY
// scale height and these knobs stay purely artistic.
// Defaults are NOT 1.0/1.0. A small planet has a short grazing path -- the ratio of horizon to
// zenith optical depth goes as sqrt(2*PI*R/H), which is ~71 on Earth but only ~18 here. Earth's
// balance therefore produces a limp sunset at this scale, so Rayleigh is pushed up to restore
// the colour separation the short path loses.
// RGB wavelengths in nanometres. Rayleigh scattering goes as 1/lambda^4, which is the entire
// reason Earth's sky is blue, so this is the physical control for an alien sky: shift these and
// the whole gradient, the sunset hue and the sun's own colour all move together and stay
// self-consistent. Defaults are the standard 680/550/440 sRGB primaries, which reproduce Earth.
// Lower values scatter harder (bluer/violet); raising red past green inverts the sky to a warm
// daytime with a blue-green sunset.
uniform vec3 rayleigh_wavelengths = vec3(680.0, 550.0, 440.0);

uniform float rayleigh_strength : hint_range(0.0, 8.0) = 2.5;
// Mie is pulled DOWN hard. Its forward phase peaks near 51 at g=0.8 against Rayleigh's 0.12,
// and being spectrally flat that lobe is white -- at 1.0 it buries the red Rayleigh signal
// under glare and every sunset renders as a white smear. This keeps the aureole without it
// dominating. Raise for hazy/dusty worlds.
uniform float mie_strength : hint_range(0.0, 8.0) = 0.35;
// Mie forward-scattering bias. Higher = tighter, brighter halo hugging the sun.
uniform float mie_anisotropy : hint_range(0.0, 0.999) = 0.76;

// ---------------------------------------------------------------------------------------
// Sun
// ---------------------------------------------------------------------------------------
// Unit vector pointing FROM the planet TOWARD the sun (i.e. the direction you look to see
// it), which is the negation of DirectionalLight3D's forward axis. planet_atmosphere.gd
// owns this and keeps the light node in sync.
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 sun_tint : source_color = vec3(1.0, 1.0, 1.0);
uniform float sun_intensity : hint_range(0.0, 100.0) = 22.0;
// Angular radius of the disc in radians (the real Sun is ~0.0046; larger reads better on
// a stylised planet and softens the aliasing on the limb).
uniform float sun_angular_radius : hint_range(0.001, 0.2) = 0.02;
// Half-width, in metres, of the soft band at the planet's shadow edge. See planet_shadow().
// Too small and sunset banding returns; too large and the night side leaks light.
uniform float shadow_softness : hint_range(0.0, 4000.0) = 600.0;

// ---------------------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------------------
const vec3 BETA_RAYLEIGH_EARTH = vec3(5.8e-6, 13.5e-6, 33.1e-6); // m^-1, sea level
const float BETA_MIE_EARTH = 21e-6;                              // m^-1, sea level
const float EARTH_RAYLEIGH_SCALE_H = 8000.0;
const float EARTH_MIE_SCALE_H = 1200.0;

// Mie absorbs as well as scatters; the standard approximation is extinction = 1.1x scatter.
const float MIE_EXTINCTION_FACTOR = 1.1;

// SUN_STEPS is only used by the FALLBACK path now: with use_transmittance_lut on (the normal
// case) the inner sun march is replaced by one texture fetch, so per-pixel cost is VIEW_STEPS
// fetches rather than VIEW_STEPS * SUN_STEPS exp() pairs.
//
// Measured on the target GTX 750 Ti at 1080p (atmosphere_demo.tscn -- --bench), whole-frame
// worst case including the placeholder sphere:
//
//   sky marched, no clouds                     8.96 ms
//   sky marched + procedural-noise clouds     16.26 ms
//   sky marched + baked-noise clouds          10.64 ms
//   sky via LUT  + baked-noise clouds          9.09 ms
//
// So sky and clouds together now cost about what the sky alone used to. Raising VIEW_STEPS to
// 16 was tried and rejected: visibly cleaner, but it pushed the frame to 13.4 ms for sky alone,
// which is most of a 60fps budget before terrain, water or foliage. The residual quantisation
// is handled by the free per-pixel jitter in atmosphere_scatter() instead -- and the banding
// that originally prompted all this was the hard shadow edge (see planet_shadow), not the step
// count.
const int VIEW_STEPS = 10; // samples along the view ray
const int SUN_STEPS = 4;   // samples along each shadow/transmittance ray toward the sun

const float PHASE_RAYLEIGH_K = 0.0596831036; // 3 / (16*PI)

// ---------------------------------------------------------------------------------------
// Coefficients
// ---------------------------------------------------------------------------------------
const vec3 EARTH_WAVELENGTHS = vec3(680.0, 550.0, 440.0); // nm, matches BETA_RAYLEIGH_EARTH

vec3 beta_rayleigh() {
	// Rayleigh scattering is proportional to 1/lambda^4. Expressing it as a ratio against the
	// wavelengths the Earth betas were measured at means the default wavelengths reproduce
	// those betas exactly, and any other choice stays physically consistent rather than being
	// an arbitrary colour tint.
	vec3 ratio = EARTH_WAVELENGTHS / max(rayleigh_wavelengths, vec3(1.0));
	vec3 spectral = ratio * ratio * ratio * ratio;
	return BETA_RAYLEIGH_EARTH * spectral
			* (EARTH_RAYLEIGH_SCALE_H / max(rayleigh_scale_height, 1.0)) * rayleigh_strength;
}

float beta_mie() {
	return BETA_MIE_EARTH * (EARTH_MIE_SCALE_H / max(mie_scale_height, 1.0)) * mie_strength;
}

float atmosphere_radius() {
	return planet_radius + atmosphere_height;
}

// ---------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------
// Ray/sphere intersection for a sphere centred on the origin, assuming rd is normalised
// (so the quadratic's a == 1). Returns vec2(t_near, t_far). A miss is signalled by
// t_far < t_near; both can be negative when the sphere is entirely behind the ray.
vec2 ray_sphere(vec3 ro, vec3 rd, float radius) {
	float b = dot(ro, rd);
	float c = dot(ro, ro) - radius * radius;
	float d = b * b - c;
	if (d < 0.0) {
		return vec2(1.0, -1.0); // miss
	}
	d = sqrt(d);
	return vec2(-b - d, -b + d);
}

// Altitude above sea level for a planet-relative point.
float altitude_of(vec3 p) {
	return length(p) - planet_radius;
}

// Rodrigues rotation of v about a unit axis. Shared by the star panorama (which counter-rotates
// with the day) and the cloud drift.
vec3 rotate_about(vec3 v, vec3 axis, float angle) {
	float c = cos(angle);
	float s = sin(angle);
	return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

// Direction -> equirectangular UV, matching Godot's own panorama sky exactly
// (servers/rendering/renderer_rd/shaders/environment/sky.glsl): u = atan2(x, -z), v = acos(y).
// Matching it matters -- it means any panorama authored or previewed against Godot's
// PanoramaSkyMaterial drops in here with identical orientation.
vec2 dir_to_panorama_uv(vec3 d) {
	vec2 uv = vec2(atan(d.x, -d.z), acos(clamp(d.y, -1.0, 1.0)));
	if (uv.x < 0.0) {
		uv.x += 6.28318530718;
	}
	return uv / vec2(6.28318530718, 3.14159265359);
}

// Interleaved gradient noise -- the standard cheap per-pixel dither. Static in screen space,
// so it breaks raymarch banding into fine noise without the frame-to-frame shimmer a
// time-varying jitter would cause in a pipeline with no temporal accumulation.
float interleaved_gradient_noise(vec2 pixel) {
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// ---------------------------------------------------------------------------------------
// Phase functions
// ---------------------------------------------------------------------------------------
// mu = cos(angle between view ray and sun direction).
float phase_rayleigh(float mu) {
	return PHASE_RAYLEIGH_K * (1.0 + mu * mu);
}

// Cornette-Shanks: smoother than raw Henyey-Greenstein at grazing angles and cheap enough
// to keep in the inner loop.
float phase_mie(float mu, float g) {
	float g2 = g * g;
	float denom = 1.0 + g2 - 2.0 * g * mu;
	// max() guards the g -> 1 singularity when looking straight at the sun.
	return (3.0 * (1.0 - g2)) / (2.0 * (2.0 + g2)) * (1.0 + mu * mu) / max(pow(denom, 1.5), 1e-4);
}

// ---------------------------------------------------------------------------------------
// Planet shadow
// ---------------------------------------------------------------------------------------
// Fraction of sunlight reaching p that the planet body does not block: 1 lit, 0 in shadow.
//
// Deliberately smooth, not a binary hit test. A hard in/out test quantises against the low
// VIEW_STEPS count -- every sample that crosses the terminator dumps a 1/VIEW_STEPS jump into
// the accumulated inscatter, which prints the raymarch's own step boundaries into the sky as
// arc-shaped bands. They only appear when the terminator is in view, i.e. exactly at sunrise
// and sunset. Softening the edge removes them at no extra sample cost.
//
// Physically this is defensible too: the real terminator is soft, because the atmosphere
// refracts and forward-scatters light around the limb rather than cutting it off geometrically.
// Same as planet_shadow(), for an arbitrary light direction -- used for moonlight.
float planet_shadow_dir(vec3 p, vec3 light_dir) {
	float b = dot(p, light_dir);
	if (b >= 0.0) {
		return 1.0;
	}
	float perp = sqrt(max(dot(p, p) - b * b, 0.0));
	float w = max(shadow_softness, 1.0);
	return smoothstep(planet_radius - w, planet_radius + w, perp);
}

float planet_shadow(vec3 p) {
	float b = dot(p, sun_direction);
	if (b >= 0.0) {
		return 1.0; // sun ray heads away from the planet, nothing to occlude it
	}
	// Closest approach of the sun ray to the planet centre. Shadowed when it passes inside
	// the surface.
	float perp = sqrt(max(dot(p, p) - b * b, 0.0));
	float w = max(shadow_softness, 1.0);
	return smoothstep(planet_radius - w, planet_radius + w, perp);
}

// ---------------------------------------------------------------------------------------
// Optical depth
// ---------------------------------------------------------------------------------------
// Precomputed lookup of the sun-ward optical depth. In a spherically symmetric atmosphere the
// integral depends on exactly two things -- the sample's altitude and the angle between its
// local up and the sun -- so it can be baked once instead of marched per sample.
//
// This is the sky's dominant cost. The scattering loop calls optical_depth_to_sun() once per
// view sample, so the naive version runs VIEW_STEPS * SUN_STEPS exp() pairs per pixel. The LUT
// turns the inner loop into one texture fetch.
//
// Stores optical DEPTH, not transmittance, so the beta coefficients stay live uniforms and
// rayleigh_strength / mie_strength remain tunable without rebaking.
uniform sampler2D transmittance_lut : filter_linear, repeat_disable, hint_default_black;
// Falls back to marching when no LUT has been supplied, so the shader still works standalone.
uniform bool use_transmittance_lut = false;

// Maps cos(sun zenith) to the LUT's u axis. The sqrt concentrates texels near the horizon,
// where optical depth changes by orders of magnitude over a couple of degrees -- a linear
// mapping there produces visible steps in the sunset gradient. Mirrored in GDScript.
float lut_cos_to_u(float c) {
	return 0.5 + 0.5 * sign(c) * sqrt(abs(c));
}

// Accumulated (rayleigh, mie) density along the ray from p toward the sun, out to the top of
// the atmosphere. Occlusion by the planet body is NOT handled here -- see planet_shadow().
vec2 optical_depth_to_sun(vec3 p) {
	if (use_transmittance_lut) {
		float alt01 = clamp(altitude_of(p) / max(atmosphere_height, 1.0), 0.0, 1.0);
		float c = clamp(dot(normalize(p), sun_direction), -1.0, 1.0);
		return texture(transmittance_lut, vec2(lut_cos_to_u(c), alt01)).rg;
	}

	vec2 atm = ray_sphere(p, sun_direction, atmosphere_radius());
	float t_end = max(atm.y, 0.0);
	if (t_end <= 0.0) {
		return vec2(0.0);
	}

	float step_size = t_end / float(SUN_STEPS);
	vec2 depth = vec2(0.0);
	// Midpoint sampling: with only 4 steps this is noticeably more accurate than sampling
	// at the segment start, which would over-weight the dense sample at p itself.
	float t = step_size * 0.5;
	for (int i = 0; i < SUN_STEPS; i++) {
		float h = max(altitude_of(p + sun_direction * t), 0.0);
		depth.x += exp(-h / rayleigh_scale_height) * step_size;
		depth.y += exp(-h / mie_scale_height) * step_size;
		t += step_size;
	}
	return depth;
}

// Colour of DIRECT sunlight arriving at planet-relative point p, after atmospheric
// extinction. This is the function that makes the sun go orange at the horizon and stay
// pale gold near the poles -- planet_atmosphere.gd mirrors it on the CPU to drive the
// DirectionalLight3D, so the terrain lighting and the sky agree.
vec3 sun_transmittance(vec3 p) {
	vec2 depth = optical_depth_to_sun(p);
	vec3 tau = beta_rayleigh() * depth.x + beta_mie() * MIE_EXTINCTION_FACTOR * depth.y;
	return exp(-tau) * planet_shadow(p);
}

// ---------------------------------------------------------------------------------------
// Main scattering integral
// ---------------------------------------------------------------------------------------
// Integrates single-scattered sunlight along the view ray segment [t_start, t_end].
//   ro, rd    : ray origin (planet-relative) and normalised direction
//   out_inscatter    : light scattered INTO the eye along the segment
//   out_transmittance: how much light from BEYOND t_end survives the segment (use this to
//                      attenuate terrain, clouds or the sun disc sitting behind it)
//   jitter01  : 0..1 offset of the sample positions within their step. Pass a per-pixel
//               dither (see interleaved_gradient_noise) so the residual quantisation of a
//               10-step march becomes fine noise instead of visible bands. Pass 0.5 for
//               plain midpoint sampling.
void atmosphere_scatter(vec3 ro, vec3 rd, float t_start, float t_end, float jitter01,
		out vec3 out_inscatter, out vec3 out_transmittance) {
	out_inscatter = vec3(0.0);
	out_transmittance = vec3(1.0);
	if (t_end <= t_start) {
		return;
	}

	vec3 b_ray = beta_rayleigh();
	float b_mie = beta_mie();

	float mu = dot(rd, sun_direction);
	float ph_ray = phase_rayleigh(mu);
	float ph_mie = phase_mie(mu, mie_anisotropy);

	float step_size = (t_end - t_start) / float(VIEW_STEPS);
	vec2 view_depth = vec2(0.0); // running optical depth from the eye to the current sample
	vec3 sum_ray = vec3(0.0);
	vec3 sum_mie = vec3(0.0);

	float t = t_start + step_size * clamp(jitter01, 0.0, 1.0);
	for (int i = 0; i < VIEW_STEPS; i++) {
		vec3 p = ro + rd * t;
		float h = max(altitude_of(p), 0.0);

		float d_ray = exp(-h / rayleigh_scale_height) * step_size;
		float d_mie = exp(-h / mie_scale_height) * step_size;
		view_depth += vec2(d_ray, d_mie);

		vec2 sun_depth = optical_depth_to_sun(p);

		// Total extinction along the full path: sun -> sample -> eye.
		vec3 tau = b_ray * (view_depth.x + sun_depth.x)
				+ b_mie * MIE_EXTINCTION_FACTOR * (view_depth.y + sun_depth.y);
		vec3 attenuation = exp(-tau) * planet_shadow(p);

		sum_ray += attenuation * d_ray;
		sum_mie += attenuation * d_mie;

		t += step_size;
	}

	out_inscatter = (sum_ray * b_ray * ph_ray + sum_mie * b_mie * ph_mie)
			* sun_intensity * sun_tint;
	out_transmittance = exp(-(b_ray * view_depth.x + b_mie * MIE_EXTINCTION_FACTOR * view_depth.y));
}

// Convenience wrapper: clip an arbitrary view ray to the atmosphere shell, stopping early
// at the planet surface or at an optional occluder distance (terrain depth, cloud hit...).
// Pass occluder_t < 0.0 for "nothing in the way".
// Returns false when the ray never touches the atmosphere at all.
bool atmosphere_ray_bounds(vec3 ro, vec3 rd, float occluder_t, out float t_start, out float t_end, out bool hit_ground) {
	hit_ground = false;
	t_start = 0.0;
	t_end = -1.0;

	vec2 atm = ray_sphere(ro, rd, atmosphere_radius());
	if (atm.y <= 0.0 || atm.y <= atm.x) {
		return false; // ray misses the shell entirely, or it is fully behind us
	}
	t_start = max(atm.x, 0.0);
	t_end = atm.y;

	vec2 ground = ray_sphere(ro, rd, planet_radius);
	if (ground.y > ground.x && ground.x > 0.0) {
		t_end = min(t_end, ground.x);
		hit_ground = true;
	}
	if (occluder_t >= 0.0 && occluder_t < t_end) {
		t_end = occluder_t;
		hit_ground = false; // the occluder is nearer than the planet surface
	}
	return t_end > t_start;
}
