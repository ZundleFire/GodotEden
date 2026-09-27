extends SceneTree
## Headless check of EdenAmbience weather on the probe scene's V4 generator: climate bake, regional storm
## coverage, snow piling up under a cold storm and ground getting wet under a warm one.
##   godot --headless --path demo_eden -s res://_weather_test.gd -- [--out=<dir>]

const SCALE := 60.0 # weather-seconds per second: a minute of weather per second


class Planet:
	extends Node3D
	var generator: VoxelGenerator


var amb: EdenAmbience
var cam: Camera3D
var gen: VoxelGenerator
var R := 40000.0
var cold_pos: Vector3
var warm_pos: Vector3
var t0 := 0
var phase := 0
var ok := true
var out := ""


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	var probe: Node = load("res://_ocean_editor_probe.tscn").instantiate()
	gen = probe.get_node("VoxelLodTerrain").generator
	probe.free()
	R = gen.planet_radius

	cold_pos = _find(func(s): return s.temperature < 0.15 and s.height > 20.0)
	warm_pos = _find(func(s): return s.temperature > 0.55 and s.moisture > 0.5 and s.height > 20.0)

	var planet := Planet.new()
	planet.generator = gen
	root.add_child(planet)
	amb = EdenAmbience.new()
	amb.audio_enabled = false
	amb.weather_time_scale = SCALE
	planet.add_child(amb)
	cam = Camera3D.new()
	cam.current = true
	root.add_child(cam)
	cam.position = cold_pos * 1.0001
	t0 = Time.get_ticks_msec()


func _find(pred: Callable) -> Vector3:
	var golden := PI * (3.0 - sqrt(5.0))
	for i in 4000:
		var y := 1.0 - float(i) / 3999.0 * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var d := Vector3(cos(golden * i) * r, y, sin(golden * i) * r)
		var s: Dictionary = gen.sample_surface(d)
		if pred.call(s):
			return d * (R + float(s.height))
	push_error("no spot found")
	return Vector3.UP * R


func _check(cond: bool, msg: String) -> void:
	print("WEATHER_TEST %s %s" % ["ok  " if cond else "FAIL", msg])
	ok = ok and cond


func _process(_d: float) -> bool:
	var st: Dictionary = amb.get_debug_state()
	var el := (Time.get_ticks_msec() - t0) / 1000.0
	if phase == 0:
		if st.climate_ready:
			print("WEATHER_TEST climate baked in %.1f s" % el)
			amb.clear_snow_and_wetness()
			amb.add_storm(cold_pos, 3000.0, 1.0, 1800.0, false)
			amb.add_storm(warm_pos, 3000.0, 1.0, 1800.0, true)
			phase = 1
			t0 = Time.get_ticks_msec()
		elif el > 60.0:
			_check(false, "climate bake finished within 60 s")
			return true
		return false
	if phase == 1 and el > 4.0: # ~4 weather-minutes of storm
		var cold: Dictionary = amb.get_weather_at(cold_pos)
		var warm: Dictionary = amb.get_weather_at(warm_pos)
		print("WEATHER_TEST cold spot ", cold)
		print("WEATHER_TEST warm spot ", warm)
		print("WEATHER_TEST camera state ", st)
		_check(cold.precipitation > 0.5 and warm.precipitation > 0.5, "storms rain/snow where placed")
		_check(cold.snow > 0.6, "snow piled up at the cold storm (%.2f)" % cold.snow)
		_check(warm.snow < 0.05 and warm.wetness > 0.6, "warm storm wets the ground, no snow")
		_check(st.freezing > 0.5 and st.snow_cover > 0.6, "camera under the cold storm sees snow")
		_check(st.effects[EdenAmbience.FX_SNOW] > 0.3 and st.effects[EdenAmbience.FX_RAIN] < 0.05, "snowing, not raining, at the camera")
		_check(warm.thunder, "thunder cell reports thunder")
		var img := amb.get_weather_texture().get_image()
		var rainy := 0
		var cloudy := 0
		for y in img.get_height():
			for x in img.get_width():
				var c := img.get_pixel(x, y)
				rainy += 1 if c.r > 0.1 else 0
				cloudy += 1 if c.a > 0.2 else 0
		var n := float(img.get_width() * img.get_height())
		print("WEATHER_TEST map: %.1f%% precipitating, %.1f%% storm cloud" % [100.0 * rainy / n, 100.0 * cloudy / n])
		_check(rainy / n > 0.01 and rainy / n < 0.5, "regional weather: some but not all of the planet precipitates")
		if out != "":
			img.save_png(out.path_join("weather_map.png"))
		# Melt: move the storms' snow into warm weather by switching the freeze point down
		amb.freeze_temperature = 0.0
		phase = 2
		t0 = Time.get_ticks_msec()
		return false
	if phase == 2 and el > 6.0:
		var cold: Dictionary = amb.get_weather_at(cold_pos)
		_check(cold.snow < 0.5, "snow melts above freezing (%.2f)" % cold.snow)
		# Under the thunderstorm: lightning strikes and it rains
		cam.position = warm_pos * 1.0001
		phase = 3
		t0 = Time.get_ticks_msec()
		return false
	if phase == 3 and el > 3.0:
		_check(st.lightning_strikes > 0, "lightning under the thunder cell (%d strikes)" % st.lightning_strikes)
		_check(st.effects[EdenAmbience.FX_RAIN] > 0.3 and st.audio_levels[AudioStreamEdenAmbience.LAYER_RAIN] > 0.3, "raining, with rain audio")
		# Weather overrides around the camera, one after another (still under the warm thunderstorm)
		amb.override_fade = 1.0
		_next_override()
		return false
	if phase >= 4 and el > 3.0:
		_check_override(st)
		if not _next_override():
			print("WEATHER_TEST ", "PASS" if ok else "FAIL")
			quit(0 if ok else 1)
			return true
	return false


