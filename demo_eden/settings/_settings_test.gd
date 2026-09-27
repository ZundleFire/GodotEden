extends SceneTree
## Graphics settings test on eden_play.tscn: opens the settings menu (screenshot), steps through Low / Medium /
## High / Ultra measuring frame rate and GPU time at each, checks each level reached the foliage and the viewport,
## makes a Custom change through the menu, and checks the choice is saved. Removes the saved file afterwards.
##   godot --path demo_eden --resolution 1920x1080 -s res://settings/_settings_test.gd -- --out=<dir>

const SETTLE_S := 7.0
const SAMPLE_S := 5.0

var play: Node
var player: EdenPlayer
var out := "user://"
var ok := true
var t0 := 0
var step := -1
var level := 0
var frames := 0
var gpu_sum := 0.0
var results := []


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(EdenGraphics.SAVE_PATH))
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play
	t0 = Time.get_ticks_msec()


func _check(cond: bool, msg: String) -> void:
	print("SETTINGS_TEST %s %s" % ["ok  " if cond else "FAIL", msg])
	ok = ok and cond


func _el() -> float:
	return (Time.get_ticks_msec() - t0) / 1000.0


func _foliage() -> EdenFoliage:
	for n in play.find_children("*", "VoxelInstancer", true, false):
		if n is EdenFoliage:
			return n
	return null


func _process(_d: float) -> bool:
	if player == null:
		player = play.get("player")
		return false
	var g := player.graphics
	match step:
		-1: # on the ground; the menu
			if player.ready_to_move and _el() > 3.0:
				_check(g != null and player.settings_menu != null, "EdenGraphics and the settings menu exist")
				_check(g.quality == EdenGraphics.Quality.MEDIUM, "starts at the scene's quality (%s)" % EdenGraphics.QUALITY_NAMES[g.quality])
				player.settings_menu.open()
				step = 0
				t0 = Time.get_ticks_msec()
		0:
			if _el() > 1.0:
				root.get_texture().get_image().save_png(out.path_join("settings_menu.png"))
				player.settings_menu.close()
				DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
				g.vsync = false
				RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
				_set_level(0)
		1: # settle, then measure this level
			if _el() > SETTLE_S:
				frames += 1
				gpu_sum += RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid())
			if _el() > SETTLE_S + SAMPLE_S:
				var fps := frames / SAMPLE_S
				var p := g.current()
				var f := _foliage()
				results.append([p.name, fps, gpu_sum / maxi(frames, 1)])
				root.get_texture().get_image().save_png(out.path_join("quality_%s.png" % p.name.to_lower()))
				# (approx: EdenFoliage stores 32-bit floats)
				_check(is_equal_approx(f.grass_density_scale, p.grass_density) and f.blades_per_tuft == p.grass_blades and is_equal_approx(f.grass_fade_end, p.grass_distance),
						"%s reached the foliage (grass x%.2f, %d blades, %.0f m)" % [p.name, f.grass_density_scale, f.blades_per_tuft, f.grass_fade_end])
				_check(is_equal_approx(root.scaling_3d_scale, p.render_scale), "%s render scale %.2f" % [p.name, root.scaling_3d_scale])
				if level < 3:
					_set_level(level + 1)
				else:
					step = 2
		2: # a single setting through the menu: switches to Custom from Ultra, and is saved
			player.settings_menu._set_value("grass_density", 0.5)
			_check(g.quality == EdenGraphics.Quality.CUSTOM and g.current().grass_density == 0.5 and g.current().grass_blades == 7,
					"a menu change makes a Custom level from Ultra (grass x%.2f)" % g.current().grass_density)
			var cfg := ConfigFile.new()
			_check(cfg.load(EdenGraphics.SAVE_PATH) == OK and cfg.get_value("graphics", "quality") == EdenGraphics.Quality.CUSTOM
					and cfg.get_value("custom", "grass_density") == 0.5, "the choice is saved in %s" % EdenGraphics.SAVE_PATH)
			for r in results:
				print("SETTINGS_TEST level %-6s %5.1f fps  GPU %5.1f ms" % r)
			_check(results[0][1] > results[2][1], "Low is faster than High")
			DirAccess.remove_absolute(ProjectSettings.globalize_path(EdenGraphics.SAVE_PATH))
			print("SETTINGS_TEST ", "PASS" if ok else "FAIL")
			quit(0 if ok else 1)
			return true
	return false


func _set_level(i: int) -> void:
	level = i
	player.graphics.quality = i
	player.graphics.apply()
	step = 1
	frames = 0
	gpu_sum = 0.0
	t0 = Time.get_ticks_msec()
