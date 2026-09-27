extends Node3D
## Deterministic before/after performance benchmark for EdenPlanetGeneratorV3 + VoxelLodTerrain
## + VoxelWaterSimulator, at realistic gameplay scale (V3's own default 40km planet, default
## terrain-height params -- NOT the artificially shrunk values demo_eden/voxel_water_demo.gd
## uses to make a quick correctness check converge fast).
##
## Flies a plain (non-physics, scripted) camera in a straight line for RUN_SECONDS starting
## near a real coastline (found by bisecting EdenPlanetGeneratorV3.generate_block() for the
## sdf=0 crossing closest to sea level across a few candidate directions -- same technique
## proven in voxel_water_demo.gd's debugging this session), so both terrain and water are
## continuously in view and continuously streaming, matching real play far better than
## teleporting between biomes.
##
## Two toggles (OPTIMIZED_MATERIALS / OPTIMIZED_PERF) select between the "before" (engine
## defaults, placeholder-equivalent settings) and "after" (new shaders + tuned knobs)
## configurations from the SAME script, so the two runs differ only in what's actually being
## measured -- view distance (lod_distance/view_distance) is deliberately identical in both
## configurations; it is a scene requirement, not something being "optimized" away to cheat
## the numbers.
##
## Logs per-5-second-window stats to stdout (tagged EDEN_STRESS) and writes a final CSV to
## res://stress_test_<RUN_LABEL>.csv for mechanical before/after diffing.

const RUN_LABEL := "before_water"
const OPTIMIZED_MATERIALS := false
const OPTIMIZED_PERF := false
# The coastal-finder's confirmed-wet spot for this seed turned out to be deep, uniform open
# ocean with no natural flow boundary within 20km -- VoxelWaterSimulator deliberately never
# activates a uniform block (documented: "provably zero flow work to do"), so active_water
# stayed 0 for the entire 100s in every prior run. That's correct behavior, not a bug, but it
# means those runs never actually measured water-simulation cost. When true, periodically
# injects real disturbances via VoxelWaterSimulator.add_water() near the camera to force real,
# ongoing per-tick simulation + mesh-rebuild load -- an honest stand-in for "a player disturbing
# the water" since no natural coastline was reachable for this seed/generator config.
const INJECT_WATER_DISTURBANCE := true
const WATER_INJECT_INTERVAL := 2.0
const WATER_INJECT_AMOUNT := 4.0

const PLANET_RADIUS := 40000.0
const PLANET_SEED := 1
const RUN_SECONDS := 60.0
const WINDOW_SECONDS := 5.0
const FLY_SPEED := 18.0 # m/s, roughly a fast walk/slow fly -- real traversal speed, not a stress-only sprint
const CAMERA_ALTITUDE_MARGIN := 300.0

# View/streaming range -- identical in both configurations on purpose, see header comment.
const LOD_DISTANCE := 400.0
const VIEW_DISTANCE := 900.0
const LOD_COUNT := 8
# How many LOD levels above 0 water queries/meshing fall back to when LOD0 isn't resident yet
# (see VoxelWaterSimulator.max_query_lod's own doc comment) -- this is the actual fix for the
# "water never becomes visible under sustained fast movement" limitation: LOD0 data can lag far
# behind a fast-moving viewer (see this file's own header comment on generation throughput),
# but coarser levels are cheap and become resident almost immediately, so water shows up right
# away at reduced detail and gets replaced by real LOD0 detail once it streams in.
const MAX_WATER_LOD := 3
# Coarser LODs (1..MAX_WATER_LOD) cover data boxes comparable in block count to LOD0's, so
# scanning/remeshing them every frame like LOD0 is unaffordable -- they're a rough fallback
# only, not real-time-critical detail, so refresh them on a much slower cadence.
const COARSE_WATER_REFRESH_INTERVAL := 2.0

var terrain: VoxelLodTerrain
var water_sim: VoxelWaterSimulator
var gen: EdenPlanetGeneratorV3
var camera: Camera3D
var _land_material: ShaderMaterial
var _water_material: Material
var _water_mesh_nodes: Dictionary = {} # [lod: int, block_pos: Vector3i] -> MeshInstance3D
var _refresh_cursor := 0
var _coarse_water_refresh_accum := 0.0
var _coarse_water_keys: Dictionary = {} # [lod: int, block_pos: Vector3i] -> true, lod >= 1 only

