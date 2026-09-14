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
// LOW-POLY LOOK: the cloud field is triangulated on a tetrahedral lattice (see cloud_field), so
// silhouettes are straight-edged polygons and every facet has one flat normal. Edges are ramped
// over ~1.5 pixels, so they stay crisp at any distance, and the lighting is cel-stepped into a
// shadow tone and a sun tone. Quantising the shading and not the density is deliberate --
// banding the density bands the alpha too and draws topographic rings.
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
uniform float cloud_coverage : hint_range(0.0, 1.0) = 0.40;
// Narrow = crisp cutout silhouettes (stylised). Wide = soft fluffy edges (realistic).
// 0 = crisp, antialiased polygon edges at any distance. Higher = softer, fluffier edges.
uniform float cloud_edge_hardness : hint_range(0.0, 1.0) = 0.14;
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
uniform float cloud_layer_offset : hint_range(0.0, 2.0) = 0.0;
// Volumetric layering. With cloud_layer_offset at 0 every layer is the SAME cloud shape, and each is
// eroded (shrunk inward) by this much times its distance from the middle of the slab. The middle
// layer carries the full silhouette; layers above and below only keep the thicker core. Seen from
// anywhere, a cloud's rim is then covered by one layer and its core by all of them -- translucent
// glowing edges around a solid middle, which is how a real cloud reads. In field units: 0.1 shrinks
// the outer layers by roughly a quarter of a typical cloud.
uniform float cloud_layer_erosion : hint_range(0.0, 0.5) = 0.22;
// Opacity of the thinnest rim relative to the core. Each layer's opacity ramps from this at the
// silhouette to full at the thick middle. Low = soft translucent edges, 1 = uniformly solid.
uniform float cloud_edge_opacity : hint_range(0.0, 1.0) = 0.12;

// ---------------------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------------------
// Clouds drift by rotating the sample direction about the planet's spin axis. Rotating the
// direction rather than translating a noise domain keeps the motion seamless and correct
// everywhere on the sphere, including over the poles.
uniform vec3 cloud_wind_axis = vec3(0.0, 1.0, 0.0);
uniform float cloud_drift_speed : hint_range(0.0, 0.05) = 0.001;

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
uniform float cloud_light_gain : hint_range(0.0, 4.0) = 1.0;
// How much direct light a back-lit cloud face keeps. 0 = pitch-black silhouettes, 1 = no
// front/back distinction at all.
uniform float cloud_backlit_floor : hint_range(0.0, 1.0) = 0.30;
// Sunlight skimming under the cloud base while the sun is low. Not gated by facing: the lit base
// is seen from below whichever way the viewer looks.
uniform float cloud_sunset_underlight : hint_range(0.0, 4.0) = 0.5;
// Beer-Powder absorption. Powder is the darkening-toward-the-middle term that stops flat
// alpha-blended cloud reading like fog.
uniform float cloud_absorption : hint_range(0.1, 8.0) = 2.0;
// Opacity per metre of slab traversed.
uniform float cloud_density_scale : hint_range(0.0, 0.05) = 0.004;
// Multiplier on alpha/opacity accumulation. Higher values make clouds more opaque and solid,
// cleanly occluding background stars and space panoramas.
uniform float cloud_opacity : hint_range(0.0, 8.0) = 3.0;

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
uniform float cloud_glow_strength : hint_range(0.0, 8.0) = 2.5;
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

// ---------------------------------------------------------------------------------------
// Style: polygon facets and sky-tinted shading
// ---------------------------------------------------------------------------------------
// The polygon look comes from triangulating the cloud field: it is sampled only at the corners of
// a tetrahedral lattice and interpolated linearly inside each tetrahedron. The coverage contour of
// a piecewise-linear field is made of straight segments, so silhouettes become true polygons, and
// the field's gradient is constant inside each tetrahedron, which gives every facet one flat
// normal to light -- the same thing that makes a low-poly mesh look low-poly.
uniform float cloud_facet_mix : hint_range(0.0, 1.0) = 0.12;
// Lattice cells per unit of noise space. Higher = smaller polygons.
uniform float cloud_facet_scale : hint_range(1.0, 48.0) = 12.0;
// How strongly the facet slope tilts the lighting normal. 0 lights every facet the same.
uniform float cloud_facet_relief : hint_range(0.0, 1.0) = 0.1;

