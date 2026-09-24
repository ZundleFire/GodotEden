class_name EdenFoliageMeshes
extends RefCounted
## Mesh builders for EdenFoliage's MultiMesh layers. Every result is ONE vertex-coloured surface with a shared
## material, so a layer draws in one call per chunk: procedural trees (EdenTreeGenerator), bushes and rocks harvested
## from eden_foliage's EdenBushInstance / EdenRockInstance nodes, and fallen wood built with EdenTreeMesher.
## Mesh origins sit at the ground contact point, +Y up.

static var _wind_material: ShaderMaterial
static var _static_material: StandardMaterial3D


## Plants sway (eden_tree_lowpoly.gdshader); rocks and wood use a static vertex-colour material.
static func get_material(wind := true) -> Material:
	if wind:
		if _wind_material == null:
			_wind_material = ShaderMaterial.new()
			_wind_material.shader = load("res://shaders/eden_tree_lowpoly.gdshader")
		return _wind_material
	if _static_material == null:
		_static_material = StandardMaterial3D.new()
		_static_material.vertex_color_use_as_albedo = true
		_static_material.roughness = 0.95
	return _static_material


## tree_type is EdenTreeShape.TREE_* (0-based). Returns {mesh, trunk_radius, trunk_height}.
static func tree(tree_type: int, season: int, variant: int) -> Dictionary:
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(Vector2i(tree_type, variant))
	var shape := EdenTreeGenerator.species_shape(tree_type, rng)
	shape.season = season
	var built: Dictionary = EdenTreeGenerator.build(rng, shape)
	var parts := []
	for part in [["trunk_mesh", "trunk_color"], ["foliage_mesh", "leaf_color"], ["fruit_mesh", "fruit_color"]]:
		if built.get(part[0]) != null:
			parts.append([built[part[0]], built.get(part[1], Color.MAGENTA)])
	return {"mesh": merge(parts, true), "trunk_radius": shape.trunk_base_radius, "trunk_height": shape.trunk_height}


## Bakes a node that builds its own MeshInstance3D children on READY (EdenBushInstance, EdenRockInstance, ...):
## each child's material_override albedo becomes its vertex colour.
static func harvest(node: Node3D, wind: bool) -> ArrayMesh:
	node.notification(Node.NOTIFICATION_READY) # builds without entering the tree (seed_override set)
	var parts := []
	for child in node.get_children():
		var mi := child as MeshInstance3D
		if mi == null or mi.mesh == null or not mi.visible:
			continue
		var mat := mi.material_override as BaseMaterial3D
		parts.append([mi.mesh, mat.albedo_color if mat else Color(0.5, 0.5, 0.5)])
	node.free()
	return merge(parts, wind)


static func bush(bush_type: int, season: int, variant: int) -> ArrayMesh:
	var b := EdenBushInstance.new()
	b.bush_type = bush_type
	b.season = season
	b.seed_override = hash(Vector2i(100 + bush_type, variant)) & 0x7fffffff
	return harvest(b, true)


static func rock(rock_type: int, variant: int) -> ArrayMesh:
	var r := EdenRockInstance.new()
	r.rock_type = rock_type
	r.seed_override = hash(Vector2i(200 + rock_type, variant)) & 0x7fffffff
	return harvest(r, false)


## Fallen wood lying along +X on the ground: a branch (thin, with twigs) or a log (thick, bare).
static func wood(is_log: bool, variant: int) -> ArrayMesh:
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(Vector2i(300 + int(is_log), variant))
	var m := EdenTreeMesher.new()
	m.set_sides(5 if is_log else 4)
	var length := rng.randf_range(3.0, 5.0) if is_log else rng.randf_range(1.2, 2.4)
	var radius := rng.randf_range(0.22, 0.34) if is_log else rng.randf_range(0.04, 0.07)
	var a := Vector3(-length * 0.5, radius * 0.8, 0.0)
	var mid := Vector3(rng.randf_range(-0.2, 0.2), radius * 0.8, rng.randf_range(-0.15, 0.15))
	var b := Vector3(length * 0.5, radius * 0.7, rng.randf_range(-0.2, 0.2))
	m.add_segment(a, mid, radius, radius * 0.9)
	m.add_segment(mid, b, radius * 0.9, radius * 0.6)
	if not is_log:
		for k in rng.randi_range(2, 3):
			var o := a.lerp(b, rng.randf_range(0.25, 0.8))
			var dir := Vector3(rng.randf_range(-0.3, 0.6), rng.randf_range(0.05, 0.4), rng.randf_range(-1.0, 1.0)).normalized()
			m.add_segment(o, o + dir * rng.randf_range(0.3, 0.7), radius * 0.5, radius * 0.15)
	var bark := Color(0.33, 0.24, 0.16).lerp(Color(0.42, 0.37, 0.3), rng.randf()) # weathered, some greyer
	return merge([[m.build_mesh(), bark]], false)


## One surface from [[Mesh, Color], ...]. No mesh LOD: generated LODs halved triangles for no measured GPU gain
## (LOD is picked per chunk MultiMesh, not per instance).
static func merge(parts: Array, wind: bool) -> ArrayMesh:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	for part in parts:
		var mesh: Mesh = part[0]
		var color: Color = part[1]
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
	var out := ArrayMesh.new()
	out.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	out.surface_set_material(0, get_material(wind))
	return out
