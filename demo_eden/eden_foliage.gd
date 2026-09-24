@tool
class_name EdenFoliage
extends VoxelInstancer
## Low-poly GPU foliage for a VoxelLodTerrain planet (EdenPlanetGeneratorV4): grass, biome trees, bushes, ground
## scatter (pebbles, rocks, boulders, fallen branches and logs) and rocks on cliffs, all MultiMesh.
## VoxelLodTerrain allows only ONE VoxelInstancer, so every layer is an item in this node's library.
##
## Biomes come from the terrain's surface data (mesher surface_data_enabled): each layer keeps a temperature and
## moisture range (VoxelInstanceGenerator surface filter) plus the MIXEL4 materials it grows on. Climate values follow
## latitude on the V4 planet: ~0.9 equator, ~0.75 subtropics, ~0.4 mid latitudes, ~0.15 subarctic, ~0 poles.
##
## The library is generated, never saved: it is detached around editor saves and rebuilt whenever an export changes.
## Trunks, logs, boulders and cliff rocks collide near the camera (not in the editor); plants sway, with wind (and
## grass itself, at its range edge) fading out with camera distance.
##
## ponytail: GDScript prototype. Port to an eden_foliage C++ node once settled.

const MAT_GRASS := 0
const MAT_ROCK := 1
const MAT_SNOW := 2
const MAT_SAND := 3
const MAT_DIRT := 4
const MAT_MOSS := 5
const VEGETATED := [MAT_GRASS, MAT_DIRT, MAT_MOSS]

## Spawn foliage in the editor viewport too (it follows the editor camera).
@export var show_in_editor := true:
	set(v):
		show_in_editor = v
		_rebuild()

@export_group("Grass")
@export var grass_enabled := true:
	set(v):
		grass_enabled = v
		_rebuild()
## Tufts per m² on lush grass. Only LOD0 terrain chunks get grass.
@export var grass_density := 8.0:
	set(v):
		grass_density = v
		_rebuild()
@export var blades_per_tuft := 7:
	set(v):
		blades_per_tuft = v
		_rebuild()
## Keep below the terrain shader's rock slope (~37°).
@export var grass_max_slope_degrees := 35.0:
	set(v):
		grass_max_slope_degrees = v
		_rebuild()
## Grass shrinks into the ground between these camera distances (m), so the range edge doesn't pop.
@export var grass_fade_start := 45.0:
	set(v):
		grass_fade_start = v
		_update_materials()
@export var grass_fade_end := 60.0:
	set(v):
		grass_fade_end = v
		_rebuild() # also moves the chunk cutoff
## Grass wind stops between these camera distances (m).
@export var grass_wind_fade_start := 20.0:
	set(v):
		grass_wind_fade_start = v
		_update_materials()
@export var grass_wind_fade_end := 35.0:
	set(v):
		grass_wind_fade_end = v
		_update_materials()

@export_group("Trees")
@export var trees_enabled := true:
	set(v):
		trees_enabled = v
		_rebuild()
## Trees per m² (LOD1 faces) for a biome's main species; others are scaled from it.
@export var tree_density := 0.012:
	set(v):
		tree_density = v
		_rebuild()
## Pre-built shapes per species; each is one draw call per chunk.
@export var tree_variants := 3:
	set(v):
		tree_variants = v
		_rebuild()
@export var tree_max_slope_degrees := 30.0:
	set(v):
		tree_max_slope_degrees = v
		_rebuild()
## Terrain LOD whose chunks trees spawn on (0 = only within the full-detail range).
@export_range(0, 3) var tree_lod := 1:
	set(v):
		tree_lod = v
		_rebuild()
## Rough size of a forest clump (m); trees thin out between clumps.
@export var forest_patch_size := 150.0:
	set(v):
		forest_patch_size = v
		_rebuild()
## Trunk/log/rock colliders exist only within this camera distance (m).
@export var collision_distance := 64.0:
	set(v):
		collision_distance = v
		_rebuild()
@export var tree_wind_fade_start := 60.0:
	set(v):
		tree_wind_fade_start = v
		_update_materials()
@export var tree_wind_fade_end := 90.0:
	set(v):
		tree_wind_fade_end = v
		_update_materials()

@export_group("Bushes & Scatter")
@export var bushes_enabled := true:
	set(v):
		bushes_enabled = v
		_rebuild()