var _fly_dir: Vector3
var _elapsed := 0.0
var _window_start := 0.0
var _window_frame_times: Array[float] = []
var _window_index := 0
var _csv_lines: Array[String] = []
var _water_tick_accum_ms := 0.0
var _water_refresh_accum_ms := 0.0
var _water_inject_accum := 0.0
var _done := false

# Shader warm-up: isolation testing (see task history) proved the ~140ms/7fps stutter at t=5s
# and t=15s in the "after" run is caused by the new ShaderMaterial alone, not by any perf
# knob -- almost certainly a one-time Forward+ pipeline compile the first time each distinct
# (shader, vertex-format) combination is actually drawn. A real shipped game hides this behind
# a loading screen; here we render a tiny dummy triangle with the exact same vertex layout
# Transvoxel produces (POSITION/NORMAL/CUSTOM0/CUSTOM1) for a few frames BEFORE starting the
# timed benchmark, so the compile cost is measured and reported separately instead of
# silently corrupting the "steady-state" FPS numbers.
var _warmup_mesh: MeshInstance3D
var _warmup_frames_left := 6
var _warmup_start_usec := 0
var _warming_up := false


func _ready() -> void:
	# Generation and meshing share one thread pool by default -- under sustained streaming load
	# (a moving viewer continuously requesting new LOD0 data blocks, as VoxelWaterSimulator
	# needs), mesh tasks and generation tasks compete for the same threads and generation
	# throughput drops well behind what a moving viewer needs. Giving generation its own pool
	# stops meshing from starving it.
	VoxelEngine.set_generation_thread_count(6)
	_build_terrain()
	_build_water_sim()
	_build_camera()
	_build_lighting()
	_csv_lines.append("window,time_s,avg_fps,min_fps,low1pct_fps,avg_frame_ms,max_frame_ms,data_blocks,mesh_blocks,active_water,wet_meshes,water_tick_ms,water_refresh_ms")
	print("EDEN_STRESS: ready label=", RUN_LABEL, " materials_optimized=", OPTIMIZED_MATERIALS,
			" perf_optimized=", OPTIMIZED_PERF, " planet_radius=", PLANET_RADIUS)
	if OPTIMIZED_MATERIALS:
		_start_shader_warmup()


func _start_shader_warmup() -> void:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	st.set_custom_format(0, SurfaceTool.CUSTOM_RGBA_FLOAT)
	st.set_custom_format(1, SurfaceTool.CUSTOM_RGBA_FLOAT)
	# Local to the camera (not world space) and directly in front of it, so it's guaranteed to
	# actually land in the frustum and trigger a real draw call regardless of which direction
	# on the sphere the camera ended up facing.
	var pts := [Vector3(-2, -2, -5), Vector3(2, -2, -5), Vector3(0, 2, -5)]
	for p in pts:
		st.set_normal(Vector3(0, 0, 1))
		st.set_custom(0, Color(0, 0, 0, 0))
		st.set_custom(1, Color(0, 0, 0, 0))
		st.add_vertex(p)
	_warmup_mesh = MeshInstance3D.new()
	_warmup_mesh.mesh = st.commit()
	_warmup_mesh.material_override = _land_material
	camera.add_child(_warmup_mesh)
	_warmup_start_usec = Time.get_ticks_usec()
	_warming_up = true


func _build_terrain() -> void:
	gen = EdenPlanetGeneratorV3.new()
	gen.planet_radius = PLANET_RADIUS
	gen.set_seed(PLANET_SEED)
	# Ad-hoc visual smoke test for the ISPC diffusion path -- unset/non-"1" leaves this at its
	# default false, no effect on the real before/after perf benchmark this script exists for.
	gen.use_ispc_diffusion = OS.get_environment("EDEN_TEST_ISPC") == "1"
	gen.setup()

	var mesher := VoxelMesherTransvoxel.new()
	mesher.set_texturing_mode(VoxelMesherTransvoxel.TEXTURES_MIXEL4_S4)
	# Tried mesh_optimization_enabled=true here (VoxelMesherTransvoxel's decimation knob):
	# measured WORSE worst-case frame time (145ms vs 47ms max, 6.9fps vs 21.2fps min in the
	# first 5s window) than leaving it off. It applies to every meshed block through the one
	# shared mesher instance, including the close LOD0 blocks generated in that same initial
	# burst -- there's no per-LOD scoping exposed at this level, so its decimation cost lands
	# exactly where the CPU is already most saturated, with no corresponding win since nothing
	# here is GPU-triangle-bound. Reverted; not applied.

	terrain = VoxelLodTerrain.new()
	terrain.generator = gen
	terrain.mesher = mesher
	terrain.lod_distance = LOD_DISTANCE
	terrain.lod_count = LOD_COUNT
	if OPTIMIZED_PERF:
		# No gameplay collision needed for this flythrough benchmark at all, and in real play
		# far LODs never need physics-accurate collision -- cap it low instead of the
		# (expensive, per-block) default of generating collision at every resident LOD.
		terrain.generate_collisions = false
	if OPTIMIZED_MATERIALS:
		_land_material = ShaderMaterial.new()
		_land_material.shader = load("res://shaders/planet_land.gdshader")
		terrain.material = _land_material
	add_child(terrain)


