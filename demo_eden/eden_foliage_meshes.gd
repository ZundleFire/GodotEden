class_name EdenFoliageMeshes
extends RefCounted
## Mesh builders for EdenFoliage's MultiMesh layers. Every result is ONE vertex-coloured surface with a shared
## material, so a layer draws in one call per chunk: procedural trees (EdenTreeGenerator), bushes harvested from
## EdenBushInstance, faceted rocks built here, fallen wood built with EdenTreeMesher, and grass tufts.
## Mesh origins sit at the ground contact point, +Y up. Rocks are ~1 m across at scale 1.

static var _wind_material: ShaderMaterial
static var _static_material: StandardMaterial3D
static var _cache := {}

const TREE_TYPES := {
	EdenFoliageLayer.Kind.OAK: EdenTreeShape.TREE_OAK, EdenFoliageLayer.Kind.PINE: EdenTreeShape.TREE_PINE,
	EdenFoliageLayer.Kind.BIRCH: EdenTreeShape.TREE_BIRCH, EdenFoliageLayer.Kind.WILLOW: EdenTreeShape.TREE_WILLOW,
	EdenFoliageLayer.Kind.PALM: EdenTreeShape.TREE_PALM, EdenFoliageLayer.Kind.DEAD_TREE: EdenTreeShape.TREE_DEAD,
	EdenFoliageLayer.Kind.FRUIT_TREE: EdenTreeShape.TREE_FRUIT,
}
const BUSH_TYPES := {
	EdenFoliageLayer.Kind.SHRUB: EdenBushInstance.BUSH_SHRUB, EdenFoliageLayer.Kind.FERN: EdenBushInstance.BUSH_FERN,
	EdenFoliageLayer.Kind.DEAD_BUSH: EdenBushInstance.BUSH_DEAD,
	EdenFoliageLayer.Kind.TALL_GRASS: EdenBushInstance.BUSH_TALL_GRASS,
	EdenFoliageLayer.Kind.CACTUS: EdenBushInstance.BUSH_CACTUS, EdenFoliageLayer.Kind.BERRY_BUSH: EdenBushInstance.BUSH_BERRY,
}


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


## Mesh for one variant of a layer (not grass: EdenFoliage owns grass materials). Returns
## {mesh, shape: [Shape3D, Transform3D] or []} for collision at scale 1. Cached by everything that shapes it.
static func build(layer: EdenFoliageLayer, variant: int) -> Dictionary:
	var key := [layer.kind, variant, layer.rock_color, layer.color_variation, layer.moss, layer.moss_color,
			layer.lichen, layer.lichen_color, layer.facet_detail, layer.stretch, layer.roughness]
	if _cache.has(key):
		return _cache[key]
	var out := {}
	var K := EdenFoliageLayer.Kind
	if TREE_TYPES.has(layer.kind):
		var t := tree(TREE_TYPES[layer.kind], EdenTreeShape.SEASON_SUMMER, variant)
		var trunk := CapsuleShape3D.new()
		trunk.radius = t.trunk_radius
		trunk.height = maxf(t.trunk_height, trunk.radius * 2.0)
		out = {"mesh": t.mesh, "shape": [trunk, Transform3D(Basis(), Vector3(0, trunk.height * 0.5, 0))]}
	elif BUSH_TYPES.has(layer.kind):
		out = {"mesh": bush(BUSH_TYPES[layer.kind], EdenTreeShape.SEASON_SUMMER, variant), "shape": []}
	elif layer.kind == K.BRANCH or layer.kind == K.LOG:
		var is_log: bool = layer.kind == K.LOG
		var shape := []
		if is_log:
			var cap := CapsuleShape3D.new()
			cap.radius = 0.28
			cap.height = 4.0
			shape = [cap, Transform3D(Basis(Vector3.BACK, PI * 0.5), Vector3(0, 0.25, 0))]
		out = {"mesh": wood(is_log, variant), "shape": shape}
	else:
		out = rock(layer, variant)
	_cache[key] = out
	return out


