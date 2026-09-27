extends Node3D
## GodotEden proof-of-concept demo: builds a VoxelWorld with a noise generator,
## lets it stream/generate/mesh a few chunks, then captures a screenshot and quits.
## This is the real, automatic VoxelWorld pipeline (generator -> buffer -> greedy
## mesh -> MeshInstance3D/CollisionShape3D children) -- not a hand-wired demo.
##
## Material demo: voxels now carry a real material_id/variant tag (see
## EdenVoxelBuffer::MaterialType) assigned by depth/latitude in generate_block(), and
## VoxelWorld renders it as real per-vertex color (grass/dirt/stone/sand distinguishable)
## instead of flat white.

var frame_count := 0
const CAPTURE_FRAME := 80
const QUIT_FRAME := 90

func _ready() -> void:
	var world := VoxelWorld.new()
	var gen := EdenVoxelGeneratorNoise.new()
	gen.planet_radius = 40.0
	gen.frequency = 0.02
	gen.octaves = 4
	gen.set_height_scale(15.0)
	world.generator = gen
	world.view_distance_chunks = 6
	world.chunks_per_frame_budget = 512
	add_child(world)

	# Ground-level camera near mid-latitude (grass band) angled to also see toward the
	# equator (sand band) in the distance.
	var up_dir := Vector3(0.4, 1.0, 0.3).normalized()
	var surface_point := up_dir * gen.planet_radius
	var cam := Camera3D.new()
	cam.position = surface_point + up_dir * 25.0
	cam.far = 400.0
	add_child(cam)
	cam.look_at(surface_point + Vector3(30, -5, 20), up_dir)
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

	print("EDEN_DEMO: scene ready, generator=", gen, " world=", world)

func _process(_delta: float) -> void:
	frame_count += 1
	if frame_count == CAPTURE_FRAME:
		var img := get_viewport().get_texture().get_image()
		var err := img.save_png("res://screenshot.png")
		print("EDEN_DEMO: screenshot save result=", err)
	if frame_count >= QUIT_FRAME:
		print("EDEN_DEMO: quitting")
		get_tree().quit()