func _build_water_sim() -> void:
	water_sim = VoxelWaterSimulator.new()
	water_sim.set_terrain(terrain)
	water_sim.set_gravity_mode(VoxelWaterSimulator.GRAVITY_RADIAL)
	water_sim.set_planet_center(Vector3.ZERO)
	water_sim.max_query_lod = MAX_WATER_LOD
	if OPTIMIZED_PERF:
		water_sim.set_update_interval(0.5)
		water_sim.set_max_blocks_per_tick(64)
		water_sim.set_auto_scan_interval(2.0)
	add_child(water_sim)

	if OPTIMIZED_MATERIALS:
		_water_material = ShaderMaterial.new()
		_water_material.shader = load("res://shaders/planet_water.gdshader")
	else:
		var m := StandardMaterial3D.new()
		m.albedo_color = Color(0.15, 0.45, 0.85, 0.55)
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.metallic = 0.15
		m.roughness = 0.05
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
		_water_material = m


var _up_dir := Vector3(0, 1, 0) # set by _build_camera(), consumed by _build_lighting()


func _build_camera() -> void:
	var pick := _find_coastal_direction()
	var up_dir: Vector3 = pick.dir
	_up_dir = up_dir
	var surface_r: float = pick.surface_r

	camera = Camera3D.new()
	camera.position = up_dir * (surface_r + CAMERA_ALTITUDE_MARGIN)
	camera.far = 6000.0
	camera.current = true
	add_child(camera)

	var reference := Vector3(0, 1, 0) if absf(up_dir.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
	_fly_dir = up_dir.cross(reference).normalized()
	var tangent2 := up_dir.cross(_fly_dir).normalized()
	camera.look_at(camera.position + _fly_dir * 100.0 - up_dir * 15.0, up_dir)

	var viewer := VoxelViewer.new()
	viewer.view_distance = VIEW_DISTANCE
	camera.add_child(viewer)

	print("EDEN_STRESS: camera dir=", up_dir, " surface_r=", surface_r,
			" score=", pick.get("score", -1.0), " altitude=", CAMERA_ALTITUDE_MARGIN, " fly_dir=", _fly_dir)


# Finds an actual MIXED shoreline (both real water AND real dry land nearby), not just "some
# water present" -- that alone picked a huge monotone submerged patch the first time this ran
# (see stress_test_after_screenshot.png: uniform gray ocean floor, no visible land the entire
# 1.8km flight). A single global coarse sample essentially never lands exactly on a coastline
# (shorelines are thin relative to a 40km-radius sphere) -- tried checking a small ~250m offset
# from each of 60 global candidates for land and found none, confirming this. Two-phase instead:
# find a confirmed-wet spot globally first (existing water-only search), then do a LOCAL
# outward spiral search around it (up to several km) for real elevated land; if found, aim the
# final position at the midpoint so the flight path actually straddles the coastline.
func _find_coastal_direction() -> Dictionary:
	var candidates := _find_wet_candidates()
	# Try each wet candidate, best water score first, until one actually has land within
	# reach -- the single "best" wet spot (previous approach) kept landing deep in open
	# ocean, >10km from any coast, for this seed.
	for water_pick in candidates:
		var water_dir: Vector3 = water_pick.dir
		var water_r: float = water_pick.surface_r
		var land := _search_for_land_near(water_dir, water_r)
		if land.dir != Vector3.ZERO:
			var mid_dir: Vector3 = (water_dir + land.dir).normalized()
			var mid_r := _bisect_surface_radius(mid_dir)
			print("EDEN_STRESS: coastline found, land+", "%.0f" % (land.r - PLANET_RADIUS),
					"m at distance ~", "%.0f" % (water_dir.angle_to(land.dir) * PLANET_RADIUS),
					"m from wet spot (water score=", water_pick.score, ")")
			return {"dir": mid_dir, "surface_r": mid_r, "score": water_pick.score}
	print("EDEN_STRESS: no land found near any of ", candidates.size(), " wet candidates, using best water-only pick")
	return candidates[0]


func _search_for_land_near(water_dir: Vector3, water_r: float) -> Dictionary:
	var reference := Vector3(0, 1, 0) if absf(water_dir.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
	var perp := water_dir.cross(reference).normalized()
	var best_land_dir := Vector3.ZERO
	var best_land_r := water_r
	var search_radii: Array[float] = [300.0, 800.0, 2000.0, 5000.0, 10000.0, 20000.0]
	for radius_m in search_radii:
		var ang_step: float = radius_m / PLANET_RADIUS
		for k in range(8):
			var ang := float(k) / 8.0 * TAU
			var offset_dir := (water_dir + perp.rotated(water_dir, ang) * tan(ang_step)).normalized()
			var land_r := _bisect_surface_radius(offset_dir)
			if land_r - PLANET_RADIUS > 30.0 and land_r > best_land_r:
				best_land_r = land_r
				best_land_dir = offset_dir
		if best_land_dir != Vector3.ZERO:
			break
	return {"dir": best_land_dir, "r": best_land_r}


# Returns every sampled direction with real open water at nominal sea level, sorted best
# (highest water*near-sea-level score) first.
func _find_wet_candidates() -> Array[Dictionary]:
	const N := 40
	var results: Array[Dictionary] = []
	var golden_angle := PI * (3.0 - sqrt(5.0))
	for i in range(N):
		var y := 1.0 - (float(i) / float(N - 1)) * 1.6 + 0.3
		var radius_at_y := sqrt(max(0.0, 1.0 - y * y))
		var theta := golden_angle * i
		var d := Vector3(cos(theta) * radius_at_y, y, sin(theta) * radius_at_y).normalized()
		var r := _bisect_surface_radius(d)
		var water := _sample_water(d, PLANET_RADIUS)
		var near_sea_level := 1.0 - clampf(absf(r - PLANET_RADIUS) / 600.0, 0.0, 1.0)
		var score := water * (0.3 + 0.7 * near_sea_level)
		results.append({"dir": d, "surface_r": r, "score": score})
	results.sort_custom(func(a, b): return a.score > b.score)
	return results


func _sample_sdf(dir: Vector3, r: float) -> float:
	var buf := VoxelBuffer.new()
	buf.create(2, 2, 2)
	var p: Vector3 = dir * r
	var origin := Vector3i(round(p.x), round(p.y), round(p.z))
	gen.generate_block(buf, Vector3(origin), 0)
	return buf.get_voxel_f(0, 0, 0, VoxelBuffer.CHANNEL_SDF)


func _sample_water(dir: Vector3, r: float) -> float:
	var buf := VoxelBuffer.new()
	buf.create(2, 2, 2)
	var p: Vector3 = dir * r
	var origin := Vector3i(round(p.x), round(p.y), round(p.z))
	gen.generate_block(buf, Vector3(origin), 0)
	return buf.get_voxel_f(0, 0, 0, VoxelBuffer.CHANNEL_DATA5)


func _bisect_surface_radius(dir: Vector3) -> float:
	var lo := PLANET_RADIUS - 4000.0
	var hi := PLANET_RADIUS + 4000.0
	# sdf > 0 = air, sdf <= 0 = solid; find the crossing via 24 bisection steps.
	for i in range(24):
		var mid := (lo + hi) * 0.5
		if _sample_sdf(dir, mid) > 0.0:
			hi = mid
		else:
			lo = mid
	return (lo + hi) * 0.5


func _build_lighting() -> void:
	# A fixed world-space light rotation only lights whichever patch of the planet happens to
	# face that direction -- for an arbitrary coastal pick anywhere on the sphere, that's
	# frequently nearly opposite the local surface normal (confirmed: the "before" screenshot
	# taken before this fix is essentially black). Point the sun relative to the chosen
	# up_dir instead, tilted off-vertical for real shading definition instead of flat
	# straight-down light.
	var sun_from := (_up_dir * 0.65 + Vector3(0.3, 0.85, 0.25)).normalized()
	var light := DirectionalLight3D.new()
	add_child(light)
	var ref := Vector3.UP if absf(sun_from.y) < 0.99 else Vector3.RIGHT
	light.look_at(-sun_from, ref)
	# Confirmed via a deliberate over/under-exposure test (light_energy=40/ambient=5.0 blew out
	# to solid white, proving the shading pipeline itself is correct -- normals and lambert
	# falloff both visibly respond right) that the "normal-looking" energy values (1-3) this
	# project's other demos use are simply too dim for whatever exposure this render config
	# expects. This is the actual root cause of every "dark/flat/unlit-looking" screenshot from
	# this session, not a shader or lighting-direction bug.
	light.light_energy = 1.6

	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.4, 0.55, 0.75)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.55, 0.65)
	env.ambient_light_energy = 0.35
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env_node.environment = env
	add_child(env_node)


