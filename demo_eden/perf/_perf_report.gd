extends SceneTree
## Performance report of the playable planet (eden_play.tscn): runs scenarios with the player (meadow, forest run,
## thunderstorm, snow, sea swim, equator desert), and per scenario records every frame's time plus the engine's
## monitors (GPU/CPU render time, draw calls, primitives, objects, memory, VRAM, nodes, physics, EdenAmbience CPU).
## Saves a screenshot per scenario and perf.json. Vsync is off so frame times are real.
##   godot --path demo_eden --resolution 1920x1080 -s res://perf/_perf_report.gd -- --out=<dir>
## perf/run_perf_report.py runs it while sampling the process (CPU %, RSS) and the GPU (nvidia-smi), and builds the
## report page.

const WARMUP_S := 6.0
const SAMPLE_S := 8.0
## [name, site, action, weather override (EdenAmbience: 0 auto 1 clear 2 rain 3 thunderstorm 4 snow 5 dust)]
const SCENARIOS := [
	["Meadow, standing", "spawn", "idle", 1],
	["Forest, running", "spawn", "run", 1],
	["Thunderstorm, walking", "spawn", "walk", 3],
	["Snowfall, standing", "spawn", "idle", 4],
	["Sea, swimming", "sea", "walk", 1],
	["Equator desert, walking", "equator", "walk", 1],
]

var play: Node
var player: EdenPlayer
var ambience: Node
var gen: Object
var out := "user://"
var index := -1
var phase := ""
var t0 := 0
var ready_at := -1.0
var frames: Array = []
var last_us := 0
var results: Array = []
var shot_taken := false
var strike_t := 0.0
var sites := {}
## GPU time per render pass (ms summed over the sample): only with --gpu-profile (the renderer's timestamps)
var passes := {}


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	DirAccess.make_dir_recursive_absolute(out)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play
	t0 = Time.get_ticks_msec()


func _el() -> float:
	return (Time.get_ticks_msec() - t0) / 1000.0


func _status(text: String) -> void:
	# For the sampler outside (run_perf_report.py): which scenario is being measured right now
	var f := FileAccess.open(out.path_join("status.txt"), FileAccess.WRITE)
	f.store_string(text)


static func _dir(lat: float, lon: float) -> Vector3:
	var a := deg_to_rad(lat)
	var b := deg_to_rad(lon)
	return Vector3(cos(a) * cos(b), sin(a), cos(a) * sin(b))


func _find(lat: float, sea: bool) -> Vector3:
	var R: float = gen.planet_radius
	var sl: float = gen.sea_level
	for i in 1440:
		var d := _dir(lat, i * 0.25)
		var h: float = gen.sample_surface(d).height
		if sea and h < sl - 6.0 and h > sl - 40.0:
			return d * (R + sl)
		if not sea and h > sl + 5.0 and h < 600.0:
			return d * (R + h + 2.0)
	return Vector3.ZERO


func _start(i: int) -> void:
	index = i
	var sc: Array = SCENARIOS[i]
	for a in ["move_forward", "sprint", "crouch", "jump"]:
		Input.action_release(a)
	if sc[1] != "spawn" or (i > 0 and SCENARIOS[i - 1][1] != "spawn"):
		player.global_position = player._planet.global_position + sites[sc[1]]
		player.velocity = Vector3.ZERO
		player.ready_to_move = false
		player.swimming = false
	ambience.set("weather_override", sc[3])
	# Local midday at the scenario's site
	var cal: EdenCalendar = player.calendar
	if cal:
		cal.days -= (cal.local_hour(sites[sc[1]]) - 12.0) / 24.0
		cal._to_fields()
		cal._apply()
	phase = "warmup"
	ready_at = -1.0
	frames = []
	passes = {}
	shot_taken = false
	t0 = Time.get_ticks_msec()
	_status("warmup|" + sc[0])
	print("PERF start ", sc[0])


func _process(delta: float) -> bool:
	if player == null:
		player = play.get("player")
		if player == null or player._planet == null:
			player = null
			return false
		ambience = player.get("_ambience")
		# Vsync off for real frame times (EdenGraphics would otherwise apply the player's setting)
		if player.graphics:
			player.graphics.vsync = false
			player.graphics.max_fps = 0
		# Midday, clock stopped: every scenario in daylight, comparable run to run
		var cal: EdenCalendar = player.calendar
		if cal:
			cal.running = false

		gen = player._planet.generator
		sites = {"sea": _find(2.0, true), "equator": _find(0.0, false)}
		sites["spawn"] = player.global_position - player._planet.global_position
		_start(0)
		return false
	var sc: Array = SCENARIOS[index]
	if phase == "warmup":
		if player.ready_to_move and ready_at < 0.0:
			ready_at = _el()
		if ready_at >= 0.0 and _el() > ready_at + WARMUP_S:
			match sc[2]:
				"walk":
					Input.action_press("move_forward")
				"run":
					Input.action_press("move_forward")
					Input.action_press("sprint")
			phase = "sample"
			t0 = Time.get_ticks_msec()
			last_us = Time.get_ticks_usec()
			_status("sample|" + sc[0])
		elif _el() > 120.0:
			print("PERF skip (no ground) ", sc[0])
			_next_or_finish()
		return false
	# Sampling: every frame
	var now := Time.get_ticks_usec()
	var vp := root.get_viewport_rid()
	frames.append([
		(now - last_us) / 1000.0,
		RenderingServer.viewport_get_measured_render_time_gpu(vp),
		RenderingServer.viewport_get_measured_render_time_cpu(vp),
		Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0,
		Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0,
	])
	last_us = now
	var rd := RenderingServer.get_rendering_device()
	var n := rd.get_captured_timestamps_count() if rd else 0
	for k in range(1, n):
		# Each timestamp marks the start of the pass it names: the time to the next one is that pass's
		var pass_name := rd.get_captured_timestamp_name(k - 1)
		passes[pass_name] = passes.get(pass_name, 0.0) + (rd.get_captured_timestamp_gpu_time(k) - rd.get_captured_timestamp_gpu_time(k - 1)) / 1e6 # (ns)
	# A slow turn so the view (and what is drawn) changes; lightning now and then in the storm
	player._yaw += delta * 0.25
	if sc[3] == 3:
		strike_t -= delta
		if strike_t <= 0.0:
			ambience.call("strike_lightning")
			strike_t = 2.5
	if not shot_taken and _el() > SAMPLE_S * 0.5:
		shot_taken = true
		var img := root.get_texture().get_image()
		img.resize(1280, 720, Image.INTERPOLATE_LANCZOS)
		img.save_jpg(out.path_join("shot_%d.jpg" % index), 0.85)
	if _el() > SAMPLE_S:
		_summarise()
		return _next_or_finish()
	return false


