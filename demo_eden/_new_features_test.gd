extends SceneTree
## Verification harness for the newest EdenPlanetAtmosphere features: Sun 2, Moon B, the parent
## planet, and EdenPlanetRings. Confirms they render without shader errors and look distinct from
## each other and from the primary sun/moon.
##   godot --path demo_eden --resolution 960x540 --script res://_new_features_test.gd -- <out_dir>

const R := 40000.0
var _out := "user://new_features_test"


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
	env_node.environment = env
	root.add_child(env_node)

	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	root.add_child(sun)
	var sun2 := DirectionalLight3D.new()
	sun2.name = "Sun2"
	root.add_child(sun2)
	var moon := DirectionalLight3D.new()
	moon.name = "Moon"
	root.add_child(moon)
	var moonb := DirectionalLight3D.new()
	moonb.name = "MoonB"
	root.add_child(moonb)

	var atmo := EdenPlanetAtmosphere.new()
	atmo.name = "Atmo"
	atmo.planet_radius = R
	atmo.day_length_seconds = 0.0
	atmo.sun_time_of_day = 0.32
	root.add_child(atmo)
	atmo.sun_light_path = atmo.get_path_to(sun)
	atmo.sun2_light_path = atmo.get_path_to(sun2)
	atmo.moon_light_path = atmo.get_path_to(moon)
	atmo.moonb_light_path = atmo.get_path_to(moonb)
	atmo.environment_path = atmo.get_path_to(env_node)

	atmo.sun2_enabled = true
	atmo.moonb_enabled = true
	atmo.moon_phase = 0.5
	atmo.moonb_phase = 0.15
	atmo.parent_planet_enabled = true
	atmo.random_seed = 42.0

	var rings := EdenPlanetRings.new()
	rings.name = "Rings"
	rings.planet_radius = R
	root.add_child(rings)
	atmo.add_linked_material(rings.get_material())

	var planet := MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = R
	sphere.height = R * 2.0
	sphere.radial_segments = 128
	sphere.rings = 64
	planet.mesh = sphere
	var pm := StandardMaterial3D.new()
	pm.albedo_color = Color(0.3, 0.28, 0.25)
	planet.material_override = pm
	root.add_child(planet)

	var cam := Camera3D.new()
	cam.near = 5.0
	cam.far = 400000.0
	root.add_child(cam)
	cam.current = true

	await _frames(10)
	print("new_features: sun2_dir=%s moonb_illum=%.2f moonb_dir=%s" % [
		str(atmo.get_sun2_direction()), atmo.get_moonb_illumination(), str(atmo.get_moonb_direction())])

	# --- Orbit view: rings, both moons, sun2 disc and parent planet all visible together.
	cam.fov = 60.0
	var site := Vector3(0.6, 0.35, 0.72).normalized()
	cam.global_transform = Transform3D(Basis.looking_at(-site, Vector3.UP), site * (R + 90000.0))
	await _frames(10)
	_save("orbit_overview")

	# --- Sun2 close-up: aim at the primary sun direction, sun2 should sit nearby, own colour.
	var sd: Vector3 = atmo.get_sun_direction()
	cam.fov = 30.0
	var away := -site * (R + 200000.0)
	cam.global_transform = Transform3D(Basis.looking_at(sd, Vector3.UP), away)
	await _frames(10)
	_save("sun2_closeup")

	# --- Rings edge-on-ish from a ground site, atmosphere visible.
	cam.fov = 70.0
	var ground_site := Vector3(0.8, 0.25, 0.55).normalized()
	cam.global_transform = Transform3D(Basis.looking_at((Vector3.UP - ground_site * ground_site.dot(Vector3.UP)).normalized(), ground_site), ground_site * (R + 100.0))
	await _frames(10)
	_save("rings_from_ground")

	# --- Parent planet close-up.
	var pd: Vector3 = atmo.parent_planet_direction.normalized()
	cam.fov = 20.0
	cam.global_transform = Transform3D(Basis.looking_at(pd, Vector3.UP), away)
	await _frames(10)
	_save("parent_planet_closeup")

	print("new_features: done")
	quit(0)


func _save(name: String) -> void:
	var img := root.get_texture().get_image()
	img.save_png(_out.path_join(name + ".png"))


func _frames(n: int) -> void:
	for _i in n:
		await RenderingServer.frame_post_draw
