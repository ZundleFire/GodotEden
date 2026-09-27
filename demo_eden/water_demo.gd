extends Node3D
## Interactive planet + water demo: FlyCamera (WASD to move, hold Right Mouse Button to
## look around), hold Left Mouse Button to pour water blobs onto the terrain wherever
## you're looking. Simulation runs every frame; on-screen stats (FPS, resident chunks,
## active water blocks, total mass, ground-absorbed mass) are for performance testing at
## different @export settings.
##
## Uses EdenWaterSimulator directly against VoxelWorld's own data_map -- see
## modules/godot_eden/simulation/eden_water_simulator.h. GRAVITY_RADIAL makes water flow
## toward the planet's center regardless of where on the sphere it's poured.
##
## Always a real 10km-radius planet (see planet_radius's default) using LOD_MODE_OCTREE +
## STYLE_SMOOTH terrain, matching main_10km.gd's already-proven configuration -- NOT the
## flat LOD_MODE_RINGS "load a full cube around the camera" approach an earlier version of
## this demo used, which is what made a small 200-unit test planet already slow: ring mode
## has no distance falloff of its own, so it metaboled/streamed every chunk within
## view_distance_chunks at FULL resolution regardless of how far it actually was, plus a
## real ConcavePolygonShape3D collision mesh per chunk just to support the mouse-placement
## raycast. Neither scales to "water distributed across an actual planet." The octree
## halves detail every LOD step outward from the camera (see VoxelWorld::LOD_MODE_OCTREE),
## so the resident/meshed chunk count stays small and roughly camera-distance-independent
## of planet size; collision is dropped entirely in favor of a direct SDF raymarch against
## the generator for cursor placement (see _raymarch_terrain), so there's no per-chunk
## PhysicsServer3D shape to build at all.

@export var planet_radius := 10000.0
@export var chunk_world_size := 16.0
@export var voxel_resolution := 16
## Budget for VoxelWorld's own per-frame chunk generate/mesh work (see
## VoxelWorld::chunks_per_frame_budget) -- matches main_10km.gd's proven octree default.
@export var chunks_per_frame_budget := 4096

@export var blob_radius := 2
@export var blob_amount := 0.6
## Frames between water placements while LMB is held (throttled so holding down the
## button doesn't dump an unbounded amount of water in a single frame).
@export var place_interval_frames := 6
## Frames between water mesh rebuild passes. Simulation still steps every frame regardless
## -- this only throttles how often the (more expensive) mesh regeneration runs, and is a
## knob worth varying for performance testing.
@export var mesh_refresh_interval_frames := 6
## Caps how many active water blocks get remeshed per refresh pass -- a real mesh rebuild
## (heightfield scan + SurfaceTool + RenderingServer commit, see EdenWaterSimulator::
## generate_water_mesh_smooth_faces) is real per-block work, and a coastline can easily
## have hundreds of blocks simultaneously active; doing them ALL every single refresh pass
## is what actually made the demo unplayable, not the (much cheaper) per-voxel simulation
## tick itself. Spreads that cost across multiple passes instead of one large per-frame
## spike -- a block not reached this pass simply shows its last mesh until its next turn,
## a frame or two later, not a visible pop for anything not actively changing fast.
@export var mesh_refresh_batch_size := 48
## Active water blocks farther than this from the camera get paused (EdenWaterSimulator::
## deactivate_block) instead of simulated/remeshed every frame -- there's no reason to pay
## per-voxel CA + mesh-rebuild cost for a chunk nobody is anywhere near seeing. Paused
## blocks keep their real, correct mass data; they just stop being touched until the
## camera comes back within range and re-activates them (or add_water() touches them
## directly). 0 disables culling entirely.
@export var water_active_radius := 400.0
## How often (in frames) the distance-cull pass runs -- this only needs to be cheap and
## occasional, not per-frame, since "camera moved far enough to matter" doesn't happen
## every single frame.
@export var water_cull_interval_frames := 30

## Mass/tick a water voxel loses into absorbing ground beneath it (dirt, sand -- see
## EdenWaterSimulator::set_absorbing_material_ids). 0 disables absorption entirely; the
## simulator's own default is also 0 (opt-in), so this demo explicitly turns it on to
## show it off and to bound how far/long a single pour keeps spreading.
##
## max_absorption_per_voxel is deliberately small (NOT the simulator's own 1.0 default,
## a whole mass unit): EdenWaterSimulator's settle detection marks any block that's still
## actively absorbing as "touched" every single tick (see _tick()'s step 0), so it can't
## start settling until every one of its absorbing ground voxels reaches this cap. A real
## coastline has sand/dirt shoreline touching ocean along its ENTIRE length -- when a big
## stretch of it streams in at once, every one of those blocks activates simultaneously,
## and with the old max_absorption_per_voxel=1.0 default (capacity/rate = 100 ticks, ~5
## real seconds at the simulator's own fixed_timestep) they'd all stay simultaneously
## active for that whole stretch, measured (headless, --eden-perf-test) at a 36-SECOND
## step_ms spike across a single 30-frame window -- the actual cause of "unplayable," not
## the per-voxel mesh/render cost. Capping capacity/rate to roughly settle_ticks_threshold
## (the simulator's own real settle window, default 4) keeps the absorption ramp-up on the
## same order as ordinary settling instead of a 25x-longer synchronized stall, and as a
## direct bonus caps how much water can vanish into any one voxel of shoreline to begin
## with (the "too much mass, can't even see water anymore" symptom).
##
## Shelved for now (rate 0.0 = fully disabled, matching the simulator's own default):
## even bounded, absorption is one more thing keeping shoreline blocks churning instead of
## settling, and the immediate priority is getting the baseline sim/render cost down to a
## real 60fps on modest hardware (GTX 750 Ti) before layering opt-in features back on top.
## max_absorption_per_voxel is left at its already-tuned value for whenever this gets
## re-enabled -- only the rate needs flipping back to bring it back.
@export var absorption_rate := 0.0
@export var max_absorption_per_voxel := 0.08

