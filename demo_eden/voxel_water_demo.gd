extends Node3D
## Minimal demo proving the new VoxelWaterSimulator system (modules/voxel fork,
## water baked by EdenPlanetGeneratorV1 into CHANNEL_DATA5) actually renders.
## Unlike water_demo.gd (the old, separate godot_eden/EdenWaterSimulator system), this
## exercises VoxelLodTerrain + VoxelMesherTransvoxel + EdenPlanetGeneratorV1 +
## VoxelWaterSimulator end to end -- nothing else in the repo wires these together.
##
## VoxelWaterSimulator's own C++ side (auto_scan_wet_blocks, generate_water_mesh_*_faces)
## does the simulation and per-block mesh generation; this script's only job is the part
## that was missing -- calling get_wet_block_positions()/generate_water_mesh_smooth_faces()
## on a timer and turning the result into visible MeshInstance3D children, the same pattern
## water_demo.gd already used for the old system (see its _update_water_mesh).

const PLANET_RADIUS := 20000.0
# max_terrain_height must be shrunk together with the actual height-contributing params
# (mountain_height/hills_amplitude/trench_depth below) rather than alone: it only tightens
# generate_block's block-level shell-skip optimization, and shrinking it without also shrinking
# real terrain height causes shell-skip to incorrectly force-flatten real terrain within camera
# range.
const MAX_TERRAIN_HEIGHT := 80.0
const REFRESH_INTERVAL := 0.5
const CAPTURE_FRAME := 1200
# How many LOD levels above 0 water queries/meshing fall back to when LOD0 isn't resident yet
# (see VoxelWaterSimulator.max_query_lod's own doc comment). Coarser water is an approximation
# (each level down samples one real LOD0-ish voxel per 2^lod world-units-wide region) but lets
# water appear immediately instead of waiting for LOD0 to stream in, then gets replaced by real
# LOD0 detail the moment it's resident.
const MAX_WATER_LOD := 3
# Coarser LODs (1..MAX_WATER_LOD) cover data boxes comparable in block count to LOD0's, so
# scanning/remeshing them on every refresh is unaffordable, especially on a mostly-open-ocean
# world -- refresh them on their own, much slower cadence (see _refresh_water_meshes()).
const COARSE_WATER_REFRESH_INTERVAL := 2.0

var terrain: VoxelLodTerrain
var water_sim: VoxelWaterSimulator
var gen: EdenPlanetGeneratorV1
var ocean_shell: Node3D # OceanShell (res://ocean_shell.gd) -- far-field background ocean, see _build_ocean_shell()
var _water_material: Material
var _water_mesh_nodes: Dictionary = {} # [lod: int, block_pos: Vector3i] -> MeshInstance3D
var _coarse_water_refresh_accum := 0.0
var _coarse_water_keys: Dictionary = {} # [lod: int, block_pos: Vector3i] -> true, lod >= 1 only
var _lod0_refresh_cursor := 0
var _refresh_accum := 0.0
var _frame_count := 0
var _up_dir := Vector3(-1, 0, 0) # consumed by _build_lighting(), same value _build_camera_and_viewer() uses


func _ready() -> void:
	# See eden_stress_test.gd's _ready() for why -- generation needs its own thread pool so
	# meshing doesn't starve it under streaming load.
	VoxelEngine.set_generation_thread_count(6)
	_build_terrain()
	_build_water_sim()
	_build_camera_and_viewer()
	_build_lighting()
	_build_ocean_shell()
	print("EDEN_VOXEL_WATER_DEMO: ready, planet_radius=", PLANET_RADIUS)