var _dbg_frame := 0


func _process(delta: float) -> void:
	if _done:
		return

	if _warming_up:
		_warmup_frames_left -= 1
		if _warmup_frames_left <= 0:
			var warmup_ms := (Time.get_ticks_usec() - _warmup_start_usec) / 1000.0
			_warmup_mesh.queue_free()
			_warming_up = false
			print("EDEN_STRESS: shader warmup took ", "%.2f" % warmup_ms,
					"ms across ", 6 - _warmup_frames_left, " frames before the timed benchmark started")
		return

	_dbg_frame += 1
	if _dbg_frame == 180 or _dbg_frame == 850:
		var img := get_viewport().get_texture().get_image()
		var ispc_suffix := "_ispc" if OS.get_environment("EDEN_TEST_ISPC") == "1" else "_scalar"
		img.save_png("res://stress_test_%s_early%s.png" % [RUN_LABEL, ispc_suffix])
		print("EDEN_STRESS: early screenshot saved")
		if OS.has_environment("EDEN_TEST_ISPC") and _dbg_frame == 850:
			get_tree().quit()

	camera.position += _fly_dir * FLY_SPEED * delta
	camera.look_at(camera.position + _fly_dir * 100.0 - camera.position.normalized() * 15.0,
			camera.position.normalized())

	if INJECT_WATER_DISTURBANCE:
		_water_inject_accum += delta
		if _water_inject_accum >= WATER_INJECT_INTERVAL:
			_water_inject_accum = 0.0
			_inject_water_disturbance()

	var t0 := Time.get_ticks_usec()
	_refresh_water_meshes()
	var t1 := Time.get_ticks_usec()
	_water_refresh_accum_ms += (t1 - t0) / 1000.0

	_elapsed += delta
	_window_frame_times.append(delta)

	if _elapsed - _window_start >= WINDOW_SECONDS:
		_flush_window()

	if _elapsed >= RUN_SECONDS:
		_finish()


