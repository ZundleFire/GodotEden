extends Node3D
## Standalone materials showcase, decoupled from eden_stress_test.gd's water-adjacency
## requirement -- searching for a real coastline near this seed's generator config
## (EdenPlanetGeneratorV3 defaults: oceanic_fraction=0.62, continent_size_scale=2.95) turned
## up nothing within 20km of any of 40 candidate wet spots, meaning continents/oceans are
## genuinely huge and sparse at this scale; chasing a coastline further would cost more search
## time than it's worth for what's just a materials reference shot. Picks the highest-elevation
## LAND spot instead (no water requirement) -- real elevation alone exercises the land
## shader's slope-based rock blend and height-based snow cap together, which is what actually
## needs demonstrating here.

const PLANET_RADIUS := 40000.0
const PLANET_SEED := 1
const CAMERA_ALTITUDE_MARGIN := 250.0

var gen: EdenPlanetGeneratorV3
var terrain: VoxelLodTerrain
var camera: Camera3D
var _up_dir := Vector3(0, 1, 0)
var _frame := 0


func _ready() -> void:
	gen = EdenPlanetGeneratorV3.new()
	gen.planet_radius = PLANET_RADIUS
	gen.set_seed(PLANET_SEED)
	gen.setup()

	var mesher := VoxelMesherTransvoxel.new()
	mesher.set_texturing_mode(VoxelMesherTransvoxel.TEXTURES_MIXEL4_S4)

	terrain = VoxelLodTerrain.new()
	terrain.generator = gen
	terrain.mesher = mesher
	terrain.lod_distance = 500.0
	terrain.lod_count = 8
	terrain.generate_collisions = false
	var land_material := ShaderMaterial.new()
	land_material.shader = load("res://shaders/planet_land.gdshader")
	terrain.material = land_material
	add_child(terrain)

	var pick := _find_highest_land()
	_up_dir = pick.dir
	var surface_r: float = pick.r
	print("EDEN_SHOWCASE: dir=", _up_dir, " elevation=", surface_r - PLANET_RADIUS)

	camera = Camera3D.new()
	camera.position = _up_dir * (surface_r + CAMERA_ALTITUDE_MARGIN)
	camera.far = 6000.0
	camera.current = true
	add_child(camera)
	var reference := Vector3(0, 1, 0) if absf(_up_dir.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
	var fwd := _up_dir.cross(reference).normalized()
	camera.look_at(camera.position + fwd * 200.0 - _up_dir * 60.0, _up_dir)

	var viewer := VoxelViewer.new()
	viewer.view_distance = 900.0
	camera.add_child(viewer)

	_build_lighting()


func _find_highest_land() -> Dictionary:
	const N := 40
	var best_dir := Vector3(0, 1, 0)
	var best_r := PLANET_RADIUS
	var golden_angle := PI * (3.0 - sqrt(5.0))
	for i in range(N):
		var y := 1.0 - (float(i) / float(N - 1)) * 2.0
		var radius_at_y := sqrt(max(0.0, 1.0 - y * y))
		var theta := golden_angle * i
		var d := Vector3(cos(theta) * radius_at_y, y, sin(theta) * radius_at_y).normalized()
		var r := _bisect_surface_radius(d)
		if r > best_r:
			best_r = r
			best_dir = d
	return {"dir": best_dir, "r": best_r}


func _bisect_surface_radius(dir: Vector3) -> float:
	var lo := PLANET_RADIUS - 4000.0
	var hi := PLANET_RADIUS + 4000.0
	for i in range(24):
		var mid := (lo + hi) * 0.5
		var buf := VoxelBuffer.new()
		buf.create(2, 2, 2)
		var p: Vector3 = dir * mid
		var origin := Vector3i(round(p.x), round(p.y), round(p.z))
		gen.generate_block(buf, Vector3(origin), 0)
		if buf.get_voxel_f(0, 0, 0, VoxelBuffer.CHANNEL_SDF) > 0.0:
			hi = mid
		else:
			lo = mid
	return (lo + hi) * 0.5


func _build_lighting() -> void:
	var sun_from := (_up_dir * 0.65 + Vector3(0.3, 0.85, 0.25)).normalized()
	var light := DirectionalLight3D.new()
	add_child(light)
	var ref := Vector3.UP if absf(sun_from.y) < 0.99 else Vector3.RIGHT
	light.look_at(-sun_from, ref)
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
	_frame += 1
	if _frame == 600:
		var img := get_viewport().get_texture().get_image()
		img.save_png("res://materials_showcase.png")
		print("EDEN_SHOWCASE: screenshot saved")
		get_tree().quit()