## Water baked directly into planet generation (see EdenVoxelGeneratorNoise::
## enable_water_generation): oceans below sea level fill statically at generation time;
## sparse high-elevation "spring" seeds are left for EdenWaterSimulator's own
## gravity-driven CA to carve/pool into rivers and lakes once it runs near them -- no
## separate river/lake-carving code, see that class' own doc comment.
@export_group("Planet Water Generation")
@export var sea_level_offset := 0.0
@export var spring_probability_threshold := 0.985
@export var spring_min_height_above_sea := 30.0
@export var spring_seed_amount := 0.4
## How many newly-streamed-in chunks get woken (mesh-checked + activated in the water
## simulator, see _scan_for_new_chunks) per scan pass. A fresh 10km planet with a nearby
## coastline/ocean can stream in 1000+ fully-submerged chunks at once near startup --
## waking too many in one pass means that many real MeshInstance3D/SurfaceTool builds
## (RenderingServer calls, not cheap) landing in the SAME frame. Measured the hard way:
## 200/scan against an all-ocean view stalled for minutes; 24 keeps each scan's real
## mesh-building work small enough not to spike a frame, at the cost of a slower (but
## still one-time, background) initial ocean fade-in.
@export var chunk_wake_batch_size := 24
@export var chunk_scan_interval_frames := 4

## Manual terrain editing (VoxelWorld::dig_sphere/build_sphere -- see voxel_world.h).
## Hold F to dig, G to build, at wherever the camera's raymarch cursor is looking; each
## press digs/builds once (not continuous while held) so a single click carves one
## deliberate sphere instead of an uncontrolled stream of them.
@export_group("Terrain Editing (dig/build)")
@export var dig_build_radius := 3.0
@export var dig_build_strength := 1.0
@export var build_material_id := EdenVoxelBuffer.MATERIAL_STONE
## R carves a single river from the cursor's hit point downhill to sea level, reusing the
## same edit_terrain_sphere_data/remesh_chunks toolset dig/build use -- see
## _carve_river_from_spring's own doc comment. This is a one-shot, debug-triggered
## demonstration of "carve a channel," NOT an automatic whole-planet river network (that
## would need real hydrology -- flow accumulation, basin detection -- to place and size
## rivers on its own, out of scope for this pass).
@export var river_carve_step_distance := 8.0
@export var river_carve_radius := 2.5
@export var river_carve_max_steps := 400

## F5 saves every currently-resident chunk (terrain edits + water, see VoxelWorld::
## save_world's own doc comment) to this directory; F9 loads it back. Explicit only --
## nothing autosaves. user:// resolves to the OS's real per-project save-data folder
## (e.g. %APPDATA%/Godot/app_userdata/<project>/ on Windows), same as any other Godot
## save file.
@export var save_dir := "user://water_demo_save"

const FlyCameraScript := preload("res://addons/sk_fly_camera/src/fly_camera.gd")

var world: VoxelWorld
var gen: EdenVoxelGeneratorNoise
var water_sim: EdenWaterSimulator
var fly_cam: CharacterBody3D
var camera: Camera3D
var stats_label: Label

var voxel_size: float
var world_origin: Vector3

# Vector3i block_pos -> MeshInstance3D. Only ever touched for blocks EdenWaterSimulator
# itself reports as active (see _refresh_water_meshes) -- a settled/dormant block's mesh
# is left exactly as it last looked, at zero further per-frame cost.
var _water_mesh_nodes: Dictionary = {}
var _water_material: StandardMaterial3D

# Vector3i block_pos -> true, for every lod-0 chunk _scan_for_new_chunks has already
# woken/meshed once -- see that function's own doc comment.
var _known_chunks: Dictionary = {}

var _frame_count := 0
var _place_timer := 0