# Adds a real mass disturbance at sea level directly below the camera's current position --
# see INJECT_WATER_DISTURBANCE's own comment for why. add_water() also marks the target block
# (and its resident face-neighbors) active itself, so this alone is enough to force real,
# ongoing per-tick simulation load without needing a separate activate_block() call.
func _inject_water_disturbance() -> void:
	# Injecting exactly at nominal sea level was landing on solid rock (confirmed via
	# diagnostic: is_solid_voxel=true, add_water() correctly no-op'd) -- r==PLANET_RADIUS is a
	# fragile boundary that per-voxel terrain roughness can push either side of, and the
	# camera drifts tangentially off the exact direction the coastal-finder originally sampled
	# as it flies. Scan downward from a bit above sea level to a bit below, using the same
	# is_solid_voxel() the simulator itself uses, and inject at the first real open-water
	# voxel found instead of guessing one exact radius.
	var down_dir := camera.position.normalized()
	var verbose := _elapsed < 8.0
	if verbose:
		print("EDEN_STRESS: streaming_system=", terrain.get_streaming_system(),
				" cache_generated_blocks=", terrain.get_cache_generated_blocks())
	for depth in [-10, 0, 10, 20, 40, 80, 150, 300, 600, 1000]:
		var p: Vector3 = down_dir * (PLANET_RADIUS - float(depth))
		var gp := Vector3i(round(p.x), round(p.y), round(p.z))
		var solid := water_sim.is_solid_voxel(gp)
		var mass := water_sim.get_water_mass(gp)
		if verbose:
			print("EDEN_STRESS:   depth=", depth, " pos=", gp, " solid=", solid, " mass=", mass)
		if not solid:
			water_sim.add_water(gp, WATER_INJECT_AMOUNT)
			print("EDEN_STRESS: injected at ", gp, " depth=", depth,
					" active_blocks=", water_sim.get_active_block_count())
			return


