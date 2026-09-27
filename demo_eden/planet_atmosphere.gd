@tool
extends Node3D
class_name PlanetAtmosphere
## Drives the planetary atmosphere: owns the sun, the sky material, and the link between them.
##
## The problem this solves: a sky shader can compute gorgeous scattering, but Godot will still
## light your terrain with whatever flat white DirectionalLight3D you configured, so at sunset
## the sky goes orange and the ground stays midday-blue. This node closes that loop. It mirrors
## `sun_transmittance()` from shaders/atmosphere_common.gdshaderinc on the CPU, evaluated at the
## camera's own planet-relative position, and feeds the result to the DirectionalLight3D. Sky
## and terrain then agree by construction, and the sun's colour changes as the player moves
## across the planet -- pale gold near the poles, white overhead at the equator, deep red at the
## terminator -- without any authored time-of-day gradient.
##
## Ambient light is handled separately and for free: the sky shader renders into Godot's radiance
## cubemap, so setting the Environment to AMBIENT_SOURCE_SKY (this node does that) makes ambient
## track the sky automatically.
##
## Usage: drop under the scene root, point `sun_light_path` at a DirectionalLight3D and
## `environment_path` at a WorldEnvironment, set `planet_radius` to match the generator. Every
## demo currently centres its planet at the world origin, so `planet_center` stays at zero.
##
## ponytail: deliberately GDScript, not a C++ module. It is a few dozen float ops per frame with
## no hot loop and no threading, and keeping it in script means atmosphere tuning is an editor
## reload instead of a full engine rebuild. Move it to C++ only if profiling ever shows it
## mattering, which it will not.

# ---------------------------------------------------------------------------------------------
# Wiring
# ---------------------------------------------------------------------------------------------
@export_node_path("DirectionalLight3D") var sun_light_path: NodePath
@export_node_path("WorldEnvironment") var environment_path: NodePath
## Extra ShaderMaterials that also include atmosphere_common.gdshaderinc (clouds, terrain haze).
## They receive exactly the same uniform values as the sky, so every surface agrees on one model.
@export var linked_materials: Array[ShaderMaterial] = []
## Reconfigure the WorldEnvironment on ready (background -> sky, ambient -> sky). Turn off if the
## scene wants to own those settings itself.
@export var manage_environment: bool = true

# ---------------------------------------------------------------------------------------------
# Planet
# ---------------------------------------------------------------------------------------------
@export_group("Planet")
## Must match the generator. EdenPlanetGeneratorV3 defaults to 40000; demos run 10k / 20k / 40k.
@export var planet_radius: float = 40000.0
@export var planet_center: Vector3 = Vector3.ZERO
@export_range(500.0, 20000.0) var atmosphere_height: float = 4000.0

# ---------------------------------------------------------------------------------------------
# Scattering
# ---------------------------------------------------------------------------------------------
@export_group("Scattering")
## Density falloff heights. See the include's header: these are compressed relative to Earth's
## 8000m/1200m so the visible gradient fits a 40km planet, and the strength knobs below are
## auto-renormalised for that, so 1.0 always means "Earth-like".
@export_range(50.0, 8000.0) var rayleigh_scale_height: float = 800.0
@export_range(10.0, 2000.0) var mie_scale_height: float = 120.0
## See the include's header for why these are not 1.0: a 40km planet's grazing path is ~4x
## shorter relative to its scale height than Earth's, so Rayleigh is boosted to recover sunset
## colour separation, and Mie is cut to stop its white forward lobe burying that colour.
@export_range(0.0, 8.0) var rayleigh_strength: float = 2.5
@export_range(0.0, 8.0) var mie_strength: float = 0.35
@export_range(0.0, 0.999) var mie_anisotropy: float = 0.76

