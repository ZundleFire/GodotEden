extends SceneTree
## Stills of the planet from orbit against the space panorama: full disc, half-lit, crescent, a few panoramas.
##   godot --path demo_eden --resolution 1280x720 -s res://settings/_orbit_shots.gd -- --out=<dir>

const FIRST_SETTLE_S := 30.0
const SETTLE_S := 6.0
## [name, angle from the sun around the planet (deg), height above the equator (deg), distance in radii, panorama steps]
const SHOTS := [
	["full", 25.0, 20.0, 3.2, 0],
	["half", 90.0, 15.0, 3.0, 0],
	["crescent", 145.0, 10.0, 3.0, 0],
	["full_pano2", 40.0, -25.0, 3.4, 3],
	["half_pano3", 80.0, 30.0, 2.8, 5],
]

var scene: Node
var cam: Camera3D
var atmo: Node
var space: Node
var out := "user://"
var index := -1
var t0 := 0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
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
			n.running = false
			n.set_date(1, 5, 2, 12.0) # northern summer
		elif n.is_class("EdenPlanetAtmosphere"):
			atmo = n
		elif n.is_class("EdenSpaceEnvironment"):
			space = n
	_next()


func _next() -> void:
	index += 1
	for i in SHOTS[index][4]:
		space.call("next_panorama")
	t0 = Time.get_ticks_msec()


# Every frame: the probe scene's own script moves the camera
func _place() -> void:
	var s: Array = SHOTS[index]
	var c: Vector3 = scene.get_node("VoxelLodTerrain").global_position
	var sun: Vector3 = atmo.call("get_sun_direction")
	var eq := (sun - Vector3.UP * sun.y).normalized()
	var dir := eq.rotated(Vector3.UP, deg_to_rad(s[1]))
	dir = dir.rotated(dir.cross(Vector3.UP).normalized(), deg_to_rad(s[2]))
	cam.global_position = c + dir * 40000.0 * s[3]
	# Look a little off the centre so the planet sits to one side with space around it
	cam.look_at(c + dir.cross(Vector3.UP).normalized() * 40000.0 * 0.45, Vector3.UP)


func _process(_d: float) -> bool:
	_place()
	if (Time.get_ticks_msec() - t0) / 1000.0 < (FIRST_SETTLE_S if index == 0 else SETTLE_S):
		return false
	root.get_texture().get_image().save_jpg(out.path_join("space_%s.jpg" % SHOTS[index][0]), 0.92)
	print("ORBIT_SHOT %s panorama %s" % [SHOTS[index][0], space.call("get_panorama_label")])
	if index + 1 >= SHOTS.size():
		quit()
		return true
	_next()
	return false