func _ready() -> void:
	_build_world()
	_build_camera()
	_build_lighting()
	# VoxelWorld's octree LOD splits only ONE depth level per update_world() call by
	# design (coarsest-first, level-synchronized progressive refinement -- see
	# VoxelWorld::octree_split_frontier_lod's doc comment), so a single call only ever
	# advances the tree from its root toward the camera by one step, not all the way down
	# to lod 0. A real, resident lod-0 chunk (and therefore a non-null
	# get_octree_level_data_map(0)) near the camera can take several calls to appear even
	# past that -- the per-call split BUDGET (chunks_per_frame_budget) can also make a
	# single frontier level itself take more than one call to fully process. main_10km.gd
	# gets this for free by simply running ~300 real frames before its own screenshot;
	# loop generously here (still cheap once nothing's left to split -- update_world()
	# becomes a same-camera-leaf early-out) so the water simulator is wired up against
	# real, already-generated terrain instead of a still-null/still-coarse level.
	for i in range(50):
		world.update_world(camera.global_position)
	_build_water_sim()
	_build_ui()
	print("EDEN_WATER_DEMO: ready. planet_radius=", planet_radius, " voxel_size=", voxel_size,
			" octree_lod_count=", world.octree_lod_count)

	if "--eden-test-water" in OS.get_cmdline_args():
		_debug_pour_at_camera()

	if "--eden-screenshot-tour" in OS.get_cmdline_args():
		_run_screenshot_tour()

	if "--eden-perf-test" in OS.get_cmdline_args():
		# Debug-only: teleports to a known-coastal direction (found via the screenshot tour's
		# own directional search in an earlier session) and lets the NORMAL per-frame loop
		# run from there -- unlike the tour's own forced-synchronous convergence, this is
		# what actually exercises mesh_refresh_batch_size/water_active_radius throttling
		# under real frame-by-frame conditions, since the tour intentionally bypasses that
		# throttling to get an instant, fully-converged shot.
		var perf_up_dir := Vector3(0.223626, 0.162669, 0.961005)
		var perf_surface_r := _raw_terrain_radius(perf_up_dir)
		camera.global_position = perf_up_dir * maxf(perf_surface_r, planet_radius + sea_level_offset) + perf_up_dir * 20.0
		for i in range(80):
			world.update_world(camera.global_position)


# Headless integration smoke test only (see the --eden-test-water guard above): pours a
# blob directly under the starting camera position without needing a real mouse raycast,
# so the world_origin/voxel_size/gravity_radial coordinate math can be verified end-to-end
# without a display.
func _debug_pour_at_camera() -> void:
	# Camera itself is already placed comfortably above the real terrain surface (see
	# _find_surface_radius), so pour right there rather than descending toward the ground
	# -- no need to guess how close that gets to solid rock.
	var target := camera.global_position
	var center := Vector3i(
			floori((target.x - world_origin.x) / voxel_size),
			floori((target.y - world_origin.y) / voxel_size),
			floori((target.z - world_origin.z) / voxel_size))

	for dz in range(-blob_radius, blob_radius + 1):
		for dy in range(-blob_radius, blob_radius + 1):
			for dx in range(-blob_radius, blob_radius + 1):
				var offset := Vector3i(dx, dy, dz)
				if Vector3(offset).length() > blob_radius:
					continue
				water_sim.add_water(center + offset, blob_amount)
	print("EDEN_WATER_DEMO: debug pour at ", center, " mass_now=", water_sim.get_total_mass())


# Same surface search _find_surface_radius uses, but WITHOUT its final maxf(terrain_r,
# sea_level) clamp -- needed here to tell ocean/coastline/land apart in the first place;
# _find_surface_radius's clamped version exists purely so a spawned camera never lands
# underwater, which would defeat classifying a direction as ocean at all.
func _raw_terrain_radius(up_dir: Vector3) -> float:
	var max_r := planet_radius * 1.2
	var step := max_r / 800.0
	var r := planet_radius * 0.8
	while r < max_r:
		if gen.generate_voxel(up_dir * r) > 0.0:
			return r
		r += step
	return max_r


# Debug-only visual QA tour (see the --eden-screenshot-tour guard above): finds one
# direction each for open ocean, a coastline, and general land, places the camera there,
# force-converges streaming + water meshing (many synchronous update_world()/
# _scan_for_new_chunks() calls instead of waiting on real frame budget), and saves a
# screenshot before moving to the next. Not part of the interactive demo itself.
func _run_screenshot_tour() -> void:
	var sea_level := planet_radius + sea_level_offset
	var coast_dir := Vector3.ZERO
	var land_dir := Vector3.ZERO
	var best_coast_delta := INF

	var rng := RandomNumberGenerator.new()
	rng.seed = 1 # Deterministic tour -- same locations every run.
	for i in range(400):
		var dir := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1))
		if dir.length() < 0.001:
			continue
		dir = dir.normalized()
		var raw_r := _raw_terrain_radius(dir)
		var delta := raw_r - sea_level

		if delta > 50.0 and land_dir == Vector3.ZERO:
			land_dir = dir
		if absf(delta) < best_coast_delta:
			best_coast_delta = absf(delta)
			coast_dir = dir

	print("EDEN_WATER_DEMO: tour directions -- coast=", coast_dir, " land=", land_dir)

	# A single-point "deepest ocean" direction search turned out unreliable at this
	# generator's terrain roughness (height_scale = planet_radius*0.012 = 120 units): a
	# point being deep underwater doesn't rule out a jagged peak poking above the surface
	# right next to it, and two rounds of neighborhood-based filtering (requiring several
	# tangent-offset points to also be underwater) still kept landing on isolated
	# underwater craters rather than real open sea -- confirmed via "Active water blocks:
	# 0" and no visible blue in the resulting screenshots despite real (multi-million-unit)
	# water mass existing nearby. coast_dir is already a confirmed-working water body (see
	# the "coastline" shot below) -- reused here at a closer, more water-filled framing
	# instead of chasing an idealized open-ocean horizon this terrain doesn't offer at
	# these settings.
	await _tour_shot("water_wide", coast_dir, 45.0, "res://screenshot_water_wide.png")
	await _tour_shot("water_closeup", coast_dir, 10.0, "res://screenshot_water_closeup.png")
	await _tour_shot("coastline", coast_dir, 22.0, "res://screenshot_coastline.png")
	await _tour_shot("land", land_dir, 30.0, "res://screenshot_land.png")

	print("EDEN_WATER_DEMO: tour complete")
	get_tree().quit()