func _build_terrain() -> void:
	gen = EdenPlanetGeneratorV1.new()
	gen.planet_radius = PLANET_RADIUS
	gen.max_terrain_height = MAX_TERRAIN_HEIGHT
	gen.mountain_height = 25.0
	gen.hills_amplitude = 10.0
	gen.trench_depth = 20.0
	gen.rift_depth = 20.0
	gen.seed_val = 1
	# All ocean, no land anywhere -- removes any chance of an unlucky direction landing on a
	# dry patch. This demo only needs to prove water renders, not exercise coastline placement.
	gen.land_coverage = 0.0
	gen.setup()

	var mesher := VoxelMesherTransvoxel.new()
	mesher.set_texturing_mode(VoxelMesherTransvoxel.TEXTURES_MIXEL4_S4)

	terrain = VoxelLodTerrain.new()
	terrain.generator = gen
	terrain.mesher = mesher
	# Default 128 doesn't comfortably clear the camera's altitude margin once chunk-quantization
	# is accounted for (see _build_camera_and_viewer's comment).
	terrain.lod_distance = 180.0
	# cache_generated_blocks is force-enabled by VoxelWaterSimulator.set_terrain() itself now
	# (see its comment in voxel_water_simulator.cpp) -- without it, VoxelLodTerrain generates
	# voxels transiently for meshing without persisting them, so every simulator query against
	# the terrain's VoxelData silently saw an empty map forever.
	terrain.lod_count = 6
	# Procedural MIXEL4 land shader (eden_stress_test.gd's materials/perf work) -- replaces the
	# flat, near-black transvoxel_minimal.gdshader placeholder fallback this demo used before.
	var land_material := ShaderMaterial.new()
	land_material.shader = load("res://shaders/planet_land.gdshader")
	terrain.material = land_material
	add_child(terrain)


func _build_water_sim() -> void:
	water_sim = VoxelWaterSimulator.new()
	water_sim.set_terrain(terrain)
	water_sim.set_gravity_mode(VoxelWaterSimulator.GRAVITY_RADIAL)
	water_sim.set_planet_center(Vector3.ZERO)
	water_sim.max_query_lod = MAX_WATER_LOD
	add_child(water_sim)

	# Procedural water shader (eden_stress_test.gd's materials/perf work) -- replaces the flat
	# translucent-blue StandardMaterial3D this demo used before.
	var water_material := ShaderMaterial.new()
	water_material.shader = load("res://shaders/planet_water.gdshader")
	# This is the caller the screen-space derivative normal fix (see the shader's own
	# use_derivative_normal comment) was built for: many independently-smoothed per-block
	# VoxelWaterSimulator meshes whose interpolated NORMAL disagrees at shared edges. The uniform
	# now defaults OFF (a different caller, ocean_shell.gd, doesn't have that problem and the
	# derivative itself was the source of a different banding there) -- turn it back on here.
	water_material.set_shader_parameter("use_derivative_normal", true)
	# FIXME: coarse-LOD water (lod 1..MAX_WATER_LOD) renders as severe screen-wide banding at
	# this camera distance -- root-caused via elimination this session (isolated away from the
	# wave shader, depth-texture sampling, alpha blending, and the ocean shell entirely; confirmed
	# gone with MAX_WATER_LOD=0, i.e. LOD0-only water). Very likely overlapping/z-fighting
	# geometry from coarse-tier mesh generation or _refresh_water_meshes()'s corner-based
	# `claimed` coverage suppression (already documented there as "a cheap approximation, not
	# exact geometric coverage"). Not yet fixed -- needs a real look at coarse-LOD mesh output,
	# not just this material. See screenshot_voxel_water.png from this session for the artifact.
	_water_material = water_material


func _build_camera_and_viewer() -> void:
	var up_dir := _up_dir
	var camera := Camera3D.new()
	# max_terrain_height is the real bound on how far the surface can deviate from
	# planet_radius -- placing the camera just above planet_radius + max_terrain_height
	# guarantees open air regardless of local terrain, without needing a real surface raycast.
	# The margin needs headroom past VoxelLodTerrain's LOD0 range (lod_distance above): that
	# coverage box is chunk-quantized around the camera's own containing chunk, not a clean
	# sphere around its exact position, so cutting it close can leave the water surface's
	# nearest chunk just outside actual LOD0 coverage.
	camera.position = up_dir * (PLANET_RADIUS + MAX_TERRAIN_HEIGHT + 70.0)
	camera.far = 20000.0
	camera.current = true
	add_child(camera) # look_at() below requires the node to already be inside the tree.
	camera.look_at(Vector3.ZERO, Vector3(0, 1, 0) if absf(up_dir.y) < 0.99 else Vector3(1, 0, 0))

	var viewer := VoxelViewer.new()
	viewer.view_distance = 300
	camera.add_child(viewer)