## Far-distance copy of a layer variant's mesh: voxelized at `resolution` cells across its width (0: the
## layer's far_resolution). Cached.
static func build_far(layer: EdenFoliageLayer, variant: int, resolution := 0) -> Mesh:
	var res := resolution if resolution > 0 else layer.far_resolution
	var key := ["far", res, layer.kind, variant, layer.rock_color, layer.color_variation, layer.moss,
			layer.moss_color, layer.lichen, layer.lichen_color, layer.facet_detail, layer.stretch, layer.roughness]
	if not _cache.has(key):
		if layer.is_rock():
			# Rocks are already low-poly and voxel cubes armour cliffs in boxes: use the 20-facet version instead
			var coarse: EdenFoliageLayer = layer.duplicate()
			coarse.facet_detail = 0
			_cache[key] = build(coarse, variant).mesh
		else:
			_cache[key] = voxelize(build(layer, variant).mesh, res)
	return _cache[key]


## Blocky low-poly copy of a vertex-coloured mesh: cells the surface passes through (and everything they enclose)
## become solid, only faces between solid and outside cells are emitted, coloured by the dominant vertex colour
## of the surface inside their cell and greedily merged. Roughly 100-300 triangles for a 1-10k triangle tree at resolution 5.
static func voxelize(mesh: Mesh, resolution: int) -> ArrayMesh:
	var aabb := mesh.get_aabb()
	# Cells across the crown width, not the height: tall thin trees (pines) would otherwise become 1-cell pillars
	var cell := maxf(maxf(aabb.size.x, aabb.size.z), aabb.size.y * 0.25) / float(maxi(resolution, 1))
	# One empty cell of padding on each side: a sample exactly on the max face lands in cell floor(size / cell) + 1,
	# so the grid needs floor + 3 cells to keep that cell off the border
	var dims := Vector3i((aabb.size / cell).floor()) + Vector3i(3, 3, 3)
	var origin := aabb.position - Vector3.ONE * cell
	var count := dims.x * dims.y * dims.z
	var solid := PackedByteArray()
	solid.resize(count)
	var cell_cols: Array[Dictionary] = [] # per cell: colour -> samples
	cell_cols.resize(count)
	var idx := func(c: Vector3i) -> int: return c.x + dims.x * (c.y + dims.y * c.z)

	# 1. Rasterize the surface: sample each triangle densely enough to touch every cell it crosses
	for s in mesh.get_surface_count():
		var a := mesh.surface_get_arrays(s)
		var v: PackedVector3Array = a[Mesh.ARRAY_VERTEX]
		var cols = a[Mesh.ARRAY_COLOR]
		var ind = a[Mesh.ARRAY_INDEX]
		var tri_count: int = (ind.size() if ind is PackedInt32Array and ind.size() > 0 else v.size()) / 3
		for t in tri_count:
			var i0: int = ind[t * 3] if ind is PackedInt32Array and ind.size() > 0 else t * 3
			var i1: int = ind[t * 3 + 1] if ind is PackedInt32Array and ind.size() > 0 else t * 3 + 1
			var i2: int = ind[t * 3 + 2] if ind is PackedInt32Array and ind.size() > 0 else t * 3 + 2
			var pa := v[i0]
			var pb := v[i1]
			var pc := v[i2]
			var col: Color = cols[i0] if cols is PackedColorArray and cols.size() > i0 else Color(0.5, 0.5, 0.5)
			var edge := maxf(maxf(pa.distance_to(pb), pb.distance_to(pc)), pc.distance_to(pa))
			var n := clampi(int(ceil(edge / cell * 2.0)), 1, 24)
			for u in n + 1:
				for w in n + 1 - u:
					var p := pa + (pb - pa) * (float(u) / n) + (pc - pa) * (float(w) / n)
					var c := Vector3i(((p - origin) / cell).floor())
					var k: int = idx.call(c)
					solid[k] = 1
					cell_cols[k][col] = cell_cols[k].get(col, 0) + 1

	# 2. Flood the outside from the padding; whatever it can't reach is solid (fills canopies and rock interiors)
	var outside := PackedByteArray()
	outside.resize(count)
	var stack: Array[Vector3i] = [Vector3i.ZERO]
	outside[0] = 1
	var dirs: Array[Vector3i] = [Vector3i(1, 0, 0), Vector3i(-1, 0, 0), Vector3i(0, 1, 0), Vector3i(0, -1, 0),
			Vector3i(0, 0, 1), Vector3i(0, 0, -1)]
	while not stack.is_empty():
		var c: Vector3i = stack.pop_back()
		for d in dirs:
			var nc: Vector3i = c + d
			if nc.x < 0 or nc.y < 0 or nc.z < 0 or nc.x >= dims.x or nc.y >= dims.y or nc.z >= dims.z:
				continue
			var k: int = idx.call(nc)
			if outside[k] == 0 and solid[k] == 0:
				outside[k] = 1
				stack.append(nc)

	# 3. Cell colours: the part colour most of the cell's surface has (tree parts are flat-coloured, so neighbouring
	# faces share exact colours and merge below); enclosed cells take the mesh's most common colour
	var colour := PackedColorArray()
	colour.resize(count)
	var totals := {}
	for k in count:
		var best := -1
		for c: Color in cell_cols[k]:
			totals[c] = totals.get(c, 0) + cell_cols[k][c]
			if cell_cols[k][c] > best:
				best = cell_cols[k][c]
				colour[k] = c
	var common := Color(0.5, 0.5, 0.5)
	var most := -1
	for c: Color in totals:
		if totals[c] > most:
			most = totals[c]
			common = c
	for k in count:
		if solid[k] == 0 and outside[k] == 0:
			colour[k] = common

	# 4. Faces between solid and outside cells, greedily merged into rectangles of one colour per slice
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	for d in dirs:
		var a := 0 if d.x != 0 else (1 if d.y != 0 else 2) # slice axis; u, v span the slice
		var u := (a + 1) % 3
		var v := (a + 2) % 3
		var sign := d[a]
		for i in dims[a]:
			# Face mask for this slice: index into `colour`, or -1
			var mask := PackedInt32Array()
			mask.resize(dims[u] * dims[v])
			mask.fill(-1)
			for jv in dims[v]:
				for ju in dims[u]:
					var c := Vector3i()
					c[a] = i
					c[u] = ju
					c[v] = jv
					var k: int = idx.call(c)
					if outside[k] == 0 and outside[idx.call(c + d)] == 1: # padding keeps c + d in the grid
						mask[ju + jv * dims[u]] = k
			for jv in dims[v]:
				for ju in dims[u]:
					var k0 := mask[ju + jv * dims[u]]
					if k0 < 0:
						continue
					var col := colour[k0]
					var w := 1
					while ju + w < dims[u] and mask[ju + w + jv * dims[u]] >= 0 \
							and colour[mask[ju + w + jv * dims[u]]] == col:
						w += 1
					var h := 1
					var grow := true
					while grow and jv + h < dims[v]:
						for x in w:
							var m := mask[ju + x + (jv + h) * dims[u]]
							if m < 0 or colour[m] != col:
								grow = false
								break
						if grow:
							h += 1
					for y in h:
						for x in w:
							mask[ju + x + (jv + y) * dims[u]] = -1
					var corners: Array[Vector3] = []
					for uv in [Vector2i(0, 0), Vector2i(w, 0), Vector2i(w, h), Vector2i(0, h)]:
						var p := Vector3()
						p[a] = i + (1 if sign > 0 else 0)
						p[u] = ju + uv.x
						p[v] = jv + uv.y
						corners.append(origin + p * cell)
					# Godot front faces: (v1 - v0) x (v2 - v0) points into the solid (as in rock()); flip if not
					var inward := (corners[1] - corners[0]).cross(corners[2] - corners[0]).dot(Vector3(d)) < 0.0
					for t in ([0, 1, 2, 0, 2, 3] if inward else [0, 2, 1, 0, 3, 2]):
						verts.append(corners[t])
						normals.append(Vector3(d))
						colors.append(col)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	var out := ArrayMesh.new()
	out.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	out.surface_set_material(0, get_material(false))
	return out


