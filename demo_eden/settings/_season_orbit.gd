extends SceneTree
## The planet from orbit (sunlit side) at chosen dates, to compare the hemispheres' seasons: snow and dormant
## ground in the winter hemisphere, green in the summer one.
##   godot --path demo_eden --resolution 1280x720 -s res://settings/_season_orbit.gd -- --out=<dir> [--months=11,5]
## --clip=N: instead, N frames (JPEG, numbered) of a whole year going by, for a video (with --fixed-fps 30)

const SETTLE_S := 25.0

var scene: Node
var cam: Camera3D
var cal: EdenCalendar
var atmo: Node
var out := "user://"
var months := [11, 5]
## Latitude the camera looks down on (degrees; 0 = the equator, the whole disc)
var view_lat := 0.0
var distance := 2.6
var index := -1
var t0 := 0
var clip := 0
var frame := 0
var day0 := 0.0
var pano_steps := 0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
		elif a.begins_with("--lat="):
			view_lat = float(a.trim_prefix("--lat="))
			distance = 1.35
		elif a.begins_with("--months="):
			months = Array(a.trim_prefix("--months=").split(",")).map(func(m): return int(m))
		elif a.begins_with("--clip="):
			clip = int(a.trim_prefix("--clip="))
			distance = 1.7
		elif a.begins_with("--pano="):
			pano_steps = int(a.trim_prefix("--pano="))
			months = [1]
	scene = load("res://_ocean_editor_probe.tscn").instantiate()
	scene.get_node("Camera3D2/VoxelViewer").free()
	var placed := scene.get_node_or_null("EdenPlayer")
	if placed:
		placed.free()
	root.add_child(scene)
	current_scene = scene
	cam = scene.get_node("Camera3D")
	cam.current = true
	cam.far = 500000.0
	for n in scene.find_children("*", "Node", true, false):
		if n is EdenCalendar:
			cal = n
		elif n.is_class("EdenPlanetAtmosphere"):
			atmo = n
	cal.running = false
	for n in scene.find_children("*", "EdenSpaceEnvironment", true, false):
		for i in pano_steps:
			n.call("next_panorama")
	# The ground, not the weather: no clouds or rings in the way
	for n in scene.find_children("*", "Node3D", true, false):
		if n.is_class("EdenCloudShell") or n.is_class("EdenPlanetRings"):
			n.visible = false
	_next()


func _next() -> void:
	index += 1
	# Mid-month, and the sun over longitude 0 (the camera sits above it)
	cal.set_date(1, months[index], 2, 12.0)
	t0 = Time.get_ticks_msec()


# Over the equator under the sun, far enough to see both poles (every frame: the probe scene's own script moves it)
func _place() -> void:
	var terrain: Node3D = scene.get_node("VoxelLodTerrain")
	var sun: Vector3 = atmo.call("get_sun_direction")
	var eq := (sun - Vector3.UP * sun.y).normalized()
	var dir := eq.rotated(eq.cross(Vector3.UP).normalized(), deg_to_rad(view_lat))
	cam.global_position = terrain.global_position + dir * 40000.0 * distance
	cam.look_at(terrain.global_position, Vector3.UP if absf(view_lat) < 80.0 else eq)


func _process(_d: float) -> bool:
	_place()
	if (Time.get_ticks_msec() - t0) / 1000.0 < SETTLE_S:
		day0 = cal.days
		return false
	if clip > 0:
		# Noon under the camera, a day further on every frame (so the sun stays put and the seasons roll by)
		# (whole days: the same hour each frame, so the ground holds still under the camera)
		cal.days = day0 + roundf(frame * float(cal.days_per_year()) / clip)
		cal._to_fields()
		cal._apply()
		if frame >= 3: # (the new date's colours reach the frame after)
			root.get_texture().get_image().save_jpg(out.path_join("%04d.jpg" % (frame - 3)), 0.9)
		frame += 1
		if frame >= clip + 3:
			quit()
			return true
		return false
	var name := ("lat%d%s_" % [absi(int(view_lat)), "n" if view_lat > 0.0 else "s"] if view_lat != 0.0 else "") + "%s_%s" % [EdenCalendar.MONTHS[months[index] - 1].to_lower(), "north_" + cal.season_name(true).to_lower()]
	root.get_texture().get_image().save_png(out.path_join("orbit_%s.png" % name))
	var tm: ShaderMaterial = scene.get_node("VoxelLodTerrain").material
	print("ORBIT terrain eden_calendar ", tm.get_shader_parameter("eden_calendar"), " axis ", tm.get_shader_parameter("eden_calendar_axis"), " ambience year_phase ", scene.get_node("VoxelLodTerrain/EdenAmbience").get("year_phase"))
	print("ORBIT saved ", name, "  north ", cal.season_name(true), ", south ", cal.season_name(false), ", declination %+.1f°" % cal.declination())
	if index + 1 >= months.size():
		quit()
		return true
	_next()
	return false
