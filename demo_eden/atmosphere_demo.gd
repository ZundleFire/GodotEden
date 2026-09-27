extends Node3D
## Interactive atmosphere sandbox. Fly around the planet and watch the sky, the sun colour and
## the terrain lighting respond to where you are and what time it is.
##
## Has NO terrain of its own -- point `EdenPlanetAtmosphere.planet_radius` at whatever planet/voxel
## terrain the scene is given and shading matches.
##
## Uses the engine nodes from modules/eden_atmosphere. The GDScript prototypes this was built
## from (planet_atmosphere.gd, cloud_shell.gd, shaders/) are superseded and no longer referenced.
##
## Controls
##   Right Mouse (hold) + WASD / QE : fly (see addons/sk_fly_camera)
##   [ and ]                        : scrub time of day
##   P                              : toggle the running day cycle
##   Mouse Wheel                    : fly speed

const FlyCameraScript := preload("res://addons/sk_fly_camera/src/fly_camera.gd")

## Matches EdenPlanetGeneratorV3's default. Drop to 10000/20000 to match the other demos.
@export var planet_radius: float = 40000.0
## Camera start height above the sphere surface.
@export var start_altitude: float = 60.0
## How far a single [ or ] press moves the clock, as a fraction of a full day.
@export var time_scrub_step: float = 0.002
## Disable vsync and the frame limiter, so the HUD shows the sky's true cost rather than the
## monitor's refresh rate.
@export var uncap_fps: bool = true
@export var enable_clouds: bool = true
## Start at night, so the moon, stars and space panoramas are visible on launch. The spawn site
## sits at ~64 degrees latitude, where the deepest the sun can go is ~14 degrees below the
## horizon -- -12 is chosen to be reachable there.
@export var start_at_night: bool = true

@export_group("Moon")
## A photo of the moon filling a square image, or (with moon_texture_equirect) an equirectangular
## surface map. Leave empty for a plain grey sphere -- phases still work either way.
@export var moon_texture: Texture2D
@export var moon_texture_equirect: bool = false
## Fraction of a lunar cycle per , or . press.
@export var moon_phase_scrub_step: float = 0.02

## Cloud slab bounds above sea level, used only if the scene has no CloudShell node. The camera
## spawns below these, so you start under the deck.
@export var cloud_bottom: float = 1200.0
@export var cloud_top: float = 1900.0
## Drive cloud coverage and wind from EdenPlanetGeneratorV3's climate (humidity, uplift, rain
## shadow). Costs ~2 s at startup. Off = the analytic circulation model alone.
@export var clouds_follow_v3_climate: bool = true

# EdenPlanetAtmosphere / EdenCloudShell are engine classes from modules/eden_atmosphere, so no
# preload is needed and they are available to any project using this build.
var _atmosphere: EdenPlanetAtmosphere
var _cloud_shell: EdenCloudShell
var _space_env: EdenSpaceEnvironment
var _ocean_shell: Node3D # OceanShell (res://ocean_shell.gd) -- far-field background ocean
var _fly_cam: CharacterBody3D
var _hud: Label
var _day_cycle_length := 120.0


