shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_never, unshaded, shadows_disabled, fog_disabled;

// Stylised planetary clouds: a small stack of flat procedural shapes parallaxed through a
// spherical slab, rather than a true volumetric march.
//
// Why this instead of real volumetrics: a proper cloud march is 32-64 samples of 3D noise per
// pixel, each with its own light march. That is the whole frame budget on a GTX 750 Ti. Three
// parallaxed layers cost three noise evaluations and still read as volume, because the parallax
// between layers is what the eye actually uses to infer depth.
//
// SEAMLESSNESS: every field here is evaluated on normalize(planet-relative position) -- a
// direction vector, never a UV. That is seamless by construction on a sphere: no wrap seam, no
// pole pinching, no projection choice to get wrong. It is the single most important decision in
// this shader and the reason it does not use an equirectangular cloud texture.
//
// LOW-POLY LOOK: comes from two things, both cheap. Edges are hardened with a narrow smoothstep
// so silhouettes are crisp cutouts rather than soft fluff, and the SHADING is quantised into a
// few discrete levels so it reads as flat facets. Quantising the shading and not the density is
// deliberate -- banding the density bands the alpha too, which draws iso-density rings and makes
// the sky look like a topographic map. Set cloud_facet_levels to 32 for conventional soft clouds.
//
// Lighting and colour come from atmosphere_common.gdshaderinc, so clouds pick up sunset
// reddening from the same sun_transmittance() that drives the sky and the terrain light.

#include "res://shaders/atmosphere_common.gdshaderinc"

// ---------------------------------------------------------------------------------------
// Slab geometry
// ---------------------------------------------------------------------------------------
uniform float cloud_bottom : hint_range(100.0, 12000.0) = 1200.0;
uniform float cloud_top : hint_range(200.0, 16000.0) = 1900.0;

// ---------------------------------------------------------------------------------------
// Shape
// ---------------------------------------------------------------------------------------
// Feature size. Larger = smaller, busier clouds. This multiplies a unit direction vector, so
// it is in "features per radian"-ish units and is independent of planet radius.
uniform float cloud_scale : hint_range(0.5, 40.0) = 3.5;
// Fraction of sky covered. 0 clear, 1 overcast.
uniform float cloud_coverage : hint_range(0.0, 1.0) = 0.50;
// Narrow = crisp cutout silhouettes (stylised). Wide = soft fluffy edges (realistic).
uniform float cloud_edge_hardness : hint_range(0.01, 1.0) = 0.16;
// Density quantisation. Low values give flat poster-like facets; 32+ is effectively smooth.
uniform float cloud_facet_levels : hint_range(1.0, 32.0) = 5.0;
// Fraction of slab height, from the floor, over which density ramps up from zero. Avoids a
// flat horizontal cutoff at the bottom of the slab.
uniform float cloud_profile_bottom_fade : hint_range(0.0, 0.5) = 0.22;
// Height (0..1 of the slab) above which density ramps back down to zero at the ceiling. A ray
// from outside the shell looking near its own horizon -- the normal way clouds are seen from
// orbit -- travels almost entirely at height close to 1.0, so a low value here fades exactly
// that view to nothing and makes the whole layer read as invisible from space. Close to 1.0
// keeps just enough fade to avoid a razor-flat ceiling seen edge-on from inside the slab.
uniform float cloud_profile_top_fade : hint_range(0.5, 1.0) = 0.96;
// NOTE: the domain warp that used to live here is now baked into the noise texture by
// cloud_shell.gd (FastNoiseLite domain_warp). Tune it there via warp_amplitude.

// Parallax separation between layers. This is what sells the fake volume, so it is exposed:
// too little and the stack reads as one flat sheet, too much and the layers visibly detach.
uniform float cloud_layer_offset : hint_range(0.0, 2.0) = 0.18;

// ---------------------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------------------
// Clouds drift by rotating the sample direction about the planet's spin axis. Rotating the
// direction rather than translating a noise domain keeps the motion seamless and correct
// everywhere on the sphere, including over the poles.
uniform vec3 cloud_wind_axis = vec3(0.0, 1.0, 0.0);
uniform float cloud_drift_speed : hint_range(0.0, 0.05) = 0.004;

