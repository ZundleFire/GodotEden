extends SceneTree
## Foliage perf bench on the V4 planet (same terrain setup as _ocean_editor_probe.tscn).
##   godot --path demo_eden --resolution 1280x720 -s res://_foliage_bench.gd -- --mode=both --out=<file.json>
## Modes: none | grass | trees | both. Optional --tree_impl=scene|multimesh, --grass_density=<f>.
## Phases: settle (wait for mesh + instance blocks to stop changing), static (fixed camera), walk
## (camera moves along the ground at WALK_SPEED, exercising streaming/spawn hitches).

const PLANET_RADIUS := 40000.0
const STATIC_FRAMES := 300
const WALK_FRAMES := 600
const WALK_SPEED := 25.0 # m/s
const EYE_HEIGHT := 2.0

var args := {}
var gen: EdenPlanetGeneratorV4
var terrain: VoxelLodTerrain
var camera: Camera3D
var instancers: Array[VoxelInstancer] = []
var up_dir: Vector3
var fwd: Vector3
var surface_r: float

var phase := "settle"
var phase_frame := 0
const SETTLE_QUIET_S := 4.0
const SETTLE_MAX_S := 120.0
var stable_since_us := 0
var last_sig := ""
var last_us := 0
var frame_ms: PackedFloat32Array = []
var gpu_ms: PackedFloat32Array = []
var cpu_ms: PackedFloat32Array = []
var settle_worst_ms := 0.0
var settle_start_us := 0
var result := {}


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=")
		args[kv[0]] = kv[1] if kv.size() > 1 else "1"
	var mode: String = args.get("mode", "both")

	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)

	if args.has("probe_scene"):
		# The real editor scene: its terrain settings and EdenFoliage node, not the bench's own copy
		var scene: Node = load("res://_ocean_editor_probe.tscn").instantiate()
		root.add_child(scene)
		terrain = scene.get_node("VoxelLodTerrain")
		gen = terrain.generator
		for key in ["generate_collisions", "far_enabled", "render_mode"]:
			result["probe_" + key] = terrain.get(key)
		if args.has("no_collisions"):
			terrain.generate_collisions = false
		var pf: Node = terrain.get_node("EdenFoliage")
		for k in args: # --f_<export>=value overrides EdenFoliage exports before it builds
			if k.begins_with("f_"):
				pf.set(k.substr(2), str_to_var(args[k]))
		if args.has("only"): # --only=grass,trees,bushes,rocks,wood (or none): other layers off, on a copy
			var groups: PackedStringArray = args.only.split(",")
			var cfg: EdenFoliageConfig = pf.config.duplicate(true)
			for b in cfg.biomes:
				for l in b.layers:
					l.enabled = _layer_group(l) in groups
			pf.config = cfg
			result.only = args.only
		instancers.append(pf)
		_setup_view()
		return

	gen = EdenPlanetGeneratorV4.new()
	gen.bake_ocean_water = false
	gen.terrain_feature_scale = 10642.0
	gen.terrain_aesthetic_bias = 0.84
	gen.continent_scale = 80420.0
	gen.island_bias = 0.0

	var mesher := VoxelMesherTransvoxel.new()
	mesher.texturing_mode = 1
	mesher.surface_data_enabled = true
	mesher.mesh_optimization_enabled = true
	var fmt := VoxelFormat.new()
	fmt.set_channel_depth(VoxelBuffer.CHANNEL_DATA6, VoxelBuffer.DEPTH_32_BIT)
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/EdenPlanet_LowPolyClimate.gdshader")
	mat.set_shader_parameter("u_rock_slope_start", 0.8)
	mat.set_shader_parameter("u_rock_slope_end", 0.78)

	terrain = VoxelLodTerrain.new()
	terrain.generator = gen
	terrain.mesher = mesher
	terrain.format = fmt
	terrain.view_distance = 80000
	terrain.lod_count = 12
	terrain.lod_distance = 128.0
	terrain.secondary_lod_distance = 256.0
	terrain.material = mat
	terrain.mesh_block_size = 32
	if args.has("gpu_driven"): # the probe scene uses render_mode = 1
		terrain.render_mode = VoxelLodTerrain.RENDER_MODE_GPU_DRIVEN
	terrain.generate_collisions = false
	root.add_child(terrain)

	# One instancer per terrain (engine limit), so every layer goes into EdenFoliage's library
	if mode != "none":
		var impl: String = args.get("tree_impl", "multimesh")
		var foliage: VoxelInstancer = load("res://eden_foliage.gd").new()
		foliage.grass_enabled = mode == "grass" or mode == "both"
		foliage.trees_enabled = (mode == "trees" or mode == "both") and impl == "multimesh"
		foliage.tree_variants = int(args.get("variants", "4"))
		if args.has("grass_density"):
			foliage.grass_density = float(args.grass_density)
		if args.has("grass_fade_end"): # e.g. 300 = no fade / chunk cutoff, for comparison
			foliage.grass_fade_start = float(args.grass_fade_end) - 15.0
			foliage.grass_fade_end = float(args.grass_fade_end)
		terrain.add_child(foliage) # parent first: the grass chunk cutoff reads the terrain's lod_distance
		foliage.library = foliage.build_library()
		if (mode == "trees" or mode == "both") and impl == "scene":
			_add_scene_trees(foliage.library)
		instancers.append(foliage)

	_setup_view()
	result.mode = mode
	result.tree_impl = args.get("tree_impl", "scene")