# ---------------------------------------------------------------------------------------------
# Sun
# ---------------------------------------------------------------------------------------------
@export_group("Sun")
@export var sun_tint: Color = Color(1.0, 1.0, 1.0)
@export_range(0.0, 100.0) var sun_intensity: float = 22.0
@export_range(0.001, 0.2) var sun_angular_radius: float = 0.02
## Disc brightness as a multiple of sun_intensity. Kept separate because sun_intensity is tuned
## for sky brightness, and a real sun is far brighter than the sky it lights -- sharing one value
## makes the disc disappear into the horizon glow at sunset.
@export_range(1.0, 200.0) var sun_disc_brightness: float = 30.0
## Half-width in metres of the soft band at the planet's shadow edge. This is the sunset-banding
## control: a hard shadow edge quantises against the raymarch step count and prints arc-shaped
## bands into the sky. Raise if banding reappears, lower if the night side looks washed out.
@export_range(0.0, 4000.0) var shadow_softness: float = 600.0
## Seconds per full planetary rotation. 0 freezes the sun at `sun_time_of_day`.
@export var day_length_seconds: float = 0.0
## Normalised position in the day when the cycle is frozen, or the starting phase when it runs.
## 0.0 = sun over longitude +X, 0.25 = over +Z, and so on.
@export_range(0.0, 1.0) var sun_time_of_day: float = 0.30
## Seasonal declination: the sun's angle above the planet's equatorial plane. Earth swings
## +/-23.5 over a year. Constant over a single day, which is why latitude alone decides whether
## the sun rises steeply (equator) or merely grazes the horizon (poles).
@export_range(-89.0, 89.0) var sun_declination_deg: float = 12.0
## Planet spin axis, in world space. Latitude in all the scattering math is measured from this.
@export var planet_spin_axis: Vector3 = Vector3.UP
## Peak energy for the DirectionalLight3D, reached when the sun is directly overhead at sea level.
@export_range(0.0, 16.0) var sun_light_energy: float = 3.2
## Floor on the light colour's brightness so the night side is navigable rather than pitch black.
@export_range(0.0, 1.0) var night_light_floor: float = 0.02

# ---------------------------------------------------------------------------------------------
# Sky appearance
# ---------------------------------------------------------------------------------------------
@export_group("Sky")
@export var ground_albedo: Color = Color(0.11, 0.10, 0.09)
@export_range(0.0, 2.0) var ground_light_scale: float = 0.15
@export_range(0.0, 4.0) var star_intensity: float = 1.0
@export_range(0.0, 8.0) var exposure: float = 1.0

# ---------------------------------------------------------------------------------------------
# Internals
# ---------------------------------------------------------------------------------------------
const SKY_SHADER_PATH := "res://shaders/planet_sky.gdshader"

# Earth reference values, duplicated from atmosphere_common.gdshaderinc. These two copies must
# stay in step; that is the price of evaluating the same model on both CPU and GPU. If they ever
# drift, the symptom is sky and terrain lighting disagreeing at sunset.
const BETA_RAYLEIGH_EARTH := Vector3(5.8e-6, 13.5e-6, 33.1e-6)
const BETA_MIE_EARTH := 21e-6
const EARTH_RAYLEIGH_SCALE_H := 8000.0
const EARTH_MIE_SCALE_H := 1200.0
const MIE_EXTINCTION_FACTOR := 1.1
const SUN_STEPS := 4

# Transmittance LUT dimensions. The u axis (sun angle) needs the resolution because optical
# depth changes fastest near the horizon; altitude is gentle by comparison.
const LUT_WIDTH := 128
const LUT_HEIGHT := 48
# Bake-time march steps. Generous compared to the shader's SUN_STEPS because this runs once.
const LUT_STEPS := 24

var _sky_material: ShaderMaterial
var _lut_texture: ImageTexture
# Parameters the LUT was baked against; changing any of them invalidates it.
var _lut_signature := ""
var _sky: Sky
var _env_configured := false
var _sun_light: DirectionalLight3D
var _world_env: WorldEnvironment
var _sun_direction := Vector3.UP


func _ready() -> void:
	_resolve_nodes()
	_build_sky()
	_try_configure_environment()
	set_process(true)


