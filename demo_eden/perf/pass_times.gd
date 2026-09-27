extends "res://perf/atmo_perf.gd"

# Per-pass GPU times from the renderer's own timestamps (what the editor's visual profiler shows), averaged over a
# yaw sweep at one view.
#   godot --path demo_eden --script res://perf/pass_times.gd --resolution 1920x1080 -- [view]

func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var view: String = args[0] if args.size() > 0 else "surface"
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	_scene = load("res://_ocean_editor_probe.tscn").instantiate()
	_scene.get_node("Camera3D2/VoxelViewer").free()
	root.add_child(_scene)
	_cam = _scene.get_node("Camera3D")
	_terrain = _scene.get_node("VoxelLodTerrain")
	_place(view)
	await _settle()
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
	var rd := RenderingServer.get_rendering_device()
	var sums := {}
	var order := []
	var frames := 120
	for i in frames:
		_cam.basis = Basis(_up, TAU * i / frames) * _base_basis
		await process_frame
		var n := rd.get_captured_timestamps_count()
		for k in range(1, n):
			var name := rd.get_captured_timestamp_name(k)
			var dt := (rd.get_captured_timestamp_gpu_time(k) - rd.get_captured_timestamp_gpu_time(k - 1)) / 1000.0
			if not sums.has(name):
				sums[name] = 0.0
				order.append(name)
			sums[name] += dt
	print("PASSES %s (ms, time since previous timestamp):" % view)
	for name in order:
		var ms: float = sums[name] / frames
		if ms >= 0.05:
			print("  %7.2f  %s" % [ms, name])
	quit(0)