## Pebbles, rocks, boulders, fallen branches and logs.
@export var ground_scatter_enabled := true:
	set(v):
		ground_scatter_enabled = v
		_rebuild()
## Multiplies every bush and ground scatter density.
@export var scatter_density := 1.0:
	set(v):
		scatter_density = v
		_rebuild()
## Boulders and spires set into steep rock (cliffs, mountain faces).
@export var cliff_rocks_enabled := true:
	set(v):
		cliff_rocks_enabled = v
		_rebuild()
@export var cliff_rock_density := 1.0:
	set(v):
		cliff_rock_density = v
		_rebuild()

var _grass_materials: Array[ShaderMaterial] = []


func _ready() -> void:
	up_mode = VoxelInstancer.UP_MODE_SPHERE
	if library == null: # a script may have assigned one before adding the node
		_rebuild()


func _notification(what: int) -> void:
	# Keep the generated library (meshes, shapes) out of the saved scene
	if what == NOTIFICATION_EDITOR_PRE_SAVE:
		library = null
	elif what == NOTIFICATION_EDITOR_POST_SAVE:
		_rebuild()


func _rebuild() -> void:
	if not is_inside_tree():
		return # setters fire while the scene loads; _ready builds once
	if Engine.is_editor_hint() and not show_in_editor:
		# Empty, not null: a VoxelInstancer without a library logs an assertion error every frame
		library = VoxelInstanceLibrary.new()
		return
	library = build_library()


