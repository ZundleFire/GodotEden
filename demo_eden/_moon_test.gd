extends SceneTree
## Moon verification harness. Renders the moon through its phases in both texture modes and logs
## the orbit numbers, so correctness is checked rather than assumed.
##   godot --path demo_eden --resolution 960x540 --script res://_moon_test.gd -- <out_dir>
##
## The camera sits OUTSIDE the atmosphere, on the moon-facing side, with a narrow FOV. That removes
## sky scattering and horizon occlusion from the picture entirely, so every phase can be judged on
## a black background regardless of where the sun happens to be.

const R := 40000.0
var _out := "user://moon_test"
var _atmo: EdenPlanetAtmosphere
var _cam: Camera3D


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		_out = args[0]
	_run.call_deferred()


func _run() -> void:
	DirAccess.make_dir_recursive_absolute(_out)

	var env_node := WorldEnvironment.new()
	env_node.name = "Env"
	var env := Environment.new()
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env_node.environment = env
	root.add_child(env_node)

	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	root.add_child(sun)
	var moon := DirectionalLight3D.new()
	moon.name = "Moon"
	moon.shadow_enabled = true
	root.add_child(moon)

	_atmo = EdenPlanetAtmosphere.new()
	_atmo.name = "Atmo"
	root.add_child(_atmo)
	_atmo.sun_light_path = _atmo.get_path_to(sun)
	_atmo.moon_light_path = _atmo.get_path_to(moon)
	_atmo.environment_path = _atmo.get_path_to(env_node)
	_atmo.planet_radius = R
	_atmo.day_length_seconds = 0.0
	_atmo.star_intensity = 0.0 # keep the frame clean around the disc
	_atmo.moon_texture = _make_disc_marker()
	_atmo.moon_disc_brightness = float(OS.get_environment("MOON_BRIGHTNESS")) if OS.has_environment("MOON_BRIGHTNESS") else 2.5

	_cam = Camera3D.new()
	# Modest near/far: a 1e7 far plane over the default near gives a ~1e8 depth ratio at this narrow
	# FOV, and RenderingLightCuller's frustum-corner plane intersections degenerate in float
	# precision ("Condition !res" every frame). The sky moon is at infinity; range is irrelevant.
	_cam.near = 50.0
	_cam.far = 400000.0
	_cam.fov = 7.0 # moon is ~2.9 degrees across, so it fills about 40% of the frame
	root.add_child(_cam)
	_cam.current = true

	await _frames(8)

	# ---- 1. Orbit maths: elongation vs phase, and illumination --------------------------------
	print("MOON: phase  elongation_deg  illumination  expected_illum")
	for phase in [0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875]:
		_atmo.moon_phase = phase
		await _frames(2)
		var s: Vector3 = _atmo.get_sun_direction()
		var m: Vector3 = _atmo.get_moon_direction()
		var elong := rad_to_deg(acos(clampf(s.dot(m), -1.0, 1.0)))
		var illum: float = _atmo.get_moon_illumination()
		var expected := 0.5 * (1.0 - cos(deg_to_rad(elong)))
		print("MOON: %.3f  %7.2f        %.3f         %.3f" % [phase, elong, illum, expected])

	# ---- 2. Rises later each day ---------------------------------------------------------------
	# Advance the real clock through several days and record where the moon is relative to the sun
	# at the same time of day. With lunar_cycle_days = 8 it should lag a further 45 degrees per day.
	_atmo.moon_phase = 0.5
	_atmo.sun_time_of_day = 0.0
	_atmo.lunar_cycle_days = 8.0
	_atmo.advance_moon_phase = true
	_atmo.day_length_seconds = 1.0
	print("MOON: day  moon_phase  elongation_from_sun")
	var last := -1
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < 3500:
		await process_frame
		var day := int((Time.get_ticks_msec() - t0) / 1000)
		if day != last:
			last = day
			var s2: Vector3 = _atmo.get_sun_direction()
			var m2: Vector3 = _atmo.get_moon_direction()
			print("MOON: %d    %.3f       %.1f" % [day, _atmo.moon_phase,
					rad_to_deg(acos(clampf(s2.dot(m2), -1.0, 1.0)))])
	_atmo.day_length_seconds = 0.0
	_atmo.advance_moon_phase = false

	# ---- 3. Phase renders, disc-image mode -----------------------------------------------------
	for phase in [0.03, 0.12, 0.25, 0.38, 0.5, 0.75]:
		await _shoot_moon(phase, "disc_phase_%03d" % int(phase * 1000))

	# ---- 4. Equirect surface-map mode ------------------------------------------------------------
	_atmo.moon_texture = _make_equirect_marker()
	_atmo.moon_texture_equirect = true
	for phase in [0.5, 0.38]:
		await _shoot_moon(phase, "equirect_phase_%03d" % int(phase * 1000))

	# ---- 5. Moon light behaviour from the ground ----------------------------------------------
	_atmo.moon_texture_equirect = false
	var site := Vector3(1, 0, 0) # equator
	print("MOON: light at equator site -- phase, moon_elev_deg, energy, visible")
	for phase in [0.0, 0.25, 0.5]:
		_atmo.moon_phase = phase
		# Put the sun well below the horizon so moonlight is the only thing to look at.
		var ph: float = _atmo.phase_for_sun_elevation(site, -40.0)
		_atmo.sun_time_of_day = maxf(ph, 0.0)
		_cam.global_position = site * (R + 50.0)
		await _frames(4)
		var md: Vector3 = _atmo.get_moon_direction()
		var elev := rad_to_deg(asin(clampf(site.dot(md), -1.0, 1.0)))
		print("MOON: %.2f  %6.1f  %.3f  %s" % [phase, elev, moon.light_energy, str(moon.visible)])

	# ---- 6. In context: from the ground at night, real sky, real panorama ------------------------
	_atmo.moon_texture = _make_disc_marker()
	_atmo.moon_texture_equirect = false
	_atmo.space_panorama = ResourceLoader.load("res://Panoramics/SkySphere_03.HDR", "Texture2D")
	_cam.near = 0.5
	_cam.far = 400000.0
	for phase in [0.5, 0.2]:
		_atmo.moon_phase = phase
		var ph2: float = _atmo.phase_for_sun_elevation(site, -25.0, false)
		_atmo.sun_time_of_day = maxf(ph2, 0.0)
		await _frames(2)
		var mdir: Vector3 = _atmo.get_moon_direction()
		_cam.fov = 40.0
		var eye := site * (R + 80.0)
		_cam.global_transform = Transform3D(Basis.looking_at(mdir, site), eye)
		await _frames(8)
		var gimg := root.get_texture().get_image()
		var gname := "ground_moon_phase_%03d" % int(phase * 1000)
		gimg.save_png(_out.path_join(gname + ".png"))
		print("MOON: shot %s  moon_elev=%.1f  illum=%.3f" % [gname, rad_to_deg(asin(clampf(site.dot(mdir), -1.0, 1.0))), _atmo.get_moon_illumination()])

	# ---- 7. Surface grading controls: isolated close-up, so contrast/saturation/opacity on the
	# disc_marker's grey body + red/green dots are unambiguous.
	_atmo.space_panorama = null
	_atmo.moon_glow_intensity = 0.0
	_atmo.moon_halo_intensity = 0.0
	_cam.near = 50.0
	_cam.far = 400000.0
	_cam.fov = 2.0
	await _shoot_moon(0.5, "grade_baseline")
	_atmo.moon_contrast = 2.5
	_atmo.moon_saturation = 0.0
	await _shoot_moon(0.5, "grade_high_contrast_bw")
	_atmo.moon_contrast = 1.0
	_atmo.moon_saturation = 3.0
	await _shoot_moon(0.5, "grade_saturated")
	_atmo.moon_saturation = 1.0
	_atmo.moon_opacity = 0.3
	await _shoot_moon(0.5, "grade_translucent")
	_atmo.moon_opacity = 1.0

	print("MOON: done")
	quit(0)


