extends SceneTree
## Landform survey for EdenPlanetGeneratorV4 with the probe scene's settings. Headless:
##   godot --headless --path demo_eden -s res://_terrain_survey.gd -- [--prop=value ...]
## Samples land points evenly over the sphere; slope from height differences over SLOPE_STEP metres.
## Classes: flat < 8 deg (fields), hills 8-20, steep 20-35, mountain/cliff > 35.

const N := 6000
const SLOPE_STEP := 30.0


func _initialize() -> void:
	var probe: Node = load("res://_ocean_editor_probe.tscn").instantiate()
	var gen: EdenPlanetGeneratorV4 = probe.get_node("VoxelLodTerrain").generator.duplicate()
	probe.free()
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=")
		gen.set(kv[0], float(kv[1]))
	var R: float = gen.planet_radius
	var golden := PI * (3.0 - sqrt(5.0))
	var land := 0
	var mountain_regions := 0
	var classes := [0, 0, 0, 0]
	var heights: Array[float] = []
	var sea: float = gen.sea_level
	for i in N:
		var y := 1.0 - float(i) / float(N - 1) * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var d := Vector3(cos(golden * i) * r, y, sin(golden * i) * r)
		var s0: Dictionary = gen.sample_surface(d)
		var h := float(s0.height)
		if h <= sea:
			continue
		land += 1
		if float(s0.get("landform", 1.0)) > 0.5:
			mountain_regions += 1
		heights.append(h)
		var t := d.cross(Vector3.UP if absf(d.y) < 0.99 else Vector3.RIGHT).normalized()
		var b := d.cross(t)
		var a := SLOPE_STEP / R
		var hx := float(gen.sample_surface(d.rotated(b, a)).height)
		var hz := float(gen.sample_surface(d.rotated(t, a)).height)
		var slope := rad_to_deg(atan(Vector2(hx - h, hz - h).length() / SLOPE_STEP))
		classes[0 if slope < 8.0 else (1 if slope < 20.0 else (2 if slope < 35.0 else 3))] += 1
	heights.sort()
	var pct := func(c): return 100.0 * c / maxf(land, 1)
	print("SURVEY land=%.0f%%  flat<8=%.0f%%  hills8-20=%.0f%%  steep20-35=%.0f%%  mountain>35=%.0f%%" % [
		100.0 * land / N, pct.call(classes[0]), pct.call(classes[1]), pct.call(classes[2]), pct.call(classes[3])])
	print("SURVEY mountain regions=%.0f%% of land" % pct.call(mountain_regions))
	print("SURVEY land height m: p10=%.0f p50=%.0f p90=%.0f max=%.0f" % [
		heights[int(land * 0.1)], heights[int(land * 0.5)], heights[int(land * 0.9)], heights[land - 1]])
	quit()