# Layer ids are fixed per layer (the id seeds instance placement, so toggling one layer must not move the others):
# grass 0-9, trees 10-59 (species base + variant), bushes 60-89, ground scatter 90-119, cliffs 120-139.
func build_library() -> VoxelInstanceLibrary:
	var lib := VoxelInstanceLibrary.new()
	_grass_materials.clear()
	var summer := EdenTreeShape.SEASON_SUMMER

	if grass_enabled:
		# Lush and dry grass split on moisture; dry grass also covers the warm steppe
		lib.add_item(0, _grass_item("grass_lush", grass_density, Vector2(0.12, 1.0), Vector2(0.35, 1.0),
				Color(0.16, 0.33, 0.11), Color(0.32, 0.54, 0.20)))
		lib.add_item(1, _grass_item("grass_dry", grass_density * 0.6, Vector2(0.45, 1.0), Vector2(0.0, 0.38),
				Color(0.36, 0.33, 0.14), Color(0.66, 0.58, 0.30)))

	if trees_enabled:
		# [id base, EdenTreeShape type, density weight, temperature, moisture, materials]
		var species := [
			[10, EdenTreeShape.TREE_OAK, 0.6, Vector2(0.42, 0.8), Vector2(0.35, 1.0), VEGETATED],
			[15, EdenTreeShape.TREE_BIRCH, 0.5, Vector2(0.25, 0.6), Vector2(0.4, 1.0), VEGETATED],
			[20, EdenTreeShape.TREE_PINE, 1.0, Vector2(0.06, 0.45), Vector2(0.25, 1.0), VEGETATED + [MAT_SNOW]],
			[25, EdenTreeShape.TREE_WILLOW, 0.3, Vector2(0.4, 0.85), Vector2(0.72, 1.0), VEGETATED],
			[30, EdenTreeShape.TREE_PALM, 0.5, Vector2(0.72, 1.0), Vector2(0.45, 1.0), VEGETATED + [MAT_SAND]],
			[35, EdenTreeShape.TREE_FRUIT, 0.25, Vector2(0.55, 0.9), Vector2(0.45, 1.0), VEGETATED],
			[40, EdenTreeShape.TREE_DEAD, 0.08, Vector2(0.45, 1.0), Vector2(0.0, 0.32), VEGETATED + [MAT_SAND]], # dry savanna
			[45, EdenTreeShape.TREE_DEAD, 0.1, Vector2(0.0, 0.14), Vector2(0.0, 1.0), VEGETATED + [MAT_SNOW]], # tundra edge
		]
		for sp in species:
			for v in tree_variants:
				var t := EdenFoliageMeshes.tree(sp[1], summer, v)
				var g := _gen(tree_density * sp[2] / tree_variants, sp[3], sp[4], sp[5], 0.0, tree_max_slope_degrees)
				g.min_scale = 0.8
				g.max_scale = 1.3
				_add_clumping(g, sp[0] + v)
				var trunk := CapsuleShape3D.new()
				trunk.radius = t.trunk_radius
				trunk.height = maxf(t.trunk_height, trunk.radius * 2.0)
				lib.add_item(sp[0] + v, _item("tree_%d_%d" % [sp[1], v], t.mesh, g, tree_lod,
						[trunk, Transform3D(Basis(), Vector3(0, trunk.height * 0.5, 0))]))

	if bushes_enabled:
		# [id base, bush type, variants, density, temperature, moisture, materials, draw distance m]
		# Every variant is a draw call per chunk, so small things get few variants and a short draw distance
		var bushes := [
			[60, EdenBushInstance.BUSH_SHRUB, 2, 0.02, Vector2(0.28, 0.85), Vector2(0.3, 1.0), VEGETATED, 110.0],
			[65, EdenBushInstance.BUSH_FERN, 1, 0.03, Vector2(0.35, 1.0), Vector2(0.6, 1.0), [MAT_GRASS, MAT_MOSS], 70.0],
			[70, EdenBushInstance.BUSH_BERRY, 1, 0.006, Vector2(0.35, 0.7), Vector2(0.4, 1.0), VEGETATED, 90.0],
			[75, EdenBushInstance.BUSH_DEAD, 2, 0.01, Vector2(0.0, 1.0), Vector2(0.0, 0.35), VEGETATED + [MAT_SAND], 110.0],
			[80, EdenBushInstance.BUSH_CACTUS, 1, 0.004, Vector2(0.7, 1.0), Vector2(0.0, 0.35), [MAT_SAND, MAT_DIRT, MAT_GRASS], 150.0],
		]
		for bu in bushes:
			for v in bu[2]:
				var g := _gen(bu[3] * scatter_density / bu[2], bu[4], bu[5], bu[6], 0.0, 35.0)
				g.min_scale = 0.7
				g.max_scale = 1.3
				lib.add_item(bu[0] + v, _item("bush_%d_%d" % [bu[1], v], EdenFoliageMeshes.bush(bu[1], summer, v), g, 0,
						[], bu[7]))

	if ground_scatter_enabled:
		var anywhere := Vector2(0.0, 1.0)
		var forest_t := Vector2(0.06, 0.85)
		var forest_m := Vector2(0.3, 1.0)
		var g := _gen(0.02 * scatter_density, anywhere, anywhere, VEGETATED + [MAT_SAND, MAT_ROCK], 0.0, 45.0)
		g.min_scale = 0.3
		g.max_scale = 0.8
		lib.add_item(90, _item("pebbles", EdenFoliageMeshes.rock(EdenRockInstance.ROCK_PEBBLES, 0), g, 0, [], 50.0))
		for v in 2:
			g = _gen(0.003 * scatter_density / 2, anywhere, anywhere, VEGETATED + [MAT_ROCK, MAT_SNOW], 0.0, 40.0)
			g.min_scale = 0.6
			g.max_scale = 1.5
			g.offset_along_normal = -0.1
			lib.add_item(95 + v, _item("rocks_%d" % v, EdenFoliageMeshes.rock(EdenRockInstance.ROCK_CLUSTER, v), g, 0,
					[], 90.0))
		for v in 3:
			var mesh := EdenFoliageMeshes.rock(EdenRockInstance.ROCK_BOULDER, v)
			g = _gen(0.0004 * scatter_density / 3, anywhere, anywhere, VEGETATED + [MAT_ROCK, MAT_SNOW, MAT_SAND], 0.0, 40.0)
			g.min_scale = 1.0
			g.max_scale = 3.0
			g.offset_along_normal = -0.3
			lib.add_item(100 + v, _item("boulder_%d" % v, mesh, g, 1, _rock_shape(mesh)))
		for v in 2:
			g = _gen(0.012 * scatter_density / 2, forest_t, forest_m, VEGETATED, 0.0, 30.0)
			g.vertical_alignment = 0.2 # lie along the ground
			lib.add_item(105 + v, _item("branch_%d" % v, EdenFoliageMeshes.wood(false, v), g, 0, [], 60.0))
		for v in 2:
			var mesh := EdenFoliageMeshes.wood(true, v)
			g = _gen(0.002 * scatter_density / 2, forest_t, forest_m, VEGETATED, 0.0, 25.0)
			g.vertical_alignment = 0.2
			var log_shape := CapsuleShape3D.new()
			log_shape.radius = 0.28
			log_shape.height = 4.0
			lib.add_item(110 + v, _item("log_%d" % v, mesh, g, 0,
					[log_shape, Transform3D(Basis(Vector3.BACK, PI * 0.5), Vector3(0, 0.25, 0))], 150.0))

	if cliff_rocks_enabled:
		# Set into steep rock faces: aligned with the face normal and partly buried, so they read as outcrops
		var anywhere := Vector2(0.0, 1.0)
		for v in 3:
			var mesh := EdenFoliageMeshes.rock(EdenRockInstance.ROCK_BOULDER, 10 + v)
			var g := _gen(0.0003 * cliff_rock_density / 3, anywhere, anywhere, [], 35.0, 90.0)
			g.min_scale = 4.0
			g.max_scale = 12.0
			g.vertical_alignment = 0.0
			g.offset_along_normal = -1.2
			lib.add_item(120 + v, _item("cliff_boulder_%d" % v, mesh, g, 1, _rock_shape(mesh)))
		for v in 2:
			var mesh := EdenFoliageMeshes.rock(EdenRockInstance.ROCK_SPIRE, 10 + v)
			var g := _gen(0.0003 * cliff_rock_density / 2, anywhere, anywhere, [], 30.0, 80.0)
			g.min_scale = 3.0
			g.max_scale = 7.0
			g.vertical_alignment = 0.3
			g.offset_along_normal = -1.0
			lib.add_item(125 + v, _item("cliff_spire_%d" % v, mesh, g, 1, _rock_shape(mesh)))
		for v in 2:
			var mesh := EdenFoliageMeshes.rock(EdenRockInstance.ROCK_CLUSTER, 10 + v)
			var g := _gen(0.001 * cliff_rock_density / 2, anywhere, anywhere, [MAT_ROCK, MAT_SNOW], 0.0, 35.0)
			g.min_scale = 2.5
			g.max_scale = 5.0
			g.offset_along_normal = -0.6
			lib.add_item(130 + v, _item("mountain_rocks_%d" % v, mesh, g, 1, _rock_shape(mesh)))

	_update_materials()
	return lib


