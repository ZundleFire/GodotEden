extends SceneTree
## Surface material mix by climate band on the V4 planet (land only): shows which ground each biome's foliage
## filters actually see, e.g. whether deserts are sand or dirt.
##   godot --headless --path demo_eden -s res://foliage/_climate_materials.gd

const MATS := ["grass", "rock", "snow", "sand", "dirt", "moss", "seafloor"]
const BANDS := [
	["desert (T>0.45, M<0.18)", Vector2(0.45, 1.0), Vector2(0.0, 0.18)],
	["steppe (T>0.45, M 0.18-0.38)", Vector2(0.45, 1.0), Vector2(0.18, 0.38)],
	["temperate", Vector2(0.35, 0.8), Vector2(0.35, 1.0)],
	["tropical", Vector2(0.72, 1.0), Vector2(0.45, 1.0)],
	["boreal", Vector2(0.06, 0.4), Vector2(0.25, 1.0)],
]


func _initialize() -> void:
	var world: Node = load("res://_ocean_editor_probe.tscn").instantiate()
	var gen: Object = world.get_node("VoxelLodTerrain").generator
	var counts := []
	for b in BANDS:
		counts.append({})
	var golden := PI * (3.0 - sqrt(5.0))
	var n := 20000
	for i in n:
		var y := 1.0 - float(i) / (n - 1) * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var s: Dictionary = gen.sample_surface(Vector3(cos(golden * i) * r, y, sin(golden * i) * r))
		if float(s.height) <= float(gen.sea_level):
			continue
		for k in BANDS.size():
			var t: float = s.temperature
			var m: float = s.moisture
			if t >= BANDS[k][1].x and t <= BANDS[k][1].y and m >= BANDS[k][2].x and m <= BANDS[k][2].y:
				var mat: String = MATS[int(s.material)]
				counts[k][mat] = counts[k][mat] + 1 if counts[k].has(mat) else 1
	for k in BANDS.size():
		var total := 0
		for v in counts[k].values():
			total += v
		var parts := []
		for mat in counts[k]:
			parts.append("%s %d%%" % [mat, roundi(100.0 * counts[k][mat] / maxf(total, 1))])
		print("CLIMATE %-30s %5d samples: %s" % [BANDS[k][0], total, ", ".join(parts)])
	world.free()
	quit()
