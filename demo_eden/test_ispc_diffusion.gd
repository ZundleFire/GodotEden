extends Node3D
## One-shot visual test for EdenPlanetGeneratorV3.use_ispc_diffusion (see
## eden_planet_generator_v3.h's doc comment on that property). Builds terrain with the toggle
## set from the EDEN_TEST_ISPC env var ("1" = on, anything else/unset = off), flies a camera to
## a fixed altitude above a fixed direction (no coastal-search needed, this is just checking the
## generator doesn't crash/produce garbage -- not a coastline-framing screenshot), and saves a
## screenshot + quits after CAPTURE_FRAME frames.

const PLANET_RADIUS := 40000.0
const PLANET_SEED := 1
const MAX_TERRAIN_HEIGHT := 4000.0
const CAPTURE_FRAME := 900
const UP_DIR := Vector3(0, 1, 0)

var _frame_count := 0
var _use_ispc := false


func _ready() -> void:
	VoxelEngine.set_generation_thread_count(6)
	_use_ispc = OS.get_environment("EDEN_TEST_ISPC") == "1"
	_build_terrain()
	_build_camera()
	_build_lighting()
	print("EDEN_TEST_ISPC_DIFFUSION: ready, use_ispc_diffusion=", _use_ispc)


func _build_terrain() -> void:
	var gen := EdenPlanetGeneratorV3.new()
	gen.planet_radius = PLANET_RADIUS
	gen.set_seed(PLANET_SEED)
	gen.use_ispc_diffusion = _use_ispc
	gen.setup()

	var mesher := VoxelMesherTransvoxel.new()
	mesher.set_texturing_mode(VoxelMesherTransvoxel.TEXTURES_MIXEL4_S4)

	var terrain := VoxelLodTerrain.new()
	terrain.generator = gen
	terrain.mesher = mesher
	terrain.lod_distance = 400.0
	terrain.lod_count = 8

	var land_material := ShaderMaterial.new()
	land_material.shader = load("res://shaders/planet_land.gdshader")
	terrain.material = land_material
	add_child(terrain)


func _build_camera() -> void:
	var camera := Camera3D.new()
	camera.position = UP_DIR * (PLANET_RADIUS + MAX_TERRAIN_HEIGHT + 300.0)
	# far must clear worst case: camera sits max_terrain_height above sea level, but the
	# surface directly below could be an ocean trench roughly max_terrain_height BELOW sea
	# level too (same amplitude, opposite sign) -- 2x margin plus slack, not just 1x.
	camera.far = 10000.0
	camera.current = true
	add_child(camera)
	camera.look_at(Vector3.ZERO, Vector3(1, 0, 0))

	var viewer := VoxelViewer.new()
	viewer.view_distance = 900.0
	camera.add_child(viewer)


func _build_lighting() -> void:
	var sun_from := (UP_DIR * 0.65 + Vector3(0.3, 0.85, 0.25)).normalized()
	var light := DirectionalLight3D.new()
	add_child(light)
	light.look_at(-sun_from, Vector3(1, 0, 0))
	light.light_energy = 1.6

	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.4, 0.55, 0.75)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.55, 0.65)
	env.ambient_light_energy = 0.35
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env_node.environment = env
	add_child(env_node)


func _process(_delta: float) -> void:
	_frame_count += 1
	if _frame_count % 120 == 0:
		print("EDEN_TEST_ISPC_DIFFUSION: frame=", _frame_count)
	if _frame_count == CAPTURE_FRAME:
		var img := get_viewport().get_texture().get_image()
		var suffix := "ispc" if _use_ispc else "scalar"
		var err := img.save_png("res://screenshot_ispc_diffusion_%s.png" % suffix)
		print("EDEN_TEST_ISPC_DIFFUSION: screenshot save result=", err, " suffix=", suffix)
	if _frame_count > CAPTURE_FRAME + 5:
		get_tree().quit()