func _resolve_nodes() -> void:
	if sun_light_path != NodePath():
		_sun_light = get_node_or_null(sun_light_path) as DirectionalLight3D
	if environment_path != NodePath():
		_world_env = get_node_or_null(environment_path) as WorldEnvironment


func _build_sky() -> void:
	var shader := load(SKY_SHADER_PATH) as Shader
	if shader == null:
		push_error("PlanetAtmosphere: could not load %s" % SKY_SHADER_PATH)
		return

	_sky_material = ShaderMaterial.new()
	_sky_material.shader = shader

	_sky = Sky.new()
	_sky.sky_material = _sky_material
	# The scattering integral is not cheap; refreshing the radiance cubemap every frame is
	# wasted work when the sun moves slowly. REALTIME keeps ambient tracking a running day
	# cycle. A static sun only needs the map built once.
	_sky.radiance_size = Sky.RADIANCE_SIZE_128
	_sky.process_mode = Sky.PROCESS_MODE_REALTIME if day_length_seconds > 0.0 else Sky.PROCESS_MODE_INCREMENTAL


## Attach the sky to the WorldEnvironment. Kept separate from _build_sky() and retried from
## _process because add_child() runs a node's _ready() immediately, so a caller that adds this
## node and only then assigns environment_path would otherwise leave the sky permanently
## unattached -- the failure looks like a working sun over a flat, dead sky.
func _try_configure_environment() -> void:
	if _env_configured or not manage_environment:
		return
	if _world_env == null or _sky == null:
		return

	var env := _world_env.environment
	if env == null:
		env = Environment.new()
		_world_env.environment = env
	env.background_mode = Environment.BG_SKY
	env.sky = _sky
	# This is what makes ambient light follow the sky instead of a hand-picked constant.
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_sky_contribution = 1.0
	env.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	if env.tonemap_mode == Environment.TONE_MAPPER_LINEAR:
		# Scattering output is HDR; linear tonemapping clips the sun and the horizon.
		env.tonemap_mode = Environment.TONE_MAPPER_ACES
	_env_configured = true


func _process(delta: float) -> void:
	if day_length_seconds > 0.0:
		# Advance the exported phase itself rather than tracking a separate clock, so freezing
		# the cycle (day_length_seconds = 0) leaves the sun exactly where it was and the
		# inspector always shows the true current time.
		sun_time_of_day = fposmod(sun_time_of_day + delta / day_length_seconds, 1.0)
	_resolve_nodes_if_needed()
	_update_sun_direction()
	_push_uniforms()
	_update_sun_light()


func _resolve_nodes_if_needed() -> void:
	if _sun_light == null or _world_env == null:
		_resolve_nodes()
	_try_configure_environment()


## Current unit vector pointing from the planet toward the sun.
func get_sun_direction() -> Vector3:
	return _sun_direction


## Colour of direct sunlight reaching a planet-relative point, after atmospheric extinction.
## Exposed so tools and tests can query the same value that drives the DirectionalLight3D.
func sun_transmittance_at(planet_relative_pos: Vector3) -> Vector3:
	return _sun_transmittance(planet_relative_pos)


## Orthonormal basis for the planet's equatorial plane: [spin_axis, equator_a, equator_b].
## The sun travels the circle spanned by equator_a/equator_b, offset along the axis by the
## seasonal declination.
func _equatorial_basis() -> Array:
	var axis := planet_spin_axis.normalized()
	if not axis.is_finite() or axis.length_squared() < 0.5:
		axis = Vector3.UP
	var equator_a := axis.cross(Vector3.RIGHT)
	if equator_a.length_squared() < 1e-4:
		equator_a = axis.cross(Vector3.FORWARD)
	equator_a = equator_a.normalized()
	return [axis, equator_a, axis.cross(equator_a).normalized()]


