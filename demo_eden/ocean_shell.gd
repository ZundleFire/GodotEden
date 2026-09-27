@tool
extends Node3D
## Global background "ocean shell": 6 cubed-sphere quadtrees (one per cube face), CPU-built
## ArrayMesh patches, LOD-split by camera distance -- modeled on how the UE5 Marketplace
## plugin "Planetar" structures its own ocean (PlanetaryOceanQuadtree.cpp): 6 independent
## per-face quadtrees, a flat-cube-subdivide-then-normalize-onto-sphere projection, and a
## simple `dist < size*factor && size/2 > min_size` split rule (Planetar's own heuristic,
## not screen-space error). Mesh geometry is fully (re)built on leaf creation via
## ArrayMesh.add_surface_from_arrays -- no GPU tessellation, matching Planetar's own
## CPU-regenerated-on-LOD-change approach.
##
## This is the far-field/background ocean layer only. VoxelWaterSimulator
## (modules/voxel/water/voxel_water_simulator.h/.cpp) continues to own all near-field water
## detail (flooding, rivers, coastline shape) close to the player; see near_field_radius
## below for the handoff between the two systems.
##
## Mesh-only: this node builds and LOD-splits the quadtree geometry and nothing else. Shading
## (FFT waves, foam, refraction, all of it) was pulled out entirely -- the shader-driven day/night
## lighting on this near-mirror surface kept coming out wrong (root cause chased through several
## rounds: sun_lit gating, then Sky radiance convergence, neither fully explained what was seen in
## the editor) and rebuilding it blind was costing more than it was worth. `material` below is a
## plain exported slot -- assign any Material in the inspector and every leaf uses it via
## material_override. Leaves have no UV2/tangents, only ARRAY_VERTEX/ARRAY_NORMAL/ARRAY_TEX_UV.
##
## Known, deliberate simplifications (see task scope notes -- these are expected, not bugs):
##  - Seam/crack hiding uses unconditional per-leaf "skirts": every leaf drops a curtain of
##    extra triangles along all 4 edges toward the planet center (see compute_down_dir's
##    convention in voxel_water_simulator.cpp, `normalize(planet_center - point)` -- skirts
##    drop along the negation of the vertex's own outward sphere normal, i.e. that same
##    "down" direction), rather than Planetar's exact neighbor-bitmask edge-vertex-matching.
##    This is simpler and always-on (a small fixed geometry cost per leaf) and reliably hides
##    T-junction cracks at any LOD boundary without needing cross-face neighbor lookups.
##  - Near-field suppression is a flat camera-distance radius (near_field_radius), not exact
##    VoxelWaterSimulator.get_wet_block_positions_at_lod() coverage querying.
##  - The quadtree split heuristic's "world size" is an approximation: cube-face parametric
##    extent is treated as scaling linearly into world arc length. It isn't exact (cube-to-
##    sphere projection stretches non-uniformly toward face corners vs. face centers), but
##    matches Planetar's own simple size/distance heuristic rather than true screen-space
##    error, and is good enough for a background ocean layer.

const GRID_RES := 8 # quads per leaf edge (per axis) -- (GRID_RES+1)^2 vertices/leaf before skirts
const MAX_DEPTH := 9

# [normal, axis_u, axis_v] per cube face, chosen so axis_u x axis_v == normal (consistent
# winding/orientation across faces) -- see class doc comment. Winding doesn't actually matter
# for visibility since planet_water.gdshader sets render_mode cull_disabled, but keeping it
# consistent avoids surprises if the material ever changes.
const FACE_BASES := [
	[Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0)],
	[Vector3(-1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0)],
	[Vector3(0, 1, 0), Vector3(1, 0, 0), Vector3(0, 0, -1)],
	[Vector3(0, -1, 0), Vector3(1, 0, 0), Vector3(0, 0, 1)],
	[Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0)],
	[Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 1, 0)],
]

@export var planet_radius: float = 20000.0
@export var planet_center: Vector3 = Vector3.ZERO
# Stop splitting a leaf once its approximate world size drops below this -- the effective
# "finest" patch size near the camera.
@export var min_leaf_world_size: float = 250.0
# Planetar's own split constant: a node splits when camera distance < world_size * this factor.
@export var split_distance_factor: float = 2.0
# Leaves whose world-space center is closer than this to the camera are hidden entirely --
# the (deliberately simple) handoff to VoxelWaterSimulator's near-field water; see class doc
# comment. Should comfortably cover VoxelWaterSimulator's own simulated near-field radius
# (WATER_SIM_MAX_SIM_LOD tiers) plus VoxelViewer's view_distance, with margin.
@export var near_field_radius: float = 500.0
# Throttle: how often (seconds) the quadtree is re-walked for split/merge decisions. Leaf
# meshes themselves are only (re)built on leaf creation, not every rebuild pass -- wave motion
# is handled entirely by the shader's vertex displacement (TIME-driven), not by regenerating
# geometry every frame.
@export var rebuild_interval: float = 0.35
# Skirt depth as a fraction of the leaf's own approximate world size.
@export var skirt_depth_ratio: float = 0.15
# Assign any Material here; every leaf mesh uses it via material_override. Empty means Godot's
# default white StandardMaterial3D.
@export var material: Material:
	set(value):
		material = value
		_apply_material()