func _layer_group(l: EdenFoliageLayer) -> String:
	if l.is_grass():
		return "grass"
	if l.is_tree():
		return "trees"
	if l.is_rock():
		return "rocks"
	if l.kind == EdenFoliageLayer.Kind.BRANCH or l.kind == EdenFoliageLayer.Kind.LOG:
		return "wood"
	return "bushes"


func _setup_view() -> void:
	var light := DirectionalLight3D.new()
	root.add_child(light)
	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.5, 0.7, 0.9)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.65, 0.7)
	env.ambient_light_energy = 0.4
	env_node.environment = env
	root.add_child(env_node)

	up_dir = _find_grass_dir()
	surface_r = _surface_radius(up_dir)
	result.sample_surface_vs_sdf_m = float(gen.sample_surface(up_dir).height) + PLANET_RADIUS - surface_r
	var ref := Vector3.UP if absf(up_dir.y) < 0.99 else Vector3.RIGHT
	fwd = up_dir.cross(ref).normalized()
	light.basis = Basis.looking_at(-(up_dir * 0.7 + fwd * 0.5).normalized(), up_dir.cross(fwd))

	camera = Camera3D.new()
	camera.far = 20000.0
	camera.current = true
	root.add_child(camera)
	var viewer := VoxelViewer.new()
	viewer.view_distance = int(args.get("viewer_distance", "20000"))
	if args.has("viewer_no_collisions"): # like the editor viewer
		viewer.requires_collisions = false
	camera.add_child(viewer)
	_place_camera()

	result.dir = up_dir
	settle_start_us = Time.get_ticks_usec()
	stable_since_us = settle_start_us
	last_us = settle_start_us


# The old path for comparison: one EdenTreeInstance scene per tree, same generator settings as
# EdenFoliage's tree layer (eden_foliage_demo.gd's densities, retargeted to V4 grass + dirt).
func _add_scene_trees(lib: VoxelInstanceLibrary) -> void:
	var id := 1 # same ids as EdenFoliage's tree items: identical placement with --variants=1
	for tree_type in [EdenTreeShape.TREE_OAK, EdenTreeShape.TREE_BIRCH, EdenTreeShape.TREE_PINE]:
		var g := VoxelInstanceGenerator.new()
		g.density = 0.02
		g.emit_mode = VoxelInstanceGenerator.EMIT_FROM_FACES_FAST
		g.max_slope_degrees = 30.0
		g.voxel_texture_filter_enabled = true
		g.voxel_texture_filter_array = PackedInt32Array([0, 4])
		g.random_rotation = true
		g.min_scale = 0.8
		g.max_scale = 1.3
		var tree := EdenTreeInstance.new()
		tree.tree_type = tree_type + 1
		tree.season = EdenTreeShape.SEASON_SUMMER
		var packed := PackedScene.new()
		packed.pack(tree)
		tree.free()
		var item := VoxelInstanceLibrarySceneItem.new()
		item.scene = packed
		item.generator = g
		item.lod_index = 1
		lib.add_item(id, item)
		id += 1


