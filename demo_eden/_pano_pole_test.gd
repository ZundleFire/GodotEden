extends SceneTree
## Verifies the equirect panorama's pole doesn't smear: look straight up (the texture's v=0 pole)
## and straight down (v=1) with a real HDR panorama loaded.
##   godot --path demo_eden --resolution 960x540 --script res://_pano_pole_test.gd -- <out_dir>

var _out := "user://pano_pole_test"


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		_out = args[0]
	_run.call_deferred()


func _run() -> void:
	DirAccess.make_dir_recursive_absolute(_out)

	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.background_mode = Environment.BG_SKY
	env_node.environment = env
	root.add_child(env_node)

	var sun := DirectionalLight3D.new()
	root.add_child(sun)

	var atmo := EdenPlanetAtmosphere.new()
	atmo.planet_radius = 40000.0
	atmo.day_length_seconds = 0.0
	atmo.star_intensity = 0.0
	atmo.space_daylight_fade = 0.0 # keep the panorama fully visible regardless of sun position
	root.add_child(atmo)
	atmo.sun_light_path = atmo.get_path_to(sun)
	atmo.environment_path = atmo.get_path_to(env_node)
	var night_phase: float = atmo.phase_for_sun_elevation(Vector3.UP, -30.0)
	if night_phase >= 0.0:
		atmo.sun_time_of_day = night_phase

	var tex: Texture2D = ResourceLoader.load("res://Panoramics/SkySphere_03.HDR", "Texture2D")
	atmo.space_panorama = tex

	var cam := Camera3D.new()
	cam.near = 5.0
	cam.far = 400000.0
	cam.fov = 70.0
	root.add_child(cam)
	cam.current = true

	await _frames(10)

	for pose in [["pole_up", Vector3.UP], ["pole_down", Vector3.DOWN],
			["near_pole_up", Vector3(0.15, 0.985, 0.08).normalized()],
			["sideways", Vector3.FORWARD]]:
		cam.global_transform = Transform3D(Basis.looking_at(pose[1], Vector3.RIGHT if pose[1].is_equal_approx(Vector3.UP) or pose[1].is_equal_approx(Vector3.DOWN) else Vector3.UP), Vector3.ZERO)
		await _frames(6)
		var img := root.get_texture().get_image()
		img.save_png(_out.path_join(str(pose[0]) + ".png"))
		print("PANO: shot %s" % pose[0])

	print("PANO: done")
	quit(0)


func _frames(n: int) -> void:
	for _i in n:
		await RenderingServer.frame_post_draw