# Far-field/background ocean shell (see ocean_shell.gd's own doc comment for the full
# design writeup) -- 6 cubed-sphere quadtree patches rendering the "everywhere else" ocean
# surface, independent of and alongside VoxelWaterSimulator's near-field wet/dry sim.
# Deliberately guarded/additive: built last, after terrain/water_sim/camera/lighting already
# succeeded, and any error inside OceanShell (e.g. a bad load()) only affects this node --
# script errors from a child node don't tear down siblings already added to the tree.
func _build_ocean_shell() -> void:
	var ocean_shell_script := load("res://ocean_shell.gd")
	if ocean_shell_script == null:
		print("EDEN_VOXEL_WATER_DEMO: ocean_shell.gd failed to load, skipping ocean shell")
		return
	ocean_shell = ocean_shell_script.new()
	ocean_shell.planet_radius = PLANET_RADIUS
	ocean_shell.planet_center = Vector3.ZERO
	# Comfortably past VoxelWaterSimulator's own near-field water meshes (see
	# _refresh_water_meshes()) and the VoxelViewer's view_distance (300) above, with margin --
	# the ocean shell should only ever appear well outside where near-field water can be.
	ocean_shell.near_field_radius = 450.0
	add_child(ocean_shell)


func _build_lighting() -> void:
	# Fixed world-space rotation + energy=3.0/LINEAR tonemap is what every screenshot this demo
	# ever produced looked essentially black with (confirmed this session via a deliberate
	# over-exposure test on eden_stress_test.gd: the shading pipeline itself was always
	# correct, it just needed far more light energy than "looks reasonable" numbers suggest,
	# and a light direction that actually points at wherever up_dir is instead of a fixed
	# world rotation that only lights whichever patch of the planet happens to face it).
	var sun_from := (_up_dir * 0.65 + Vector3(0.3, 0.85, 0.25)).normalized()
	var light := DirectionalLight3D.new()
	add_child(light)
	var ref := Vector3.UP if absf(sun_from.y) < 0.99 else Vector3.RIGHT
	light.look_at(-sun_from, ref)
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


func _process(delta: float) -> void:
	_refresh_accum += delta
	if _refresh_accum >= REFRESH_INTERVAL and water_sim != null:
		_refresh_accum = 0.0
		_refresh_water_meshes()

	_frame_count += 1
	if _frame_count % 120 == 0:
		var ocean_leaf_count := -1
		if ocean_shell != null and ocean_shell.has_method("get_leaf_count"):
			ocean_leaf_count = ocean_shell.get_leaf_count()
		print("EDEN_VOXEL_WATER_DEMO: frame=", _frame_count,
				" data_blocks=", terrain.debug_get_data_block_count(),
				" mesh_blocks=", terrain.debug_get_mesh_block_count(),
				" active_water=", (water_sim.get_active_block_count() if water_sim != null else -1),
				" wet_meshes=", _water_mesh_nodes.size(),
				" ocean_leaves=", ocean_leaf_count)
	if _frame_count == CAPTURE_FRAME:
		var img := get_viewport().get_texture().get_image()
		var err := img.save_png("res://screenshot_voxel_water.png")
		print("EDEN_VOXEL_WATER_DEMO: screenshot save result=", err, " wet_meshes=", _water_mesh_nodes.size())
	if _frame_count == 300:
		var img2 := get_viewport().get_texture().get_image()
		var err2 := img2.save_png("res://screenshot_ocean_shell_early.png")
		print("EDEN_VOXEL_WATER_DEMO: early ocean-shell screenshot save result=", err2)