func _update_sun_direction() -> void:
	# Rotating the sun about the spin axis is exactly what a planetary day is, which is why this
	# produces correct sunrise/sunset behaviour at every latitude with no per-latitude cases.
	var basis_parts := _equatorial_basis()
	var axis: Vector3 = basis_parts[0]
	var dec := deg_to_rad(sun_declination_deg)
	var ang := TAU * sun_time_of_day
	_sun_direction = (basis_parts[1] * (cos(dec) * cos(ang))
			+ basis_parts[2] * (cos(dec) * sin(ang))
			+ axis * sin(dec)).normalized()


## Time-of-day phase that puts the sun `elev_deg` above the local horizon for an observer whose
## local up is `up_dir`. Returns -1.0 when that elevation is unreachable at this latitude and
## declination (e.g. asking for a high sun inside the polar night).
##
## Useful for "start at dawn" and for screenshot harnesses, which want a specific sun elevation
## rather than a clock value -- the same clock reads as noon at one latitude and dusk at another.
func phase_for_sun_elevation(up_dir: Vector3, elev_deg: float, rising: bool = true) -> float:
	var basis_parts := _equatorial_basis()
	var up := up_dir.normalized()
	var dec := deg_to_rad(sun_declination_deg)

	# sin(elevation) = dot(up, sun) = A*cos(ang) + B*sin(ang) + C, which is a single sinusoid
	# R*cos(ang - phi) + C, so it inverts in closed form.
	var a := cos(dec) * up.dot(basis_parts[1])
	var b := cos(dec) * up.dot(basis_parts[2])
	var c := sin(dec) * up.dot(basis_parts[0])
	var r := sqrt(a * a + b * b)
	if r < 1e-6:
		return -1.0

	var ratio := (sin(deg_to_rad(elev_deg)) - c) / r
	if absf(ratio) > 1.0:
		return -1.0
	var phi := atan2(b, a)
	var offset := acos(clampf(ratio, -1.0, 1.0))
	var ang := phi - offset if rising else phi + offset
	return fposmod(ang / TAU, 1.0)


## Pushed unconditionally every frame. There was a dirty-flag fast path here; it saved about
## sixteen float writes and cost a real bug -- any property changed from script without going
## through a setter (sun_time_of_day, most obviously) left the sky shader frozen on a stale sun
## direction while the CPU-driven light kept updating, so terrain lighting and sky silently
## disagreed. Not worth re-adding without a profile that says these writes matter.
func _push_uniforms() -> void:
	_set_on_all("planet_center", planet_center)
	_set_on_all("planet_radius", planet_radius)
	_set_on_all("atmosphere_height", atmosphere_height)
	_set_on_all("rayleigh_scale_height", rayleigh_scale_height)
	_set_on_all("mie_scale_height", mie_scale_height)
	_set_on_all("rayleigh_strength", rayleigh_strength)
	_set_on_all("mie_strength", mie_strength)
	_set_on_all("mie_anisotropy", mie_anisotropy)
	_set_on_all("sun_direction", _sun_direction)
	_set_on_all("sun_tint", sun_tint)
	_set_on_all("sun_intensity", sun_intensity)
	_set_on_all("sun_angular_radius", sun_angular_radius)
	_set_on_all("sun_disc_brightness", sun_disc_brightness)
	_set_on_all("shadow_softness", shadow_softness)
	# Rebakes only when the geometry or density profile changed; a no-op on most frames.
	_bake_transmittance_lut()
	_set_on_all("transmittance_lut", _lut_texture)
	_set_on_all("use_transmittance_lut", _lut_texture != null)
	# Sky shaders get no viewport-size built-in, so the dither needs its scale supplied.
	_set_on_all("dither_resolution", get_viewport().get_visible_rect().size)
	# Sky-only uniforms. Setting an unknown parameter on a material is harmless, so linked
	# cloud/terrain materials simply ignore these.
	_set_on_all("ground_albedo", ground_albedo)
	_set_on_all("ground_light_scale", ground_light_scale)
	_set_on_all("star_intensity", star_intensity)
	_set_on_all("exposure", exposure)