// ---------------------------------------------------------------------------------------
// Cloud types: regional shape variety
// ---------------------------------------------------------------------------------------
// Rounds the vertical profile into a bulging dome instead of a flat-topped slab -- the puffy,
// cauliflower look of cumulus. 0 keeps the old flat slab (more stratus-like).
uniform float cloud_puffiness : hint_range(0.0, 1.0) = 0.55;
// Stretches the noise sample along the wind direction, drawing out thin streaky wisps -- the
// cirrus look. 0 is round/isotropic.
uniform float cloud_wispiness : hint_range(0.0, 1.0) = 0.25;
// How much puffiness/wispiness vary from one region of sky to another, so puffy and wispy patches
// both appear rather than the whole sky reading as one uniform cloud type. Sampled from the same
// baked noise at a much lower frequency than the clouds themselves.
uniform float cloud_type_variation : hint_range(0.0, 1.0) = 0.6;
uniform float cloud_type_scale : hint_range(0.01, 2.0) = 0.35;
uniform vec3 cloud_shadow_color : source_color = vec3(0.72, 0.78, 0.95);
uniform float cloud_sky_ambient_mix : hint_range(0.0, 1.0) = 0.7;
uniform float cloud_sky_ambient_gain : hint_range(0.0, 8.0) = 1.2;
// Sky radiance straight up from the camera, pushed each frame by EdenPlanetAtmosphere.
uniform vec3 sky_ambient_color = vec3(0.35, 0.45, 0.6);

// ---------------------------------------------------------------------------------------
// Weather: flow map + regional coverage
// ---------------------------------------------------------------------------------------
// Baked by EdenCloudShell::rebuild_weather(): RGB world-space wind (tangent to the sphere), A a
// coverage delta. A cubemap rather than an equirect texture, because it is sampled by direction
// like everything else here -- no pole singularity in the wind vectors, no wrap seam.
uniform samplerCube weather_map : filter_linear, hint_default_black;
uniform bool weather_enabled = false;
uniform float flow_speed : hint_range(0.0, 1.0) = 0.002;
uniform float flow_strength : hint_range(0.0, 1.0) = 0.2;
uniform float coverage_map_strength : hint_range(0.0, 2.0) = 1.0;

// Noise at the four corners of a lattice tetrahedron, read with an advection offset.
vec4 corner_values(vec3 c0, vec3 c1, vec3 c2, vec3 c3, vec3 offset) {
	return vec4(textureLod(cloud_noise, c0 + offset, 0.0).r, textureLod(cloud_noise, c1 + offset, 0.0).r,
			textureLod(cloud_noise, c2 + offset, 0.0).r, textureLod(cloud_noise, c3 + offset, 0.0).r);
}

float smooth_value(vec3 sp, vec3 offset) {
	vec3 np = sp + offset;
	return mix(textureLod(cloud_noise, np, 0.0).r, textureLod(cloud_noise, np * cloud_detail_scale, 0.0).r, cloud_detail_weight);
}

