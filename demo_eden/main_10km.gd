extends Node3D
## GodotEden 10km-planet demo: builds a VoxelWorld with octree LOD streaming around a
## camera sitting on the surface of a 10km-radius planet, using STYLE_SMOOTH (dual
## contouring) terrain with flat per-triangle normals for a low-poly look, then captures a
## screenshot once generation has had time to converge.

var frame_count := 0
const CAPTURE_FRAME := 300
const QUIT_FRAME := 310

func _ready() -> void:
	var world := VoxelWorld.new()
	var gen := EdenVoxelGeneratorNoise.new()
	gen.planet_radius = 10000.0
	gen.frequency = 0.002
	gen.octaves = 4
	gen.set_height_scale(80.0)
	gen.mesh_style = EdenVoxelGeneratorNoise.STYLE_SMOOTH
	world.generator = gen
	world.lod_mode = VoxelWorld.LOD_MODE_OCTREE
	world.octree_lod_count = world.estimate_octree_lod_count_for_planet_radius(gen.planet_radius)
	world.chunks_per_frame_budget = 4096
	world.enable_collision = false
	add_child(world)

	# Near-equatorial (low |up_dir.y|, see EdenVoxelGeneratorNoise::classify_material's lat
	# thresholds) for a more varied grass/dirt/sand look -- at planetary scale a close-up
	# view only sees a tiny local patch, so a near-polar up_dir (e.g. (0.4, 1.0, 0.3), high
	# y-component) reads as uniformly tundra-colored, which is correct, just a poor demo pick.
	var up_dir := Vector3(1.0, 0.3, 0.2).normalized()
	var surface_point := up_dir * gen.planet_radius
	# VoxelWorld itself must stay at the origin -- its octree math treats camera_pos (read
	# straight from the active Camera3D's GLOBAL position, see _notification(NOTIFICATION_
	# PROCESS)) as already being in the same frame the octree itself is centered in. Move
	# the camera to the surface point instead, matching main.gd's own working pattern.
	# Tangent to the surface at up_dir, for a look-at target that's robust to whatever up_dir
	# is chosen (a fixed world-space offset like (60,-5,40) only reliably points "along the
	# ground" near the specific up_dir it was tuned for -- everywhere else it can point into
	# the sky or nearly back at the camera, producing a degenerate look_at).
	var reference := Vector3(0, 1, 0) if absf(up_dir.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
	var tangent := up_dir.cross(reference).normalized()

	var cam := Camera3D.new()
	cam.position = surface_point + up_dir * 10000.0
	cam.far = 80000.0
	add_child(cam)
	cam.look_at(surface_point + tangent * 60.0 - up_dir * 5.0, up_dir)
	cam.current = true

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-55, -35, 0)
	light.light_energy = 3.0
	add_child(light)

	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.4, 0.55, 0.75)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.55, 0.65)
	env.ambient_light_energy = 1.2
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env_node.environment = env
	add_child(env_node)

	print("EDEN_DEMO_10KM: scene ready, planet_radius=", gen.planet_radius, " mesh_style=SMOOTH")

func _process(_delta: float) -> void:
	frame_count += 1
	if frame_count == CAPTURE_FRAME:
		var img := get_viewport().get_texture().get_image()
		var err := img.save_png("res://screenshot_10km.png")
		print("EDEN_DEMO_10KM: screenshot save result=", err)
	