func _gen(density: float, temperature: Vector2, moisture: Vector2, materials: Array, min_slope: float,
		max_slope: float) -> VoxelInstanceGenerator:
	var g := VoxelInstanceGenerator.new()
	g.density = density
	# Area-based (per m²). FACES_FAST is per triangle, and mesh optimization makes flat ground few big triangles
	g.emit_mode = VoxelInstanceGenerator.EMIT_FROM_FACES
	g.random_rotation = true
	g.min_slope_degrees = min_slope
	g.max_slope_degrees = max_slope
	g.max_slope_falloff_degrees = 5.0
	# Empty = any material. Cliffs need it: V4 marks rock by altitude, steep faces are rock only in the shader
	g.voxel_texture_filter_enabled = not materials.is_empty()
	g.voxel_texture_filter_array = PackedInt32Array(materials)
	g.surface_filter_enabled = true
	g.temperature_range = temperature
	g.moisture_range = moisture
	return g


# Forest clumps: a noise field thins trees between patches instead of a uniform sprinkle
func _add_clumping(g: VoxelInstanceGenerator, noise_seed: int) -> void:
	var noise := FastNoiseLite.new()
	noise.seed = 4000 + noise_seed / 5 # shared by a species' variants
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.frequency = 1.0 / maxf(forest_patch_size, 1.0)
	g.noise = noise
	g.noise_dimension = VoxelInstanceGenerator.DIMENSION_3D
	g.noise_threshold = -0.1
	g.noise_falloff = 0.4


func _item(item_name: String, mesh: Mesh, gen: VoxelInstanceGenerator, lod: int,
		shapes: Array = [], draw_distance := 0.0) -> VoxelInstanceLibraryMultiMeshItem:
	var item := VoxelInstanceLibraryMultiMeshItem.new()
	item.name = item_name
	item.lod_index = lod
	item.generator = gen
	item.persistent = false
	item.mesh = mesh
	if not shapes.is_empty() and not Engine.is_editor_hint(): # no physics in the editor: bodies only cost time
		item.collision_shapes = shapes
		item.collision_distance = collision_distance
	if draw_distance > 0.0:
		_limit_draw_distance(item, draw_distance)
	return item