var _faces: Array = [] # Array[QuadNode], one root per cube face
var _rebuild_accum := 0.0


class QuadNode:
	var face: int
	var center: Vector2 # cube-face-local uv, each axis in [-1, 1]
	var half_size: float
	var depth: int
	var children: Array = [] # Array[QuadNode], empty when this node is a leaf
	var mesh_instance: MeshInstance3D = null

	func _init(p_face: int, p_center: Vector2, p_half_size: float, p_depth: int) -> void:
		face = p_face
		center = p_center
		half_size = p_half_size
		depth = p_depth


func _ready() -> void:
	for i in range(6):
		_faces.append(QuadNode.new(i, Vector2.ZERO, 1.0, 0))


# Applies `material` to every leaf already built (new leaves pick it up in _update_leaf).
func _apply_material() -> void:
	for root in _faces:
		_apply_material_to_node(root)


func _apply_material_to_node(node: QuadNode) -> void:
	if node.mesh_instance != null:
		node.mesh_instance.material_override = material
	for child in node.children:
		_apply_material_to_node(child)


func _process(delta: float) -> void:
	_rebuild_accum += delta
	if _rebuild_accum < rebuild_interval:
		return
	_rebuild_accum = 0.0

	var cam_pos := _reference_camera_position()

	for root in _faces:
		_update_node(root, cam_pos)


## A running game always has a current Camera3D to LOD-split around; the editor's static 3D view
## (this script is @tool -- see class doc comment -- specifically so a mesh exists there at all,
## the same way CloudShell/EdenParentPlanet/etc. are visible without pressing Play) usually does
## not, since atmosphere_demo.tscn creates its FlyCamera from script at runtime rather than saving
## one in the scene. Falls back to the editor's own 3D viewport camera, then to a fixed point
## above the planet, so leaves still build under either circumstance.
func _reference_camera_position() -> Vector3:
	var cam := get_viewport().get_camera_3d()
	if cam != null:
		return cam.global_position
	if Engine.is_editor_hint():
		var editor_cam := EditorInterface.get_editor_viewport_3d(0).get_camera_3d()
		if editor_cam != null:
			return editor_cam.global_position
	return planet_center + Vector3.UP * (planet_radius + 250.0)


func _cube_point(face: int, u: float, v: float) -> Vector3:
	var basis: Array = FACE_BASES[face]
	var normal: Vector3 = basis[0]
	var axis_u: Vector3 = basis[1]
	var axis_v: Vector3 = basis[2]
	return normal + axis_u * u + axis_v * v


# Returns [world_position: Vector3, sphere_normal: Vector3] for cube-face point (face, u, v),
# projected onto the sphere via normalize-and-scale (the same flat-cube-subdivide-then-
# normalize technique Planetar's own PlanetaryOceanQuadtree uses).
func _sphere_pos_and_normal(face: int, u: float, v: float) -> Array:
	var cube_pt := _cube_point(face, u, v)
	var n := cube_pt.normalized()
	return [planet_center + n * planet_radius, n]


func _node_world_center(node: QuadNode) -> Vector3:
	var pn := _sphere_pos_and_normal(node.face, node.center.x, node.center.y)
	return pn[0]


func _node_world_size(node: QuadNode) -> float:
	# Approximation only -- see class doc comment.
	return node.half_size * 2.0 * planet_radius


func _update_node(node: QuadNode, cam_pos: Vector3) -> void:
	var world_center := _node_world_center(node)
	var world_size := _node_world_size(node)
	var dist := cam_pos.distance_to(world_center)
	var should_split := dist < world_size * split_distance_factor \
			and world_size * 0.5 > min_leaf_world_size \
			and node.depth < MAX_DEPTH

	if should_split:
		if node.children.is_empty():
			_split(node)
		for child in node.children:
			_update_node(child, cam_pos)
		return

	if not node.children.is_empty():
		_merge(node)

	_update_leaf(node, cam_pos, dist)


func _split(node: QuadNode) -> void:
	_free_leaf_mesh(node)
	var h := node.half_size * 0.5
	for dx in [-1.0, 1.0]:
		for dy in [-1.0, 1.0]:
			var c := Vector2(node.center.x + dx * h, node.center.y + dy * h)
			node.children.append(QuadNode.new(node.face, c, h, node.depth + 1))


func _merge(node: QuadNode) -> void:
	for child in node.children:
		if not child.children.is_empty():
			_merge(child)
		_free_leaf_mesh(child)
	node.children.clear()


func _free_leaf_mesh(node: QuadNode) -> void:
	if node.mesh_instance != null:
		node.mesh_instance.queue_free()
		node.mesh_instance = null