# Capture poses, specified by the sun's true elevation above the LOCAL horizon rather than by a
# clock value -- the same time-of-day reads as noon at one latitude and dusk at another, so a
# hardcoded clock makes the sheet meaningless. `elev` is degrees above the horizon at the camera.
# These cover where scattering models usually break: high sun, the golden/red band either side of
# the terminator, full night, straight up, and the lit limb seen from altitude.
const CAPTURE_POSES := [
	{"name": "01_high_sun", "elev": 70.0, "alt": 250.0, "pitch": 0.0},
	{"name": "02_midmorning", "elev": 35.0, "alt": 250.0, "pitch": 0.0},
	{"name": "03_golden", "elev": 10.0, "alt": 250.0, "pitch": 0.0},
	{"name": "04_sunset", "elev": 1.0, "alt": 250.0, "pitch": 2.0},
	{"name": "05_twilight", "elev": -4.0, "alt": 250.0, "pitch": 2.0},
	{"name": "06_night", "elev": -30.0, "alt": 250.0, "pitch": 10.0},
	{"name": "07_zenith_up", "elev": 45.0, "alt": 250.0, "pitch": 70.0},
	{"name": "08_orbit_limb", "elev": 20.0, "alt": 18000.0, "pitch": -25.0},
	# Deep twilight, where the sun is genuinely below the visible horizon and the planet's
	# shadow edge sweeps across the sky. This is the banding case: at -4 degrees from 250m the
	# sun is still geometrically visible (the horizon dips ~6.4 degrees), so nothing is
	# shadowed and nothing bands. These two are an A/B on `soft` -- the hard-edged one should
	# show arc-shaped bands and the soft one should not. Keep both; they are the regression
	# test for the fix.
	{"name": "09_deep_twilight_soft", "elev": -11.0, "alt": 96.0, "pitch": 18.0, "soft": 600.0},
	{"name": "10_deep_twilight_hard", "elev": -11.0, "alt": 96.0, "pitch": 18.0, "soft": 0.0},

	# Seam / distortion check. The cloud field is sampled on a direction vector in a 3D texture,
	# which should make it isotropic -- no pole, no wrap seam, no preferred axis. These poses
	# exist to prove that rather than assert it. `up` overrides the capture site; the pole poses
	# sit on the spin axis, where an equirectangular mapping would show a starburst pinch.
	# The pole poses set `phase` directly instead of solving for an elevation: on the spin axis
	# the sun's elevation equals the declination for the whole day and never varies with phase,
	# so asking for any particular elevation there has no solution by definition.
	{"name": "11_pole_up", "phase": 0.25, "alt": 250.0, "pitch": 55.0, "up": Vector3(0, 1, 0)},
	{"name": "12_equator_up", "elev": 30.0, "alt": 250.0, "pitch": 55.0, "up": Vector3(1, 0, 0)},
	{"name": "13_orbit_over_pole", "phase": 0.25, "alt": 26000.0, "pitch": -90.0, "up": Vector3(0, 1, 0)},
	{"name": "14_orbit_over_equator", "elev": 30.0, "alt": 26000.0, "pitch": -90.0, "up": Vector3(1, 0, 0)},

	# Straight-down ocean-color check (day vs night), for the fft_ocean_water.gdshader sun_lit
	# gating fix -- the other poses look mostly along the horizon at 250m, which shows mostly sky.
	{"name": "15_ocean_day", "elev": 60.0, "alt": 500.0, "pitch": -90.0},
	{"name": "16_ocean_night", "elev": -60.0, "alt": 500.0, "pitch": -90.0},
]


func _ready() -> void:
	# Read before anything is built: lets a bench run attribute cost between sky and clouds.
	var cmdline_args := OS.get_cmdline_user_args()
	if cmdline_args.has("--no-clouds"):
		enable_clouds = false

	if uncap_fps:
		# Both are needed: max_fps alone still leaves the swapchain waiting on vblank, which is
		# what pins this to the monitor's refresh rate and hides the real cost of the sky.
		Engine.max_fps = 0
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)

	# 8-bit output quantisation is a second, independent source of banding in smooth sky
	# gradients, on top of the raymarch stepping. Godot's debanding dithers the final tonemap
	# and costs essentially nothing.
	get_viewport().use_debanding = true
	# TEMP: testing whether the ocean's grazing-angle banding is geometric/rasterization aliasing
	# (no AA was enabled anywhere in this project) rather than a shader bug.
	get_viewport().msaa_3d = Viewport.MSAA_4X

	_build_lighting()
	_build_camera()
	_build_ocean_shell()
	_build_hud()

	var args := OS.get_cmdline_user_args()
	if args.has("--capture"):
		var dir := "user://atmosphere_captures"
		var idx := args.find("--capture")
		if idx >= 0 and idx + 1 < args.size():
			dir = args[idx + 1]
		_run_capture(dir)
	elif args.has("--bench"):
		_run_bench()