func _set_on_all(param: StringName, value: Variant) -> void:
	if _sky_material != null:
		_sky_material.set_shader_parameter(param, value)
	for mat in linked_materials:
		if mat != null:
			mat.set_shader_parameter(param, value)


# ---------------------------------------------------------------------------------------------
# CPU mirror of atmosphere_common.gdshaderinc
# ---------------------------------------------------------------------------------------------

func _beta_rayleigh() -> Vector3:
	return BETA_RAYLEIGH_EARTH * (EARTH_RAYLEIGH_SCALE_H / maxf(rayleigh_scale_height, 1.0)) * rayleigh_strength


func _beta_mie() -> float:
	return BETA_MIE_EARTH * (EARTH_MIE_SCALE_H / maxf(mie_scale_height, 1.0)) * mie_strength


## Ray/sphere intersection, sphere at origin, `rd` normalised. Returns (t_near, t_far); a miss
## is signalled by y < x, matching ray_sphere() in the include.
func _ray_sphere(ro: Vector3, rd: Vector3, radius: float) -> Vector2:
	var b := ro.dot(rd)
	var c := ro.dot(ro) - radius * radius
	var d := b * b - c
	if d < 0.0:
		return Vector2(1.0, -1.0)
	d = sqrt(d)
	return Vector2(-b - d, -b + d)


## Maps cos(sun zenith) to the LUT's u axis. Must stay identical to lut_cos_to_u() in the
## include, or the sky reads the wrong texel and the horizon gradient goes wrong.
func _lut_cos_to_u(c: float) -> float:
	return 0.5 + 0.5 * signf(c) * sqrt(absf(c))


func _lut_u_to_cos(u: float) -> float:
	var t := 2.0 * u - 1.0
	return signf(t) * t * t


## Bake the sun-ward optical depth into a 2D texture, indexed by (sun angle, altitude).
##
## Valid because the atmosphere is spherically symmetric: the integral from any point depends
## only on how high it is and how the sun sits relative to its local up, never on where it is.
## Rebaked only when the geometry or density profile actually changes -- see _lut_signature.
func _bake_transmittance_lut() -> void:
	var signature := "%f|%f|%f|%f" % [planet_radius, atmosphere_height,
			rayleigh_scale_height, mie_scale_height]
	if signature == _lut_signature and _lut_texture != null:
		return

	var r_atm := planet_radius + atmosphere_height
	# RGH = two half floats. Depths reach ~1.4e4 metres, well inside half-float range, and its
	# ~0.1% relative precision is far below anything visible after the beta multiply.
	var img := Image.create_empty(LUT_WIDTH, LUT_HEIGHT, false, Image.FORMAT_RGH)

	for yi in LUT_HEIGHT:
		var alt := (float(yi) + 0.5) / float(LUT_HEIGHT) * atmosphere_height
		var p := Vector3(0.0, planet_radius + alt, 0.0)
		for xi in LUT_WIDTH:
			var u := (float(xi) + 0.5) / float(LUT_WIDTH)
			var c := _lut_u_to_cos(u)
			# Any sun direction with this cosine against local up will do; symmetry makes the
			# azimuth irrelevant.
			var s := Vector3(sqrt(maxf(1.0 - c * c, 0.0)), c, 0.0)

			var hit := _ray_sphere(p, s, r_atm)
			var t_end := maxf(hit.y, 0.0)
			var depth := Vector2.ZERO
			if t_end > 0.0:
				var step_size := t_end / float(LUT_STEPS)
				var t := step_size * 0.5
				for i in LUT_STEPS:
					var h := maxf((p + s * t).length() - planet_radius, 0.0)
					depth.x += exp(-h / rayleigh_scale_height) * step_size
					depth.y += exp(-h / mie_scale_height) * step_size
					t += step_size
			img.set_pixel(xi, yi, Color(depth.x, depth.y, 0.0, 1.0))

	_lut_texture = ImageTexture.create_from_image(img)
	_lut_signature = signature