# Places the camera p_altitude above p_up_dir's real (unclamped) surface point -- p_altitude
# 30 is an eye-level shot; a much larger p_altitude (e.g. planet_radius * 0.15) gives a
# pulled-back orbital view showing curvature and a wide stretch of coastline/ocean at once.
# Forces streaming/meshing/water-activation to converge synchronously (see this function's
# own inline comments) rather than waiting on the normal per-frame budget, then waits two
# real rendered frames (get_viewport().get_texture() reflects the LAST rendered frame, not
# whatever was just drawn synchronously) before saving p_path.
func _tour_shot(p_label: String, p_up_dir: Vector3, p_altitude: float, p_path: String) -> void:
	if p_up_dir == Vector3.ZERO:
		print("EDEN_WATER_DEMO: tour shot '", p_label, "' skipped -- no matching direction found")
		return

	var surface_r := _raw_terrain_radius(p_up_dir)
	var surface_point := p_up_dir * maxf(surface_r, planet_radius + sea_level_offset)
	camera.global_position = surface_point + p_up_dir * p_altitude
	var reference := Vector3(0, 1, 0) if absf(p_up_dir.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
	var tangent := p_up_dir.cross(reference).normalized()
	# Fixed (not altitude-proportional) downward tilt -- at low altitude (the ocean_surface
	# shot), an altitude-proportional tilt barely dips below level and can end up staring
	# straight into a nearby wave/reef instead of out across open water.
	var look_target := surface_point + tangent * maxf(p_altitude * 2.0, 60.0) - p_up_dir * 6.0
	camera.look_at(look_target, p_up_dir)

	# Force the octree to fully converge around the new camera position (see _ready()'s own
	# identical warmup loop and its doc comment for why one call isn't enough) instead of
	# relying on chunks_per_frame_budget across real frames, which could take many seconds
	# of wall-clock time per stop on a 10km planet.
	for i in range(80):
		world.update_world(camera.global_position)
	# Wake + mesh every newly-resident chunk near this stop (batched at chunk_wake_batch_
	# size per call, see _scan_for_new_chunks's own doc comment) -- enough passes to clear
	# out a full octree neighborhood's worth in one go instead of waiting chunk_scan_
	# interval_frames-many real frames for it to trickle through _process().
	for i in range(40):
		_scan_for_new_chunks()
	_refresh_water_meshes()

	await get_tree().process_frame
	await get_tree().process_frame

	var img := get_viewport().get_texture().get_image()
	var err := img.save_png(p_path)
	print("EDEN_WATER_DEMO: tour shot '", p_label, "' -> ", p_path, " save_result=", err)


func _build_world() -> void:
	world = VoxelWorld.new()
	gen = EdenVoxelGeneratorNoise.new()
	gen.planet_radius = planet_radius
	gen.frequency = 0.002
	gen.octaves = 4
	gen.set_height_scale(planet_radius * 0.012)
	gen.mesh_style = EdenVoxelGeneratorNoise.STYLE_SMOOTH # Smooth (dual-contoured) terrain -- water flows over/around it identically to blocky terrain, see the class doc comment.
	gen.enable_water_generation = true
	gen.sea_level_offset = sea_level_offset
	gen.spring_probability_threshold = spring_probability_threshold
	gen.spring_min_height_above_sea = spring_min_height_above_sea
	gen.spring_seed_amount = spring_seed_amount
	world.generator = gen
	world.chunk_world_size = chunk_world_size
	world.voxel_resolution = voxel_resolution
	world.lod_mode = VoxelWorld.LOD_MODE_OCTREE
	world.octree_lod_count = world.estimate_octree_lod_count_for_planet_radius(planet_radius)
	world.chunks_per_frame_budget = chunks_per_frame_budget
	# No PhysicsServer3D collision shapes at all -- see the class doc comment. Cursor
	# placement uses _raymarch_terrain against the generator directly instead.
	world.enable_collision = false
	add_child(world)


func _build_camera() -> void:
	fly_cam = FlyCameraScript.new()
	fly_cam.fly_speed = maxf(20.0, planet_radius * 0.1)
	# Deliberately not setting use_collision at all: its @export setter unconditionally
	# calls _coll.queue_free() on assignment to false, even before _coll has ever been
	# created (_set_collisions null-derefs) -- and false is already its default, so there's
	# nothing to gain from assigning it explicitly here.

	# Near-polar up_dir: FlyCamera's mouse-look yaws around global +Y, so starting close to
	# a point where the local "up" (surface normal) already IS +Y keeps the initial camera
	# orientation intuitive. Flight still works anywhere on the sphere either way (WASD is
	# relative to the camera's own aim basis, not to world/surface up) -- this only avoids
	# an obviously tilted horizon at the start.
	var up_dir := Vector3(0.4, 1.0, 0.3).normalized()
	# Terrain height along a given ray isn't just +/- height_scale from planet_radius --
	# domain warp, fbm octaves, and tectonic-plate elevation bias (see
	# EdenVoxelGeneratorNoise::get_single_sdf) can all push the real surface well past
	# that. Querying the generator directly finds the actual surface radius here instead
	# of guessing and risking a camera (or later, a water pour target) spawning
	# underground.
	var surface_r := _find_surface_radius(up_dir)
	var surface_point := up_dir * surface_r
	fly_cam.position = surface_point + up_dir * 30.0

	var reference := Vector3(0, 1, 0) if absf(up_dir.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
	var tangent := up_dir.cross(reference).normalized()
	var look_target := surface_point + tangent * 60.0 - up_dir * 10.0
	var aim_basis := Basis.looking_at(look_target - fly_cam.position, up_dir)
	var euler := aim_basis.get_euler() # EULER_ORDER_YXZ default: x=pitch, y=yaw.
	fly_cam.rotation_degrees = Vector3(rad_to_deg(euler.x), rad_to_deg(euler.y), 0.0)

	add_child(fly_cam)
	camera = fly_cam.get_camera()
	camera.far = maxf(2000.0, planet_radius * 0.2)


# Scans outward from the planet center along up_dir for the first radius where the
# generator's SDF turns positive (open air), i.e. the real terrain surface in that
# direction -- see the comment where this is called for why height_scale alone can't be
# trusted to bound it.
func _find_surface_radius(up_dir: Vector3) -> float:
	var max_r := planet_radius * 1.2
	var step := max_r / 800.0
	var r := planet_radius * 0.8
	var terrain_r := max_r
	while r < max_r:
		if gen.generate_voxel(up_dir * r) > 0.0:
			terrain_r = r
			break
		r += step
	# If this direction is ocean (terrain surface sits below sea level), place the camera
	# at the water surface instead -- otherwise it spawns underwater, looking straight up
	# through however many units of ocean happen to be there.
	var sea_level := planet_radius + sea_level_offset
	return maxf(terrain_r, sea_level)


func _build_lighting() -> void:
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-55, -35, 0)
	light.light_energy = 3.0
	add_child(light)

	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.4, 0.55, 0.75)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.55, 0.65)
	env.ambient_light_energy = 1.2
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env_node.environment = env
	add_child(env_node)