// The cloud field, triangulated. `sp` picks the lattice tetrahedron; the offsets shift only where
// the corner values are read from the noise. Every layer and both flow phases therefore share ONE
// triangulation and only the corner values differ -- and a blend of linear fields on the same
// tetrahedron is still linear, so the result stays a clean polygon mesh instead of several
// overlapping lattices shattering into slivers. It is also the classic low-poly animation: the mesh
// stays put while its vertex values move, so edges slide across it like a low-poly water surface.
//
// The lattice is the simplex-noise one: a cube grid in skewed space, each cube split into six
// tetrahedra along the path 0 -> largest axis -> two largest axes -> (1,1,1) (Kuhn triangulation).
// Inside a tetrahedron the barycentric weights are just differences of the SORTED fractional
// coordinates, so interpolation and the constant per-facet gradient need no matrix inverse.
//
// `blend` weights offset1 against offset0; at exactly 0 or 1 only one set of corners is read.
// Returns (signed density before the edge ramp, gradient in sampling-space units).
vec4 cloud_field(vec3 sp, vec3 offset0, vec3 offset1, float blend, float coverage) {
	vec3 qs = sp * cloud_facet_scale;
	qs += dot(qs, vec3(1.0 / 3.0));
	vec3 cell = floor(qs);
	vec3 f = qs - cell;
	vec3 g = step(f.yzx, f);
	vec3 l = 1.0 - g;
	vec3 i1 = min(g, l.zxy); // largest fractional axis
	vec3 i2 = max(g, l.zxy); // the two largest
	vec3 i12 = i2 - i1;
	vec3 i3 = 1.0 - i2; // smallest
	float inv_scale = 1.0 / cloud_facet_scale;
	// Corners unskewed back into noise space.
	vec3 k1 = cell + i1;
	vec3 k2 = cell + i2;
	vec3 k3 = cell + 1.0;
	vec3 c0 = (cell - dot(cell, vec3(1.0 / 6.0))) * inv_scale;
	vec3 c1 = (k1 - dot(k1, vec3(1.0 / 6.0))) * inv_scale;
	vec3 c2 = (k2 - dot(k2, vec3(1.0 / 6.0))) * inv_scale;
	vec3 c3 = (k3 - dot(k3, vec3(1.0 / 6.0))) * inv_scale;

	bool soft = cloud_facet_mix < 1.0;
	vec4 v = vec4(0.0);
	float n = 0.0;
	if (blend < 1.0) {
		v += corner_values(c0, c1, c2, c3, offset0) * (1.0 - blend);
		if (soft) {
			n += smooth_value(sp, offset0) * (1.0 - blend);
		}
	}
	if (blend > 0.0) {
		v += corner_values(c0, c1, c2, c3, offset1) * blend;
		if (soft) {
			n += smooth_value(sp, offset1) * blend;
		}
	}

	// Contrast-preserving crossfade. A plain 50/50 mix of two different noise patterns pulls every value
	// toward the middle, so mid-fade the coverage threshold sees a flatter field and clouds visibly thin
	// out and pop back. Rescaling by the blend's standard deviation keeps coverage steady through it.
	float spread = inversesqrt(blend * blend + (1.0 - blend) * (1.0 - blend));
	v = 0.5 + (v - 0.5) * spread;
	n = 0.5 + (n - 0.5) * spread;
	vec3 dv = vec3(v.y - v.x, v.z - v.y, v.w - v.z);
	float value = v.x + dot(vec3(dot(f, i1), dot(f, i12), dot(f, i3)), dv);
	vec3 grad_skewed = i1 * dv.x + i12 * dv.y + i3 * dv.z;
	// Chain rule through the skew (s = q + sum(q) / 3), then into sampling-space units.
	vec3 grad = (grad_skewed + dot(grad_skewed, vec3(1.0 / 3.0))) * cloud_facet_scale;

	float d = value - (1.0 - coverage);
	if (soft) {
		d = mix(n - (1.0 - coverage), d, cloud_facet_mix);
	}
	return vec4(d, grad);
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
// dir      : unit direction from the planet centre (this is what makes it seamless)
// height01 : 0 at the slab bottom, 1 at the top
// layer    : layer index, used to decorrelate and parallax the stack
// top_fade : cloud_profile_top_fade from inside/below the shell, 1.0 (no fade) from outside
// coverage : local coverage after the weather map
// flow     : world-space wind at this location
// Returns (edge-ramped density, field gradient in sampling space).
vec4 cloud_density(vec3 dir, float height01, float layer, float top_fade, float coverage, vec3 flow, float pixel_sp, out float thickness) {
	vec3 layer_offset = vec3(layer * cloud_layer_offset);

	// Regional cloud type: a single low-frequency lookup shared by puffiness and wispiness, so
	// nearby clouds vary together instead of flickering independently facet to facet.
	float type_t = cloud_type_variation > 0.0 ? textureLod(cloud_noise, dir * cloud_type_scale, 0.0).r : 0.5;
	float puff = cloud_puffiness * mix(1.0, type_t, cloud_type_variation);
	float wisp = cloud_wispiness * mix(1.0, 1.0 - type_t, cloud_type_variation);
	// Two kinds of motion, kept apart on purpose.
	//
	// DRIFT carries the clouds: the whole field, lattice included, rotates about the wind axis. That is
	// pure transport -- shapes glide along intact and never form or dissolve because of it.
	//
	// FLOW is the regional weather: the noise is also offset along the baked wind, so clouds bend around
	// ranges and stream along the circulation bands. An offset that grows forever shears shapes into
	// streaks, so it runs in cycles, two phases half a cycle apart, each reset hidden while its weight
	// is zero. The crossfade IS clouds forming and dissolving, so it spans the whole cycle at a steady
	// rate -- with flow_speed the default 0.002 a cloud takes about four minutes to fade in or out.
	// Layers get a small phase offset so the stack does not pulse in unison.
	vec3 axis = normalize(cloud_wind_axis);
	float drift = TIME * cloud_drift_speed;
	vec3 dir_r = rotate_about(dir, axis, drift);
	if (wisp > 0.0) {
		// Compress along the wind axis and stretch across it: noise features become long streaks
		// pointing along the wind, the way cirrus is combed out by high-altitude shear.
		vec3 wind_axis = normalize(cross(axis, dir_r) + axis * 1e-4);
		float along = dot(dir_r, wind_axis);
		dir_r += wind_axis * (along * wisp * 2.0);
	}
	vec3 sp = dir_r * cloud_scale;
	vec4 field;
	if (weather_enabled) {
		float t = TIME * flow_speed + layer * 0.07;
		float ph0 = fract(t);
		float ph1 = fract(t + 0.5);
		float blend = smoothstep(0.0, 1.0, abs(ph0 - 0.5) * 2.0);
		// The wind is baked in planet space; turn it with the drift so it stays aligned with the field.
		vec3 wind = rotate_about(flow, axis, drift) * (flow_strength * cloud_scale);
		field = cloud_field(sp, layer_offset - wind * ph0, layer_offset - wind * ph1, blend, coverage);
	} else {
		field = cloud_field(sp, layer_offset, layer_offset, 0.0, coverage);
	}

	// Vertical profile: fade in off the slab floor, fade out under the ceiling, so layers do
	// not terminate in hard horizontal planes.
	// Each fade is skipped outright at its "off" value rather than passed to smoothstep. With
	// top_fade = 1.0 (set for views from outside the shell) or bottom_fade = 0.0, smoothstep gets
	// two equal edges, divides by zero, and is undefined in GLSL -- on NVIDIA it returns 0, which
	// zeroed the density and made every cloud vanish from orbit.
	float floor_fade = cloud_profile_bottom_fade <= 1e-4 ? 1.0 : smoothstep(0.0, cloud_profile_bottom_fade, height01);
	float ceiling_fade = top_fade >= 0.9999 ? 1.0 : smoothstep(1.0, top_fade, height01);
	// Puffiness: bulge the profile so it peaks at the slab middle and rounds off toward both the
	// floor and the ceiling, rather than sitting flat once past the fade bands. A dome, not a slab.
	float dome = 1.0 - puff * (1.0 - 4.0 * height01 * (1.0 - height01));
	floor_fade *= mix(1.0, dome, step(height01, 0.5));
	ceiling_fade *= mix(1.0, dome, step(0.5, height01));
	// Onion erosion (see cloud_layer_erosion). Seen from outside the shell only the lower half erodes:
	// that view looks down through the top, so the top layer must carry the full silhouette.
	float from_middle = top_fade >= 0.9999 ? max(1.0 - 2.0 * height01, 0.0) : abs(2.0 * height01 - 1.0);
	float d_raw = field.x * floor_fade * ceiling_fade - cloud_layer_erosion * from_middle;
	// 0 at the silhouette, 1 in the thickest part of the cloud.
	thickness = clamp(d_raw / max(coverage, 0.05), 0.0, 1.0);

	// Crisp silhouette of even width on screen. The ramp runs over distance to the contour (field /
	// slope) rather than over field value -- otherwise a shallow facet stretches it into a wide blur
	// while a steep one stays razor sharp -- and is at least ~1.5 pixels wide, so edges antialias
	// instead of shimmering, and no wider unless cloud_edge_hardness asks for softness. A width fixed
	// in sphere units instead blurs over dozens of pixels when looking up at a cloud base from below.
	float dist = d_raw / max(length(field.yzw), 0.5);
	float width = max(pixel_sp * 1.5, cloud_edge_hardness / 6.0);
	return vec4(smoothstep(0.0, width, dist), field.yzw);
}

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec3 cam = clamp_planet_relative(CAMERA_POSITION_WORLD - planet_center);
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

	// Regional weather along this view ray, sampled once at the slab midpoint: weather varies over
	// thousands of kilometres, so one lookup per pixel is plenty. Explicit LOD, because derivatives
	// are undefined inside the per-pixel early-outs above.
	vec3 flow = vec3(0.0);
	float coverage = cloud_coverage;
	if (weather_enabled) {
		vec4 weather = textureLod(weather_map, normalize(mid_p), 0.0);
		flow = weather.xyz;
		coverage = clamp(cloud_coverage + weather.a * coverage_map_strength, 0.0, 1.0);
	}

	vec3 accum = vec3(0.0);
	float trans = 1.0;
	float pixel_angle = 2.0 / (abs(PROJECTION_MATRIX[1][1]) * VIEWPORT_SIZE.y);

	// ---- Per-pixel lighting terms, hoisted out of the layer loop. A layer is a few hundred metres of
	// altitude apart from the next; nothing below changes visibly across that, and on a GTX 750 Ti the
	// per-layer arithmetic, not the texture reads, is what the frame time is made of.
	vec3 mid_up = normalize(mid_p);
	// Front-lit vs back-lit. ndl alone cannot tell these apart: it only knows where the sun is relative
	// to the cloud, not which face the viewer sees.
	float facing_gain = mix(cloud_backlit_floor, 1.0, 0.5 - 0.5 * mu) * cloud_light_gain;
	// Only a view from BELOW sees the base that light has crossed the body to reach.
	float looking_up = clamp(dot(rd, mid_up) * 4.0, 0.0, 1.0);
	// Low sun skims under the base, unshadowed by the body: orange and pink undersides at sunset.
	// One-sided on purpose -- real grazing light only reaches cloud approaching the terminator from
	// the DAY side, never past it into true night -- and narrow, so from orbit it reads as a glowing
	// ring localised at the terminator rather than smeared across most of the visible disc. A
	// symmetric, wider falloff here used to do exactly that: a single mid_p sample per pixel has no
	// idea it is being viewed from a wide orbital FOV instead of from the ground it was tuned for.
	float sun_alt = dot(mid_up, sun_direction);
	float grazing = smoothstep(-0.06, 0.16, sun_alt) * (1.0 - smoothstep(0.16, 0.55, sun_alt));
	float underlight = grazing * cloud_sunset_underlight;
	// Low sun: saturate the sunlight's hue (ratios squared, brightest channel kept) so lit undersides
	// read orange and pink instead of being bleached to cream by the tonemapper's highlight rolloff.
	vec3 sun_tone = mix(sun_col, sun_col * sun_col / max(max(sun_col.r, max(sun_col.g, sun_col.b)), 1e-4), grazing);
	// Two-tone: a shadow tone taken from the sky (blue by day, violet at dusk) and the sunlight. Mixing
	// rather than adding keeps real contrast between them; ambient added on top of the lit tone washed
	// every cloud out to one pale grey. The sky colour is half desaturated first: at full saturation
	// shadowed cloud is the same blue as the sky behind it and reads as a hole.
	vec3 sky_fill = mix(vec3(dot(sky_ambient_color, vec3(0.3, 0.59, 0.11))), sky_ambient_color, 0.5);
	// sky_ambient_color is ONE value: the sky straight up from the CAMERA, not from each cloud. From
	// the ground camera and cloud share roughly the same sky, so that is a fine stand-in -- but from
	// orbit one camera position sees clouds on both the day and night side at once, and without this
	// they all took the same bright daytime fill regardless. Fading it by the cloud's OWN local sun
	// elevation keeps it truthful there too; the night ambient term below covers what is left once
	// this fades out.
	float local_daylight = smoothstep(-0.3, 0.15, dot(mid_up, sun_direction));
	vec3 shadow_tone = mix(sun_col * cloud_ambient, sky_fill * cloud_shadow_color * cloud_sky_ambient_gain * local_daylight, cloud_sky_ambient_mix);
	vec3 glow_col = sun_col * forward_lobe * cloud_glow_strength;
	// Moonlight: direction, facing, phase (a new moon lights nothing) and planet occlusion.
	float moon_light = max(dot(mid_up, moon_direction), 0.0) * mix(cloud_backlit_floor, 1.0, 0.5 - 0.5 * dot(rd, moon_direction))
			* 0.4 * moon_phase_brightness * planet_shadow_dir(mid_p, moon_direction);
	float opacity_per_m = cloud_density_scale * max(cloud_opacity, 0.001) * step_len;

	int layer_count = clamp(cloud_layers, 1, CLOUD_LAYERS_MAX);
	for (int i = 0; i < CLOUD_LAYERS_MAX; i++) {
		// Stop once what is behind can no longer show through.
		if (i >= layer_count || trans < 0.02) {
			break;
		}
		float t = t_enter + step_len * (float(i) + jitter);
		vec3 p = cam + rd * t;
		float pr = length(p);
		float height01 = clamp((pr - r_in) / max(r_out - r_in, 1.0), 0.0, 1.0);

		vec3 up = p / max(pr, 1e-3);
		float thick;
		// One pixel's footprint at this sample, in cloud sampling-space units (dir * cloud_scale).
		float pixel_sp = t * pixel_angle * cloud_scale / max(pr, 1.0);
		vec4 cd = cloud_density(up, height01, float(i), top_fade, coverage, flow, pixel_sp, thick);
		float dens = cd.x;
		if (dens <= 0.0) {
			continue;
		}

		float beer = exp(-thick * cloud_absorption);
		float beer_powder = beer * (1.0 - beer * beer); // Beer x powder, powder = 1 - exp(-2 * a * thick)

		// Facet normal: the cloud treated as a heightfield whose surface falls away where density does.
		// The gradient is constant per lattice tetrahedron, so each facet gets one flat shade. Tilt is
		// bounded below 45 degrees: thin facets can be steep, and an unbounded tilt flips them to shadow.
		vec3 tilt = (cd.yzw - up * dot(cd.yzw, up)) * (cloud_facet_relief * cloud_facet_mix);
		vec3 facet_n = normalize(up - tilt / (1.0 + length(tilt)));
		float facet_sun = dot(facet_n, sun_direction);

		float light = max(facet_sun, 0.0) * facing_gain * mix(1.0, beer, looking_up) + underlight;
		// Cel-step into flat tones. Quantising the light and not the density is deliberate: banding the
		// density bands the alpha too and draws topographic rings.
		light = floor(min(light, 1.25) * cloud_facet_levels + 0.5) / cloud_facet_levels;

		// Thin cloud glows when looking toward the sun. Driven by thickness, not by the silhouette ramp,
		// which is only a pixel or two wide.
		float edge = pow(1.0 - thick, cloud_edge_power);

		// Night: starlight/airglow fill with a rim on thin parts, plus moonlight, so clouds keep form
		// in the dark.
		float moon_direct = beer_powder * moon_light;
		float night_shade = cloud_night_ambient * mix(0.5, 1.0, beer_powder) * (1.0 + cloud_night_rim * edge) + moon_direct * moon_intensity;
		// Not cel-stepped: night light is a few percent, and rounding to cloud_facet_levels steps took
		// it to exactly zero, leaving night clouds as flat black cutouts.
		vec3 night_lit = mix(cloud_night_color, moon_light_color, clamp(moon_direct * 2.0, 0.0, 1.0)) * night_shade;

		// Flat per-facet brightness: faces tilted toward the sun are lighter, away darker, in the shadow
		// tone too -- so the low-poly faceting still reads from below, where almost everything is shadow.
		// Clamped: unbounded, a facet tilted enough toward the sun could push this well past 1 even on
		// a facet whose SMOOTH position is on the night side, amplifying the (correctly dark) shadow
		// tone back up into something that reads as lit -- which is exactly what large cloud_absorption/
		// cloud_sun_intensity values (a bright sun_tone to amplify toward) made obvious from orbit.
		float facet_mod = clamp(1.0 + facet_sun - dot(up, sun_direction), 0.5, 1.5);
		vec3 lit = cloud_albedo * (mix(shadow_tone, sun_tone, light) * facet_mod + night_lit);

		// Front-to-back compositing.
		// Opacity rises from cloud_edge_opacity at the rim to full in the thick core.
		float alpha = 1.0 - exp(-dens * opacity_per_m * mix(cloud_edge_opacity, 1.0, thick));
		// Glow is light scattered out of the thin rim, so it is added as emission rather than scaled by
		// the rim's (deliberately low) opacity -- otherwise translucent edges would lose their glow.
		accum += (lit * alpha + glow_col * (edge * dens)) * trans;
		trans *= 1.0 - alpha;
	}

	float total_alpha = 1.0 - trans;
	if (total_alpha <= 0.002) {
		discard;
	}

	ALBEDO = accum / max(total_alpha, 1e-4);
	ALPHA = clamp(total_alpha, 0.0, 1.0);
}