# Low, mild, moist, FLAT land: where V4 paints grass and slope doesn't turn it to rock. Climate
# scoring alone landed on a rocky patch with the meadow out of grass range.
func _find_grass_dir() -> Vector3:
	const N := 2000
	var golden := PI * (3.0 - sqrt(5.0))
	var scored := []
	for i in N:
		var y := 1.0 - float(i) / float(N - 1) * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var d := Vector3(cos(golden * i) * r, y, sin(golden * i) * r)
		var s: Dictionary = gen.sample_surface(d)
		if s.height < 30.0:
			continue
		scored.append([-absf(s.height - 300.0) / 300.0 + s.moisture - absf(s.temperature - 0.55) * 2.0 - absf(d.y), d])
	scored.sort_custom(func(a, b): return a[0] > b[0])
	# Among the best climates, take the flattest ground: max height step over a 30 m ring
	var best: Vector3 = scored[0][1]
	var best_relief := INF
	for c in scored.slice(0, 40):
		var d: Vector3 = c[1]
		var h0 := float(gen.sample_surface(d).height)
		var t := d.cross(Vector3.UP if absf(d.y) < 0.99 else Vector3.RIGHT).normalized()
		var relief := 0.0
		for k in 8:
			var o := d.rotated(t.rotated(d, TAU * k / 8.0), 30.0 / PLANET_RADIUS)
			relief = maxf(relief, absf(float(gen.sample_surface(o).height) - h0))
		if relief < best_relief:
			best_relief = relief
			best = d
	result.spot_relief_30m = best_relief
	return best


# Surface radius where the generator's SDF (what gets meshed) crosses zero. One-voxel blocks take
# the exact (non-lattice) path; ~1 m resolution from the integer voxel origin.
var _probe := VoxelBuffer.new()


func _surface_radius(dir: Vector3) -> float:
	if _probe.get_size() != Vector3i.ONE:
		_probe.create(1, 1, 1)
	var lo := PLANET_RADIUS - 6000.0
	var hi := PLANET_RADIUS + 12000.0
	for i in 26:
		var mid := (lo + hi) * 0.5
		var p := dir * mid
		gen.generate_block(_probe, Vector3(Vector3i(p.floor())), 0)
		if _probe.get_voxel_f(0, 0, 0, VoxelBuffer.CHANNEL_SDF) > 0.0:
			hi = mid
		else:
			lo = mid
	return (lo + hi) * 0.5


func _place_camera() -> void:
	var p := up_dir * (surface_r + EYE_HEIGHT)
	camera.transform = Transform3D(Basis.looking_at(fwd * 100.0 - up_dir * 12.0, up_dir), p)


func _instance_total() -> int:
	var n := 0
	for inst in instancers:
		var counts: Dictionary = inst.debug_get_instance_counts()
		for k in counts:
			n += counts[k]
	return n


func _stats() -> Dictionary:
	var f := frame_ms.duplicate()
	f.sort()
	var sum := 0.0
	for v in f:
		sum += v
	var g := 0.0
	for v in gpu_ms:
		g += v
	var c := 0.0
	for v in cpu_ms:
		c += v
	var hitches := 0
	for v in f:
		if v > 33.3:
			hitches += 1
	return {
		"avg_ms": sum / f.size(), "p50_ms": f[f.size() / 2], "p95_ms": f[int(f.size() * 0.95)],
		"max_ms": f[f.size() - 1], "gpu_avg_ms": g / gpu_ms.size(), "render_cpu_avg_ms": c / cpu_ms.size(),
		"frames_over_33ms": hitches,
		"hitch_frames": Array(range(frame_ms.size())).filter(func(i): return frame_ms[i] > 33.3),
		"draw_calls": Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		"primitives": Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
		"objects": Performance.get_monitor(Performance.RENDER_TOTAL_OBJECTS_IN_FRAME),
		"nodes": Performance.get_monitor(Performance.OBJECT_NODE_COUNT),
		"instances": _instance_total(),
	}


