extends "res://perf/atmo_perf.gd"

# Reference screenshots for visual A/B of performance changes: every view at 8 headings, time frozen so two runs
# render the same moment.
#   godot --path demo_eden --script res://perf/capture.gd --resolution 1920x1080 -- <out_dir> [views=...]

func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var out_dir: String = args[0]
	var views := ["orbit", "high", "low", "surface"]
	for a in args:
		if a.begins_with("views="):
			views = Array(a.substr(6).split(","))
	DirAccess.make_dir_recursive_absolute(out_dir)
	_scene = load("res://_ocean_editor_probe.tscn").instantiate()
	_scene.get_node("Camera3D2/VoxelViewer").free()
	root.add_child(_scene)
	_cam = _scene.get_node("Camera3D")
	_terrain = _scene.get_node("VoxelLodTerrain")
	# set=NodePath:property:value overrides, e.g. set=VoxelLodTerrain/Ocean:lighting_mode:2 (applied in the tree,
	# where the nodes react to them)
	for a in args:
		if a.begins_with("set="):
			# The property can be a path, e.g. set=VoxelLodTerrain:material:shader_parameter/u_debug_mode:0
			var parts: PackedStringArray = a.substr(4).split(":")
			var node := _scene.get_node(parts[0])
			var prop := ":".join(parts.slice(1, parts.size() - 1))
			node.set_indexed(prop, str_to_var(parts[-1]))
			print("CAPTURE set %s.%s = %s" % [parts[0], prop, node.get_indexed(prop)])
	var ocean_mat: ShaderMaterial = _scene.get_node("VoxelLodTerrain/Ocean").material
	await _apply_patches()
	ocean_mat.set_shader_parameter("freeze_time", true)
	# Shader TIME drives the clouds too; a fixed time scale of 0 keeps every run at the same moment
	Engine.time_scale = 0.0
	for view in views:
		_place(view)
		await _settle()
		for k in 8:
			_cam.basis = Basis(_up, TAU * k / 8.0) * _base_basis
			for i in 4:
				await process_frame
			await RenderingServer.frame_post_draw
			root.get_texture().get_image().save_png(out_dir.path_join("%s_%d.png" % [view, k]))
	quit(0)
