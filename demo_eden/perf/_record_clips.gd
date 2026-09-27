extends SceneTree
## Records short clips of the playable planet as numbered JPEG frames, one folder per clip, for encoding to video
## (perf/make_clips.py runs this and ffmpeg). Run with --fixed-fps 30 so every rendered frame is exactly 1/30 s of
## game time: the clips play back smoothly however slow the recording is.
##   godot --path demo_eden --resolution 1280x720 --fixed-fps 30 -s res://perf/_record_clips.gd -- --out=<dir> [--only=walk,storm]
## Clips: walk, wind, storm, daynight, seasons, dig, build, swim (the year from orbit: settings/_season_orbit.gd --clip).

const FPS := 30

var play: Node
var player: EdenPlayer
var amb: Node
var cal: EdenCalendar
var gen: Object
var out := "user://"
var only := []
var clips := []
var clip := -1
var frame := 0
var state := "boot"
var wait := 0
var caption: Label
var home := Vector3.ZERO
var home_yaw := 0.0
var ctx := {}


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
		elif a.begins_with("--only="):
			only = Array(a.trim_prefix("--only=").split(","))
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play
	clips = [
		["walk", 240, _walk_setup, _walk_frame],
		["wind", 240, _wind_setup, _wind_frame],
		["storm", 270, _storm_setup, _storm_frame],
		["daynight", 300, _daynight_setup, _daynight_frame],
		["seasons", 336, _seasons_setup, _seasons_frame],
		["dig", 210, _dig_setup, _dig_frame],
		["build", 300, _build_setup, _build_frame],
		["swim", 240, _swim_setup, _swim_frame],
	]
	if not only.is_empty():
		clips = clips.filter(func(c): return c[0] in only)


var _last_us := 0


func _process(_d: float) -> bool:
	# Diagnostics: long frames (what makes the window "not responding") and the camera's roll against planet up
	var now := Time.get_ticks_usec()
	if _last_us > 0 and now - _last_us > 1000000:
		print("CLIPS stall %.1f s in state %s (clip %d frame %d)" % [(now - _last_us) / 1e6, state, clip, frame])
	_last_us = now
	if player and Engine.get_process_frames() % 60 == 0:
		var cam := root.get_camera_3d()
		var up := (player.global_position - player._center()).normalized()
		print("CLIPS %s frame %d ready %s roll %.2f deg cam %s" % [state, Engine.get_process_frames(), player.ready_to_move,
				rad_to_deg(asin(clampf(cam.global_basis.x.dot(up), -1.0, 1.0))) if cam else 0.0, cam.get_path() if cam else "none"])
	match state:
		"boot":
			if player == null:
				for n in play.find_children("*", "CharacterBody3D", true, false):
					if n is EdenPlayer:
						player = n
				return false
			if not player.ready_to_move:
				return false
			wait += 1
			if wait < 90:
				return false
			amb = player._planet.get_node_or_null("EdenAmbience")
			cal = player.calendar
			gen = player._planet.generator
			cal.running = false
			var t := Time.get_ticks_msec()
			player.graphics.quality = EdenGraphics.Quality.HIGH
			player.graphics.vsync = false
			player.graphics.apply()
			print("CLIPS graphics.apply %d ms" % (Time.get_ticks_msec() - t))
			t = Time.get_ticks_msec()
			_hide_ui()
			caption = _make_caption()
			home = player.global_position
			_face_clear_way()
			print("CLIPS face_clear_way %d ms" % (Time.get_ticks_msec() - t))
			home_yaw = player._yaw
			t = Time.get_ticks_msec()
			_next_clip()
			print("CLIPS first setup %d ms" % (Time.get_ticks_msec() - t))
		"settle":
			wait -= 1
			if clips[clip].size() > 4:
				pass
			if wait <= 0 and player.ready_to_move:
				state = "record"
				frame = 0
		"record":
			clips[clip][3].call(frame)
			if frame >= 2: # (the first frames can still hold the previous clip's image)
				var img := root.get_texture().get_image()
				img.save_jpg(out.path_join("%s/%04d.jpg" % [clips[clip][0], frame - 2]), 0.9)
			frame += 1
			if frame >= clips[clip][1] + 2:
				_release()
				_next_clip()
		"done":
			quit()
			return true
	return false


