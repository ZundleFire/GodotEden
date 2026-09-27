extends SceneTree

# Frame-time suite for the planet scene: ocean, clouds and atmosphere cost at orbit, high, low and surface views.
#   godot --path demo_eden --script res://perf/atmo_perf.gd --resolution 1920x1080 -- [views=orbit,surface] [quick]
# Each view: load the terrain, then sweep the camera 360 degrees in yaw, measuring the full scene and the scene with
# each component removed. Prints PERF_JSON= with everything.

const RADIUS := 40000.0
const SWEEP_FRAMES := 240
const COMPONENTS := ["VoxelLodTerrain/EdenPlanetAtmosphere", "VoxelLodTerrain/EdenCloudShell", "VoxelLodTerrain/Ocean"]

var _scene: Node3D
var _cam: Camera3D
var _terrain: Node
var _up := Vector3.UP
var _base_basis := Basis()


func _init() -> void:
	_run.call_deferred()


func _settle() -> void:
	var engine: Object = Engine.get_singleton("VoxelEngine")
	var quiet := 0
	var t0 := Time.get_ticks_msec()
	while quiet < 60 and Time.get_ticks_msec() - t0 < 60000:
		await process_frame
		var tk: Dictionary = engine.get_stats().tasks
		var busy: int = tk.meshing + tk.generation + tk.streaming + tk.main_thread
		busy += int(_terrain.get_gpu_driven_statistics().get("pending_ops", 0))
		quiet = quiet + 1 if busy == 0 else 0


# Full yaw sweep around the local up axis: catches the sun direction, the horizon and the planet limb
func _sweep(frames: int) -> Dictionary:
	var vp := root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	var gpu := PackedFloat64Array()
	var times := PackedFloat64Array()
	var last := Time.get_ticks_usec()
	for i in frames:
		_cam.basis = Basis(_up, TAU * i / frames) * _base_basis
		await process_frame
		var now := Time.get_ticks_usec()
		times.append((now - last) / 1000.0)
		last = now
		gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(vp))
	_cam.basis = _base_basis
	var sum := 0.0
	for t in times:
		sum += t
	var gsum := 0.0
	for g in gpu:
		gsum += g
	times.sort()
	gpu.sort()
	var n_low := maxi(1, frames / 100)
	var low := 0.0
	for i in n_low:
		low += times[frames - 1 - i]
	return {
		"fps": snappedf(1000.0 * frames / sum, 0.1),
		"low1_fps": snappedf(1000.0 * n_low / low, 0.1),
		"frame_ms": snappedf(sum / frames, 0.01),
		"gpu_ms": snappedf(gsum / frames, 0.01),
		"gpu_max_ms": snappedf(gpu[frames - 1], 0.01),
	}


func _place(view: String) -> void:
	var dir := Vector3(-12863.9, -1618.6, 37846.1).normalized()
	var east := dir.cross(Vector3.UP).normalized()
	var north := east.cross(dir).normalized()
	_up = dir
	var ground: float = maxf(_terrain.generator.sample_surface(dir).height, 0.0)
	match view:
		"orbit":
			_cam.position = dir * RADIUS * 2.5
			_cam.look_at(Vector3.ZERO, north)
			_up = (-dir).normalized() # sweep around the view axis would be pointless; spin around the planet axis
			_up = north
		"high":
			_cam.position = dir * (RADIUS + 10000.0)
			_cam.look_at(_cam.position + north * 10000.0 - dir * 3000.0, dir)
		"low":
			_cam.position = dir * (RADIUS + ground + 1500.0)
			_cam.look_at(_cam.position + north * 10000.0 - dir * 1000.0, dir)
		"surface":
			_cam.position = dir * (RADIUS + ground + 30.0)
			_cam.look_at(_cam.position + north * 10000.0, dir)
	_base_basis = _cam.basis


# patch=<sky|clouds|ocean>@@old@@new rewrites that built-in shader in place, for A/B-ing a shader change without a
# rebuild. Repeatable; applied in order. Call once the scene is in the tree. "@@", not "|": shader code has "||".
func _apply_patches() -> void:
	await process_frame
	for a in OS.get_cmdline_user_args():
		if not a.begins_with("patch="):
			continue
		var parts: PackedStringArray = a.substr(6).split("@@")
		if parts.size() != 3:
			printerr("PATCH malformed: ", a)
			quit(1)
			return
		var sh: Shader
		match parts[0]:
			"sky": sh = _scene.get_node("VoxelLodTerrain/EdenPlanetAtmosphere").get_sky_material().shader
			"clouds": sh = _scene.get_node("VoxelLodTerrain/EdenCloudShell").get_material().shader
			"ocean": sh = _scene.get_node("VoxelLodTerrain/Ocean").material.shader
		print("PATCH %s found=%s" % [parts[0], sh.code.contains(parts[1])])
		sh.code = sh.code.replace(parts[1], parts[2])


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var views := ["orbit", "high", "low", "surface"]
	var quick := "quick" in args
	for a in args:
		if a.begins_with("views="):
			views = Array(a.substr(6).split(","))
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	_scene = load("res://_ocean_editor_probe.tscn").instantiate()
	# One viewer, on the camera: Camera3D2's viewer keeps surface detail loaded elsewhere and measures the terrain
	_scene.get_node("Camera3D2/VoxelViewer").free()
	# set=NodePath:property:value, e.g. set=VoxelLodTerrain:render_mode:0 (traditional terrain)
	for a in args:
		if a.begins_with("set="):
			var parts: PackedStringArray = a.substr(4).split(":")
			_scene.get_node(parts[0]).set(parts[1], str_to_var(parts[2]))
	root.add_child(_scene)
	_cam = _scene.get_node("Camera3D")
	_terrain = _scene.get_node("VoxelLodTerrain")
	await _apply_patches()

	var results := {}
	for view in views:
		_place(view)
		await _settle()
		await _sweep(30)
		var r := { "full": await _sweep(SWEEP_FRAMES) }
		if not quick:
			for path in COMPONENTS:
				var n := _scene.get_node(path)
				var parent := n.get_parent()
				var idx := n.get_index()
				parent.remove_child(n)
				await _sweep(20)
				var without := await _sweep(SWEEP_FRAMES)
				r[path.get_file()] = snappedf(r.full.gpu_ms - without.gpu_ms, 0.01)
				parent.add_child(n)
				parent.move_child(n, idx)
				await _sweep(20)
		results[view] = r
		print("PERF %s: %s" % [view, r])
	print("PERF_JSON=", JSON.stringify(results))
	quit(0)
