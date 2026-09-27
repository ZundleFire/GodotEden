extends SceneTree
## Headless benchmark: measures real wall-clock time to stream/generate/mesh a
## 10km-radius planet's initial view using VoxelWorld's octree LOD mode, run via:
##   bin\godot.windows.editor.x86_64.console.exe --headless -s demo_eden/bench_10km.gd
## Stops once resident chunk/node counts go stable for STABLE_FRAMES straight (or
## FRAME_CAP is hit as a safety net) and prints elapsed ms.

const STABLE_FRAMES := 15
const FRAME_CAP := 4000

var world: VoxelWorld
var start_ms: int
var last_node_count := -1
var last_child_count := -1
var stable_streak := 0
var frame := 0
var started := false

func _initialize() -> void:
	world = VoxelWorld.new()
	var gen := EdenVoxelGeneratorNoise.new()
	gen.planet_radius = 10000.0
	gen.frequency = 0.002
	gen.octaves = 4
	gen.set_height_scale(80.0)
	world.generator = gen
	world.lod_mode = VoxelWorld.LOD_MODE_OCTREE
	world.octree_lod_count = world.estimate_octree_lod_count_for_planet_radius(gen.planet_radius)
	world.chunks_per_frame_budget = 4096
	world.enable_collision = false

	var up_dir := Vector3(0.4, 1.0, 0.3).normalized()
	world.position = up_dir * gen.planet_radius + up_dir * 25.0

	print("EDEN_BENCH: planet_radius=", gen.planet_radius, " octree_lod_count=", world.octree_lod_count)
	start_ms = Time.get_ticks_msec()

func _process(_delta: float) -> bool:
	if not started:
		started = true
		root.add_child(world)

	frame += 1
	var node_count: int = world.get_octree_node_count()
	var child_count: int = world.get_child_count()
	if node_count == last_node_count and child_count == last_child_count:
		stable_streak += 1
	else:
		stable_streak = 0
	last_node_count = node_count
	last_child_count = child_count

	if stable_streak >= STABLE_FRAMES or frame >= FRAME_CAP:
		var elapsed := Time.get_ticks_msec() - start_ms
		print("EDEN_BENCH: frames=", frame, " octree_nodes=", node_count, " chunk_children=", child_count,
			" elapsed_ms=", elapsed, " stable=", stable_streak >= STABLE_FRAMES)
		return true # stop the main loop
	return false