func _next_clip() -> void:
	clip += 1
	if clip >= clips.size():
		print("CLIPS done")
		state = "done"
		return
	DirAccess.make_dir_recursive_absolute(out.path_join(clips[clip][0]))
	print("CLIPS recording ", clips[clip][0])
	caption.text = ""
	wait = 45
	clips[clip][2].call()
	state = "settle"


func _release() -> void:
	for a in ["move_forward", "sprint", "crouch", "jump"]:
		Input.action_release(a)


# ------------------------------------------------------------------------------------------------------------
# Helpers

func _hide_ui() -> void:
	for n in root.find_children("*", "CanvasLayer", true, false):
		n.visible = false


func _make_caption() -> Label:
	var layer := CanvasLayer.new()
	layer.layer = 100
	root.add_child(layer)
	var l := Label.new()
	l.position = Vector2(28, 22)
	l.add_theme_font_size_override("font_size", 30)
	l.add_theme_color_override("font_color", Color(1, 1, 1))
	l.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.8))
	l.add_theme_constant_override("outline_size", 8)
	layer.add_child(l)
	return l


func _noon_at(dir: Vector3, day_offset := 0.0) -> void:
	cal.days = floor(cal.days) + day_offset
	cal.days -= (cal.local_hour(dir) - 12.0) / 24.0
	cal._to_fields()
	cal._apply()


func _set_date(month: int, hour_local: float) -> void:
	cal.set_date(1, month, 2, 12.0)
	cal.days -= (cal.local_hour(player.up_direction) - hour_local) / 24.0
	cal._to_fields()
	cal._apply()


func _go_home() -> void:
	player.global_position = home
	player.velocity = Vector3.ZERO
	player._yaw = home_yaw
	player._pitch = -0.2
	player.camera_distance = 4.0
	player.camera_shoulder = 0.55


func _face_clear_way() -> void:
	var up := player.up_direction
	var space := player.get_world_3d().direct_space_state
	var best := -1.0
	for k in 12:
		var yaw := TAU * k / 12.0
		var dir := EdenPlayer._north(up).rotated(up, yaw)
		var free := 40.0
		for h in [0.5, 1.2]:
			var from: Vector3 = player.global_position + up * h
			var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(from, from + dir * 40.0, player.collision_mask, [player.get_rid()]))
			if not hit.is_empty():
				free = minf(free, from.distance_to(hit.position))
		if free > best:
			best = free
			player._yaw = yaw


func _ground(p: Vector3) -> float:
	var up := (p - player._center()).normalized()
	var hit := player.get_world_3d().direct_space_state.intersect_ray(
			PhysicsRayQueryParameters3D.create(p + up * 6.0, p - up * 8.0, 1, [player.get_rid()]))
	return (hit.position - p).dot(up) if not hit.is_empty() else 0.0


# ------------------------------------------------------------------------------------------------------------
# Clips

func _walk_setup() -> void:
	_go_home()
	_set_date(5, 11.0)
	amb.set("weather_override", 1)


func _walk_frame(f: int) -> void:
	caption.text = "Walk, run, jump" if f < 60 else caption.text
	if f == 20:
		Input.action_press("move_forward")
	if f == 90:
		Input.action_press("sprint")
	if f == 160:
		Input.action_press("jump")
	if f == 164:
		Input.action_release("jump")
	if f == 200:
		Input.action_release("sprint")
	player._yaw += 0.004


func _wind_setup() -> void:
	_go_home()
	player._pitch = -0.08
	player.camera_distance = 3.0
	_set_date(5, 12.0)
	amb.set("weather_override", 1)
	amb.set("gustiness", 0.9)
	ctx.wind0 = amb.get("wind_speed")