## Frame-time benchmark over the poses that cost the most. Sky cost is dominated by ray length
## and by how many samples fall inside dense atmosphere, so a horizon view at twilight is the
## worst case and a straight-up daytime view close to the best.
func _run_bench() -> void:
	if _hud != null:
		_hud.visible = false
	var up_dir := Vector3(0.94, 0.15, 0.30).normalized()

	var cam := Camera3D.new()
	cam.near = 0.5
	cam.far = maxf(planet_radius * 6.0, 10000.0)
	add_child(cam)
	cam.current = true

	var bench_poses := [
		{"name": "horizon_twilight (worst case)", "elev": -11.0, "pitch": 18.0},
		{"name": "horizon_sunset", "elev": 1.0, "pitch": 2.0},
		{"name": "horizon_noon", "elev": 70.0, "pitch": 0.0},
		{"name": "zenith_noon (best case)", "elev": 70.0, "pitch": 80.0},
	]
	var sample_frames := 180

	print("resolution: %s   VIEW_STEPS/SUN_STEPS are consts in modules/eden_atmosphere/shaders/atmosphere_common.gdshaderinc"
			% str(get_viewport().get_visible_rect().size))
	for pose in bench_poses:
		_atmosphere.day_length_seconds = 0.0
		var phase: float = _atmosphere.phase_for_sun_elevation(up_dir, float(pose["elev"]))
		if phase < 0.0:
			continue
		_atmosphere.sun_time_of_day = phase
		await RenderingServer.frame_post_draw

		var sun_dir: Vector3 = _atmosphere.get_sun_direction()
		var horiz := (sun_dir - up_dir * sun_dir.dot(up_dir)).normalized()
		var pitch := deg_to_rad(float(pose["pitch"]))
		cam.global_transform = Transform3D(
				Basis.looking_at((horiz * cos(pitch) + up_dir * sin(pitch)).normalized(), up_dir),
				up_dir * (planet_radius + 250.0))

		# Discard the first frames: shader compilation and the radiance cubemap rebuild land
		# there and would dominate the average.
		for _w in 30:
			await RenderingServer.frame_post_draw

		var start := Time.get_ticks_usec()
		for _i in sample_frames:
			await RenderingServer.frame_post_draw
		var elapsed_us := Time.get_ticks_usec() - start
		var ms := float(elapsed_us) / 1000.0 / float(sample_frames)
		print("  %-32s %6.2f ms   %6.1f fps" % [pose["name"], ms, 1000.0 / maxf(ms, 0.001)])

	print("bench complete")
	get_tree().quit()


## Headless-ish verification pass: step through CAPTURE_POSES, screenshot each, then quit.
## Mirrors how voxel_water_demo.gd and eden_materials_showcase.gd validate themselves.
## Hides any VoxelLodTerrain node (by group/name convention -- this scene doesn't build its own
## terrain, see class doc comment) so --bench/--capture camera poses tuned for orbital/ocean shots
## don't end up with the camera embedded inside real terrain geometry, which happened once this
## session. Ocean/atmosphere/space rendering is unaffected; this only hides solid ground.
func _hide_voxel_terrain_for_test() -> void:
	for child in get_children():
		if child is VoxelLodTerrain:
			child.visible = false


func _run_capture(dir: String) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	_hide_voxel_terrain_for_test()
	if _hud != null:
		_hud.visible = false # keep the HUD text out of the captured frames

	# Capture from a near-equatorial site, unlike the interactive start position. The interactive
	# spawn sits at ~64 degrees latitude (chosen so FlyCamera's +Y-yaw mouse-look feels upright),
	# where the sun can never climb above ~38 degrees -- half the poses below would be physically
	# unreachable there. Near the equator the full elevation range exists.
	var up_dir := Vector3(0.94, 0.15, 0.30).normalized()

	# Capture uses its own camera rather than FlyCamera. FlyCamera keeps its own pitch/yaw state
	# and forces roll to zero, so driving it by euler angles tilts the horizon at an arbitrary
	# site -- which makes the frames useless for judging a horizon gradient.
	var cam := Camera3D.new()
	cam.near = 0.5
	cam.far = maxf(planet_radius * 6.0, 10000.0)
	add_child(cam)
	cam.current = true

	for pose in CAPTURE_POSES:
		# Poses may override the capture site, so the poles and equator can be inspected.
		var site: Vector3 = (pose["up"] as Vector3).normalized() if pose.has("up") else up_dir
		_atmosphere.day_length_seconds = 0.0
		var phase: float = float(pose["phase"]) if pose.has("phase") \
				else _atmosphere.phase_for_sun_elevation(site, float(pose["elev"]))
		if phase < 0.0:
			push_warning("pose %s: sun elevation %s unreachable at this declination, skipping"
					% [pose["name"], pose["elev"]])
			continue
		_atmosphere.sun_time_of_day = phase
		# Per-pose shadow softness, for the banding A/B. Restores the default otherwise.
		_atmosphere.shadow_softness = float(pose.get("soft", 600.0))
		# The sun direction only updates in _process, so settle a frame before aiming at it.
		await RenderingServer.frame_post_draw

		# Always look toward the sun's azimuth: the interesting gradient at low sun elevations
		# is the one behind the sun, and a fixed compass heading would miss it entirely.
		var sun_dir: Vector3 = _atmosphere.get_sun_direction()
		var sun_horizontal := (sun_dir - site * sun_dir.dot(site))
		if sun_horizontal.length_squared() < 1e-6:
			sun_horizontal = site.cross(Vector3.UP) # sun at zenith: any heading will do
		sun_horizontal = sun_horizontal.normalized()

		var pitch := deg_to_rad(float(pose["pitch"]))
		var aim := (sun_horizontal * cos(pitch) + site * sin(pitch)).normalized()
		var pos := site * (planet_radius + float(pose["alt"]))
		# Basis.looking_at needs an up reference not parallel to the aim; straight up or
		# straight down (the orbit-over-pole poses) would otherwise be degenerate.
		var basis_up := site
		if absf(aim.dot(site)) > 0.99:
			basis_up = sun_horizontal
		# Setting the basis directly keeps local up on screen-up, so the horizon stays level.
		cam.global_transform = Transform3D(Basis.looking_at(aim, basis_up), pos)

		# Several frames: one to let the transform and uniforms land, more to let the sky's
		# radiance cubemap (which feeds ambient) converge before the shot is taken.
		for _i in 8:
			await RenderingServer.frame_post_draw

		var img := get_viewport().get_texture().get_image()
		var path := "%s/%s.png" % [dir, pose["name"]]
		var err := img.save_png(path)

		# Log the physical state alongside the image: without this, judging a scattering bug
		# from a screenshot alone is guesswork.
		var sun_node := _atmosphere.get_node_or_null(_atmosphere.sun_light_path) as DirectionalLight3D
		var sun_col: Color = sun_node.light_color if sun_node != null else Color.WHITE
		var sun_e: float = sun_node.light_energy if sun_node != null else 0.0
		var actual_elev := rad_to_deg(asin(clampf(site.dot(_atmosphere.get_sun_direction()), -1.0, 1.0)))
		# Poses driven by an explicit phase have no requested elevation to compare against.
		var wanted: float = float(pose["elev"]) if pose.has("elev") else actual_elev
		print("capture %-22s elev=%6.1f (want %5.1f)  phase=%.3f  sun_rgb=(%.3f %.3f %.3f)  energy=%.2f  err=%d"
				% [pose["name"], actual_elev, wanted, phase,
					sun_col.r, sun_col.g, sun_col.b, sun_e, err])

	print("atmosphere capture complete: %d poses" % CAPTURE_POSES.size())
	get_tree().quit()