## Placeholder for "nothing here": the instancer needs a non-null mesh for every LOD slot.
static func empty_mesh() -> ArrayMesh:
	if not _cache.has("empty"):
		_cache["empty"] = ArrayMesh.new()
	return _cache["empty"]


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


## Bakes a node that builds its own MeshInstance3D children on READY (EdenBushInstance, ...): each child's
## material_override albedo becomes its vertex colour.
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


## Faceted low-poly rock: a noise-displaced icosphere with a flat base, flat-shaded, each facet coloured on its own
## (base colour +- variation, moss on upward facets in noise patches, lichen spots). Boulders ~1 m across at scale 1;
## slabs are long and flat (cliff strata), spires tall, pebbles a scatter of small stones.
static func rock(layer: EdenFoliageLayer, variant: int) -> Dictionary:
	var K := EdenFoliageLayer.Kind
	var rng := RandomNumberGenerator.new()
	rng.seed = hash([layer.kind, variant, 7])
	var verts := PackedVector3Array()
	var colors := PackedColorArray()
	var stones := []
	if layer.kind == K.PEBBLES:
		for i in rng.randi_range(4, 7):
			var r := rng.randf_range(0.08, 0.22)
			var at := Vector3(rng.randf_range(-0.5, 0.5), 0.0, rng.randf_range(-0.5, 0.5))
			stones.append([at, Vector3(r, r * rng.randf_range(0.5, 0.8), r * rng.randf_range(0.8, 1.2)), 0])
	else:
		var s := layer.stretch
		if layer.kind == K.ROCK_SLAB:
			s *= Vector3(1.9, 0.55, 1.3)
		elif layer.kind == K.ROCK_SPIRE:
			s *= Vector3(0.45, 2.6, 0.45)
		s *= Vector3(rng.randf_range(0.85, 1.15), rng.randf_range(0.85, 1.15), rng.randf_range(0.85, 1.15))
		stones.append([Vector3.ZERO, s * 0.5, layer.facet_detail])
	for st in stones:
		_faceted_stone(rng, layer, st[0], st[1], st[2], verts, colors)
	var normals := PackedVector3Array()
	normals.resize(verts.size())
	for i in range(0, verts.size(), 3):
		var n := (verts[i + 2] - verts[i]).cross(verts[i + 1] - verts[i]).normalized()
		normals[i] = n
		normals[i + 1] = n
		normals[i + 2] = n
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.surface_set_material(0, get_material(false))
	var shape := []
	if layer.kind != K.PEBBLES:
		var aabb := mesh.get_aabb()
		var sphere := SphereShape3D.new()
		sphere.radius = minf(maxf(aabb.size.x, aabb.size.z), aabb.size.y * 1.6) * 0.42
		shape = [sphere, Transform3D(Basis(), aabb.get_center())]
	return {"mesh": mesh, "shape": shape}


