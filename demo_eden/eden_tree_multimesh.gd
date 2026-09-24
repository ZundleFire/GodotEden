class_name EdenTreeMultiMesh
extends RefCounted
## GPU-instanced procedural trees for VoxelInstancer. Instead of one EdenTreeInstance scene per
## tree (4 nodes, unique meshes + materials, generated on the main thread at spawn), each species
## gets a small pool of pre-built variants. Each variant's trunk/foliage/fruit are merged into one
## vertex-coloured mesh with a shared material, and drawn as one MultiMesh item: one draw call per
## variant per chunk, zero spawn-time generation.
##
## ponytail: variety is capped at `variants` shapes per species (instances still differ by the
## generator's random rotation + scale). Raise variants if repetition shows; each costs one more
## draw call per chunk.

static var _material: ShaderMaterial


## Shared by every tree variant (wind + vertex colour), so one set of parameters drives them all.
static func get_material() -> ShaderMaterial:
	if _material == null:
		_material = ShaderMaterial.new()
		_material.shader = load("res://shaders/eden_tree_lowpoly.gdshader")
	return _material


## tree_type is EdenTreeShape.TREE_* (0-based). Splits base_gen's density evenly over the variants.
static func build_items(tree_type: int, season: int, variants: int, base_gen: VoxelInstanceGenerator,
		lod_index := 1) -> Array[VoxelInstanceLibraryMultiMeshItem]:
	var items: Array[VoxelInstanceLibraryMultiMeshItem] = []
	for v in variants:
		var rng := RandomNumberGenerator.new()
		rng.seed = hash(Vector2i(tree_type, v))
		var shape := EdenTreeGenerator.species_shape(tree_type, rng)
		shape.season = season
		var built: Dictionary = EdenTreeGenerator.build(rng, shape)

		var g: VoxelInstanceGenerator = base_gen.duplicate()
		g.density = base_gen.density / variants
		var item := VoxelInstanceLibraryMultiMeshItem.new()
		item.name = "tree_%d_%d" % [tree_type, v]
		item.lod_index = lod_index
		item.generator = g
		item.mesh = merge(built)
		# Trunk-only capsule: walk under the crown, bump into the trunk. Scales with the instance.
		var trunk := CapsuleShape3D.new()
		trunk.radius = shape.trunk_base_radius
		trunk.height = maxf(shape.trunk_height, trunk.radius * 2.0)
		item.collision_shapes = [trunk, Transform3D(Basis(), Vector3(0, trunk.height * 0.5, 0))]
		items.append(item)
	return items


## One surface, colour baked per vertex from each part's flat colour.
static func merge(built: Dictionary) -> ArrayMesh:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	for part in [["trunk_mesh", "trunk_color"], ["foliage_mesh", "leaf_color"], ["fruit_mesh", "fruit_color"]]:
		var mesh: ArrayMesh = built.get(part[0])
		if mesh == null:
			continue
		var color: Color = built.get(part[1], Color.MAGENTA)
		for s in mesh.get_surface_count():
			var a := mesh.surface_get_arrays(s)
			var base := verts.size()
			var sv: PackedVector3Array = a[Mesh.ARRAY_VERTEX]
			verts.append_array(sv)
			var sn = a[Mesh.ARRAY_NORMAL]
			if sn is PackedVector3Array and sn.size() == sv.size():
				normals.append_array(sn)
			else:
				for i in sv.size():
					normals.append(Vector3.UP)
			for i in sv.size():
				colors.append(color)
			var si = a[Mesh.ARRAY_INDEX]
			if si is PackedInt32Array and si.size() > 0:
				for i in si:
					indices.append(base + i)
			else:
				for i in sv.size():
					indices.append(base + i)

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	# No mesh LOD: ImporterMesh.generate_lods halved drawn triangles but measured no GPU gain
	# (10.88 vs 10.84 ms, 1351 trees, GTX 750 Ti) -- LOD is picked per chunk MultiMesh, not per tree.
	var out := ArrayMesh.new()
	out.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	out.surface_set_material(0, get_material())
	return out
