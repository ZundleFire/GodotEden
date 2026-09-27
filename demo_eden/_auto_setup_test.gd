extends SceneTree
## Verifies EdenPlanetAtmosphere works with ZERO manual wiring: no sibling lights, no
## WorldEnvironment, no planet mesh -- just the bare node, exactly as it would sit freshly dropped
## into a scene in the editor. auto_create_lights/auto_create_environment/auto_create_planet_mesh
## default true, so this should render a normal sky, sun and ground with nothing else in the tree.
##   godot --path demo_eden --resolution 960x540 --script res://_auto_setup_test.gd -- <out_dir>

var _out := "user://auto_setup_test"


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		_out = args[0]
	_run.call_deferred()


func _run() -> void:
	DirAccess.make_dir_recursive_absolute(_out)

	# Nothing but the bare node -- no Sun, no WorldEnvironment, no ground mesh, no NodePaths set.
	var atmo := EdenPlanetAtmosphere.new()
	atmo.planet_radius = 40000.0
	atmo.day_length_seconds = 0.0
	atmo.sun_time_of_day = 0.35
	root.add_child(atmo)

	await _frames(10)

	print("PANO: sun_light_path=%s moon_light_path=%s environment_path=%s children=%d" % [
		str(atmo.sun_light_path), str(atmo.moon_light_path), str(atmo.environment_path), atmo.get_child_count()])
	for c in atmo.get_children():
		print("  child: %s (%s)" % [c.name, c.get_class()])

	var cam := Camera3D.new()
	cam.fov = 70.0
	cam.near = 5.0
	cam.far = 400000.0
	root.add_child(cam)
	cam.current = true
	cam.global_transform = Transform3D(Basis.looking_at(Vector3(0.3, 0.1, 0.95), Vector3.UP), Vector3(0, 40060.0, 0))
	await _frames(10)
	var img := root.get_texture().get_image()
	img.save_png(_out.path_join("bare_node.png"))

	print("AUTO: done")
	quit(0)


func _frames(n: int) -> void:
	for _i in n:
		await RenderingServer.frame_post_draw