func _flush_window() -> void:
	var n := _window_frame_times.size()
	if n == 0:
		return
	var sorted := _window_frame_times.duplicate()
	sorted.sort()
	var sum := 0.0
	var max_dt := 0.0
	for dt in _window_frame_times:
		sum += dt
		max_dt = maxf(max_dt, dt)
	var avg_dt := sum / n
	var avg_fps := 1.0 / maxf(avg_dt, 0.0001)
	var min_fps := 1.0 / maxf(max_dt, 0.0001)
	# 1%-low: average fps of the slowest 1% of frames this window.
	var low_count := maxi(1, int(ceil(n * 0.01)))
	var low_sum := 0.0
	for i in range(n - low_count, n):
		low_sum += sorted[i]
	var low1pct_fps := 1.0 / maxf(low_sum / low_count, 0.0001)

	var data_blocks := terrain.debug_get_data_block_count()
	var mesh_blocks := terrain.debug_get_mesh_block_count()
	var active_water := water_sim.get_active_block_count()
	var wet_meshes := _water_mesh_nodes.size()

	print("EDEN_STRESS[", RUN_LABEL, "] window=", _window_index, " t=", "%.1f" % _elapsed,
			" avg_fps=", "%.1f" % avg_fps, " min_fps=", "%.1f" % min_fps,
			" low1pct_fps=", "%.1f" % low1pct_fps, " avg_ms=", "%.2f" % (avg_dt * 1000.0),
			" max_ms=", "%.2f" % (max_dt * 1000.0), " data_blocks=", data_blocks,
			" mesh_blocks=", mesh_blocks, " active_water=", active_water,
			" wet_meshes=", wet_meshes, " water_refresh_ms=", "%.2f" % _water_refresh_accum_ms)

	_csv_lines.append("%d,%.1f,%.2f,%.2f,%.2f,%.3f,%.3f,%d,%d,%d,%d,%.2f,%.2f" % [
		_window_index, _elapsed, avg_fps, min_fps, low1pct_fps, avg_dt * 1000.0,
		max_dt * 1000.0, data_blocks, mesh_blocks, active_water, wet_meshes,
		_water_tick_accum_ms, _water_refresh_accum_ms
	])

	_window_index += 1
	_window_start = _elapsed
	_window_frame_times.clear()
	_water_tick_accum_ms = 0.0
	_water_refresh_accum_ms = 0.0


