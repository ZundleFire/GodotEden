extends SceneTree
## Where the frame goes at the play spawn: GPU time and primitives with everything, without grass, without any
## foliage, and without foliage or effects. Same view each time (camera held still).
##   godot --path demo_eden --resolution 1920x1080 -s res://perf/_cost_split.gd -- [--quality=medium]

const SETTLE_S := 8.0
const SAMPLE_S := 4.0

var play: Node
var player: EdenPlayer
var foliage: EdenFoliage
var _original: EdenFoliageConfig
var _base := {}
var t0 := 0
var step := -1
var frames := 0
var gpu := 0.0
var prims := 0.0
var cases := [
	["everything", {}],
	["no grass", {"grass_density_scale": 0.0}],
	["no far tiers", {"far_foliage_enabled": false}],
	["far visibility 150", {"far_visibility": 150.0}],
	["no bushes/ferns", {"kinds": ["SHRUB", "FERN", "BERRY_BUSH", "DEAD_BUSH"]}],
	["no trees", {"kinds": ["OAK", "BIRCH", "WILLOW", "FRUIT_TREE", "PINE", "PALM", "DEAD_TREE"]}],
	["no rocks/wood", {"kinds": ["BOULDER", "PEBBLES", "ROCK_SLAB", "ROCK_SPIRE", "BRANCH", "LOG"]}],
	["no foliage", {"grass_density_scale": 0.0, "density_scale": 0.0}],
]


func _initialize() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play
	t0 = Time.get_ticks_msec()


func _el() -> float:
	return (Time.get_ticks_msec() - t0) / 1000.0


func _process(_d: float) -> bool:
	if player == null:
		player = play.get("player")
		return false
	if step == -1:
		if player.ready_to_move and _el() > 4.0:
			for n in play.find_children("*", "VoxelInstancer", true, false):
				if n is EdenFoliage:
					foliage = n
			_original = foliage.config
			_base = {"grass_density_scale": foliage.grass_density_scale, "far_visibility": foliage.far_visibility}
			player.graphics.vsync = false
			_next_case()
		return false
	if _el() > SETTLE_S:
		RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
		frames += 1
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid())
		prims += Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)
	if _el() > SETTLE_S + SAMPLE_S:
		print("SPLIT %-12s GPU %5.1f ms  %4.1f M primitives  %.0f fps" % [cases[step][0], gpu / frames, prims / frames / 1e6, frames / SAMPLE_S])
		if step + 1 >= cases.size():
			quit()
			return true
		_next_case()
	return false


func _next_case() -> void:
	step += 1
	frames = 0
	gpu = 0.0
	prims = 0.0
	var values: Dictionary = cases[step][1]
	if values.has("kinds"):
		# Only these kinds off (from the full config)
		var cfg: EdenFoliageConfig = _original.duplicate(true)
		for b in cfg.biomes:
			for l in b.layers:
				l.enabled = not (EdenFoliageLayer.Kind.keys()[l.kind] in values.kinds)
		foliage.config = cfg
	elif not values.is_empty():
		foliage.config = _original
		foliage.apply_quality({"grass_density_scale": _base.grass_density_scale, "far_foliage_enabled": true, "far_visibility": _base.far_visibility})
		foliage.apply_quality(values)
	t0 = Time.get_ticks_msec()