## Fraction of sunlight reaching `p` that the planet does not block: 1 lit, 0 shadowed.
## GPU twin: planet_shadow() in the include. Smooth rather than binary -- on the GPU that kills
## sunset banding, and here it makes the sun light fade through the terminator instead of
## snapping off in one frame.
func _planet_shadow(p: Vector3) -> float:
	var b := p.dot(_sun_direction)
	if b >= 0.0:
		return 1.0
	var perp := sqrt(maxf(p.dot(p) - b * b, 0.0))
	var w := maxf(shadow_softness, 1.0)
	return smoothstep(planet_radius - w, planet_radius + w, perp)


## Accumulated (rayleigh, mie) density from `p` toward the sun. Planet occlusion is handled
## separately by _planet_shadow(), matching the include.
func _optical_depth_to_sun(p: Vector3) -> Vector2:
	var atm := _ray_sphere(p, _sun_direction, planet_radius + atmosphere_height)
	var t_end := maxf(atm.y, 0.0)
	if t_end <= 0.0:
		return Vector2.ZERO

	var step_size := t_end / float(SUN_STEPS)
	var depth := Vector2.ZERO
	var t := step_size * 0.5
	for i in SUN_STEPS:
		var h := maxf((p + _sun_direction * t).length() - planet_radius, 0.0)
		depth.x += exp(-h / rayleigh_scale_height) * step_size
		depth.y += exp(-h / mie_scale_height) * step_size
		t += step_size
	return depth


## Colour of direct sunlight reaching planet-relative point `p`. The GPU twin of this is
## sun_transmittance() in the include; keeping them identical is what makes terrain lighting
## and the sky agree.
func _sun_transmittance(p: Vector3) -> Vector3:
	var depth := _optical_depth_to_sun(p)
	var br := _beta_rayleigh()
	var bm := _beta_mie() * MIE_EXTINCTION_FACTOR
	var shadow := _planet_shadow(p)
	return Vector3(
		exp(-(br.x * depth.x + bm * depth.y)),
		exp(-(br.y * depth.x + bm * depth.y)),
		exp(-(br.z * depth.x + bm * depth.y))) * shadow


func _camera_planet_relative() -> Vector3:
	var cam := get_viewport().get_camera_3d()
	var world_pos := global_position if cam == null else cam.global_position
	var p := world_pos - planet_center
	# Guard the degenerate exactly-on-the-surface case: the shadow test in
	# _optical_depth_to_sun collapses when |p| == planet_radius, so keep the sample just above.
	var r := p.length()
	if r < planet_radius + 1.0:
		p = (p / maxf(r, 1e-3)) * (planet_radius + 1.0)
	return p


func _update_sun_light() -> void:
	if _sun_light == null:
		return

	# DirectionalLight3D shines along its local -Z, so it must look toward the anti-sun point.
	var up_ref := planet_spin_axis.normalized()
	if absf(_sun_direction.dot(up_ref)) > 0.99:
		up_ref = Vector3.RIGHT
	_sun_light.look_at_from_position(_sun_light.global_position,
			_sun_light.global_position - _sun_direction, up_ref)

	var t := _sun_transmittance(_camera_planet_relative())

	# Split transmittance into hue and brightness: the light's colour carries the sunset
	# reddening, its energy carries the dimming. Driving both through light_color instead would
	# fight Godot's own tonemapping and wash out at noon.
	var peak: float = maxf(maxf(t.x, t.y), t.z)
	if peak <= 1e-5:
		_sun_light.light_color = Color(1.0, 1.0, 1.0)
		_sun_light.light_energy = sun_light_energy * night_light_floor
		return

	var hue := t / peak
	_sun_light.light_color = Color(hue.x, hue.y, hue.z)
	# Luminance-weighted, so a red-shifted sun correctly reads as dimmer than a white one.
	var luma: float = t.x * 0.2126 + t.y * 0.7152 + t.z * 0.0722
	_sun_light.light_energy = sun_light_energy * maxf(luma, night_light_floor)