// ---------------------------------------------------------------------------------------
// Lighting
// ---------------------------------------------------------------------------------------
uniform vec3 cloud_albedo : source_color = vec3(1.0, 0.99, 0.97);
// Surface irradiance for lit cloud. Deliberately NOT sun_intensity: that value (~22) is a
// coefficient inside the scattering integral, not a surface brightness, and using it here
// overdrives cloud by more than an order of magnitude and clips everything to flat white.
// Only the *colour* of sun_transmittance() is borrowed from the atmosphere; the scale is set
// here.
uniform float cloud_sun_intensity : hint_range(0.0, 12.0) = 2.4;
// Ambient fill so shadowed cloud is not black. Tinted by the sun colour, so it reddens too.
uniform float cloud_ambient : hint_range(0.0, 1.0) = 0.12;
uniform float cloud_light_gain : hint_range(0.0, 4.0) = 1.9;
// How much direct light a back-lit cloud face keeps. 0 = pitch-black silhouettes, 1 = no
// front/back distinction at all.
uniform float cloud_backlit_floor : hint_range(0.0, 1.0) = 0.30;
// Beer-Powder absorption. Powder is the darkening-toward-the-middle term that stops flat
// alpha-blended cloud reading like fog.
uniform float cloud_absorption : hint_range(0.1, 8.0) = 2.2;
// Opacity per metre of slab traversed.
uniform float cloud_density_scale : hint_range(0.0, 0.05) = 0.004;
// Multiplier on alpha/opacity accumulation. Higher values make clouds more opaque and solid,
// cleanly occluding background stars and space panoramas.
uniform float cloud_opacity : hint_range(0.0, 8.0) = 1.5;

// ---------------------------------------------------------------------------------------
// Night lighting
// ---------------------------------------------------------------------------------------
// Ambient starlight/airglow illumination at night, so clouds stay visible and have form in the dark.
uniform float cloud_night_ambient : hint_range(0.0, 1.0) = 0.05;
uniform vec3 cloud_night_color : source_color = vec3(0.15, 0.18, 0.28);
// Extra rim lighting from the night sky starlight along thin edges.
uniform float cloud_night_rim : hint_range(0.0, 2.0) = 0.25;

// Moon illumination on clouds (fed from EdenPlanetAtmosphere when linked).
uniform vec3 moon_direction = vec3(0.0, -1.0, 0.0);
uniform vec3 moon_tint : source_color = vec3(0.85, 0.90, 1.0);
uniform float moon_intensity : hint_range(0.0, 10.0) = 1.0;
// Pushed by EdenPlanetAtmosphere: illuminated-fraction-driven brightness (full = 1, new = 0)
// and the same colour the moon DirectionalLight3D uses, so clouds and terrain agree.
uniform float moon_phase_brightness = 1.0;
uniform vec3 moon_light_color : source_color = vec3(0.62, 0.70, 0.95);

// Sunlight leaking through thin cloud edges. Only appears when looking toward the sun, which
// is exactly when real cloud edges light up.
uniform float cloud_glow_strength : hint_range(0.0, 8.0) = 1.6;
// Tightness of the forward lobe. This is a normalised pow() lobe peaking at exactly 1.0, not
// phase_mie() -- that function peaks near 26 at these g values, which multiplied through the
// sun colour and the glow strength produced values in the hundreds and blew the sky out around
// the sun. A bounded lobe is also far easier to art-direct.
uniform float cloud_glow_sharpness : hint_range(1.0, 64.0) = 16.0;
// How sharply the glow concentrates at thin edges. Higher = a thinner, brighter rim.
uniform float cloud_edge_power : hint_range(0.5, 12.0) = 3.5;

// Per-pixel jitter of the layer sample positions, 0..1 of a step. Defaults to OFF: with only
// three layers, jittering moves samples far enough between facet shading steps that the result
// reads as a heavy stipple rather than as smooth dithering. Deterministic sampling wins here --
// unlike the sky, where the march is 10 steps and jitter genuinely helps.
uniform float cloud_jitter : hint_range(0.0, 1.0) = 0.0;