func _update_leaf(node: QuadNode, cam_pos: Vector3, dist: float) -> void:
	var suppressed := dist < near_field_radius
	if suppressed:
		if node.mesh_instance != null:
			node.mesh_instance.visible = false
		return

	if node.mesh_instance == null:
		var mi := MeshInstance3D.new()
		mi.material_override = material
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(mi)
		node.mesh_instance = mi
		_build_leaf_mesh(node)
	else:
		node.mesh_instance.visible = true


# Builds a (GRID_RES+1)^2 grid ArrayMesh for `node`'s cube-face patch, spherified per-vertex,
# plus unconditional skirts on all 4 edges (see class doc comment). Vertices are stored
# relative to the patch's own world center (mesh_instance.global_position = that center) to
# keep vertex-local coordinates small regardless of planet_radius.
func _build_leaf_mesh(node: QuadNode) -> void:
	var res := GRID_RES
	var origin := _node_world_center(node)
	# Capped at min_leaf_world_size -- an unsplit coarse/distant patch's world_size can be a
	# large fraction of the whole planet (e.g. an entire un-split cube face), and skirt_depth
	# scaling directly off that produced skirts thousands of units deep dominating the screen
	# with degenerate, coarsely-tessellated (GRID_RES=8 across the whole patch) alpha-blended
	# geometry -- confirmed via screenshot (severe banding, visible even over open sky).
	var skirt_depth: float = min(_node_world_size(node), min_leaf_world_size) * skirt_depth_ratio

	var positions := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var grid_index := {} # Vector2i(i, j) -> vertex index

	for j in range(res + 1):
		for i in range(res + 1):
			var u: float = node.center.x - node.half_size + (2.0 * node.half_size) * float(i) / float(res)
			var v: float = node.center.y - node.half_size + (2.0 * node.half_size) * float(j) / float(res)
			var pn := _sphere_pos_and_normal(node.face, u, v)
			var world_pos: Vector3 = pn[0]
			var n: Vector3 = pn[1]
			positions.append(world_pos - origin)
			normals.append(n)
			uvs.append(Vector2(float(i) / float(res), float(j) / float(res)))
			grid_index[Vector2i(i, j)] = positions.size() - 1

	var indices := PackedInt32Array()
	for j in range(res):
		for i in range(res):
			var a: int = grid_index[Vector2i(i, j)]
			var b: int = grid_index[Vector2i(i + 1, j)]
			var c: int = grid_index[Vector2i(i + 1, j + 1)]
			var d: int = grid_index[Vector2i(i, j + 1)]
			# Reversed from the "expected" (a,b,c)/(a,c,d) order -- verified empirically this
			# session that this face-basis parametrization's un-reversed winding is backwards
			# for Godot's front-face convention (mesh was only visible looking up from inside
			# the planet, i.e. from the back side).
			indices.append(a); indices.append(c); indices.append(b)
			indices.append(a); indices.append(d); indices.append(c)

	# Skirts: unconditional dropped-vertex curtain along each of the 4 edges, connected back
	# to the edge with a triangle strip -- hides T-junction cracks against any differently-
	# leveled neighbor patch without needing cross-tree/cross-face neighbor lookups.
	var edges := [
		{"along_i": true, "fixed": 0},        # left   (i == 0)
		{"along_i": true, "fixed": res},      # right  (i == res)
		{"along_i": false, "fixed": 0},       # bottom (j == 0)
		{"along_i": false, "fixed": res},     # top    (j == res)
	]
	for edge in edges:
		var along_i: bool = edge["along_i"]
		var fixed: int = edge["fixed"]
		var prev_skirt_idx := -1
		var prev_edge_idx := -1
		for step in range(res + 1):
			var gi: Vector2i = Vector2i(fixed, step) if along_i else Vector2i(step, fixed)
			var edge_idx: int = grid_index[gi]
			var edge_pos: Vector3 = positions[edge_idx]
			var n: Vector3 = normals[edge_idx]
			var skirt_pos: Vector3 = edge_pos - n * skirt_depth # drop toward planet center
			positions.append(skirt_pos)
			normals.append(n)
			uvs.append(Vector2.ZERO)
			var skirt_idx := positions.size() - 1
			if prev_skirt_idx != -1:
				# Reversed to match the main grid's winding fix above.
				indices.append(prev_edge_idx); indices.append(skirt_idx); indices.append(edge_idx)
				indices.append(prev_edge_idx); indices.append(prev_skirt_idx); indices.append(skirt_idx)
			prev_edge_idx = edge_idx
			prev_skirt_idx = skirt_idx

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = positions
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_INDEX] = indices

	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	node.mesh_instance.mesh = mesh
	node.mesh_instance.global_position = origin


# Total leaf-mesh-instance count currently in the tree (debug/telemetry only).
func get_leaf_count() -> int:
	var count := 0
	for root in _faces:
		count += _count_leaves(root)
	return count


func _count_leaves(node: QuadNode) -> int:
	if node.children.is_empty():
		return 1 if node.mesh_instance != null else 0
	var count := 0
	for child in node.children:
		count += _count_leaves(child)
	return count