# get_wet_block_positions_at_lod() (unlike get_active_block_positions()) also reports uniform,
# at-rest-and-not-simulated blocks (e.g. open ocean) -- see its own doc comment. Needed here
# since baked ocean can be entirely uniform far from any shore.
#
# Scans LOD0 first (real detail), then progressively coarser levels up to MAX_WATER_LOD, only
# using a coarser block where finer data hasn't streamed in yet -- see MAX_WATER_LOD's own
# comment. `claimed` tracks which LOD0-block-grid cells are already covered by finer data this
# refresh (checked/marked via each candidate's 8 corners rather than its full interior -- a
# cheap approximation, not exact geometric coverage, but enough to suppress the common case:
# every LOD's box always nests around/contains the next-finer LOD's box, so without this a
# coarse fallback mesh would routinely render directly underneath (and z-fight with) real LOD0
# water once it arrives).
func _refresh_water_meshes() -> void:
	var bs := terrain.get_data_block_size()
	var seen := {}
	var claimed := {}

	# Round-robin batched: with tiered simulation now keeping LOD0 wet blocks numerous on a
	# mostly-open-ocean world, regenerating geometry for every single one every REFRESH_INTERVAL
	# stalls the main thread (measured: hangs indefinitely as the wet set grows into the
	# thousands). Only a rotating slice gets remeshed each pass, matching
	# eden_stress_test.gd's OPTIMIZED_PERF pattern.
	const LOD0_BATCH_SIZE := 64
	var wet0 := water_sim.get_wet_block_positions_at_lod(0, 0.05)
	var n := wet0.size()
	if n > 0:
		var count: int = mini(LOD0_BATCH_SIZE, n)
		for i in range(count):
			var idx := (_lod0_refresh_cursor + i) % n
			var block_pos: Vector3i = wet0[idx]
			var key := [0, block_pos]
			var faces := water_sim.generate_water_mesh_smooth_faces(block_pos, 0.05, 0)
			if faces.size() == 0:
				_remove_water_mesh(key)
			else:
				_update_water_mesh(key, block_pos, 0, faces, bs)
		_lod0_refresh_cursor = (_lod0_refresh_cursor + count) % n
	for v in wet0:
		var block_pos: Vector3i = v
		var key := [0, block_pos]
		seen[key] = true
		for c in _lod0_corners(block_pos, 0):
			claimed[c] = true

	# Coarser LODs (1..MAX_WATER_LOD) can have data-box block counts comparable to LOD0's, so
	# scanning/remeshing them every single refresh (this function already only runs every
	# REFRESH_INTERVAL, but that's not enough on a mostly-open-ocean world where nearly every
	# resident block at every LOD is wet) is too expensive -- throttle them to their own slower
	# cadence, and skip regenerating geometry for blocks that already have a mesh (coarse water
	# is a static-ish fallback, not real-time-critical -- see MAX_WATER_LOD's own comment).
	_coarse_water_refresh_accum += REFRESH_INTERVAL
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
				if _water_mesh_nodes.has(key):
					continue
				var faces := water_sim.generate_water_mesh_smooth_faces(block_pos, 0.05, lod)
				if faces.size() == 0:
					continue
				_update_water_mesh(key, block_pos, lod, faces, bs)
	for k in _coarse_water_keys.keys():
		seen[k] = true

	for k in _water_mesh_nodes.keys():
		if not seen.has(k):
			_remove_water_mesh(k)


# The 8 corners of `block_pos` (at `lod`)'s footprint, expressed in LOD0's block-grid units --
# see generate_water_mesh_smooth_faces()'s own doc comment for why block grids nest this way
# (a block at lod L covers exactly the LOD0-block-grid range [pos << L, (pos << L) + 2^L)).
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
