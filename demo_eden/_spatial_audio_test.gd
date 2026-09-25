extends SceneTree
## Headless check that EdenAmbience's spatial sounds sit where their sources are, on the probe scene:
## surf on shorelines near a beach, rustling in forest around a camera inside one, neither far inland.
##   godot --headless --path demo_eden -s res://_spatial_audio_test.gd

var amb: EdenAmbience
var foliage: EdenFoliage
var gen: VoxelGenerator
var cam: Camera3D
var R := 40000.0
var spots := {}
var order := ["beach", "forest", "inland"]
var current := -1
var t0 := 0
var ok := true


func _initialize() -> void:
	var scene: Node = load("res://_ocean_editor_probe.tscn").instantiate()
	root.add_child(scene)
	var terrain: VoxelLodTerrain = scene.get_node("VoxelLodTerrain")
	terrain.view_distance = 256 # only the generator is needed, not meshes
	gen = terrain.generator
	R = gen.planet_radius
	foliage = terrain.get_node("EdenFoliage")
	amb = terrain.get_node("EdenAmbience")
	amb.surf_volume = 1.0
	amb.leaves_volume = 1.0
	cam = Camera3D.new()
	cam.current = true
	root.add_child(cam)
	_find_spots()


func _find_spots() -> void:
	var golden := PI * (3.0 - sqrt(5.0))
	for i in 20000:
		if spots.size() == 3:
			return
		var y := 1.0 - float(i) / 19999.0 * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var d := Vector3(cos(golden * i) * r, y, sin(golden * i) * r)
		var s: Dictionary = gen.sample_surface(d)
		var h := float(s.height)
		if h < 1.0:
			continue
		var p := d * (R + h)
		if not spots.has("beach") and h < 8.0 and _water_within(d, 80.0):
			spots.beach = p
		elif not spots.has("forest") and foliage.get_forest_density(p) > 0.95 and not _water_within(d, 600.0):
			spots.forest = p
		elif not spots.has("inland") and h > 50.0 and not _water_within(d, 900.0) and _forest_within(p, d, 150.0) == 0.0:
			spots.inland = p


func _ring(d: Vector3, dist: float) -> Array:
	var t := d.cross(Vector3.UP if absf(d.y) < 0.99 else Vector3.RIGHT).normalized()
	var out := []
	for k in 12:
		out.append(d.rotated(t.rotated(d, TAU * k / 12.0), dist / R))
	return out


func _water_within(d: Vector3, dist: float) -> bool:
	for rd in [dist * 0.25, dist * 0.5, dist]:
		for o in _ring(d, rd):
			if float(gen.sample_surface(o).height) < 0.0:
				return true
	return false


func _forest_within(p: Vector3, d: Vector3, dist: float) -> float:
	var m := foliage.get_forest_density(p)
	for o in _ring(d, dist):
		m = maxf(m, foliage.get_forest_density(o * (R + float(gen.sample_surface(o).height))))
	return m


func _check(cond: bool, msg: String) -> void:
	print("SPATIAL_TEST %s %s" % ["ok  " if cond else "FAIL", msg])
	ok = ok and cond


func _process(_d: float) -> bool:
	if current < 0 or Time.get_ticks_msec() - t0 > 4000:
		if current >= 0:
			_evaluate(order[current])
		current += 1
		while current < order.size() and not spots.has(order[current]):
			print("SPATIAL_TEST skip %s: no such spot found" % order[current])
			current += 1
		if current >= order.size():
			print("SPATIAL_TEST ", "PASS" if ok else "FAIL")
			quit(0 if ok else 1)
			return true
		var p: Vector3 = spots[order[current]]
		cam.position = p + p.normalized() * 2.0
		t0 = Time.get_ticks_msec()
	return false


func _evaluate(spot: String) -> void:
	var st: Dictionary = amb.get_debug_state()
	var surf := []
	var leaves := []
	for s in st.sound_sources:
		if s.target_level > 0.0:
			(surf if s.kind == "surf" else leaves).append(s)
	print("SPATIAL_TEST %s: %d surf, %d leaves sources, forest_here=%.2f" % [spot, surf.size(), leaves.size(), st.forest_here])
	match spot:
		"beach":
			_check(surf.size() >= 1, "beach hears surf")
			for s in surf:
				var pos: Vector3 = s.position
				var dist := pos.distance_to(cam.position)
				var h := float(gen.sample_surface(pos.normalized()).height)
				_check(dist <= amb.surf_range + 60.0 and absf(h) < 40.0,
						"surf source %.0f m away on the shoreline (ground %.1f m)" % [dist, h])
		"forest":
			_check(leaves.size() >= 3, "forest rustles all around")
			_check(surf.is_empty(), "no surf inland")
			for s in leaves:
				var pos: Vector3 = s.position
				_check(foliage.get_forest_density(pos.normalized() * (R + float(gen.sample_surface(pos.normalized()).height))) > 0.15,
						"leaves source %.0f m away is in forest" % pos.distance_to(cam.position))
		"inland":
			_check(surf.is_empty() and leaves.is_empty(), "open country far from sea and forest is quiet")
