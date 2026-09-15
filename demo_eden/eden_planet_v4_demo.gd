extends Node3D
## EdenPlanetGeneratorV4 demo: tectonics + erosion filter terrain, rendered with planet_v4.gdshader using the
## generator's surface data (erosion/ridge/moisture/temperature via CUSTOM2).
##
## Env vars (optional): EDEN_V4_CAPTURE=path.png saves a screenshot and quits; EDEN_V4_DEBUG=1..4 sets the
## shader debug view (1 erosion, 2 ridge, 3 moisture, 4 temperature); EDEN_V4_ALTITUDE overrides camera altitude;
## EDEN_V4_SET="name=value;..." overrides generator properties.

const PLANET_RADIUS := 40000.0
const CAPTURE_FRAME := 900

var _frame := 0
var _capture_path := ""
var _gen: EdenPlanetGeneratorV4


func _ready() -> void:
	_capture_path = OS.get_environment("EDEN_V4_CAPTURE")
	_gen = EdenPlanetGeneratorV4.new()
	_gen.planet_radius = PLANET_RADIUS
	# EDEN_V4_SET="use_erosion=false;erosion_strength=0.3" overrides generator properties
	for pair in OS.get_environment("EDEN_V4_SET").split(";", false):
		var kv := pair.split("=")
		_gen.set(kv[0], str_to_var(kv[1]))

	var mesher := VoxelMesherTransvoxel.new()
	mesher.texturing_mode = VoxelMesherTransvoxel.TEXTURES_MIXEL4_S4
	mesher.surface_data_enabled = true

	# Surface data lives in CHANNEL_DATA6 as 4 packed bytes, so it needs 32 bits
	var format := VoxelFormat.new()
	format.set_channel_depth(VoxelBuffer.CHANNEL_DATA6, VoxelBuffer.DEPTH_32_BIT)

	var material := ShaderMaterial.new()
	material.shader = load("res://shaders/planet_v4.gdshader")
	material.set_shader_parameter("u_planet_radius", PLANET_RADIUS)
	material.set_shader_parameter("u_debug_mode", OS.get_environment("EDEN_V4_DEBUG").to_int())

	var terrain := VoxelLodTerrain.new()
	terrain.generator = _gen
	terrain.mesher = mesher
	terrain.format = format
	terrain.material = material
	terrain.lod_distance = 400.0
	terrain.lod_count = 8
	terrain.view_distance = 12000
	terrain.generate_collisions = false
	add_child(terrain)

	_build_camera()
	_build_lighting()


# Looks across a mountainous land area picked with sample_surface()
func _build_camera() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	var best_dir := Vector3.UP
	var best_height := -INF
	for i in 400:
		var d := Vector3(rng.randf_range(-1, 1), rng.randf_range(-0.6, 0.6), rng.randf_range(-1, 1)).normalized()
		var h: float = _gen.sample_surface(d).height
		if h > best_height and h < 2500.0:
			best_height = h
			best_dir = d

	var altitude_env := OS.get_environment("EDEN_V4_ALTITUDE")
	var altitude := altitude_env.to_float() if altitude_env != "" else 1800.0
	var up := best_dir
	var side := up.cross(Vector3.RIGHT if absf(up.x) < 0.9 else Vector3.FORWARD).normalized()

	var camera := Camera3D.new()
	camera.far = 60000.0
	camera.near = 1.0
	camera.current = true
	add_child(camera)
	camera.global_position = up * (PLANET_RADIUS + maxf(best_height, 0.0) + altitude)
	var target := (up * PLANET_RADIUS + side * altitude * 3.0).normalized() * (PLANET_RADIUS + maxf(best_height, 0.0))
	camera.look_at(target, up)

	var viewer := VoxelViewer.new()
	viewer.view_distance = 12000
	camera.add_child(viewer)
	print("EDEN_V4_DEMO: camera over ", best_dir, " surface height ", best_height)


func _build_lighting() -> void:
	var light := DirectionalLight3D.new()
	add_child(light)
	light.look_at(-Vector3(0.4, 0.8, 0.3).normalized(), Vector3.FORWARD)
	light.light_energy = 1.6

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.45, 0.6, 0.8)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.55, 0.65)
	env.ambient_light_energy = 0.4
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	var env_node := WorldEnvironment.new()
	env_node.environment = env
	add_child(env_node)


func _process(_delta: float) -> void:
	_frame += 1
	if _capture_path == "":
		return
	if _frame == CAPTURE_FRAME:
		var err := get_viewport().get_texture().get_image().save_png(_capture_path)
		print("EDEN_V4_DEMO: screenshot ", _capture_path, " err=", err)
	elif _frame > CAPTURE_FRAME + 2:
		get_tree().quit()
