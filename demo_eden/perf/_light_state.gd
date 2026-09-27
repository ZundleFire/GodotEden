extends SceneTree
## Prints what decides how bright the planet looks: tonemapping and exposure, ambient light, the sun, and the
## terrain material's colour/lighting parameters.
##   godot --path demo_eden --resolution 960x540 -s res://perf/_light_state.gd

var play: Node
var t0 := 0


func _initialize() -> void:
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play
	t0 = Time.get_ticks_msec()


func _process(_d: float) -> bool:
	if Time.get_ticks_msec() - t0 < 9000:
		return false
	for we in root.find_children("*", "WorldEnvironment", true, false):
		var e: Environment = we.environment
		print("LIGHT env tonemap %d exposure %.3f white %.3f  ambient src %d energy %.3f color %s sky_contrib %.2f  adjust %s b %.2f c %.2f s %.2f  glow %s intensity %.2f bloom %.2f  ssao %s int %.2f" % [
			e.tonemap_mode, e.tonemap_exposure, e.tonemap_white, e.ambient_light_source, e.ambient_light_energy, e.ambient_light_color,
			e.ambient_light_sky_contribution, e.adjustment_enabled, e.adjustment_brightness, e.adjustment_contrast, e.adjustment_saturation,
			e.glow_enabled, e.glow_intensity, e.glow_bloom, e.ssao_enabled, e.ssao_intensity])
	for l in root.find_children("*", "DirectionalLight3D", true, false):
		print("LIGHT sun %s visible %s energy %.3f color %s indirect %.2f specular %.2f" % [l.name, l.visible, l.light_energy, l.light_color, l.light_indirect_energy, l.light_specular])
	var terrain: VoxelLodTerrain = play.find_children("*", "VoxelLodTerrain", true, false)[0]
	var m: ShaderMaterial = terrain.material
	for u in m.shader.get_shader_uniform_list():
		var n: String = u.name
		if n.containsn("bright") or n.containsn("vibr") or n.containsn("rough") or n.containsn("spec") or n.containsn("facet") \
				or n.containsn("light") or n.containsn("contrast") or n.containsn("satur") or n.containsn("exposure") \
				or n.containsn("ambient") or n.containsn("metal") or n.containsn("palette") or n.containsn("shade") or n.containsn("_color"):
			print("LIGHT terrain %s = %s" % [n, m.get_shader_parameter(n)])
	for n in root.find_children("*", "", true, false):
		if n.get_class() == "EdenAmbience":
			for p in ["exposure", "saturation", "contrast", "look_enabled", "sun_energy", "ambient_energy", "day_exposure", "night_exposure"]:
				if n.get(p) != null:
					print("LIGHT ambience %s = %s" % [p, n.get(p)])
			for pr in n.get_property_list():
				if (pr.name as String).containsn("expos") or (pr.name as String).containsn("bright") or (pr.name as String).containsn("satur"):
					print("LIGHT ambience %s = %s" % [pr.name, n.get(pr.name)])
		if n.get_class() == "EdenPlanetAtmosphere":
			for pr in n.get_property_list():
				var pn: String = pr.name
				if pn.containsn("energy") or pn.containsn("intensity") or pn.containsn("expos") or pn.containsn("ambient"):
					print("LIGHT atmosphere %s = %s" % [pn, n.get(pn)])
	quit()
	return true
