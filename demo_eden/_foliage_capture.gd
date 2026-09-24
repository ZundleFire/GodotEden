extends SceneTree
## Ground-level captures of EdenFoliage per biome on the probe scene, with the sun moved over each spot.
##   godot --path demo_eden --resolution 1280x720 -s res://_foliage_capture.gd -- --biome=temperate --out=<png>
## Biomes: temperate, boreal, tropical, dry, tundra, cliff.

const QUIET_S := 4.0
const CRITERIA := {
	# temperature range, moisture range
	"temperate": [Vector2(0.45, 0.7), Vector2(0.5, 1.0)],
	"boreal": [Vector2(0.12, 0.35), Vector2(0.4, 1.0)],
	"tropical": [Vector2(0.78, 1.0), Vector2(0.55, 1.0)],
	"dry": [Vector2(0.6, 1.0), Vector2(0.0, 0.3)],
	"tundra": [Vector2(0.0, 0.1), Vector2(0.0, 1.0)],
}

var args := {}
var terrain: VoxelLodTerrain
var foliage: VoxelInstancer
var last_sig := ""
var stable_since := 0
var start := 0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=")
		args[kv[0]] = kv[1] if kv.size() > 1 else "1"
	var scene: Node = load("res://_ocean_editor_probe.tscn").instantiate()
	root.add_child(scene)
	terrain = scene.get_node("VoxelLodTerrain")
	foliage = terrain.get_node("EdenFoliage")
	for k in args: # --f_<export>=value overrides EdenFoliage exports (before it builds on entering the tree)
		if k.begins_with("f_"):
			foliage.set(k.substr(2), str_to_var(args[k]))
	var gen: EdenPlanetGeneratorV4 = terrain.generator
	var R: float = gen.planet_radius
	var biome: String = args.get("biome", "temperate")

	var golden := PI * (3.0 - sqrt(5.0))
	var best := Vector3.UP
	var best_score := -INF
	for i in 6000:
		var y := 1.0 - float(i) / 5999.0 * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var d := Vector3(cos(golden * i) * r, y, sin(golden * i) * r)
		var s: Dictionary = gen.sample_surface(d)
		if float(s.height) < 20.0:
			continue
		var score := 0.0
		if biome == "cliff":
			if float(s.landform) < 0.8:
				continue
			score = _relief(gen, d, 60.0)
		else:
			var c: Array = CRITERIA[biome]
			if not _in(float(s.temperature), c[0]) or not _in(float(s.moisture), c[1]) or float(s.landform) > 0.35:
				continue
			score = -_relief(gen, d, 30.0)
		if score > best_score:
			best_score = score
			best = d
	var s0: Dictionary = gen.sample_surface(best)
	print("CAPTURE %s at %s: temp=%.2f moist=%.2f landform=%.2f material=%s score=%.1f" % [biome, best,
			s0.temperature, s0.moisture, s0.landform, s0.material, best_score])

	var up := best
	var t := up.cross(Vector3.UP if absf(up.y) < 0.99 else Vector3.RIGHT).normalized()
	var ground := R + float(s0.height)
	var cam_pos: Vector3
	var look: Vector3
	if biome == "cliff":
		# Stand off along the downhill direction, look back at the face
		var down_dir := t
		var lowest := INF
		for k in 8:
			var dir := t.rotated(up, TAU * k / 8.0)
			var h := float(gen.sample_surface(up.rotated(up.cross(dir).normalized(), 80.0 / R)).height)
			if h < lowest:
				lowest = h
				down_dir = dir
		cam_pos = up * (ground + 40.0) + down_dir * 220.0
		look = up * ground - cam_pos
	else:
		cam_pos = up * (ground + 2.5)
		look = t * 50.0 - up * 6.0
	var cam := Camera3D.new()
	cam.far = 30000.0
	cam.current = true
	root.add_child(cam)
	var viewer := VoxelViewer.new()
	viewer.view_distance = 30000
	cam.add_child(viewer)
	var cam_up := cam_pos.normalized()
	cam.transform = Transform3D(Basis.looking_at(look.normalized(), cam_up), cam_pos)

	# Sun ~55 degrees from overhead at the spot (see EdenPlanetAtmosphere::_update_sun_direction: axis +Y,
	# ea = (0,0,-1), eb = (-1,0,0))
	var atmo := terrain.get_node_or_null("EdenPlanetAtmosphere")
	if atmo:
		atmo.set("sun_declination_deg", rad_to_deg(asin(up.y)))
		atmo.set("sun_time_of_day", fposmod(atan2(-up.x, -up.z) / TAU + 0.15, 1.0))
	start = Time.get_ticks_msec()
	stable_since = start


func _in(v: float, r: Vector2) -> bool:
	return v >= r.x and v <= r.y


# Max height difference to 8 neighbours at `dist` metres
func _relief(gen: EdenPlanetGeneratorV4, d: Vector3, dist: float) -> float:
	var h0 := float(gen.sample_surface(d).height)
	var t := d.cross(Vector3.UP if absf(d.y) < 0.99 else Vector3.RIGHT).normalized()
	var m := 0.0
	for k in 8:
		var o := d.rotated(t.rotated(d, TAU * k / 8.0), dist / gen.planet_radius)
		m = maxf(m, absf(float(gen.sample_surface(o).height) - h0))
	return m


func _process(_d: float) -> bool:
	var counts: Dictionary = foliage.debug_get_instance_counts()
	var total := 0
	for k in counts:
		total += counts[k]
	var sig := str(terrain.debug_get_mesh_block_count(), "/", total)
	var now := Time.get_ticks_msec()
	if sig != last_sig:
		last_sig = sig
		stable_since = now
	if (now - stable_since > QUIET_S * 1000 and now - start > 8000) or now - start > 120000:
		root.get_viewport().get_texture().get_image().save_png(args.get("out", "res://_foliage_capture.png"))
		var nonzero := {}
		for k in counts:
			if counts[k] > 0:
				nonzero[foliage.library.get_item(k).name] = counts[k]
		print("CAPTURE saved; instances by layer: ", nonzero)
		return true
	return false