func _process(_delta: float) -> bool:
	var now := Time.get_ticks_usec()
	var ms := (now - last_us) / 1000.0
	last_us = now
	phase_frame += 1
	var vp := root.get_viewport_rid()

	match phase:
		"settle":
			if args.has("viewer_teleport"):
				# Editor case: the editor viewer starts at the origin (planet core), then jumps to the camera
				if phase_frame == 1:
					camera.position = Vector3.ZERO
				if now - settle_start_us < 3e6:
					stable_since_us = now
					return false
				if not result.has("teleported"):
					result.teleported = true
					_place_camera()
			if phase_frame > 5:
				settle_worst_ms = maxf(settle_worst_ms, ms)
			# Stable = no mesh/instance change for SETTLE_QUIET_S of wall time (frame counts are
			# meaningless uncapped: thousands of frames pass while streaming is still going)
			var sig := str(terrain.debug_get_mesh_block_count(), "/", _instance_total())
			if sig != last_sig:
				last_sig = sig
				stable_since_us = now
			var timed_out := now - settle_start_us > SETTLE_MAX_S * 1e6
			var settled := now - stable_since_us > SETTLE_QUIET_S * 1e6 and terrain.debug_get_mesh_block_count() > 0
			if settled and args.has("late_library") and not result.has("late_library_instances_before"):
				# Editor case: the library (re)assigned after blocks already meshed
				result.late_library_instances_before = _instance_total()
				instancers[0].library = instancers[0].build_library()
				stable_since_us = now
				return false
			if settled or timed_out:
				result.settle = {"frames": phase_frame, "seconds": (stable_since_us - settle_start_us) / 1e6,
						"worst_frame_ms": settle_worst_ms, "timed_out": timed_out}
				_next("static")
		"static":
			_sample(ms, vp)
			if phase_frame >= STATIC_FRAMES:
				result.static = _stats()
				_capture("static")
				_next("walk")
				# Walk at a real 60 fps pace so streaming/spawning keeps up like gameplay; avg_ms is then
				# capped, so read max_ms, frames_over_33ms and gpu_avg_ms for this phase
				Engine.max_fps = 60
		"walk":
			if phase_frame > 1: # frame 1 carries the static-phase screenshot save
				_sample(ms, vp)
			var ang := WALK_SPEED * phase_frame / 60.0 / surface_r # fixed step: identical path every run
			var axis := up_dir.cross(fwd).normalized()
			var d := up_dir.rotated(axis, -ang)
			var r := _surface_radius(d)
			var p := d * (r + EYE_HEIGHT)
			var f := fwd.rotated(axis, -ang)
			camera.transform = Transform3D(Basis.looking_at(f * 100.0 - d * 12.0, d), p)
			if phase_frame >= WALK_FRAMES:
				result.walk = _stats()
				result.walk_series = {"frame_ms": Array(frame_ms), "gpu_ms": Array(gpu_ms)}
				_finish()
				return true
	return false


func _sample(ms: float, vp: RID) -> void:
	frame_ms.append(ms)
	gpu_ms.append(RenderingServer.viewport_get_measured_render_time_gpu(vp))
	cpu_ms.append(RenderingServer.viewport_get_measured_render_time_cpu(vp))


func _next(p: String) -> void:
	phase = p
	phase_frame = 0
	frame_ms.clear()
	gpu_ms.clear()
	cpu_ms.clear()


func _capture(tag: String) -> void:
	var out: String = args.get("out", "")
	if out.is_empty():
		return
	root.get_viewport().get_texture().get_image().save_png(out.get_basename() + "_" + tag + ".png")


func _finish() -> void:
	var text := JSON.stringify(result, "  ")
	print("FOLIAGE_BENCH ", text)
	var out: String = args.get("out", "")
	if not out.is_empty():
		var f := FileAccess.open(out, FileAccess.WRITE)
		f.store_string(text)