func _next_or_finish() -> bool:
	if index + 1 < SCENARIOS.size():
		_start(index + 1)
		return false
	for a in ["move_forward", "sprint"]:
		Input.action_release(a)
	var f := FileAccess.open(out.path_join("perf.json"), FileAccess.WRITE)
	f.store_string(JSON.stringify({
		"gpu": RenderingServer.get_video_adapter_name(),
		"driver": RenderingServer.get_video_adapter_vendor() + " " + RenderingServer.get_video_adapter_api_version(),
		"cpu": OS.get_processor_name(),
		"cores": OS.get_processor_count(),
		"os": OS.get_name() + " " + OS.get_version(),
		"resolution": "%dx%d" % [root.size.x, root.size.y],
		"engine": Engine.get_version_info().string,
		"scenarios": results,
	}, "\t"))
	_status("done|")
	print("PERF done")
	quit()
	return true


# Passes costing 0.1 ms or more per frame, most expensive first
func _top_passes(frame_count: int) -> Array:
	var out_passes := []
	for k in passes:
		var ms: float = passes[k] / maxf(frame_count, 1)
		if ms >= 0.1:
			out_passes.append([k, ms])
	out_passes.sort_custom(func(a, b): return a[1] > b[1])
	return out_passes


static func _pct(sorted: Array, p: float) -> float:
	return sorted[clampi(int(p * (sorted.size() - 1)), 0, sorted.size() - 1)]


func _summarise() -> void:
	var sc: Array = SCENARIOS[index]
	var ft: Array = []
	var cols := [[], [], [], [], []]
	for f in frames.slice(1): # the first delta spans the warmup switch
		for k in 5:
			cols[k].append(f[k])
	ft = cols[0].duplicate()
	ft.sort()
	var n := ft.size()
	var total := 0.0
	for v in ft:
		total += v
	var worst := ft.slice(int(n * 0.99))
	var worst_avg := 0.0
	for v in worst:
		worst_avg += v
	worst_avg /= maxf(worst.size(), 1)
	var mean := func(a: Array) -> float:
		var s := 0.0
		for v in a:
			s += v
		return s / maxf(a.size(), 1)
	var dbg: Dictionary = ambience.call("get_debug_state") if ambience else {}
	var stats: Dictionary = player._planet.call("get_statistics") if player._planet.has_method("get_statistics") else {}
	var r := {
		"name": sc[0],
		"frames": n,
		"fps_avg": 1000.0 * n / total,
		"fps_1pct_low": 1000.0 / worst_avg,
		"frame_ms_p50": _pct(ft, 0.5),
		"frame_ms_p95": _pct(ft, 0.95),
		"frame_ms_p99": _pct(ft, 0.99),
		"frame_ms_max": ft[n - 1],
		"gpu_ms_avg": mean.call(cols[1]),
		"render_cpu_ms_avg": mean.call(cols[2]),
		"process_ms_avg": mean.call(cols[3]),
		"physics_ms_avg": mean.call(cols[4]),
		"draw_calls": Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		"primitives": Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
		"objects": Performance.get_monitor(Performance.RENDER_TOTAL_OBJECTS_IN_FRAME),
		"vram_mb": Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0,
		"vram_texture_mb": Performance.get_monitor(Performance.RENDER_TEXTURE_MEM_USED) / 1048576.0,
		"vram_buffer_mb": Performance.get_monitor(Performance.RENDER_BUFFER_MEM_USED) / 1048576.0,
		"static_mem_mb": Performance.get_monitor(Performance.MEMORY_STATIC) / 1048576.0,
		"nodes": Performance.get_monitor(Performance.OBJECT_NODE_COUNT),
		"objects_total": Performance.get_monitor(Performance.OBJECT_COUNT),
		"physics_active": Performance.get_monitor(Performance.PHYSICS_3D_ACTIVE_OBJECTS),
		"physics_pairs": Performance.get_monitor(Performance.PHYSICS_3D_COLLISION_PAIRS),
		"ambience_cpu_us": dbg.get("cpu_avg_us", 0.0),
		"weather": ambience.call("get_weather_name") if ambience else "",
		"player_state": player.animator.state,
		"voxel_tasks": stats.get("tasks", {}),
		"series_frame_ms": cols[0],
		"series_gpu_ms": cols[1],
		"shot": "shot_%d.jpg" % index,
		"passes": _top_passes(n),
	}
	results.append(r)
	print("PERF %s: %.1f fps avg, %.1f 1%% low, gpu %.2f ms, %d draw calls" % [sc[0], r.fps_avg, r.fps_1pct_low, r.gpu_ms_avg, r.draw_calls])
