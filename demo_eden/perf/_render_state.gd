extends SceneTree
## Prints the render-relevant state of the running play scene after it settles: the environment's effects, the
## lights' shadows, the viewport's AA/scaling, and the quality knobs of the Eden nodes. For choosing what the
## graphics presets control.
##   godot --path demo_eden --resolution 960x540 -s res://perf/_render_state.gd

var play: Node
var t0 := 0


func _initialize() -> void:
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play
	t0 = Time.get_ticks_msec()


func _process(_d: float) -> bool:
	if Time.get_ticks_msec() - t0 < 8000:
		if Time.get_ticks_msec() - t0 > 6000 and Engine.get_process_frames() % 30 == 0:
			print("RS wind %s flow %s" % [RenderingServer.global_shader_parameter_get("eden_wind"), RenderingServer.global_shader_parameter_get("eden_wind_flow")])
		return false
	for we in root.find_children("*", "WorldEnvironment", true, false):
		var e: Environment = we.environment
		print("RS env %s: ssao %s ssil %s sdfgi %s glow %s vfog %s fog %s ssr %s tonemap %d adjust %s" % [we.get_path(), e.ssao_enabled,
				e.ssil_enabled, e.sdfgi_enabled, e.glow_enabled, e.volumetric_fog_enabled, e.fog_enabled, e.ssr_enabled, e.tonemap_mode, e.adjustment_enabled])
	var cams := root.find_children("*", "Camera3D", true, false)
	for c in cams:
		if c.current and c.environment:
			print("RS camera env on ", c.get_path())
	for l in root.find_children("*", "Light3D", true, false):
		var extra := ""
		if l is DirectionalLight3D:
			extra = "mode %d max_dist %.0f" % [l.directional_shadow_mode, l.directional_shadow_max_distance]
		print("RS light %s %s visible %s shadow %s %s" % [l.get_class(), l.get_path(), l.visible, l.shadow_enabled, extra])
	print("RS viewport msaa %d aa %d taa %s scale %.2f mode %d fsr_sharp %.2f" % [root.msaa_3d, root.screen_space_aa, root.use_taa,
			root.scaling_3d_scale, root.scaling_3d_mode, root.fsr_sharpness])
	print("RS shadow atlas dir size ", ProjectSettings.get_setting("rendering/lights_and_shadows/directional_shadow/size"),
			" soft ", ProjectSettings.get_setting("rendering/lights_and_shadows/directional_shadow/soft_shadow_filter_quality"))
	for n in root.find_children("*", "", true, false):
		var c := n.get_class()
		if c.begins_with("Eden") or c == "VoxelLodTerrain" or c == "VoxelViewer":
			var props := []
			for p in n.get_property_list():
				var nm: String = p.name
				if p.usage & PROPERTY_USAGE_EDITOR and (nm.contains("quality") or nm.contains("step") or nm.contains("distance")
						or nm.contains("resolution") or nm.contains("enabled") or nm.contains("density") or nm.contains("samples")):
					props.append("%s=%s" % [nm, n.get(nm)])
			print("RS node %s (%s): %s" % [n.name, c, ", ".join(props)])
	quit()
	return true