func _build_lighting() -> void:
	var env_node := WorldEnvironment.new()
	env_node.name = "WorldEnvironment"
	var env := Environment.new()
	# PlanetAtmosphere overwrites background/ambient on ready, and the tonemapper ONLY if it is
	# still LINEAR -- so whatever is set here wins. Everything else is just a sane starting point
	# in case the atmosphere node is disabled.
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.02, 0.03, 0.05)
	# Filmic at exposure 0.5, picked by measuring a sweep of every tonemapper x exposure over the
	# capture poses (sky saturation, surface detail, blown highlights, and how much of the night
	# sky survives instead of crushing to black):
	#   ACES       bleaches saturated highlights toward white -- worst sunset and worst water.
	#   AGX        good, slightly more restrained; loses more of the night than Filmic does.
	#   Reinhardt  keeps the most night but is the flattest everywhere else.
	#   Filmic     best sunset saturation, ties on water, keeps the most night of the punchy curves.
	# Exposure matters MORE than the curve: at 1.0 the scene sits in the compressed top of every
	# one of them and reads flat and bleached; below ~0.4 the night sky starts crushing to black
	# (44% of it at 0.35), taking the stars with it. 0.5 is the balance point.
	# Overridable for further sweeps:
	#   -- --tonemap agx|filmic|aces|reinhardt|linear --exposure 0.5 --white 8
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.tonemap_white = 8.0
	env.tonemap_exposure = 0.5
	var tm_args := OS.get_cmdline_user_args()
	var tm_names := {
		"linear": Environment.TONE_MAPPER_LINEAR,
		"reinhardt": Environment.TONE_MAPPER_REINHARDT,
		"filmic": Environment.TONE_MAPPER_FILMIC,
		"aces": Environment.TONE_MAPPER_ACES,
		"agx": Environment.TONE_MAPPER_AGX,
	}
	for pair in [["--tonemap", "tonemap_mode"], ["--exposure", "tonemap_exposure"], ["--white", "tonemap_white"]]:
		var i: int = tm_args.find(pair[0])
		if i < 0 or i + 1 >= tm_args.size():
			continue
		var raw: String = tm_args[i + 1]
		if pair[0] == "--tonemap":
			if tm_names.has(raw.to_lower()):
				env.tonemap_mode = tm_names[raw.to_lower()]
		else:
			env.set(pair[1], raw.to_float())
	env_node.environment = env
	add_child(env_node)

	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	sun.light_energy = 3.2
	sun.shadow_enabled = true
	# Planetary distances: without a long shadow range the sphere self-shadows in bands.
	sun.directional_shadow_max_distance = maxf(2000.0, planet_radius * 0.05)
	add_child(sun)

	var moon := DirectionalLight3D.new()
	moon.name = "Moon"
	moon.light_energy = 0.4
	moon.light_color = Color(0.85, 0.90, 1.0)
	moon.shadow_enabled = true
	moon.directional_shadow_max_distance = maxf(2000.0, planet_radius * 0.05)
	add_child(moon)

	# The PlanetAtmosphere and CloudShell nodes live in atmosphere_demo.tscn, so every look property
	# (sky, stars, space panorama, moon, fog, rays, clouds) is editable in the inspector and saved with
	# the scene. Only the wiring this sandbox depends on is set from here. If the nodes are missing
	# from the scene they are created with defaults instead.
	_atmosphere = get_node_or_null("PlanetAtmosphere") as EdenPlanetAtmosphere
	var atmosphere_from_scene := _atmosphere != null
	if not atmosphere_from_scene:
		_atmosphere = EdenPlanetAtmosphere.new()
		_atmosphere.name = "PlanetAtmosphere"
	_atmosphere.planet_radius = planet_radius
	_atmosphere.planet_center = Vector3.ZERO
	_atmosphere.atmosphere_height = maxf(2000.0, planet_radius * 0.1)
	_atmosphere.day_length_seconds = 0.0 # start frozen; P toggles the cycle
	_atmosphere.sun_time_of_day = 0.30
	if _atmosphere.moon_texture == null:
		_atmosphere.moon_texture = moon_texture
		_atmosphere.moon_texture_equirect = moon_texture_equirect
	# A/B switches for the post-process, for isolating artefacts and attributing cost in --bench.
	var fx_args := OS.get_cmdline_user_args()
	var oz := fx_args.find("--ozone")
	if oz >= 0 and oz + 1 < fx_args.size():
		_atmosphere.ozone_strength = fx_args[oz + 1].to_float()
	if fx_args.has("--no-fog"):
		_atmosphere.fog_enabled = false
	if fx_args.has("--no-rays"):
		_atmosphere.light_rays_enabled = false
	if fx_args.has("--no-bloom"):
		_atmosphere.bloom_intensity = 0.0
	if fx_args.has("--no-glow"):
		_atmosphere.sun_glow_intensity = 0.0
		_atmosphere.sun_corona_intensity = 0.0
		_atmosphere.sun_halo_intensity = 0.0
		_atmosphere.moon_glow_intensity = 0.0
		_atmosphere.moon_halo_intensity = 0.0
	# Set before add_child: add_child() runs _ready() immediately, and the atmosphere resolves
	# these paths there. Both targets are siblings-to-be under this node, hence "../".
	_atmosphere.sun_light_path = NodePath("../Sun")
	_atmosphere.moon_light_path = NodePath("../Moon")
	_atmosphere.environment_path = NodePath("../WorldEnvironment")
	# `--sky-set name=value` (repeatable) overrides any EdenPlanetAtmosphere property, for A/B benches.
	for i in fx_args.size() - 1:
		if fx_args[i] == "--sky-set":
			var kv := fx_args[i + 1].split("=")
			_atmosphere.set(kv[0], str_to_var(kv[1]))
			print("sky: %s = %s" % [kv[0], _atmosphere.get(kv[0])])

	_cloud_shell = get_node_or_null("CloudShell") as EdenCloudShell
	if not enable_clouds:
		if _cloud_shell != null:
			_cloud_shell.queue_free()
			_cloud_shell = null
	else:
		if _cloud_shell == null:
			_cloud_shell = EdenCloudShell.new()
			_cloud_shell.name = "CloudShell"
			_cloud_shell.cloud_bottom = cloud_bottom
			_cloud_shell.cloud_top = cloud_top
			add_child(_cloud_shell)
		_cloud_shell.planet_radius = planet_radius
		_cloud_shell.planet_center = Vector3.ZERO
		if clouds_follow_v3_climate:
			var t0 := Time.get_ticks_msec()
			_cloud_shell.weather_source_image = EdenPlanetGeneratorV3.new().get_climate_debug_image(512, 256)
			print("clouds: V3 climate weather source in %d ms" % (Time.get_ticks_msec() - t0))
		# Linking the material is what makes clouds share the sky's scattering state -- sun
		# direction, transmittance, the lot. Without this they would light independently and
		# stay white through a red sunset.
		_atmosphere.add_linked_material(_cloud_shell.get_material())
		# `--cloud-set name=value` (repeatable) overrides any EdenCloudShell property, for A/B benches.
		var user_args := OS.get_cmdline_user_args()
		for i in user_args.size() - 1:
			if user_args[i] == "--cloud-set":
				var kv := user_args[i + 1].split("=")
				_cloud_shell.set(kv[0], str_to_var(kv[1]))
				print("clouds: %s = %s" % [kv[0], _cloud_shell.get(kv[0])])

	if not atmosphere_from_scene:
		add_child(_atmosphere)

	_space_env = get_node_or_null("SpaceEnvironment") as EdenSpaceEnvironment
	if _space_env == null:
		_space_env = EdenSpaceEnvironment.new()
		_space_env.name = "SpaceEnvironment"
		add_child(_space_env)


