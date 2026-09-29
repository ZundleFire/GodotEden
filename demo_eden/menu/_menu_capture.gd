extends SceneTree
## Screenshots of the main menu and its screens (the planet needs a window).
##   godot --path demo_eden --resolution 1920x1080 -s res://menu/_menu_capture.gd -- --out=<dir> [--wait=15]

var out := "user://menu_capture"
var wait := 15.0
var menu: EdenMainMenu


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
		elif a.begins_with("--wait="):
			wait = float(a.trim_prefix("--wait="))
	DirAccess.make_dir_recursive_absolute(out)
	menu = load("res://menu/main_menu.tscn").instantiate()
	root.add_child(menu)
	current_scene = menu
	_run()


func _run() -> void:
	await process_frame
	if "--no-ocean" in OS.get_cmdline_user_args():
		for n in menu.world.find_children("*", "EdenPlanetOcean", true, false):
			n.visible = false
	await _seconds(wait)
	if "--diag" in OS.get_cmdline_user_args():
		for a in menu.world.find_children("*", "EdenPlanetAtmosphere", true, false):
			for c in a.get_children(true):
				if c is MeshInstance3D:
					print("DIAG atmosphere child %s mesh %s visible %s" % [c.name, c.mesh, c.visible])
		for g in ["eden_weather_snow", "eden_weather_planet", "eden_weather_params"]:
			print("DIAG ", g, " = ", RenderingServer.global_shader_parameter_get(g))
	_shot("menu")
	if "--moon-styles" in OS.get_cmdline_user_args():
		for st in range(1, 4):
			for at in menu.world.find_children("*", "EdenPlanetAtmosphere", true, false):
				at.set("moon_style", st)
			await _seconds(0.5)
			_shot("moon_style_%d" % st)
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--rolls="):
			for r in a.trim_prefix("--rolls=").split(","):
				menu.roll = float(r)
				await _seconds(2.0)
				_shot("roll_" + r)
	for b in ["OPTIONS", "SCHEMATICA", "MODS", "PLAY"]:
		(menu._buttons.get_node(b) as Button).pressed.emit()
		await _seconds(2.5 if b == "PLAY" else 0.6)
		_shot(b.to_lower())
		if b == "OPTIONS":
			menu._options.close()
		else:
			for play in menu.find_children("*", "EdenPlayScreen", true, false):
				play._show_form(play._host_form, true)
				await _seconds(0.4)
				_shot("play_single_player")
				play._show_form(play._host_form, false)
				await _seconds(0.4)
				_shot("play_host")
			menu._set_screen(null)
		await _seconds(0.3)
	quit(0)


func _shot(name: String) -> void:
	root.get_viewport().get_texture().get_image().save_png(out.path_join(name + ".png"))
	print("CAPTURE ", name)


func _seconds(s: float) -> void:
	var end := Time.get_ticks_msec() + int(s * 1000.0)
	while Time.get_ticks_msec() < end:
		await process_frame