# Stops drawing whole chunks whose centre is beyond `meters` (plus half a chunk diagonal, so chunks straddling the
# limit still draw). The instancer measures it as a ratio of the terrain's distance for the item's LOD.
func _limit_draw_distance(item: VoxelInstanceLibraryMultiMeshItem, meters: float) -> void:
	var terrain := get_parent() as VoxelLodTerrain
	if terrain == null:
		return
	# VoxelLodTerrain::get_lod_distances: legacy octree doubles per LOD, clipbox adds the secondary distance
	var lod := item.lod_index
	var lod_range := terrain.lod_distance * (1 << lod)
	if terrain.streaming_system != 0:
		lod_range = terrain.lod_distance + (terrain.secondary_lod_distance * (1 << lod) if lod > 0 else 0.0)
	var margin := terrain.mesh_block_size * (1 << lod) * 0.87
	item.hide_beyond_max_lod = true
	# Whole-array setter: the per-LOD one clamps LOD0 to the LOD1 default (0.35)
	var r := minf((meters + margin) / lod_range, 2.0)
	item.set("_mesh_lod_distance_ratios", PackedFloat32Array([r, 2.0, 2.0, 2.0]))


# Sphere a bit inside the rock's bounds (they're lumpy); scales with the instance
func _rock_shape(mesh: Mesh) -> Array:
	var aabb := mesh.get_aabb()
	var s := SphereShape3D.new()
	s.radius = maxf(aabb.size.x, aabb.size.z) * 0.4
	return [s, Transform3D(Basis(), aabb.get_center())]


func _update_materials() -> void:
	for m in _grass_materials:
		m.set_shader_parameter("u_fade_start", grass_fade_start)
		m.set_shader_parameter("u_fade_end", grass_fade_end)
		m.set_shader_parameter("u_wind_fade_start", grass_wind_fade_start)
		m.set_shader_parameter("u_wind_fade_end", grass_wind_fade_end)
	var tm := EdenFoliageMeshes.get_material(true) as ShaderMaterial
	tm.set_shader_parameter("u_wind_fade_start", tree_wind_fade_start)
	tm.set_shader_parameter("u_wind_fade_end", tree_wind_fade_end)


func _grass_item(item_name: String, density: float, temperature: Vector2, moisture: Vector2, base: Color,
		tip: Color) -> VoxelInstanceLibraryMultiMeshItem:
	var gen := _gen(density, temperature, moisture, [MAT_GRASS], 0.0, grass_max_slope_degrees)
	gen.min_scale = 0.7
	gen.max_scale = 1.4
	gen.vertical_alignment = 0.6 # lean partway into the slope, like the reference
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/eden_grass_lowpoly.gdshader")
	mat.set_shader_parameter("u_base_color", Vector3(base.r, base.g, base.b))
	mat.set_shader_parameter("u_tip_color", Vector3(tip.r, tip.g, tip.b))
	_grass_materials.append(mat)

	# Chunks past the fade stop drawing (the shader already shrank their tufts to nothing)
	var item := _item(item_name, _build_tuft(mat), gen, 0, [], grass_fade_end)
	item.cast_shadow = RenderingServer.SHADOW_CASTING_SETTING_OFF
	return item


func _build_tuft(mat: Material) -> ArrayMesh:
	var rng := RandomNumberGenerator.new()
	rng.seed = 7 # fixed so every tuft is the same mesh; variety comes from instance scale/rotation
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in blades_per_tuft:
		var yaw := TAU * (float(i) + rng.randf() * 0.5) / blades_per_tuft
		var dir := Vector3(cos(yaw), 0.0, sin(yaw))
		var side := Vector3(-dir.z, 0.0, dir.x)
		var root := dir * rng.randf_range(0.0, 0.15)
		var half_w := rng.randf_range(0.04, 0.07)
		var tip := root + dir * rng.randf_range(0.08, 0.2) + Vector3.UP * rng.randf_range(0.35, 0.65)
		st.set_uv(Vector2(0, 0)); st.add_vertex(root - side * half_w)
		st.set_uv(Vector2(1, 0)); st.add_vertex(root + side * half_w)
		st.set_uv(Vector2(0.5, 1)); st.add_vertex(tip)
	st.set_material(mat)
	return st.commit()