## Panorama browsing (directory scan, load, cycling, and the generated placeholder) now lives on
## the EdenSpaceEnvironment node (_space_env) -- see eden_space_environment.cpp. This used to be
## _build_space_textures()/_apply_space_texture()/_make_placeholder_panorama() here.


## Finds the real VoxelLodTerrain in the scene (if any -- this scene builds no terrain of its own,
## see class doc comment) and searches its generator for an actual ocean basin to spawn over,
## instead of a fixed arbitrary direction. Same technique eden_stress_test.gd already uses for
## coastal spawning (generate_block + CHANNEL_SDF bisection), generalised to any VoxelGenerator
## rather than assuming EdenPlanetGeneratorV3 specifically.
##
## Root cause this fixes: a fixed spawn direction has no guarantee of being anywhere near water,
## and from dry land the ocean shell -- a sphere sitting exactly at sea level -- is fully behind
## solid ground from the player's position and correctly z-buffered away. That is not a rendering
## bug (confirmed: the same ocean renders correctly in --capture, where terrain is hidden for
## exactly this reason), but "the ocean doesn't show up by default" is still a real usability
## problem for a demo whose purpose is showing the ocean off.
func _find_ocean_up_dir(default_dir: Vector3) -> Vector3:
	var gen := _find_voxel_generator()
	if gen == null:
		return default_dir

	const N := 24
	var golden_angle := PI * (3.0 - sqrt(5.0))
	var best_dir := default_dir
	var best_depth := -INF
	for i in range(N):
		var y := 1.0 - (float(i) / float(N - 1)) * 2.0
		var radius_at_y := sqrt(max(0.0, 1.0 - y * y))
		var theta := golden_angle * i
		var d := Vector3(cos(theta) * radius_at_y, y, sin(theta) * radius_at_y).normalized()
		var r := _bisect_surface_radius(gen, d)
		var depth := planet_radius - r # positive = real terrain sits below sea level here
		if depth > best_depth:
			best_depth = depth
			best_dir = d

	# Require a non-trivial basin, not just numerical noise around sea level, so a genuinely
	# waterless generator config falls back to the original fixed direction instead of spawning
	# over an arbitrary near-zero puddle.
	if best_depth > 20.0:
		print("EDEN_ATMOSPHERE_DEMO: spawning over an ocean basin, ", "%.0f" % best_depth, "m deep")
		return best_dir
	print("EDEN_ATMOSPHERE_DEMO: no ocean basin found near sea level, using default spawn direction")
	return default_dir