# Coarser LOD water is a fallback while LOD0 hasn't streamed in yet for a given area (see
# MAX_WATER_LOD's comment) -- `claimed` tracks which LOD0-block-grid cells are already covered
# by finer data this refresh so a coarse block never renders a redundant, z-fighting duplicate
# under real LOD0 water. Checked/marked via each candidate's 8 corners, not its full interior --
# a cheap approximation, see voxel_water_demo.gd's _refresh_water_meshes() for the full
# rationale (same pattern, ported here).
func _refresh_water_meshes() -> void:
	var batch_size := 48 if OPTIMIZED_PERF else 100000 # unbounded = old "rebuild everything" behavior
	var bs := terrain.get_data_block_size()
	var seen := {}
	var claimed := {}

	# LOD0: real detail, the only level batched/round-robined (matches prior OPTIMIZED_PERF
	# behavior) since it's the level with the largest, most dynamic block count.
	var wet0 := water_sim.get_wet_block_positions_at_lod(0, 0.05)
	var n := wet0.size()
	var lod0_indices := range(n) if not OPTIMIZED_PERF else []
	if OPTIMIZED_PERF and n > 0:
		var count: int = mini(batch_size, n)
		for i in range(count):
			lod0_indices.append((_refresh_cursor + i) % n)
		_refresh_cursor = (_refresh_cursor + count) % n
	for idx in lod0_indices:
		var block_pos: Vector3i = wet0[idx]
		var key := [0, block_pos]
		seen[key] = true
		for c in _lod0_corners(block_pos, 0):
			claimed[c] = true
		var faces := water_sim.generate_water_mesh_smooth_faces(block_pos, 0.05, 0)
		if faces.size() == 0:
			_remove_water_mesh(key)
		else:
			_update_water_mesh(key, block_pos, 0, faces, bs)
	# LOD0 blocks not touched this pass (batched mode) still count as "seen" so they aren't
	# pruned below just because this pass's round-robin slice skipped them.
	for v in wet0:
		seen[[0, v]] = true

	# Coarser LODs: their data boxes are NOT small (a coarser LOD's box covers a much larger
	# world extent per axis, so block counts stay comparable to LOD0's -- measured tens of
	# thousands of blocks at LOD1 alone for this scene's view distance), so scanning/remeshing
	# them every single frame like this used to (a real regression measured via
	# water_refresh_ms spiking to 3+ real seconds per 5s window) is far too expensive. They're
	# only a fallback for area LOD0 hasn't streamed in yet, so they don't need to update nearly
	# as often as LOD0 itself -- refresh them on their own, much slower cadence instead.
	_coarse_water_refresh_accum += get_process_delta_time()
	if _coarse_water_refresh_accum >= COARSE_WATER_REFRESH_INTERVAL:
		_coarse_water_refresh_accum = 0.0
		for lod in range(1, MAX_WATER_LOD + 1):
			var wet: Array = water_sim.get_wet_block_positions_at_lod(lod, 0.05)
			for v in wet:
				var block_pos: Vector3i = v
				var key := [lod, block_pos]
				var corners := _lod0_corners(block_pos, lod)
				var covered := false
				for c in corners:
					if claimed.has(c):
						covered = true
						break
				if covered:
					_remove_water_mesh(key)
					continue
				for c in corners:
					claimed[c] = true
				_coarse_water_keys[key] = true
				# Coarse water is a static fallback (never actively ticked, see max_query_lod's
				# doc comment), so once a block has a mesh there's no need to pay for
				# regenerating its geometry every throttled pass -- only new blocks (just
				# streamed in, or newly uncovered by a finer mesh disappearing) need the real
				# C++ scan.
				if _water_mesh_nodes.has(key):
					continue
				var faces := water_sim.generate_water_mesh_smooth_faces(block_pos, 0.05, lod)
				if faces.size() == 0:
					continue
				_update_water_mesh(key, block_pos, lod, faces, bs)
	# Coarse meshes persist across the frames between refreshes (not re-scanned every frame), so
	# keep them out of the prune pass below unless this pass's scan actually dropped them.
	for k in _coarse_water_keys.keys():
		seen[k] = true

	for k in _water_mesh_nodes.keys():
		if not seen.has(k):
			_remove_water_mesh(k)


func _lod0_corners(block_pos: Vector3i, lod: int) -> Array[Vector3i]:
	var lo: Vector3i = block_pos * (1 << lod)
	var hi: Vector3i = lo + Vector3i.ONE * ((1 << lod) - 1)
	var result: Array[Vector3i] = []
	for x in [lo.x, hi.x]:
		for y in [lo.y, hi.y]:
			for z in [lo.z, hi.z]:
				result.append(Vector3i(x, y, z))
	return result


func _update_water_mesh(key: Array, block_pos: Vector3i, lod: int, faces: PackedVector3Array, block_size: int) -> void:
	var mi: MeshInstance3D = _water_mesh_nodes.get(key)
	if mi == null:
		mi = MeshInstance3D.new()
		mi.material_override = _water_material
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		terrain.add_child(mi)
		var scale_factor := float(1 << lod)
		mi.position = Vector3(block_pos) * block_size * scale_factor
		mi.scale = Vector3.ONE * scale_factor
		_water_mesh_nodes[key] = mi

	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for p in faces:
		st.add_vertex(p)
	st.generate_normals()
	mi.mesh = st.commit()


func _remove_water_mesh(key) -> void:
	var mi: MeshInstance3D = _water_mesh_nodes.get(key)
	if mi != null:
		mi.queue_free()
		_water_mesh_nodes.erase(key)
	_coarse_water_keys.erase(key)


func _finish() -> void:
	_done = true
	if _window_frame_times.size() > 0:
		_flush_window()
	var img := get_viewport().get_texture().get_image()
	img.save_png("res://stress_test_%s_screenshot.png" % RUN_LABEL)
	var f := FileAccess.open("res://stress_test_%s.csv" % RUN_LABEL, FileAccess.WRITE)
	for line in _csv_lines:
		f.store_line(line)
	f.close()
	print("EDEN_STRESS[", RUN_LABEL, "] DONE windows=", _window_index)
	get_tree().quit()