func _wind_frame(f: int) -> void:
	var w := lerpf(1.0, 18.0, smoothstep(0.0, 1.0, f / 240.0))
	amb.set("wind_speed", w)
	caption.text = "Wind %d km/h" % roundi(w * 3.6)
	player._yaw += 0.002
	if f == 239:
		amb.set("wind_speed", ctx.wind0)


func _storm_setup() -> void:
	_go_home()
	player._pitch = 0.05
	_set_date(5, 16.0)
	amb.set("weather_override", 3)
	wait = 150 # let the storm build


func _storm_frame(f: int) -> void:
	caption.text = "Thunderstorm"
	if f in [40, 125, 200]:
		# In view: ahead of the camera, a few hundred metres out
		var up := player.up_direction
		var fwd := EdenPlayer._north(up).rotated(up, player._yaw)
		var p := player.global_position + fwd * (250.0 + f) + EdenPlayer._north(up).cross(up) * (f - 120.0)
		var d := (p - player._center()).normalized()
		amb.call("strike_lightning", player._center() + d * (float(gen.planet_radius) + float(gen.sample_surface(d).height)))
	player._yaw += 0.0015
	if f == 269:
		amb.set("weather_override", 0)


func _daynight_setup() -> void:
	_go_home()
	player._pitch = -0.12
	player.camera_distance = 5.0
	amb.set("weather_override", 1)
	_set_date(5, 14.0)


func _daynight_frame(f: int) -> void:
	# A whole day in 10 s, from mid-afternoon through the night to the next morning
	cal.days += 1.0 / 300.0
	cal._to_fields()
	cal._apply()
	caption.text = "%s local time" % EdenCalendar.clock(cal.local_hour(player.up_direction))
	player._yaw += 0.003


func _seasons_setup() -> void:
	_go_home()
	player._pitch = -0.05
	player.camera_distance = 6.0
	amb.set("weather_override", 1)
	cal.set_date(1, 1, 2, 12.0)
	_noon_at(player.up_direction)
	ctx.day0 = cal.days


func _seasons_frame(f: int) -> void:
	# A year in 11 s: a day every 7 frames, always at local noon
	var d := int(f / 7)
	cal.days = ctx.day0 + d
	cal._to_fields()
	cal._apply()
	var t: float = amb.call("get_debug_state").temperature
	caption.text = "%s  -  %s  -  %d °C" % [cal.date_string(), cal.season_at(player.up_direction), roundi(EdenCalendar.celsius(t))]
	player._yaw += 0.0012


func _dig_setup() -> void:
	_go_home()
	_set_date(5, 11.0)
	amb.set("weather_override", 1)
	player._pitch = -0.45 # (the crosshair lands about 3 m ahead)
	player.camera_distance = 3.0
	player.miner.counts[EdenMiner.STONE] = 10


func _dig_frame(f: int) -> void:
	caption.text = "Digging" if f < 120 else "Placing stone"
	if f > 20 and f < 120 and f % 12 == 0:
		player.miner.dig()
	if f >= 130 and f % 20 == 0:
		player.miner.select(EdenMiner.STONE)
		player.miner.place()


func _build_setup() -> void:
	_go_home()
	_set_date(5, 10.0)
	amb.set("weather_override", 1)
	var up := player.up_direction
	var fwd := EdenPlayer._north(up).rotated(up, player._yaw)
	var basis := Basis.looking_at(fwd, up)
	var spot := player.global_position + fwd * 5.0
	player.miner._foliage.remove_instances_in_sphere(player.miner._foliage.to_local(spot - basis.z * 5.0), 18.0)
	player.miner.counts[EdenMiner.WOOD] = 60
	player.miner.counts[EdenMiner.STONE] = 40
	player.builder.set_active(true)
	ctx.basis = basis
	ctx.spot = spot + up * _ground(spot)
	ctx.steps = []
	# Stand off to the side, looking across the plot
	# (the character hidden: the camera looks over where they stand)
	# A camera of its own off to the side of the span (foundation to the last hanging floor, ~9 m), looking at its
	# middle; the character steps back out of the shot
	var middle := spot - basis.z * 4.0 + up * 1.8
	var cam := Camera3D.new()
	cam.far = 400000.0
	root.add_child(cam)
	cam.global_position = middle + basis.x * 7.0 + basis.z * 3.0 + up * 5.5 # (above: the floors show their support colours)
	cam.look_at(middle, up)
	cam.current = true
	ctx.cam = cam
	player._model.visible = false