func _find_voxel_generator() -> VoxelGenerator:
	for child in get_children():
		if child is VoxelLodTerrain and child.generator != null:
			return child.generator
	return null


func _sample_sdf(gen: VoxelGenerator, dir: Vector3, r: float) -> float:
	var buf := VoxelBuffer.new()
	buf.create(2, 2, 2)
	var p: Vector3 = dir * r
	var origin := Vector3i(round(p.x), round(p.y), round(p.z))
	gen.generate_block(buf, Vector3(origin), 0)
	return buf.get_voxel_f(0, 0, 0, VoxelBuffer.CHANNEL_SDF)


func _bisect_surface_radius(gen: VoxelGenerator, dir: Vector3) -> float:
	var lo := planet_radius - 4000.0
	var hi := planet_radius + 4000.0
	# sdf > 0 = air, sdf <= 0 = solid; find the crossing via 24 bisection steps.
	for i in range(24):
		var mid := (lo + hi) * 0.5
		if _sample_sdf(gen, dir, mid) > 0.0:
			hi = mid
		else:
			lo = mid
	return (lo + hi) * 0.5


func _build_camera() -> void:
	_fly_cam = FlyCameraScript.new()
	_fly_cam.fly_speed = maxf(20.0, planet_radius * 0.02)
	# Matching water_demo.gd: start where the surface normal is already close to global +Y, so
	# FlyCamera's +Y-yaw mouse-look feels upright. Flight works anywhere on the sphere regardless.
	var up_dir := _find_ocean_up_dir(Vector3(0.4, 1.0, 0.3).normalized())
	if start_at_night and _atmosphere != null:
		# Solve for the evening phase that puts the sun 12 degrees under THIS site's horizon,
		# rather than guessing a clock value -- the same clock reads as noon at one latitude and
		# dusk at another.
		var night_phase: float = _atmosphere.phase_for_sun_elevation(up_dir, -12.0, false)
		if night_phase >= 0.0:
			_atmosphere.sun_time_of_day = night_phase
	var surface_point := up_dir * planet_radius
	_fly_cam.position = surface_point + up_dir * start_altitude

	# Aim along the surface at the horizon, which is where the scattering gradient is strongest
	# and therefore the most useful default view.
	var reference := Vector3.UP if absf(up_dir.dot(Vector3.UP)) < 0.99 else Vector3.RIGHT
	var tangent := up_dir.cross(reference).normalized()
	var aim_basis := Basis.looking_at(tangent, up_dir)
	var euler := aim_basis.get_euler()
	_fly_cam.rotation_degrees = Vector3(rad_to_deg(euler.x), rad_to_deg(euler.y), 0.0)
	add_child(_fly_cam)

	var cam: Camera3D = _fly_cam.get_camera()
	if cam != null:
		# Default far plane is 4000m, well inside a 40km planet -- without this the horizon and
		# the whole far limb are clipped away.
		cam.far = maxf(planet_radius * 6.0, 10000.0)
		cam.near = 0.5
		cam.current = true


