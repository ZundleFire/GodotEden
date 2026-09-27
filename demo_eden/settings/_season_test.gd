extends SceneTree
## Seasons test on eden_play.tscn: moves the player to a temperate forest at ~45° N, steps the calendar through
## mid-spring, -summer, -autumn and -winter (northern), and checks at each: the sun's declination and the day
## length, the temperature where the player stands, that a matching place at ~45° S has the opposite season (and
## temperature swing), and at a cold northern site that the air freezes in winter (precipitation falls as snow)
## but not in summer. Screenshots each season at the forest.
##   godot --audio-driver Dummy --path demo_eden --resolution 1280x720 -s res://settings/_season_test.gd -- --out=<dir>

var play: Node
var player: EdenPlayer
var cal: EdenCalendar
var amb: Node
var gen: Object
var out := "user://"
var ok := true
var t0 := 0
var step := 0
var season := 0
var north_site := Vector3.ZERO
var south_dir := Vector3.ZERO
var cold_dir := Vector3.ZERO
var results := []


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play
	t0 = Time.get_ticks_msec()


func _check(cond: bool, msg: String) -> void:
	print("SEASON_TEST %s %s" % ["ok  " if cond else "FAIL", msg])
	ok = ok and cond


func _el() -> float:
	return (Time.get_ticks_msec() - t0) / 1000.0


static func _dir(lat: float, lon: float) -> Vector3:
	var a := deg_to_rad(lat)
	var b := deg_to_rad(lon)
	return Vector3(cos(a) * cos(b), sin(a), cos(a) * sin(b))


# Land in a band of latitudes matching a climate (temperature, moisture ranges); prefers forest when asked
func _find(lats: Vector2, temp: Vector2, moist: Vector2, forest := false) -> Vector3:
	var R: float = gen.planet_radius
	var foliage: EdenFoliage = player._planet.get_node_or_null("EdenFoliage")
	var best := Vector3.ZERO
	var best_score := -INF
	for i in 2000:
		var lat := lerpf(lats.x, lats.y, fposmod(i * 0.618034, 1.0))
		var d := _dir(lat, i * 0.9)
		var s: Dictionary = gen.sample_surface(d)
		var h: float = s.height
		if h < 10.0 or h > 500.0 or float(s.landform) > 0.4:
			continue
		if s.temperature < temp.x or s.temperature > temp.y or s.moisture < moist.x or s.moisture > moist.y:
			continue
		var score := 0.0
		if forest and foliage:
			var t := d.cross(Vector3.UP).normalized()
			score = foliage.get_forest_density(d.rotated(t, 40.0 / R) * (R + h)) - foliage.get_forest_density(d * (R + h))
		if score > best_score:
			best_score = score
			best = d * (R + h + 2.0)
			if not forest:
				break
	return best


func _process(_d: float) -> bool:
	if player == null:
		player = play.get("player")
		return false
	match step:
		0:
			if player.ready_to_move and _el() > 2.0:
				cal = player.calendar
				amb = player._ambience
				gen = player._planet.generator
				_check(cal != null and amb != null, "the calendar and the ambience are there")
				# Before the first day of Year 1: the last day of Year 0, not "day 0"
				var keep := cal.days
				cal.days = -0.25
				cal._to_fields()
				_check(cal.date_string() == "Lethe %d, Year 0" % cal.days_per_month, "the day before the calendar starts reads %s" % cal.date_string())
				cal.days = keep
				cal._to_fields()
				cal.running = false
				north_site = _find(Vector2(40, 50), Vector2(0.42, 0.7), Vector2(0.45, 1.0), true)
				_check(north_site != Vector3.ZERO, "found a temperate forest edge at %.0f° N" % cal.latitude(north_site))
				south_dir = Vector3(north_site.x, -north_site.y, north_site.z).normalized()
				cold_dir = _find(Vector2(55, 70), Vector2(0.3, 0.42), Vector2(0.3, 1.0)).normalized()
				_check(cold_dir != Vector3.ZERO, "found a cold site at %.0f° N (mean %.0f °C)" % [cal.latitude(cold_dir), EdenCalendar.celsius(gen.sample_surface(cold_dir).temperature)])
				player.global_position = player._planet.global_position + north_site
				player.velocity = Vector3.ZERO
				player.ready_to_move = false
				step = 1
				t0 = Time.get_ticks_msec()
		1:
			if player.ready_to_move and _el() > 10.0:
				_set_season(0)
		2: # mid-season: settle, read, screenshot
			if _el() > 4.0:
				var up := player.up_direction
				var st: Dictionary = amb.call("get_debug_state")
				var sun := cal.sun_times(cal.latitude(up))
				var n_t: float = amb.call("get_weather_at", player._planet.global_position + north_site).temperature
				var s_t: float = amb.call("get_weather_at", player._planet.global_position + south_dir * north_site.length()).temperature
				var cold := _freezing_at(cold_dir)
				results.append({"season": EdenCalendar.SEASONS[season], "here": st.temperature, "north": n_t, "south": s_t,
						"decl": cal.declination(), "day": sun[2], "cold_freezing": cold, "name_n": cal.season_name(true), "name_s": cal.season_name(false)})
				print("SEASON_TEST info %-6s %s  here %.1f °C  45N %.1f °C  45S %.1f °C  declination %+.1f°  daylight %.1f h  cold site %.1f °C  N %s / S %s" % [
						EdenCalendar.SEASONS[season], cal.date_string(), EdenCalendar.celsius(st.temperature), EdenCalendar.celsius(n_t),
						EdenCalendar.celsius(s_t), cal.declination(), sun[2], EdenCalendar.celsius(cold), cal.season_name(true), cal.season_name(false)])
				root.get_texture().get_image().save_png(out.path_join("season_%s.png" % EdenCalendar.SEASONS[season].to_lower()))
				if season < 3:
					_set_season(season + 1)
				else:
					_verify()
					return _finish()
	return false