func _shoot_moon(phase: float, name: String) -> void:
	_atmo.moon_phase = phase
	_atmo.sun_time_of_day = 0.3
	await _frames(2)
	var m: Vector3 = _atmo.get_moon_direction()
	# Outside the atmosphere on the moon-facing side, so the planet is behind the camera.
	var pos := m * (R + 60000.0)
	var up := Vector3.UP if absf(m.dot(Vector3.UP)) < 0.98 else Vector3.RIGHT
	_cam.global_transform = Transform3D(Basis.looking_at(m, up), pos)
	await _frames(6)
	var img := root.get_texture().get_image()
	img.save_png(_out.path_join(name + ".png"))
	print("MOON: shot %s  illum=%.3f" % [name, _atmo.get_moon_illumination()])


func _frames(n: int) -> void:
	for _i in n:
		await RenderingServer.frame_post_draw


## Disc-image test texture: a moon photo stand-in with a RED dot at the top and a GREEN dot on the
## right. Rendered correctly, red is up and green is right; anything else means a flip or mirror.
func _make_disc_marker() -> ImageTexture:
	const N := 512
	var img := Image.create_empty(N, N, true, Image.FORMAT_RGB8)
	var c := N * 0.5
	for y in N:
		for x in N:
			var dx := (x - c) / c
			var dy := (y - c) / c
			var r := sqrt(dx * dx + dy * dy)
			var col := Color(0, 0, 0)
			if r <= 1.0:
				var g := 0.62
				# A few dark "maria" so rotation is readable.
				for mare in [Vector3(-0.3, -0.2, 0.28), Vector3(0.25, 0.15, 0.22), Vector3(-0.1, 0.45, 0.18)]:
					if Vector2(dx - mare.x, dy - mare.y).length() < mare.z:
						g = 0.38
				col = Color(g, g, g)
				if Vector2(dx, dy + 0.78).length() < 0.12:
					col = Color(1, 0.1, 0.1) # top
				if Vector2(dx - 0.78, dy).length() < 0.12:
					col = Color(0.1, 1, 0.1) # right
			img.set_pixel(x, y, col)
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


## Equirect test texture: graticule, RED dot at the map centre (the near side, should face the
## camera), GREEN dot at u=0.75 (should sit on the right limb), BLUE band at the top (north).
func _make_equirect_marker() -> ImageTexture:
	const W := 1024
	const H := 512
	var img := Image.create_empty(W, H, true, Image.FORMAT_RGB8)
	for y in H:
		for x in W:
			var u := float(x) / W
			var v := float(y) / H
			var g := 0.55
			if fposmod(u * 12.0, 1.0) < 0.03 or fposmod(v * 6.0, 1.0) < 0.06:
				g = 0.3
			var col := Color(g, g, g)
			if v < 0.08:
				col = Color(0.15, 0.25, 1.0)
			if Vector2((u - 0.5) * 2.0, v - 0.5).length() < 0.06:
				col = Color(1, 0.1, 0.1)
			if Vector2((u - 0.75) * 2.0, v - 0.5).length() < 0.06:
				col = Color(0.1, 1, 0.1)
			img.set_pixel(x, y, col)
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)
