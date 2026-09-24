extends SceneTree
## Climate by latitude band for EdenPlanetGeneratorV4 with the probe scene's settings. Headless:
##   godot --headless --path demo_eden -s res://_climate_survey.gd -- [--prop=value ...]

const N := 8000
const BIOMES := ["deep", "shallow", "coast", "river", "tropical", "forest", "plains", "desert", "tundra",
		"mountain", "snow"]
const MATS := ["grass", "rock", "snow", "sand", "dirt", "moss", "ocean_floor"]
const BANDS := [0.0, 15.0, 35.0, 55.0, 75.0, 90.1]


func _initialize() -> void:
	var probe: Node = load("res://_ocean_editor_probe.tscn").instantiate()
	var gen: EdenPlanetGeneratorV4 = probe.get_node("VoxelLodTerrain").generator.duplicate()
	probe.free()
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=")
		gen.set(kv[0], float(kv[1]))
	var golden := PI * (3.0 - sqrt(5.0))
	var stats := []
	for b in BANDS.size() - 1:
		stats.append({"n": 0, "t": 0.0, "m": 0.0, "biomes": {}, "mats": {}})
	for i in N:
		var y := 1.0 - float(i) / float(N - 1) * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var d := Vector3(cos(golden * i) * r, y, sin(golden * i) * r)
		var s: Dictionary = gen.sample_surface(d)
		if float(s.height) <= gen.sea_level:
			continue
		var lat := rad_to_deg(asin(absf(d.y)))
		var band := 0
		while lat >= BANDS[band + 1]:
			band += 1
		var st: Dictionary = stats[band]
		st.n += 1
		st.t += float(s.temperature)
		st.m += float(s.moisture)
		var name: String = BIOMES[clampi(int(s.biome_id), 0, BIOMES.size() - 1)]
		st.biomes[name] = st.biomes.get(name, 0) + 1
		var mat: String = MATS[int(s.material)]
		st.mats[mat] = st.mats.get(mat, 0) + 1
	for b in stats.size():
		var st: Dictionary = stats[b]
		var parts := []
		for k in st.biomes:
			parts.append([st.biomes[k], k])
		parts.sort()
		parts.reverse()
		var top := []
		for p in parts.slice(0, 4):
			top.append("%s %d%%" % [p[1], 100 * p[0] / maxi(st.n, 1)])
		var mparts := []
		for k in st.mats:
			mparts.append("%s %d%%" % [k, 100 * st.mats[k] / maxi(st.n, 1)])
		print("CLIMATE   materials: ", ", ".join(mparts))
		print("CLIMATE lat %2d-%2d: land=%4d temp=%.2f moist=%.2f  %s" % [BANDS[b], BANDS[b + 1], st.n,
				st.t / maxf(st.n, 1), st.m / maxf(st.n, 1), ", ".join(top)])
	quit()
