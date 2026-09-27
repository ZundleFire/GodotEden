extends SceneTree
## Reproduces the editor-viewport failure: a bare, auto-set-up EdenPlanetAtmosphere with the
## camera near world origin -- deep inside a 40km-radius planet, the default position of the
## editor's own 3D viewport camera before the user has flown it anywhere. Confirms the sky renders
## a sane picture (or at least nothing exotic) instead of the reported striped banding.
##   godot --path demo_eden --resolution 960x540 --script res://_degenerate_camera_test.gd -- <out_dir>

var _out := "user://degenerate_camera_test"


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		_out = args[0]
	_run.call_deferred()


func _run() -> void:
	DirAccess.make_dir_recursive_absolute(_out)

	# Bare node, auto_create_* all default true -- exactly atmosphere_demo.tscn's PlanetAtmosphere
	# as it appears in the editor (no runtime script has run to wire sun_light_path etc.).
	var atmo := EdenPlanetAtmosphere.new()
	root.add_child(atmo)

	var cam := Camera3D.new()
	cam.fov = 70.0
	root.add_child(cam)
	cam.current = true

	# The Godot editor's default 3D viewport camera position -- nowhere near a 40km planet.
	for pose in [["origin_look_forward", Vector3(0, 1, 5), Vector3(0, 1, 0)],
			["origin_look_down", Vector3(0, 1, 5), Vector3(0, -1, -0.3)],
			["exact_origin", Vector3.ZERO, Vector3(0, 0, -1)]]:
		cam.global_position = pose[1]
		cam.look_at(pose[1] + pose[2], Vector3.UP)
		await _frames(10)
		var img := root.get_texture().get_image()
		img.save_png(_out.path_join(str(pose[0]) + ".png"))
		# A crude "is this exotic banding" check: sample a horizontal run of pixels down the middle
		# row and count how many times the colour reverses direction (a smooth sky gradient does
		# this 0-1 times; a striped/banded artifact does it many times).
		var reversals := 0
		var prev_delta := 0.0
		var mid_y := img.get_height() / 2
		for x in range(1, img.get_width()):
			var c0 := img.get_pixel(x - 1, mid_y)
			var c1 := img.get_pixel(x, mid_y)
			var delta: float = (c1.r + c1.g + c1.b) - (c0.r + c0.g + c0.b)
			if absf(delta) > 0.01 and prev_delta != 0.0 and sign(delta) != sign(prev_delta):
				reversals += 1
			if absf(delta) > 0.01:
				prev_delta = delta
		print("DEGEN: %s brightness reversals across middle row = %d (>20 suggests banding)" % [pose[0], reversals])

	print("DEGEN: done")
	quit(0)


func _frames(n: int) -> void:
	for _i in n:
		await RenderingServer.frame_post_draw