// Number of parallaxed layers. Exposed rather than fixed because it is the main quality/cost
// dial: 1 is a flat sheet, 3 reads as volume, 5+ is smoother but linearly more expensive. The
// loop is bounded by a compile-time max so the shader still unrolls sensibly.
uniform int cloud_layers : hint_range(1, 8) = 3;
const int CLOUD_LAYERS_MAX = 8;

varying vec3 v_world_pos;

// ---------------------------------------------------------------------------------------
// Noise -- baked, not procedural
// ---------------------------------------------------------------------------------------
// The cloud field is precomputed into a seamless 3D texture (see cloud_shell.gd) rather than
// evaluated procedurally. This is the single biggest performance decision in the shader.
//
// The previous version ran a hash-based value noise: 8 hashes plus 7 lerps per sample, 3
// octaves per field, 3 fields for the domain warp, 3 layers -- about 144 hash evaluations per
// pixel, which measured 10-16ms on a GTX 750 Ti. A 3D texture fetch is one hardware-filtered
// read, and the trilinear interpolation that value noise was doing by hand is exactly what the
// texture unit does for free.
//
// The domain warp is baked into the texture too (FastNoiseLite's own domain_warp at generation
// time), so the warp that used to cost three noise fields per layer now costs nothing at all.
//
// Tiling: the texture is generated seamless and sampled with repeat, so it wraps cleanly. Two
// octaves at a deliberately non-integer scale ratio keep the repeat period long enough not to
// read as an obvious grid.
uniform sampler3D cloud_noise : repeat_enable, filter_linear, hint_default_black;

// Second octave scale. Non-integer on purpose: an integer ratio makes both octaves repeat on
// the same period and the tiling becomes obvious.
uniform float cloud_detail_scale : hint_range(1.1, 8.0) = 2.37;
uniform float cloud_detail_weight : hint_range(0.0, 1.0) = 0.25;

float cloud_fbm(vec3 p) {
	float base = texture(cloud_noise, p).r;
	float detail = texture(cloud_noise, p * cloud_detail_scale).r;
	return mix(base, detail, cloud_detail_weight);
}

// rotate_about() now lives in atmosphere_common.gdshaderinc -- shared with the star panorama.

