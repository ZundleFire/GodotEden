extends "res://perf/atmo_perf.gd"

# Cost of individual ocean / cloud / atmosphere settings: per-pass GPU time over a yaw sweep, baseline vs each variant.
# Needs --gpu-profile (per-pass timestamps).
#   godot --gpu-profile --path demo_eden --script res://perf/toggle_probe.gd --resolution 1920x1080 -- <view> <group>


func _passes(frames: int) -> Dictionary:
	var rd := RenderingServer.get_rendering_device()
	var sums := {}
	for i in frames:
		_cam.basis = Basis(_up, TAU * i / frames) * _base_basis
		await process_frame
		var n := rd.get_captured_timestamps_count()
		for k in range(1, n):
			# Each timestamp marks the START of a step, so the gap before it is the previous step's duration
			var name := rd.get_captured_timestamp_name(k - 1)
			var dt := (rd.get_captured_timestamp_gpu_time(k) - rd.get_captured_timestamp_gpu_time(k - 1)) / 1000.0
			sums[name] = sums.get(name, 0.0) + dt
		if n > 1:
			sums["TOTAL"] = sums.get("TOTAL", 0.0) + (rd.get_captured_timestamp_gpu_time(n - 1) - rd.get_captured_timestamp_gpu_time(0)) / 1000.0
	_cam.basis = _base_basis
	var out := {}
	for k in ["TOTAL", "Render Depth Pre-Pass", "Render Opaque Pass", "Render 3D Transparent Pass", "Process Post Opaque Compositor Effects", "Process Post Transparent Compositor Effects", "Render Sky", "Setup Sky", "Setup Sky Resolution Buffers", "Copy Screen Texture"]:
		# Timestamps are in microseconds
		out[k.replace("Process ", "").replace(" Compositor Effects", "").replace("Render 3D ", "")] = snappedf(sums.get(k, 0.0) / frames / 1000.0, 0.01)
	return out


# [label, [[object, property, value], ...]]: a property on a node, or "shader_parameter/x" on a material.
# Originals are read back before each variant and restored after it.
func _variants(group: String) -> Array:
	var ocean := _scene.get_node("VoxelLodTerrain/Ocean")
	var clouds := _scene.get_node("VoxelLodTerrain/EdenCloudShell")
	var rings := _scene.get_node("VoxelLodTerrain/EdenPlanetRings")
	var atmo := _scene.get_node("VoxelLodTerrain/EdenPlanetAtmosphere")
	var om: ShaderMaterial = ocean.material
	match group:
		"ocean":
			return [
				["no ocean", [[ocean, "visible", false]]],
				["tri 2x larger", [[ocean, "lod0_triangle_size", ocean.lod0_triangle_size * 2.0]]],
				["split 0.5x", [[ocean, "split_distance_factor", ocean.split_distance_factor * 0.5]]],
				["WaveCount 0", [[om, "shader_parameter/WaveCount", 0]]],
				["UVWaveCount 0", [[om, "shader_parameter/UVWaveCount", 0]]],
				["currents off", [[om, "shader_parameter/current_enabled", false]]],
				["low_poly off", [[om, "shader_parameter/low_poly_normals", false]]],
				["refraction off", [[om, "shader_parameter/refraction", 0.0]]],
				["lighting Fast Sky", [[ocean, "lighting_mode", 1]]],
				["lighting Fast", [[ocean, "lighting_mode", 2]]],
				["lighting Godot", [[ocean, "lighting_mode", 0]]],
			]
		"clouds":
			return [
				["no clouds", [[clouds, "visible", false]]],
				["no rings", [[rings, "visible", false]]],
			]
		"clouds2":
			return [
				["micro off", [[clouds, "cloud_micro_weight", 0.0]]],
				["detail off", [[clouds, "cloud_detail_weight", 0.0]]],
				["type var off", [[clouds, "cloud_type_variation", 0.0]]],
				["facet only (no soft)", [[clouds, "cloud_facet_mix", 1.0]]],
				["weather off (1 phase)", [[clouds, "weather_enabled", false]]],
				["2 layers", [[clouds, "cloud_layers", 2]]],
			]
		"terrain":
			var tm: ShaderMaterial = _terrain.material
			return [
				["far field off", [[_terrain, "far_enabled", false]]],
				["detail normals off", [[tm, "shader_parameter/detail_normal_strength", 0.0]]],
				["3D at half res", [[root, "scaling_3d_scale", 0.5]]],
				["traditional path", [[_terrain, "render_mode", 0]]],
			]
		"sky":
			var sky: Sky = root.world_3d.environment.sky if root.world_3d.environment else null
			for we in _scene.find_children("*", "WorldEnvironment", true, false):
				sky = we.environment.sky
			if sky == null:
				sky = atmo.get_viewport().find_world_3d().environment.sky
			return [
				["radiance 32", [[sky, "radiance_size", Sky.RADIANCE_SIZE_32]]],
				["parent planet paused", [[_scene.get_node("EdenParentPlanet"), "process_mode", Node.PROCESS_MODE_DISABLED]]],
				["space env paused", [[_scene.get_node("EdenSpaceEnvironment"), "process_mode", Node.PROCESS_MODE_DISABLED]]],
				["atmosphere paused", [[atmo, "process_mode", Node.PROCESS_MODE_DISABLED]]],
				["clouds paused", [[clouds, "process_mode", Node.PROCESS_MODE_DISABLED]]],
			]
		"skyfeat":
			var sm: ShaderMaterial = atmo.get_sky_material()
			var space := _scene.get_node("EdenSpaceEnvironment")
			var parent := _scene.get_node("EdenParentPlanet")
			return [
				["parent planet off", [[parent, "process_mode", Node.PROCESS_MODE_DISABLED], [sm, "shader_parameter/parent_planet_enabled", false]]],
				["panorama off", [[space, "process_mode", Node.PROCESS_MODE_DISABLED], [sm, "shader_parameter/space_panorama_enabled", false], [sm, "shader_parameter/space_overlay_enabled", false]]],
				["stars off", [[space, "process_mode", Node.PROCESS_MODE_DISABLED], [sm, "shader_parameter/star_intensity", 0.0]]],
			]
		"atmo":
			return [
				["rays 32", [[atmo, "light_ray_samples", 32]]],
				["rays off", [[atmo, "sun_ray_intensity", 0.0], [atmo, "moon_ray_intensity", 0.0]]],
				["fog off", [[atmo, "fog_density", 0.0]]],
				["no atmosphere node", [[atmo, "visible", false]]],
			]
	return []


func _apply(changes: Array) -> Array:
	var undo := []
	for c in changes:
		undo.append([c[0], c[1], c[0].get(c[1])])
		c[0].set(c[1], c[2])
	return undo


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var view: String = args[0] if args.size() > 0 else "surface"
	var group: String = args[1] if args.size() > 1 else "ocean"
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	_scene = load("res://_ocean_editor_probe.tscn").instantiate()
	_scene.get_node("Camera3D2/VoxelViewer").free()
	root.add_child(_scene)
	_cam = _scene.get_node("Camera3D")
	_terrain = _scene.get_node("VoxelLodTerrain")
	await _apply_patches()
	_place(view)
	await _settle()
	await _passes(30)
	print("TOGGLE %s baseline: %s" % [view, await _passes(120)])
	for v in _variants(group):
		var undo := _apply(v[1])
		await _passes(30)
		print("TOGGLE %s %s: %s" % [view, v[0], await _passes(120)])
		_apply(undo)
		await _passes(30)
	print("TOGGLE %s baseline again: %s" % [view, await _passes(120)])
	quit(0)
