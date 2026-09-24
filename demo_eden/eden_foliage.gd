@tool
class_name EdenFoliage
extends VoxelInstancer
## Low-poly GPU foliage for a VoxelLodTerrain planet: grass tufts + procedural trees, all
## MultiMesh. VoxelLodTerrain allows only ONE VoxelInstancer, so every foliage layer is an item in
## this node's library rather than its own instancer.
##
## The library is generated, never saved: it is detached around editor saves and rebuilt whenever
## an export changes. Grass has no collision; trees get a trunk capsule near the camera.
## Wind (and grass itself, at the edge of its range) fades out with distance from the camera.
##
## ponytail: GDScript prototype. Port to an eden_foliage C++ node once settled.

const MAT_GRASS := 0
const MAT_DIRT := 4

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
## Tufts per m². Only LOD0 terrain chunks get grass.
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
## Per species, per m² on LOD1 faces.
@export var tree_density := 0.02:
	set(v):
		tree_density = v
		_rebuild()
## Pre-built shapes per species; each is one draw call per chunk.
@export var tree_variants := 4:
	set(v):
		tree_variants = v
		_rebuild()
@export var tree_max_slope_degrees := 30.0:
	set(v):
		tree_max_slope_degrees = v
		_rebuild()
## Trunk colliders exist only for trees within this camera distance (m).
@export var tree_collision_distance := 64.0:
	set(v):
		tree_collision_distance = v
		_rebuild()
@export var tree_wind_fade_start := 60.0:
	set(v):
		tree_wind_fade_start = v
		_update_materials()
@export var tree_wind_fade_end := 90.0:
	set(v):
		tree_wind_fade_end = v
		_update_materials()

var _grass_material: ShaderMaterial


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


func build_library() -> VoxelInstanceLibrary:
	var lib := VoxelInstanceLibrary.new()
	# Fixed ids: the id seeds instance placement, so toggling a layer must not move the others
	if grass_enabled:
		lib.add_item(0, _grass_item())
	var id := 1
	if trees_enabled:
		for tree_type in [EdenTreeShape.TREE_OAK, EdenTreeShape.TREE_BIRCH, EdenTreeShape.TREE_PINE]:
			var g := VoxelInstanceGenerator.new()
			g.density = tree_density
			g.emit_mode = VoxelInstanceGenerator.EMIT_FROM_FACES_FAST
			g.max_slope_degrees = tree_max_slope_degrees
			g.voxel_texture_filter_enabled = true
			g.voxel_texture_filter_array = PackedInt32Array([MAT_GRASS, MAT_DIRT])
			g.random_rotation = true
			g.min_scale = 0.8
			g.max_scale = 1.3
			for item in EdenTreeMultiMesh.build_items(tree_type, EdenTreeShape.SEASON_SUMMER, tree_variants, g):
				item.collision_distance = tree_collision_distance
				if Engine.is_editor_hint():
					item.collision_shapes = [] # no physics in the editor: bodies only cost main-thread time
				lib.add_item(id, item)
				id += 1
	_update_materials()
	return lib


func _update_materials() -> void:
	if _grass_material:
		_grass_material.set_shader_parameter("u_fade_start", grass_fade_start)
		_grass_material.set_shader_parameter("u_fade_end", grass_fade_end)
		_grass_material.set_shader_parameter("u_wind_fade_start", grass_wind_fade_start)
		_grass_material.set_shader_parameter("u_wind_fade_end", grass_wind_fade_end)
	var tm := EdenTreeMultiMesh.get_material()
	tm.set_shader_parameter("u_wind_fade_start", tree_wind_fade_start)
	tm.set_shader_parameter("u_wind_fade_end", tree_wind_fade_end)


func _grass_item() -> VoxelInstanceLibraryMultiMeshItem:
	var gen := VoxelInstanceGenerator.new()
	gen.emit_mode = VoxelInstanceGenerator.EMIT_FROM_FACES
	gen.density = grass_density
	gen.min_scale = 0.7
	gen.max_scale = 1.4
	gen.random_rotation = true
	gen.vertical_alignment = 0.6 # lean partway into the slope, like the reference
	gen.max_slope_degrees = grass_max_slope_degrees
	gen.max_slope_falloff_degrees = 5.0
	gen.voxel_texture_filter_enabled = true
	gen.voxel_texture_filter_array = PackedInt32Array([MAT_GRASS])

	var item := VoxelInstanceLibraryMultiMeshItem.new()
	item.name = "grass"
	item.lod_index = 0
	item.generator = gen
	item.persistent = false
	item.mesh = _build_tuft()
	item.cast_shadow = RenderingServer.SHADOW_CASTING_SETTING_OFF
	# No collision_shapes: grass never gets physics bodies
	# Stop drawing whole chunks past the fade (the shader already shrank their tufts to nothing).
	# The ratio is of the terrain's LOD0 distance; the margin keeps chunks straddling the fade drawn.
	var terrain := get_parent() as VoxelLodTerrain
	if terrain:
		var margin := terrain.mesh_block_size * 0.87 # half a chunk diagonal
		item.hide_beyond_max_lod = true
		# Whole-array setter: the per-LOD one clamps LOD0 to the LOD1 default (0.35)
		var r := minf((grass_fade_end + margin) / terrain.lod_distance, 2.0)
		item.set("_mesh_lod_distance_ratios", PackedFloat32Array([r, 2.0, 2.0, 2.0]))
	return item


func _build_tuft() -> ArrayMesh:
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
	_grass_material = ShaderMaterial.new()
	_grass_material.shader = load("res://shaders/eden_grass_lowpoly.gdshader")
	st.set_material(_grass_material)
	return st.commit()