## Far-field/background ocean shell (see ocean_shell.gd's own doc comment for the full design
## writeup) -- 6 cubed-sphere quadtree patches rendering the "everywhere else" ocean surface,
## proven in voxel_water_demo.gd. Deliberately guarded/additive: built last, after camera/terrain
## already succeeded, and any error inside OceanShell only affects this node -- script errors from
## a child node don't tear down siblings already added to the tree.
func _build_ocean_shell() -> void:
	# Same "use the scene's own node if present, else create one" pattern _build_lighting() uses
	# for CloudShell -- OceanShell used to be created unconditionally here, which meant it existed
	# ONLY while the scene was actually playing and never showed up as a node in the editor's Scene
	# panel the way CloudShell/PlanetAtmosphere/EdenParentPlanet/SpaceEnvironment all do just by
	# opening the .tscn. It is now a real saved node (see atmosphere_demo.tscn) with the tuned
	# values below already baked in as its own property overrides; this is the from-script
	# fallback for a scene that doesn't have one yet, e.g. a fresh copy of this demo.
	var from_scene := get_node_or_null("OceanShell")
	if from_scene != null:
		_ocean_shell = from_scene
		return

	var ocean_shell_script := load("res://ocean_shell.gd")
	if ocean_shell_script == null:
		print("EDEN_ATMOSPHERE_DEMO: ocean_shell.gd failed to load, skipping ocean shell")
		return
	_ocean_shell = ocean_shell_script.new()
	_ocean_shell.name = "OceanShell"
	_ocean_shell.planet_radius = planet_radius
	_ocean_shell.planet_center = Vector3.ZERO
	# ocean_shell.gd's own defaults (min_leaf_world_size 250, near_field_radius 500) assume a
	# VoxelWaterSimulator covers everything closer than that -- this scene has none, so the
	# camera's normal ~60-260m flying altitude sat well inside the "far field only" zone and saw
	# raw ~31-62m coarse quad facets close up (confirmed via --capture screenshot: a blocky waffle
	# pattern dominating the whole near view). Without near-field water to hand off to, this shell
	# has to cover close range acceptably on its own: finer finest-leaf size, and no suppression
	# radius since there is nothing to suppress in favour of.
	_ocean_shell.min_leaf_world_size = 20.0
	_ocean_shell.near_field_radius = 2.0
	# Root-caused this session: the "seam lines" visible across the whole ocean at grazing angles
	# were ocean_shell's own per-leaf skirts (geometry dropped toward planet-centre at every leaf
	# edge, unconditionally, to hide LOD T-junction cracks -- see the class's own doc comment).
	# Confirmed via elimination: neither FFT tessellation density (GRID_RES) nor patch_size moved
	# the lines at all, but widening min_leaf_world_size (fewer, larger leaves) visibly widened
	# their spacing to match -- proving they track leaf boundaries, not wave/texture detail. The
	# default skirt_depth_ratio (0.15) drops each skirt far enough that even a shallow drop gets a
	# large angular footprint at a grazing view, reading as a dark line. Shallower skirts still
	# hide the geometric cracks (that only needs SOME depth) while cutting how much of that darker,
	# away-facing strip is visible from a normal flying altitude.
	_ocean_shell.skirt_depth_ratio = 0.02
	add_child(_ocean_shell)