var _overrides := [
	[EdenAmbience.WEATHER_CLEAR, "warm"],
	[EdenAmbience.WEATHER_SNOW, "warm"],
	[EdenAmbience.WEATHER_RAIN, "cold"],
	[EdenAmbience.WEATHER_DUST_STORM, "cold"],
	[EdenAmbience.WEATHER_THUNDERSTORM, "cold"],
	[EdenAmbience.WEATHER_AUTO, "cold"],
]
var _override_index := -1
var _strikes_before := 0


func _next_override() -> bool:
	_override_index += 1
	if _override_index >= _overrides.size():
		return false
	var o: Array = _overrides[_override_index]
	amb.weather_override = o[0]
	cam.position = (warm_pos if o[1] == "warm" else cold_pos) * 1.0001
	_strikes_before = amb.get_debug_state().lightning_strikes
	phase = 4 + _override_index
	t0 = Time.get_ticks_msec()
	return true


func _check_override(st: Dictionary) -> void:
	var fx: Array = st.effects
	match _overrides[_override_index][0]:
		EdenAmbience.WEATHER_CLEAR:
			_check(st.precipitation < 0.05 and st.cloud < 0.1, "Clear clears the storm overhead (precip %.2f)" % st.precipitation)
		EdenAmbience.WEATHER_SNOW:
			_check(st.freezing > 0.9 and fx[EdenAmbience.FX_SNOW] > 0.3 and fx[EdenAmbience.FX_RAIN] < 0.05, "Snow snows even somewhere warm")
		EdenAmbience.WEATHER_RAIN:
			_check(st.freezing < 0.1 and fx[EdenAmbience.FX_RAIN] > 0.3, "Rain rains even somewhere freezing")
		EdenAmbience.WEATHER_DUST_STORM:
			_check(st.dust_storm > 0.5 and fx[EdenAmbience.FX_DUST] > 0.3, "Dust Storm blows dust (%.2f)" % st.dust_storm)
		EdenAmbience.WEATHER_THUNDERSTORM:
			_check(st.thunder and st.lightning_strikes > _strikes_before, "Thunderstorm strikes lightning (%d)" % (st.lightning_strikes - _strikes_before))
		EdenAmbience.WEATHER_AUTO:
			_check(st.override_strength == 0.0 and st.weather == "Auto", "Auto hands back to the natural weather")