func _build_water_sim() -> void:
	voxel_size = world.get_voxel_size()
	# Octree mode's chunk (0,0,0) is centered on the WHOLE tree's root, not just one
	# chunk -- see VoxelWorld::_octree_node_world_center/_lod_placement_offset's own doc
	# comments: offset = chunk_world_size * 2^(octree_lod_count-1) * 0.5, not
	# chunk_world_size * 0.5 the way ring mode's ring-0 is. Getting this wrong silently
	# places every water voxel at the wrong world position relative to the real terrain.
	var half := chunk_world_size * pow(2.0, world.octree_lod_count - 1) * 0.5
	world_origin = Vector3(-half, -half, -half)

	water_sim = EdenWaterSimulator.new()
	# Level 0 = the finest octree level, matching voxel_resolution voxels per
	# chunk_world_size world units exactly like ring-0 did -- coarser levels only exist to
	# be rendered from a distance and aren't where local water interaction happens.
	water_sim.set_data_map(world.get_octree_level_data_map(0))
	water_sim.set_block_size(voxel_resolution)
	water_sim.set_voxel_size(voxel_size)
	water_sim.set_world_origin(world_origin)
	water_sim.set_gravity_mode(EdenWaterSimulator.GRAVITY_RADIAL)
	water_sim.set_planet_center(Vector3.ZERO)
	water_sim.set_absorption_rate(absorption_rate)
	water_sim.set_max_absorption_per_voxel(max_absorption_per_voxel)

	_water_material = StandardMaterial3D.new()
	_water_material.albedo_color = Color(0.15, 0.45, 0.85, 0.55)
	_water_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_water_material.metallic = 0.15
	_water_material.roughness = 0.05
	_water_material.cull_mode = BaseMaterial3D.CULL_DISABLED
	_water_material.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL


func _build_ui() -> void:
	var canvas := CanvasLayer.new()
	add_child(canvas)

	stats_label = Label.new()
	stats_label.position = Vector2(16, 16)
	stats_label.add_theme_font_size_override("font_size", 18)
	stats_label.add_theme_color_override("font_color", Color.WHITE)
	stats_label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	stats_label.add_theme_constant_override("shadow_offset_x", 1)
	stats_label.add_theme_constant_override("shadow_offset_y", 1)
	canvas.add_child(stats_label)


var _profile_step_ms := 0.0
var _profile_scan_ms := 0.0
var _profile_refresh_ms := 0.0
var _profile_cull_ms := 0.0
var _refresh_cursor := 0

var _dig_key_was_pressed := false
var _build_key_was_pressed := false
var _river_key_was_pressed := false
var _save_key_was_pressed := false
var _load_key_was_pressed := false


