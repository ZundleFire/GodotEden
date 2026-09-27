@tool
extends Node3D
## Live in-editor test bench for EdenTreeBuilder-generated trees. This is a @tool script: every
## exported parameter's setter calls _rebuild() immediately, and Godot runs property setters in
## the editor the same as at runtime -- so dragging a slider in the Inspector regenerates the
## tree right there in the 3D viewport, no Play needed.
##
## Uses EdenTreeBuilder directly (the same code eden_tree_instance.gd calls at runtime), so what
## you tune here is exactly what VoxelInstancer-spawned trees will look like -- this doesn't
## reimplement the mesher, it's just a knob panel in front of it.

@export var tree_seed: int = 0:
	set(value):
		tree_seed = value
		_rebuild()

@export_range(1.0, 8.0, 0.05) var trunk_height: float = 3.0:
	set(value):
		trunk_height = value
		_rebuild()

@export_range(0.05, 0.6, 0.01) var trunk_base_radius: float = 0.2:
	set(value):
		trunk_base_radius = value
		_rebuild()

@export_range(0.1, 0.8, 0.01) var crown_radius_mul: float = 0.35:
	set(value):
		crown_radius_mul = value
		_rebuild()

@export_range(0, 8, 1) var branch_count: int = 3:
	set(value):
		branch_count = value
		_rebuild()

@export_range(3, 16, 1) var trunk_sides: int = 7:
	set(value):
		trunk_sides = value
		_rebuild()

@export_range(0.3, 1.6, 0.01) var branch_len_min: float = 0.8:
	set(value):
		branch_len_min = value
		_rebuild()

@export_range(0.3, 2.5, 0.01) var branch_len_max: float = 1.6:
	set(value):
		branch_len_max = value
		_rebuild()

@export_range(0.5, 5.0, 0.05) var leaf_radius_mul: float = 2.6:
	set(value):
		leaf_radius_mul = value
		_rebuild()

@export_range(0.0, 1.0, 0.01) var leaf_hue_min: float = 0.22:
	set(value):
		leaf_hue_min = value
		_rebuild()

@export_range(0.0, 1.0, 0.01) var leaf_hue_max: float = 0.36:
	set(value):
		leaf_hue_max = value
		_rebuild()

@export_range(1, 8, 1) var leaf_clusters_min: int = 3:
	set(value):
		leaf_clusters_min = value
		_rebuild()

@export_range(1, 8, 1) var leaf_clusters_max: int = 5:
	set(value):
		leaf_clusters_max = value
		_rebuild()

var _trunk_inst: MeshInstance3D
var _foliage_inst: MeshInstance3D
var _trunk_mat: StandardMaterial3D
var _foliage_mat: StandardMaterial3D
var _ready_done := false

func _ready() -> void:
	# Without an explicit light, this @tool scene is lit by whatever the Godot editor's own
	# default viewport environment happens to use -- a direction we don't control and can't
	# reason about, which made "is the shading right" impossible to judge reliably. A known
	# light removes that ambiguity, matching tree_gen_demo.gd's setup.
	_build_lighting()

	_trunk_inst = MeshInstance3D.new()
	_trunk_mat = StandardMaterial3D.new()
	_trunk_mat.albedo_color = Color(0.42, 0.30, 0.17)
	_trunk_mat.roughness = 0.85
	_trunk_inst.material_override = _trunk_mat
	add_child(_trunk_inst)

	_foliage_inst = MeshInstance3D.new()
	_foliage_mat = StandardMaterial3D.new()
	_foliage_mat.roughness = 0.95
	_foliage_inst.material_override = _foliage_mat
	add_child(_foliage_inst)

	# Exported setters can fire (from scene deserialization) before this point -- _rebuild()
	# no-ops until _ready_done is set, then this call catches up on whatever values loaded.
	_ready_done = true
	_rebuild()


func _build_lighting() -> void:
	# Guards against duplicate lights/environments: if this scene is ever saved while these
	# runtime-added nodes exist, they get serialized into the .tscn -- without this check, the
	# next _ready() would add a second light and a second WorldEnvironment on top of the saved
	# ones, and two overlapping directional lights (each casting its own shadow) is exactly the
	# kind of thing that can make correct shading look inexplicably wrong.
	if has_node("PreviewSun"):
		return

	var light := DirectionalLight3D.new()
	light.name = "PreviewSun"
	light.rotation_degrees = Vector3(-55, 35, 0)
	light.light_energy = 1.4
	# Shadows add nothing for judging a small preview mesh's shape/shading, and a shadow map
	# sized wrong for whatever's in view is a common source of surfaces looking incorrectly
	# dark independent of their actual normals -- simplest to just not have that variable here.
	light.shadow_enabled = false
	add_child(light)

	var env_node := WorldEnvironment.new()
	env_node.name = "PreviewEnvironment"
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.5, 0.65, 0.8)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.6, 0.65)
	env.ambient_light_energy = 0.4
	env_node.environment = env
	add_child(env_node)


func _rebuild() -> void:
	if not _ready_done:
		return
	var rng := RandomNumberGenerator.new()
	rng.seed = tree_seed

	var shape := EdenTreeBuilder.Shape.new()
	shape.trunk_height = trunk_height
	shape.trunk_base_radius = trunk_base_radius
	shape.crown_radius_mul = crown_radius_mul
	shape.branch_count = branch_count
	shape.sides = trunk_sides
	shape.branch_len_min = branch_len_min
	shape.branch_len_max = maxf(branch_len_max, branch_len_min)
	shape.leaf_radius_mul = leaf_radius_mul
	shape.leaf_hue_min = leaf_hue_min
	shape.leaf_hue_max = maxf(leaf_hue_max, leaf_hue_min)
	shape.leaf_cluster_min = leaf_clusters_min
	shape.leaf_cluster_max = max(leaf_clusters_max, leaf_clusters_min)

	var result := EdenTreeBuilder.build(rng, shape)
	_trunk_inst.mesh = result["trunk_mesh"]
	_foliage_inst.mesh = result["foliage_mesh"]
	_foliage_mat.albedo_color = result["leaf_color"]