static func _faceted_stone(rng: RandomNumberGenerator, layer: EdenFoliageLayer, at: Vector3, half: Vector3,
		detail: int, verts: PackedVector3Array, colors: PackedColorArray) -> void:
	var sphere := _icosphere(detail)
	var noise := FastNoiseLite.new()
	noise.seed = rng.randi()
	noise.frequency = 1.1 # broad planes: chiselled, not crumpled
	var patches := FastNoiseLite.new() # moss comes in patches, not per-facet confetti
	patches.seed = rng.randi()
	patches.frequency = 2.2
	var pts := PackedVector3Array()
	for p: Vector3 in sphere[0]:
		var d := 1.0 + noise.get_noise_3dv(p) * layer.roughness * 1.4
		var q := p * d * half
		q.y = maxf(q.y, -half.y * 0.45) # flat base: sits on the ground instead of rolling
		pts.append(q)
	var base_y := -half.y * 0.45 + half.y * 0.12 # sink the base a little
	for tri in sphere[1]:
		var a: Vector3 = pts[tri[0]]
		var b: Vector3 = pts[tri[1]]
		var c: Vector3 = pts[tri[2]]
		var n := (b - a).cross(c - a).normalized() # icosphere table is CCW from outside
		var center := (a + b + c) / 3.0
		var col := layer.rock_color * (1.0 + rng.randf_range(-layer.color_variation, layer.color_variation))
		col = col.lerp(Color(col.r * 1.05, col.g, col.b * 0.92), rng.randf() * 0.5) # warm/cool facet shifts
		var up := n.y
		var patch := patches.get_noise_3dv(center / maxf(half.length(), 0.01)) * 0.5 + 0.5
		if up > 0.25 and patch < layer.moss * (0.4 + up):
			col = layer.moss_color * rng.randf_range(0.85, 1.15)
		elif rng.randf() < layer.lichen:
			col = layer.lichen_color * rng.randf_range(0.85, 1.15)
		col.a = 1.0
		for v in [a, c, b]: # Godot front faces are clockwise
			verts.append(v + at + Vector3(0, -base_y, 0))
			colors.append(col)


