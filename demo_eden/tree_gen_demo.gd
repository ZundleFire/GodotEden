extends Node3D
## In-editor test bench for EdenTreeMesher-based procedural trees (trunk + branches + low-poly
## leaf blobs from eden_tree_instance.gd). Lays out a grid of independently-seeded trees on a
## flat plane so different generation results can be compared side by side without waiting on
## VoxelTerrain streaming, which is what tree_instancer_demo.gd is for instead.
##
## Press R to reroll every tree with a new batch of seeds.

const GRID_SIZE := 4
const SPACING := 3.0
const TREE_SCENE: PackedScene = preload("res://eden_tree_instance.tscn")

var _base_seed := 0
var _trees: Array[Node3D] = []

func _ready() -> void:
	_build_ground()
	_build_lighting()
	_build_camera()
	_spawn_grid()
	print("TREE_GEN_DEMO: ready -- press R to reroll seeds")


func _build_ground() -> void:
	var mesh_inst := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	var extent := GRID_SIZE * SPACING
	plane.size = Vector2(extent + 4.0, extent + 4.0)
	mesh_inst.mesh = plane
	mesh_inst.position = Vector3(extent * 0.5 - SPACING * 0.5, 0, extent * 0.5 - SPACING * 0.5)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.32, 0.38, 0.28)
	mesh_inst.material_override = mat
	add_child(mesh_inst)


func _build_lighting() -> void:
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-55, 35, 0)
	light.light_energy = 1.4
	light.shadow_enabled = true
	add_child(light)

	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.5, 0.65, 0.8)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.6, 0.65)
	env.ambient_light_energy = 0.4
	env_node.environment = env
	add_child(env_node)


func _build_camera() -> void:
	var extent := GRID_SIZE * SPACING
	var cam := Camera3D.new()
	cam.position = Vector3(extent * 0.5, extent * 0.75, extent * 1.3)
	add_child(cam)
	cam.look_at(Vector3(extent * 0.5, 1.0, extent * 0.5), Vector3.UP)
	cam.current = true


func _spawn_grid() -> void:
	for tree in _trees:
		tree.queue_free()
	_trees.clear()

	var i := 0
	for x in range(GRID_SIZE):
		for z in range(GRID_SIZE):
			var tree: Node3D = TREE_SCENE.instantiate()
			tree.seed_override = _base_seed + i
			tree.position = Vector3(x * SPACING, 0, z * SPACING)
			add_child(tree)
			_trees.append(tree)
			i += 1


func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and event.keycode == KEY_R:
		_base_seed += GRID_SIZE * GRID_SIZE
		_spawn_grid()
		print("TREE_GEN_DEMO: rerolled, base_seed=", _base_seed)