func _build_hud() -> void:
	var layer := CanvasLayer.new()
	_hud = Label.new()
	_hud.position = Vector2(12, 10)
	_hud.add_theme_color_override("font_color", Color(0.259, 0.259, 0.259, 0.118))
	_hud.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_hud.add_theme_constant_override("outline_size", 1)
	layer.add_child(_hud)
	add_child(layer)


func _unhandled_input(event: InputEvent) -> void:
	if _atmosphere == null:
		return
	if event is InputEventKey and event.pressed and not event.echo:
		match event.keycode:
			KEY_BRACKETLEFT:
				_scrub_time(-time_scrub_step)
			KEY_BRACKETRIGHT:
				_scrub_time(time_scrub_step)
			KEY_P:
				# Freeze by zeroing day_length; sun_time_of_day already holds the current phase.
				if _atmosphere.day_length_seconds > 0.0:
					_atmosphere.day_length_seconds = 0.0
				else:
					_atmosphere.day_length_seconds = _day_cycle_length
			KEY_N:
				if _space_env != null:
					_space_env.next_panorama()
			KEY_B:
				if _space_env != null:
					_space_env.previous_panorama()
			KEY_COMMA:
				_scrub_moon(-moon_phase_scrub_step)
			KEY_PERIOD:
				_scrub_moon(moon_phase_scrub_step)


func _scrub_moon(delta_phase: float) -> void:
	_atmosphere.moon_phase = fposmod(_atmosphere.moon_phase + delta_phase, 1.0)


func _scrub_time(delta_phase: float) -> void:
	# Scrubbing only makes sense against a frozen clock; a running cycle would overwrite it.
	_atmosphere.day_length_seconds = 0.0
	_atmosphere.sun_time_of_day = fposmod(_atmosphere.sun_time_of_day + delta_phase, 1.0)


func _process(_delta: float) -> void:
	if _hud == null or _atmosphere == null or _fly_cam == null:
		return
	var altitude := _fly_cam.global_position.length() - planet_radius
	var sun_col: Color = Color.WHITE
	var sun_energy := 0.0
	var sun_node := _atmosphere.get_node_or_null(_atmosphere.sun_light_path) as DirectionalLight3D
	if sun_node != null:
		sun_col = sun_node.light_color
		sun_energy = sun_node.light_energy

	var moon_energy := 0.0
	var moon_lit := false
	var moon_node := _atmosphere.get_node_or_null(_atmosphere.moon_light_path) as DirectionalLight3D
	if moon_node != null:
		moon_energy = moon_node.light_energy
		moon_lit = moon_node.visible

	var cycle_state := "running" if _atmosphere.day_length_seconds > 0.0 else "frozen"
	var fps := Engine.get_frames_per_second()
	# Frame time is the number that matters for optimisation work -- FPS is a reciprocal, so
	# equal FPS deltas mean very different amounts of actual work at different frame rates.
	var frame_ms := 1000.0 / maxf(float(fps), 1.0)
	_hud.text = "\n".join([
		"FPS            %d   (%.2f ms)%s" % [fps, frame_ms, "  UNCAPPED" if uncap_fps else ""],
		"Altitude       %.0f m" % altitude,
		"Time of day    %.3f  (%s)" % [_atmosphere.sun_time_of_day, cycle_state],
		"Sun colour     %.2f %.2f %.2f" % [sun_col.r, sun_col.g, sun_col.b],
		"Sun energy     %.2f" % sun_energy,
		"Moon           phase %.2f   %d%% lit   energy %.2f  %s" % [_atmosphere.moon_phase,
				int(round(_atmosphere.get_moon_illumination() * 100.0)), moon_energy,
				"(light on)" if moon_lit else "(light off)"],
		"Panorama       %s" % _space_hud_label(),
		"",
		"RMB+WASD fly   [ ] time   , . moon phase   P cycle   N/B panorama",
	])


func _space_hud_label() -> String:
	return _space_env.get_panorama_label() if _space_env != null else "(none - procedural stars)"