# The weather's temperature at a site (climate + season) against freezing
func _freezing_at(dir: Vector3) -> float:
	return amb.call("get_weather_at", player._planet.global_position + dir * float(gen.planet_radius)).temperature


func _set_season(i: int) -> void:
	season = i
	# The middle of the northern season (month 2, 5, 8, 11), at local noon at the site
	cal.set_date(1, 2 + 3 * i, 2, 12.0)
	var shift := (cal.local_hour(player.up_direction) - 12.0) / 24.0
	cal.days -= shift
	cal._to_fields()
	cal._apply()
	player._yaw = 0.6
	player._pitch = -0.05
	step = 2
	t0 = Time.get_ticks_msec()


func _verify() -> void:
	var spring: Dictionary = results[0]
	var summer: Dictionary = results[1]
	var autumn: Dictionary = results[2]
	var winter: Dictionary = results[3]
	_check(summer.name_n == "Summer" and summer.name_s == "Winter" and winter.name_n == "Winter" and winter.name_s == "Summer",
			"hemispheres have opposite seasons (N summer = S winter, and back)")
	_check(summer.decl > 12.0 and winter.decl < -12.0, "the sun stands north of the equator in northern summer (%+.1f°), south in winter (%+.1f°)" % [summer.decl, winter.decl])
	# The tropics: wet season when the sun is overhead (their summer), dry in their winter; opposite across the equator
	cal.set_date(1, 5, 2, 12.0)
	var n_july := cal.season_at(_dir(15.0, 0.0))
	var s_july := cal.season_at(_dir(-15.0, 0.0))
	cal.set_date(1, 11, 2, 12.0)
	var n_jan := cal.season_at(_dir(15.0, 0.0))
	var s_jan := cal.season_at(_dir(-15.0, 0.0))
	_check(n_july == "Wet season" and s_july == "Dry season" and n_jan == "Dry season" and s_jan == "Wet season",
			"tropical wet/dry seasons (15° N: %s then %s; 15° S: %s then %s)" % [n_july, n_jan, s_july, s_jan])
	cal.set_date(1, 4, 1, 0.0) # the northern summer solstice starts month 4
	_check(absf(cal.declination() - cal.axial_tilt) < 1.0, "at the solstice the sun is at the axial tilt (%+.1f°)" % cal.declination())
	_check(summer.day > 14.0 and winter.day < 10.0, "long summer days (%.1f h), short winter days (%.1f h) at 45° N" % [summer.day, winter.day])
	var swing_here := EdenCalendar.celsius(summer.here) - EdenCalendar.celsius(winter.here)
	_check(swing_here > 12.0, "summer is %.0f °C warmer than winter where the player is" % swing_here)
	var n_swing := EdenCalendar.celsius(summer.north) - EdenCalendar.celsius(winter.north)
	var s_swing := EdenCalendar.celsius(summer.south) - EdenCalendar.celsius(winter.south)
	_check(n_swing > 12.0 and s_swing < -12.0, "at 45° S the swing is reversed (N %+.0f °C, S %+.0f °C from northern winter to summer)" % [n_swing, s_swing])
	_check(autumn.here < summer.here and autumn.here > winter.here, "autumn falls between")
	var freeze := 0.3
	_check(winter.cold_freezing < freeze and summer.cold_freezing > freeze,
			"the cold site freezes in winter (%.0f °C: snow) and thaws in summer (%.0f °C: rain)" % [EdenCalendar.celsius(winter.cold_freezing), EdenCalendar.celsius(summer.cold_freezing)])


func _finish() -> bool:
	print("SEASON_TEST ", "PASS" if ok else "FAIL")
	quit(0 if ok else 1)
	return true