// ---------------------------------------------------------------------------------------
// Cloud field
// ---------------------------------------------------------------------------------------
// dir      : unit direction from the planet centre (this is what makes it seamless)
// height01 : 0 at the slab bottom, 1 at the top
// layer    : layer index, used to decorrelate and parallax the stack
// top_fade : cloud_profile_top_fade from outside the shell, or 1.0 (no fade) from inside/below --
//            see the call site for why the two views need different values.
float cloud_density(vec3 dir, float height01, float layer, float top_fade) {
	vec3 axis = normalize(cloud_wind_axis);
	// Each layer drifts at a slightly different rate, so the stack shears instead of sliding
	// as one rigid block -- the visual cue that reads as depth.
	float drift = TIME * cloud_drift_speed * (1.0 + layer * 0.18);
	vec3 d = rotate_about(dir, axis, drift);

	// Texture coordinates are the direction vector itself, scaled. Because it is a direction
	// and the texture is seamless, this stays free of wrap seams and pole pinching.
	vec3 sp = d * cloud_scale + vec3(layer * cloud_layer_offset);

	float n = cloud_fbm(sp);

	// Coverage threshold. Subtracting rather than smoothstepping keeps the field's own shape
	// intact and simply raises the waterline.
	float d_raw = n - (1.0 - cloud_coverage);

	// Vertical profile: fade in off the slab floor, fade out under the ceiling, so layers do
	// not terminate in hard horizontal planes.
	// Each fade is skipped outright at its "off" value rather than passed to smoothstep. With
	// top_fade = 1.0 (set for views from outside the shell) or bottom_fade = 0.0, smoothstep gets
	// two equal edges, divides by zero, and is undefined in GLSL -- on NVIDIA it returns 0, which
	// zeroed the density and made every cloud vanish from orbit.
	float floor_fade = cloud_profile_bottom_fade <= 1e-4 ? 1.0 : smoothstep(0.0, cloud_profile_bottom_fade, height01);
	float ceiling_fade = top_fade >= 0.9999 ? 1.0 : smoothstep(1.0, top_fade, height01);
	float profile = floor_fade * ceiling_fade;
	d_raw *= profile;

	// Crisp silhouette. Deliberately NOT quantised here: banding the density bands the ALPHA
	// too, which draws iso-density rings and makes the sky read as a topographic contour map
	// rather than as clouds. The facet quantisation happens on the shading term instead (see
	// fragment()), which gives flat cel-shaded interiors inside clean cutout silhouettes --
	// the look actually wanted.
	return clamp(smoothstep(0.0, cloud_edge_hardness, d_raw), 0.0, 1.0);
}

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 cam = CAMERA_POSITION_WORLD - planet_center;
	vec3 rd = normalize(v_world_pos - CAMERA_POSITION_WORLD);

	float r_in = planet_radius + cloud_bottom;
	float r_out = planet_radius + max(cloud_top, cloud_bottom + 1.0);
	float cam_r = length(cam);

	vec2 hit_out = ray_sphere(cam, rd, r_out);
	vec2 hit_in = ray_sphere(cam, rd, r_in);
	bool inner_ahead = (hit_in.y > hit_in.x) && (hit_in.x > 0.0);

	// The top-of-slab fade (cloud_profile_top_fade) exists to avoid a hard flat ceiling when
	// flying UP into the layer and looking through its own thickness from below. From outside
	// the shell that artifact cannot occur -- you only ever see its curved outer surface -- but
	// a ray grazing that surface tangentially (i.e. any view of the clouds from space) spends
	// almost its entire length at height01 close to 1, which is exactly the band the fade
	// suppresses. Disabling it outside the shell keeps the ceiling fix for the case it was
	// built for without erasing the clouds for every orbital or distant view.
	float top_fade = (cam_r > r_out) ? 1.0 : cloud_profile_top_fade;

	// Clip the view ray to the slab. Three cases, by where the camera sits relative to it.
	float t_enter;
	float t_exit;
	if (cam_r < r_in) {
		// Below the clouds: enter where the ray leaves the inner sphere.
		t_enter = max(hit_in.y, 0.0);
		t_exit = hit_out.y;
	} else if (cam_r > r_out) {
		// Above the clouds, looking down (or past them).
		if (hit_out.y <= hit_out.x || hit_out.y <= 0.0) {
			discard; // ray never reaches the slab
		}
		t_enter = max(hit_out.x, 0.0);
		t_exit = inner_ahead ? min(hit_in.x, hit_out.y) : hit_out.y;
	} else {
		// Inside the slab.
		t_enter = 0.0;
		t_exit = inner_ahead ? min(hit_in.x, hit_out.y) : hit_out.y;
	}

	if (t_exit <= t_enter) {
		discard;
	}

	// Stop at the planet surface, so clouds never draw over ground seen beyond the limb.
	vec2 ground = ray_sphere(cam, rd, planet_radius);
	if (ground.y > ground.x && ground.x > 0.0) {
		t_exit = min(t_exit, ground.x);
		if (t_exit <= t_enter) {
			discard;
		}
	}

	float seg = t_exit - t_enter;
	float step_len = seg / float(clamp(cloud_layers, 1, CLOUD_LAYERS_MAX));
	float jitter = mix(0.5, interleaved_gradient_noise(FRAGCOORD.xy), cloud_jitter);

	float mu = dot(rd, sun_direction);
	float forward_lobe = pow(max(mu, 0.0), cloud_glow_sharpness);

	// Sun colour sampled once at the slab midpoint rather than per layer. sun_transmittance()
	// runs its own SUN_STEPS march, so calling it three times tripled that cost to resolve a
	// difference of a few hundred metres of altitude. The one case where it is visibly wrong is
	// a grazing view at the terminator, where the segment can span tens of kilometres and the
	// far end really is in shadow -- acceptable for the saving.
	vec3 mid_p = cam + rd * ((t_enter + t_exit) * 0.5);
	vec3 sun_col = sun_transmittance(mid_p) * cloud_sun_intensity * sun_tint;

	vec3 accum = vec3(0.0);
	float trans = 1.0;

	int layer_count = clamp(cloud_layers, 1, CLOUD_LAYERS_MAX);
	for (int i = 0; i < CLOUD_LAYERS_MAX; i++) {
		if (i >= layer_count) {
			break;
		}
		float t = t_enter + step_len * (float(i) + jitter);
		vec3 p = cam + rd * t;
		float pr = length(p);
		float height01 = clamp((pr - r_in) / max(r_out - r_in, 1.0), 0.0, 1.0);

		float dens = cloud_density(p / max(pr, 1e-3), height01, float(i), top_fade);
		if (dens <= 0.0) {
			continue;
		}

		// Beer-Powder: Beer alone makes thick cloud uniformly dark, which reads as fog. The
		// powder term restores the darkening toward the interior that gives clouds their form.
		float beer = exp(-dens * cloud_absorption);
		float powder = 1.0 - exp(-dens * 2.0 * cloud_absorption);

		// Sun elevation at the cloud, so the underside goes dark as the sun sets.
		float ndl = max(dot(p / max(pr, 1e-3), sun_direction), 0.0);

		// Front-lit vs back-lit. ndl alone cannot tell these apart: it only knows where the sun
		// is relative to the cloud, not which face the viewer sees. Looking away from the sun
		// you see the lit top; looking toward it you see a shadowed base that should go dark
		// and grow a bright rim. Without this every cloud reads as uniformly bright paper.
		float facing = 0.5 - 0.5 * mu;
		float direct = beer * powder * ndl * cloud_light_gain * mix(cloud_backlit_floor, 1.0, facing);

		// Thin edges glow when looking toward the sun.
		float edge = pow(1.0 - dens, cloud_edge_power);
		vec3 glow = sun_col * forward_lobe * edge * cloud_glow_strength;

		// Cel-step the shading into flat facets. This is where the low-poly look comes from --
		// on the shading, not on the density. cloud_facet_levels >= 32 is effectively smooth.
		float shade = cloud_ambient + direct;
		shade = floor(shade * cloud_facet_levels + 0.5) / cloud_facet_levels;

		// Night ambient fill: starlight/airglow with volumetric beer/powder shaping and rim
		// highlight, so clouds stay visible as 3D cel-shaded bodies even when the sun is down.
		float night_facing = mix(1.0, 1.0 + cloud_night_rim, edge);
		float night_shade = cloud_night_ambient * mix(0.5, 1.0, beer * powder) * night_facing;

		// Directional moonlight on the cloud tops/sides
		float moon_ndl = max(dot(p / max(pr, 1e-3), moon_direction), 0.0);
		float moon_mu = dot(rd, moon_direction);
		float moon_facing = 0.5 - 0.5 * moon_mu;
		float moon_direct = beer * powder * moon_ndl * mix(cloud_backlit_floor, 1.0, moon_facing) * 0.4;
		// Scaled by phase (a new moon lights nothing) and occluded by the planet when the moon
		// is below this cloud's horizon.
		moon_direct *= moon_phase_brightness * planet_shadow_dir(p, moon_direction);
		night_shade += moon_direct * moon_intensity;

		night_shade = floor(night_shade * cloud_facet_levels + 0.5) / cloud_facet_levels;
		vec3 night_ambient_lit = mix(cloud_night_color, moon_light_color, clamp(moon_direct * 2.0, 0.0, 1.0)) * night_shade;

		vec3 lit = cloud_albedo * (sun_col * shade + night_ambient_lit) + glow;

		// Front-to-back compositing with opacity scale.
		float alpha = 1.0 - exp(-dens * cloud_density_scale * max(cloud_opacity, 0.001) * step_len);
		accum += lit * alpha * trans;
		trans *= 1.0 - alpha;
	}

	float total_alpha = 1.0 - trans;
	if (total_alpha <= 0.002) {
		discard;
	}

	ALBEDO = accum / max(total_alpha, 1e-4);
	ALPHA = clamp(total_alpha, 0.0, 1.0);
}
