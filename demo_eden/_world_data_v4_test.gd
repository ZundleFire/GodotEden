extends SceneTree
## WorldDataModule answering from EdenPlanetGeneratorV4 (what eden-project's WorldQuery / BiomeClassifier read).
##   godot --headless --path demo_eden -s res://_world_data_v4_test.gd

var ok := true


func _check(cond: bool, msg: String) -> void:
	print("WDM_V4_TEST %s %s" % ["ok  " if cond else "FAIL", msg])
	ok = ok and cond


func _initialize() -> void:
	var wdm: Object = Engine.get_singleton("WorldDataModule")
	var gen := EdenPlanetGeneratorV4.new()
	gen.set("planet_radius", 40000.0)
	gen.set("sea_level", 0.0)
	wdm.set_generator(gen)
	wdm.set_planet_center(Vector3(0, 0, 0))
	_check(wdm.get_generator() == gen, "V4 accepted as the query generator")
	_check(is_equal_approx(wdm.get_planet_radius(), 40000.0) and is_equal_approx(gen.get_planet_radius(), 40000.0), "planet radius from V4")

	# Agreement with the generator's own sampler, and a land/ocean mix
	var land := 0
	var sea := 0
	var biomes := {}
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for i in 200:
		var d := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized()
		var sd = wdm.get_surface_data_at(d * 40100.0)
		var s: Dictionary = gen.sample_surface(d)
		if absf(sd.height - float(s.height)) > 0.01 or absf(sd.temperature - clampf(s.temperature, 0, 1)) > 1e-4:
			_check(false, "sample %d disagrees with V4.sample_surface" % i)
			break
		if sd.is_ocean:
			sea += 1
			if sd.biome_type != 9 or sd.water_depth <= 0.0:
				_check(false, "ocean sample %d: biome %d depth %.1f" % [i, sd.biome_type, sd.water_depth])
				break
		else:
			land += 1
			biomes[sd.biome_type] = true
	_check(land > 20 and sea > 20, "land and sea both found (%d land, %d sea of 200)" % [land, sea])
	_check(biomes.size() >= 4 and not biomes.has(10), "several ADR biome types on land, none UNKNOWN: %s" % str(biomes.keys()))

	var pts: Array = wdm.scatter_surface_points(3, 12, {"land_only": true, "min_spacing_m": 2000.0})
	var all_land := pts.all(func(p): return not p.is_ocean)
	_check(pts.size() == 12 and all_land, "scatter_surface_points: %d land points" % pts.size())
	if pts.size() > 0:
		var p: Dictionary = pts[0]
		_check(absf(p.position.length() - (40000.0 + p.height)) < 0.5, "scatter point sits on the surface")

	print("WDM_V4_TEST %s" % ("PASS" if ok else "FAIL"))
	quit(0 if ok else 1)
