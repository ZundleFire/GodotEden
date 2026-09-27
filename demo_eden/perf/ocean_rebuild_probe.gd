extends "res://perf/atmo_perf.gd"

# Main-thread cost of the ocean's LOD rebuild while moving near the surface, and of whole frames around it.
#   godot --path demo_eden --script res://perf/ocean_rebuild_probe.gd --resolution 1920x1080 -- [speed_m_s]

func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var speed: float = float(args[0]) if args.size() > 0 else 100.0
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	_scene = load("res://_ocean_editor_probe.tscn").instantiate()
	_scene.get_node("Camera3D2/VoxelViewer").free()
	root.add_child(_scene)
	_cam = _scene.get_node("Camera3D")
	_terrain = _scene.get_node("VoxelLodTerrain")
	var ocean := _scene.get_node("VoxelLodTerrain/Ocean")
	_place("surface")
	await _settle()
	var start := _cam.position
	var fwd := -_cam.global_basis.z
	# Direct cost of one rebuild at a few offsets
	var costs := []
	for i in 10:
		_cam.position = start + fwd * (i * 60.0)
		await process_frame
		var t0 := Time.get_ticks_usec()
		ocean.update_lod()
		costs.append((Time.get_ticks_usec() - t0) / 1000.0)
	print("UPDATE_LOD ms: ", costs)
	# Frame times while flying at `speed`, with the frames that follow a rebuild marked
	_cam.position = start
	await _settle()
	var frames := []
	var last := Time.get_ticks_usec()
	var t_start := last
	var leaves: int = ocean.get_leaf_count()
	while (Time.get_ticks_usec() - t_start) / 1e6 < 10.0:
		# Fixed step per frame (speed at 60 fps), so a slow frame doesn't also move further
		_cam.position += fwd * speed / 60.0
		await process_frame
		var now := Time.get_ticks_usec()
		var lc: int = ocean.get_leaf_count()
		frames.append([snappedf((now - last) / 1000.0, 0.1), lc != leaves, snappedf(Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0, 0.1)])
		leaves = lc
		last = now
	var ms := []
	for f in frames:
		ms.append(f[0])
	ms.sort()
	var med: float = ms[ms.size() / 2]
	var spikes := frames.filter(func(f): return f[0] > med * 2.0)
	print("FRAMES %d median %.1f p99 %.1f max %.1f spikes>2x %d, of which after leaf change %d" % [frames.size(), med,
			ms[int(ms.size() * 0.99)], ms[-1], spikes.size(), spikes.filter(func(f): return f[1]).size()])
	for s in spikes.slice(0, 15):
		print("  SPIKE ", s)
	quit(0)