# Edge-triggered (once per press, not once per frame held) F/G/R handling for dig/build/
# river-carve -- Input.is_action_just_pressed() would need input map actions defined in
# project settings that this standalone demo scene doesn't have, so this tracks the
# previous frame's key state by hand instead.
func _handle_terrain_edit_input() -> void:
	var dig_pressed := Input.is_key_pressed(KEY_F)
	if dig_pressed and not _dig_key_was_pressed:
		var hit := _cursor_hit()
		if not hit.is_empty():
			world.dig_sphere(hit.position, dig_build_radius, dig_build_strength)
	_dig_key_was_pressed = dig_pressed

	var build_pressed := Input.is_key_pressed(KEY_G)
	if build_pressed and not _build_key_was_pressed:
		var hit := _cursor_hit()
		if not hit.is_empty():
			world.build_sphere(hit.position, dig_build_radius, dig_build_strength, build_material_id)
	_build_key_was_pressed = build_pressed

	var river_pressed := Input.is_key_pressed(KEY_R)
	if river_pressed and not _river_key_was_pressed:
		var hit := _cursor_hit()
		if not hit.is_empty():
			_carve_river_from_spring(hit.position)
	_river_key_was_pressed = river_pressed

	var save_pressed := Input.is_key_pressed(KEY_F5)
	if save_pressed and not _save_key_was_pressed:
		var err := world.save_world(save_dir)
		print("EDEN_WATER_DEMO: save_world(", save_dir, ") -> ", error_string(err))
	_save_key_was_pressed = save_pressed

	var load_pressed := Input.is_key_pressed(KEY_F9)
	if load_pressed and not _load_key_was_pressed:
		var err := world.load_world(save_dir)
		print("EDEN_WATER_DEMO: load_world(", save_dir, ") -> ", error_string(err))
		_known_chunks.clear() # Force _scan_for_new_chunks to re-mesh/re-activate every chunk it sees again.
		for pos in _water_mesh_nodes.keys().duplicate():
			_remove_water_mesh(pos)
	_load_key_was_pressed = load_pressed