func _build_frame(f: int) -> void:
	var b: EdenBuilder = player.builder
	var basis: Basis = ctx.basis
	var up := player.up_direction
	var g: Vector3 = ctx.spot
	if f == 10:
		caption.text = "Building: foundation"
		b.place_at("stone_floor", Transform3D(basis, g + up * 0.2))
	if f == 25:
		var f1 := Transform3D(basis, g + up * 0.2)
		b.place_at("stone_floor", b.snapped("stone_floor", Transform3D(basis, f1.origin + basis.x * 2.3 + up * 0.15), f1 * Vector3(1.0, 0.2, 0.0)))
		ctx.f1 = f1
	if f == 45:
		caption.text = "Walls snap to the floor's edge"
		var f1: Transform3D = ctx.f1
		var edge := f1 * Vector3(0.0, 0.2, -1.0)
		var w := b.snapped("stone_wall", Transform3D(basis, edge + up * 1.3), edge)
		b.place_at("stone_wall", w)
		ctx.wall = b.pieces[-1]
		ctx.aim = w * Vector3(0.0, 1.0, 0.0)
	if f >= 70 and f <= 190 and (f - 70) % 20 == 0:
		caption.text = "Support: blue grounded, fading to red"
		var raw := Transform3D(basis, ctx.aim - basis.z * 1.0 + up * 0.1)
		var fx := b.snapped("wood_floor", raw, ctx.aim)
		if b.place_at("wood_floor", fx) == "":
			ctx.aim = fx * Vector3(0.0, 0.1, -1.0)
		else:
			caption.text = "Not enough support: refused"
	if f == 230:
		caption.text = "Remove the wall: what it held collapses"
		b.remove(ctx.wall)
	if f == 299:
		b.set_active(false)
		player._model.visible = true
		ctx.cam.queue_free()
		player.get_camera().current = true


func _swim_setup() -> void:
	var R: float = gen.planet_radius
	var sea := Vector3.ZERO
	# Open sea near the equator: 10-40 m deep here and 40 m out every way (no cliffs beside the camera)
	for i in 1440:
		var d := Vector3(cos(deg_to_rad(i * 0.25)) * cos(deg_to_rad(2.0)), sin(deg_to_rad(2.0)), sin(deg_to_rad(i * 0.25)) * cos(deg_to_rad(2.0)))
		var open := true
		for k in 9:
			var dk := d if k == 0 else d.rotated(d.cross(Vector3.UP).normalized().rotated(d, k * TAU / 8.0), 40.0 / R)
			var h: float = gen.sample_surface(dk).height
			open = open and h < float(gen.sea_level) - 10.0 and h > float(gen.sea_level) - 40.0
		if open:
			sea = d * (R + float(gen.sea_level))
			break
	player.global_position = player._planet.global_position + sea
	player.velocity = Vector3.ZERO
	player.ready_to_move = false
	player.swimming = false
	player._pitch = -0.3
	player.camera_distance = 4.0
	amb.set("weather_override", 1)
	wait = 240


func _swim_frame(f: int) -> void:
	if f == 1:
		_noon_at(player.up_direction)
	caption.text = "Swimming" if f < 150 else ("Diving" if f < 200 else "Back up")
	if f == 20:
		Input.action_press("move_forward")
	if f == 150:
		Input.action_release("move_forward")
		Input.action_press("crouch")
	if f == 200:
		Input.action_release("crouch")
	player._yaw += 0.003