## Unit icosphere: [vertices, triangles as [i, j, k]], subdivided `level` times.
static func _icosphere(level: int) -> Array:
	var t := (1.0 + sqrt(5.0)) / 2.0
	var v: Array[Vector3] = []
	for p in [Vector3(-1, t, 0), Vector3(1, t, 0), Vector3(-1, -t, 0), Vector3(1, -t, 0), Vector3(0, -1, t),
			Vector3(0, 1, t), Vector3(0, -1, -t), Vector3(0, 1, -t), Vector3(t, 0, -1), Vector3(t, 0, 1),
			Vector3(-t, 0, -1), Vector3(-t, 0, 1)]:
		v.append(p.normalized())
	var f := [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2],
			[10, 7, 6], [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11],
			[6, 2, 10], [8, 6, 7], [9, 8, 1]]
	for _l in level:
		var mid := {}
		var nf := []
		for tri in f:
			var m := []
			for e in 3:
				var i: int = tri[e]
				var j: int = tri[(e + 1) % 3]
				var k := Vector2i(mini(i, j), maxi(i, j))
				if not mid.has(k):
					mid[k] = v.size()
					v.append(((v[i] + v[j]) * 0.5).normalized())
				m.append(mid[k])
			nf.append([tri[0], m[0], m[2]])
			nf.append([tri[1], m[1], m[0]])
			nf.append([tri[2], m[2], m[1]])
			nf.append([m[0], m[1], m[2]])
		f = nf
	return [PackedVector3Array(v), f]


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


## Grass tuft: `blades` flat triangles, UV.y 0 at the base .. 1 at the tip (the grass shader's gradient and wind).
static func tuft(blades: int, material: Material) -> ArrayMesh:
	var rng := RandomNumberGenerator.new()
	rng.seed = 7 # every tuft is the same mesh; variety comes from instance scale/rotation
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in blades:
		var yaw := TAU * (float(i) + rng.randf() * 0.5) / blades
		var dir := Vector3(cos(yaw), 0.0, sin(yaw))
		var side := Vector3(-dir.z, 0.0, dir.x)
		var root := dir * rng.randf_range(0.0, 0.15)
		var half_w := rng.randf_range(0.04, 0.07)
		var tip := root + dir * rng.randf_range(0.08, 0.2) + Vector3.UP * rng.randf_range(0.35, 0.65)
		st.set_uv(Vector2(0, 0)); st.add_vertex(root - side * half_w)
		st.set_uv(Vector2(1, 0)); st.add_vertex(root + side * half_w)
		st.set_uv(Vector2(0.5, 1)); st.add_vertex(tip)
	st.set_material(material)
	return st.commit()


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