# Single-river demonstration, reusing the exact same dig toolset (edit_terrain_sphere_data
# + one batched remesh_chunks()) as manual digging -- NOT an automatic whole-planet river
# network. A real network would need actual hydrology (flow accumulation across the whole
# generated terrain, basin/outlet detection) to decide where rivers even go; this only
# carves ONE channel from a given starting point, chosen by the caller (here, wherever the
# player's cursor is), which is the scope the "reuse it for river logic" ask covers without
# taking on a full separate hydrology system.
#
# Steepest-descent walk: at each step, samples real terrain height (via the same SDF
# surface search _find_surface_radius already uses for camera placement) at several
# tangent-plane neighbors around the current point and moves to whichever is lowest,
# carving a dig sphere along the way. Stops at a local minimum (nowhere lower to go) or
# once it reaches sea level -- either way, that's the channel done.
func _carve_river_from_spring(p_start: Vector3) -> void:
	var pos := p_start
	var sea_level := planet_radius + sea_level_offset
	var touched_chunks: Dictionary = {}
	const NEIGHBOR_COUNT := 8

	for step in range(river_carve_max_steps):
		if pos.length() <= sea_level:
			break

		for c in world.edit_terrain_sphere_data(pos, river_carve_radius, 1.0, true, 0):
			touched_chunks[c] = true

		var up_dir := pos.normalized()
		var reference := Vector3(0, 1, 0) if absf(up_dir.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
		var tangent_a := up_dir.cross(reference).normalized()
		var tangent_b := up_dir.cross(tangent_a).normalized()

		var best_pos := pos
		var best_height := pos.length()
		for i in range(NEIGHBOR_COUNT):
			var angle := TAU * float(i) / float(NEIGHBOR_COUNT)
			var lateral := tangent_a * cos(angle) + tangent_b * sin(angle)
			var candidate_up := (pos + lateral * river_carve_step_distance).normalized()
			var candidate_r := _find_surface_radius(candidate_up)
			if candidate_r < best_height:
				best_height = candidate_r
				best_pos = candidate_up * candidate_r

		if best_pos.is_equal_approx(pos):
			break # Local minimum -- nowhere downhill left to carve toward.
		pos = best_pos

	var touched_array: Array = touched_chunks.keys()
	world.remesh_chunks(touched_array)
	print("EDEN_WATER_DEMO: carved river from ", p_start, " -- ", touched_array.size(), " chunks remeshed")


func _process(delta: float) -> void:
	_frame_count += 1

	var t0 := Time.get_ticks_usec()
	water_sim.step(delta)
	var t1 := Time.get_ticks_usec()
	_profile_step_ms += (t1 - t0) / 1000.0

	if _frame_count % chunk_scan_interval_frames == 0:
		var t2 := Time.get_ticks_usec()
		_scan_for_new_chunks()
		var t3 := Time.get_ticks_usec()
		_profile_scan_ms += (t3 - t2) / 1000.0

	if _frame_count % mesh_refresh_interval_frames == 0:
		var t4 := Time.get_ticks_usec()
		_refresh_water_meshes()
		var t5 := Time.get_ticks_usec()
		_profile_refresh_ms += (t5 - t4) / 1000.0

	if water_cull_interval_frames > 0 and _frame_count % water_cull_interval_frames == 0:
		var t6 := Time.get_ticks_usec()
		_cull_distant_water_blocks()
		var t7 := Time.get_ticks_usec()
		_profile_cull_ms += (t7 - t6) / 1000.0

	if Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		_place_timer += 1
		if _place_timer >= place_interval_frames:
			_place_timer = 0
			_try_place_water_blob()
	else:
		_place_timer = place_interval_frames # Place immediately on the next click.

	_handle_terrain_edit_input()

	_update_stats_label()

	if _frame_count % 30 == 0:
		print("EDEN_WATER_DEMO: frame=", _frame_count,
				" octree_nodes=", world.get_octree_node_count(),
				" resident_lod0_chunks=", water_sim.get_data_map().get_block_count(),
				" known_chunks=", _known_chunks.size(),
				" active_water_blocks=", water_sim.get_active_block_count(),
				" total_mass=", water_sim.get_total_mass(),
				" step_ms=", "%.1f" % _profile_step_ms,
				" scan_ms=", "%.1f" % _profile_scan_ms,
				" refresh_ms=", "%.1f" % _profile_refresh_ms,
				" cull_ms=", "%.1f" % _profile_cull_ms)
		_profile_step_ms = 0.0
		_profile_scan_ms = 0.0
		_profile_refresh_ms = 0.0
		_profile_cull_ms = 0.0


# Sphere-traces the generator's own SDF field directly (see EdenVoxelGeneratorNoise::
# generate_voxel) instead of a PhysicsServer3D raycast -- works with world.enable_collision
# left off entirely (no per-chunk collision shape cost, see the class doc comment), and is
# exact against smooth/dual-contoured terrain since it queries the same continuous field
# the mesh itself was contoured from, not an approximating triangle mesh.
func _raymarch_terrain(from: Vector3, dir: Vector3, max_dist: float) -> Dictionary:
	var t := 0.0
	while t < max_dist:
		var sdf := gen.generate_voxel(from + dir * t)
		if sdf <= 0.0:
			# Crossed the surface between (t - last_step) and t; binary-search refine.
			var lo := maxf(0.0, t - voxel_size * 2.0)
			var hi := t
			for i in range(14):
				var mid := (lo + hi) * 0.5
				if gen.generate_voxel(from + dir * mid) <= 0.0:
					hi = mid
				else:
					lo = mid
			var hit_t := (lo + hi) * 0.5
			var hit_pos := from + dir * hit_t
			var eps := voxel_size * 0.5
			var normal := Vector3(
					gen.generate_voxel(hit_pos + Vector3(eps, 0, 0)) - gen.generate_voxel(hit_pos - Vector3(eps, 0, 0)),
					gen.generate_voxel(hit_pos + Vector3(0, eps, 0)) - gen.generate_voxel(hit_pos - Vector3(0, eps, 0)),
					gen.generate_voxel(hit_pos + Vector3(0, 0, eps)) - gen.generate_voxel(hit_pos - Vector3(0, 0, eps))).normalized()
			return {"position": hit_pos, "normal": normal}
		# Sphere-trace step: sdf is a safe lower bound on distance to the surface, floored
		# so a near-zero (but still positive) sdf near the crossing doesn't take forever to
		# converge in open air far from any surface.
		t += maxf(sdf, voxel_size * 0.5)
	return {}


## Raymarches from the mouse cursor's screen position into the terrain -- shared by water
## placement and terrain editing (dig/build/river-carve) so they all target the same spot
## the player is actually looking at.
func _cursor_hit() -> Dictionary:
	var mouse_pos := get_viewport().get_mouse_position()
	var from := camera.project_ray_origin(mouse_pos)
	var dir := camera.project_ray_normal(mouse_pos)
	return _raymarch_terrain(from, dir, planet_radius * 0.1)


func _try_place_water_blob() -> void:
	var result := _cursor_hit()
	if result.is_empty():
		return

	var hit_pos: Vector3 = result.position
	var hit_normal: Vector3 = result.normal
	# Push slightly off the surface along its normal so the target voxel is air, not the
	# solid terrain voxel the raymarch actually hit.
	var target := hit_pos + hit_normal * (voxel_size * 1.5)

	var center := Vector3i(
			floori((target.x - world_origin.x) / voxel_size),
			floori((target.y - world_origin.y) / voxel_size),
			floori((target.z - world_origin.z) / voxel_size))

	for dz in range(-blob_radius, blob_radius + 1):
		for dy in range(-blob_radius, blob_radius + 1):
			for dx in range(-blob_radius, blob_radius + 1):
				var offset := Vector3i(dx, dy, dz)
				if Vector3(offset).length() > blob_radius:
					continue
				water_sim.add_water(center + offset, blob_amount)


# A chunk generated with water generation enabled can already contain baked-in water
# (ocean fill, spring seeds -- see EdenVoxelGeneratorNoise::enable_water_generation) that
# was never routed through add_water(), so EdenWaterSimulator has no idea it's there
# until something calls activate_block() on it. This is that "something": periodically
# diffs the world's resident lod-0 chunks against ones already handled, and for each new
# one, meshes it once (covers static ocean fill, which needs to be SEEN but not
# simulated) and, if it actually has non-uniform water content, wakes it in the
# simulator too (covers spring seeds and coastline boundaries, which need to actually
# flow/settle).
#
# "Non-uniform" (EdenVoxelBuffer::is_uniform(CHANNEL_WATER)) is the exact right test, not
# a geometric guess: a chunk with a single uniform water value everywhere -- whether
# that's 0 (no water at all) or a nonzero constant (fully submerged, every neighbor at
# the same mass) -- is ALREADY at its final state; EdenWaterSimulator's own flow math
# produces zero flow when every neighbor has equal mass, so activating it buys nothing.
# Only a chunk with an actual gradient in it (a coastline's air/water boundary, a spring
# seed sitting among otherwise-dry air) has real flow work to do.
#
# An earlier version tried to guess this from chunk-center distance vs. sea level
# instead, and got it backwards in a way that activated far MORE chunks than intended
# (every plain-air chunk above the water, not just real boundaries) -- confirmed via
# profiling (Time.get_ticks_usec() around water_sim.step()): step_ms ballooned to ~38
# SECONDS across 30 frames once ~1/3 of a large coastal view's chunks got wrongly
# activated, each paying a full per-voxel _tick() scan for nothing. The is_uniform()
# check reads the buffer's own already-computed compression state (see
# EdenVoxelBuffer::compress_palette), not a re-derived heuristic -- O(1), not a guess.
#
# Batched (chunk_wake_batch_size per scan) since a fresh planet can still stream in a
# large ocean's worth of chunks needing their one-time mesh build at once -- see
# chunk_wake_batch_size's own doc comment.
func _scan_for_new_chunks() -> void:
	var data_map := water_sim.get_data_map()
	var woken := 0
	for v in data_map.get_active_block_positions():
		var block_pos: Vector3i = v
		if _known_chunks.has(block_pos):
			continue
		_known_chunks[block_pos] = true

		var buf: EdenVoxelBuffer = data_map.get_block(block_pos)
		if not buf.is_uniform(EdenVoxelBuffer.CHANNEL_WATER):
			water_sim.activate_block(block_pos)

		var faces := water_sim.generate_water_mesh_smooth_faces(block_pos, 0.05)
		if faces.size() > 0:
			_update_water_mesh(block_pos, faces)
		woken += 1
		if woken >= chunk_wake_batch_size:
			break


# Rebuilds mesh only for up to mesh_refresh_batch_size active blocks per call, round-robin
# across calls via _refresh_cursor -- see mesh_refresh_batch_size's own doc comment for why
# (a real mesh rebuild is real per-block work, and doing every active block every single
# pass is what made the demo unplayable with a large coastline resident). A block not
# reached this pass keeps showing its last mesh until its turn comes back around, a few
# refresh passes later -- fine for anything that isn't actively changing every frame.
func _refresh_water_meshes() -> void:
	var active := water_sim.get_active_block_positions()
	var n := active.size()
	if n == 0:
		return
	var count: int = mini(mesh_refresh_batch_size, n)
	for i in range(count):
		var idx := (_refresh_cursor + i) % n
		var block_pos: Vector3i = active[idx]
		var faces := water_sim.generate_water_mesh_smooth_faces(block_pos, 0.05)
		if faces.size() == 0:
			_remove_water_mesh(block_pos)
			continue
		_update_water_mesh(block_pos, faces)
	_refresh_cursor = (_refresh_cursor + count) % n


# Pauses simulation/meshing (EdenWaterSimulator::deactivate_block) for any active water
# block far enough from the camera that nobody could plausibly be looking at it -- see
# water_active_radius's own doc comment. Cheap and occasional (water_cull_interval_frames),
# not per-frame: this only needs to catch up once movement has actually put distance
# between the camera and a block, not track it continuously.
func _cull_distant_water_blocks() -> void:
	if water_active_radius <= 0.0:
		return
	var chunk_span := voxel_resolution * voxel_size
	var cam_pos := camera.global_position
	var radius_sq := water_active_radius * water_active_radius
	for v in water_sim.get_active_block_positions():
		var block_pos: Vector3i = v
		var block_center := world_origin + (Vector3(block_pos) + Vector3(0.5, 0.5, 0.5)) * chunk_span
		if block_center.distance_squared_to(cam_pos) > radius_sq:
			water_sim.deactivate_block(block_pos)


func _update_water_mesh(block_pos: Vector3i, faces: PackedVector3Array) -> void:
	var mi: MeshInstance3D = _water_mesh_nodes.get(block_pos)
	if mi == null:
		mi = MeshInstance3D.new()
		mi.material_override = _water_material
		add_child(mi)
		mi.position = world_origin + Vector3(block_pos) * (voxel_resolution * voxel_size)
		mi.scale = Vector3.ONE * voxel_size
		_water_mesh_nodes[block_pos] = mi

	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for p in faces:
		st.add_vertex(p)
	st.generate_normals()
	mi.mesh = st.commit()


func _remove_water_mesh(block_pos: Vector3i) -> void:
	var mi: MeshInstance3D = _water_mesh_nodes.get(block_pos)
	if mi != null:
		mi.queue_free()
		_water_mesh_nodes.erase(block_pos)


func _update_stats_label() -> void:
	stats_label.text = "FPS: %d\nOctree nodes: %d\nResident LOD0 chunks: %d\nActive water blocks: %d\nTotal water mass (fluid + absorbed): %.2f\n\nWASD: move   Hold RMB: look   Hold LMB: pour water\nF: dig   G: build   R: carve river from cursor\nF5: save   F9: load" % [
		Engine.get_frames_per_second(),
		world.get_octree_node_count(),
		water_sim.get_data_map().get_block_count(),
		water_sim.get_active_block_count(),
		water_sim.get_total_mass(),
	]
