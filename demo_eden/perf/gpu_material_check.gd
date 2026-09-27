extends "res://perf/atmo_perf.gd"

# Whether the GPU-driven terrain draws with each given shader (or falls back to its built-in shading, and why).
#   godot --path demo_eden --script res://perf/gpu_material_check.gd -- res://shaders/a.gdshader res://shaders/b.gdshader

func _run() -> void:
	_scene = load("res://_ocean_editor_probe.tscn").instantiate()
	_scene.get_node("Camera3D2/VoxelViewer").free()
	root.add_child(_scene)
	_terrain = _scene.get_node("VoxelLodTerrain")
	_terrain.render_mode = 1
	var failures := 0
	for path in OS.get_cmdline_user_args():
		_terrain.material.shader = load(path)
		var stats: Dictionary
		for i in 600:
			await process_frame
			stats = _terrain.get_gpu_driven_statistics()
			if stats.material_shader or stats.material_error != "":
				break
		print("GPU_MATERIAL %s: %s %s" % [path, "OK" if stats.material_shader else "FALLBACK", stats.material_error])
		failures += 0 if stats.material_shader else 1
	quit(failures)
