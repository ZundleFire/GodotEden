extends SceneTree
## Aerial capture of the probe scene's terrain: a lowland spot looking toward a mountain region.
##   godot --path demo_eden --resolution 1280x720 -s res://_terrain_capture.gd -- --out=<png> [--alt=600] [--prop=value]
## Waits for streaming to settle, then saves one PNG.

const QUIET_S := 4.0

var args := {}
var terrain: VoxelLodTerrain
var camera: Camera3D
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
	var gen: EdenPlanetGeneratorV4 = terrain.generator

	var atmo := terrain.get_node_or_null("EdenPlanetAtmosphere")
	var sun: Vector3 = atmo.get_sun_direction().normalized() if atmo else Vector3.UP
	# Lowland land point whose neighbour ~6 km away is a mountain region
	var R: float = gen.planet_radius
	var golden := PI * (3.0 - sqrt(5.0))
	var best := Vector3.UP
	var best_look := Vector3.RIGHT
	var best_score := -INF
	for i in 3000:
		var y := 1.0 - float(i) / 2999.0 * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var d := Vector3(cos(golden * i) * r, y, sin(golden * i) * r)
		var s: Dictionary = gen.sample_surface(d)
		if float(s.height) < 40.0 or float(s.landform) > 0.1:
			continue
		var t := d.cross(Vector3.UP if absf(d.y) < 0.99 else Vector3.RIGHT).normalized()
		for k in 6:
			var dir := t.rotated(d, TAU * k / 6.0)
			var far := d.rotated(d.cross(dir).normalized(), 6000.0 / R)
			# Mountains ahead, on the sunlit side (the atmosphere drives the sun, there is no DirectionalLight)
			var score := float(gen.sample_surface(far).landform) + d.dot(sun) * 2.0
			if score > best_score:
				best_score = score
				best = d
				best_look = dir
	# Overrides after the spot search, so A/B captures share one viewpoint
	for k in args:
		if k in ["out", "alt"]:
			continue
		gen.set(k, float(args[k]))
	var ground := R + float(gen.sample_surface(best).height)
	var alt := float(args.get("alt", "600"))
	camera = Camera3D.new()
	camera.far = 60000.0
	camera.current = true
	root.add_child(camera)
	var viewer := VoxelViewer.new()
	viewer.view_distance = 60000
	camera.add_child(viewer)
	var p := best * (ground + alt)
	camera.transform = Transform3D(Basis.looking_at(best_look - best * 0.12, best), p)
	print("CAPTURE spot=", best, " ground=", ground - R, " score=", best_score)
	start = Time.get_ticks_msec()
	stable_since = start


func _process(_d: float) -> bool:
	var sig := str(terrain.debug_get_mesh_block_count())
	var now := Time.get_ticks_msec()
	if sig != last_sig:
		last_sig = sig
		stable_since = now
	if (now - stable_since > QUIET_S * 1000 and now - start > 8000) or now - start > 120000:
		root.get_viewport().get_texture().get_image().save_png(args.get("out", "res://_terrain_capture.png"))
		print("CAPTURE saved after ", (now - start) / 1000.0, " s")
		return true
	return false
