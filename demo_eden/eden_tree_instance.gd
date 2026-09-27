extends Node3D
## Randomized procedural tree, built via EdenTreeBuilder (which wraps EdenTreeMesher), meant to
## be wrapped in a PackedScene and registered as a VoxelInstanceLibrarySceneItem so VoxelInstancer
## spawns/positions/culls copies of it directly on VoxelTerrain -- this script only builds ITS
## OWN geometry, it knows nothing about density/placement rules (that's the instancer's job).
##
## Each spawned instance gets a different look: global_position (set by the instancer before
## _ready() runs) seeds an RNG, so no two trees on the same terrain are identical, without
## needing per-instance data from the instancer itself. seed_override lets a test bench (see
## tree_gen_demo.gd) force a specific tree shape instead of deriving one from world position.
## For hand-tuning exact parameters live in the editor instead, see eden_tree_preview.gd.

@export var seed_override: int = -1

func _ready() -> void:
	var rng := RandomNumberGenerator.new()
	if seed_override >= 0:
		rng.seed = seed_override
	else:
		# Seed from world position so the same spot always regrows the same tree shape (stable
		# across reloads) but different spots differ -- hashing avoids float-precision seed
		# clashes for nearby positions.
		var p := global_position
		rng.seed = hash(Vector3i(round(p.x * 4.0), round(p.y * 4.0), round(p.z * 4.0)))

	var shape := EdenTreeBuilder.build_random_shape(rng)
	var result := EdenTreeBuilder.build(rng, shape)

	var mesh_inst := MeshInstance3D.new()
	mesh_inst.mesh = result["trunk_mesh"]
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.42, 0.30, 0.17)
	mat.roughness = 0.85
	mesh_inst.material_override = mat
	add_child(mesh_inst)

	var foliage_mesh: ArrayMesh = result["foliage_mesh"]
	if foliage_mesh.get_surface_count() > 0:
		var foliage_inst := MeshInstance3D.new()
		foliage_inst.mesh = foliage_mesh
		var leaf_mat := StandardMaterial3D.new()
		leaf_mat.albedo_color = result["leaf_color"]
		leaf_mat.roughness = 0.95
		foliage_inst.material_override = leaf_mat
		add_child(foliage_inst)
