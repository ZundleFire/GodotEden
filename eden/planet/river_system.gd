## river_system.gd
## Binary-tree river network built on top of the Voronoi region graph.
## Uses downhill BFS/flow accumulation to carve rivers into the elevation field.
## Rivers always flow from high elevation toward low (ocean).
## Data layout: parallel arrays indexed by region_id (same as other planet files).

class_name RiverSystem
extends RefCounted

# --------------------------------------------------------------------------- #
# Public data
# --------------------------------------------------------------------------- #

## Parent array: r_parent[r] = region_id of the region this one flows into.
## -1 = no parent (ocean outlet or isolated).
var r_parent       : PackedInt32Array

## Accumulated upstream "catchment" count (number of upstream regions draining through here).
## High values → large river.
var r_flow         : PackedInt32Array

## Normalised flow strength [0, 1].  Used by generator to carve river channels.
var r_flow_norm    : PackedFloat32Array

## Whether this region carries a notable river (flow above threshold).
var r_is_river     : PackedByteArray

# --------------------------------------------------------------------------- #
# Internals
# --------------------------------------------------------------------------- #

var _sphere    : VoronoiSphere
var _tectonics : PlateTectonics
var _rng       : RandomNumberGenerator
var _noise     : FastNoiseLite

const RIVER_FLOW_THRESHOLD := 8     # minimum flow to be considered a river
const RIVER_CARVE_DEPTH    := 0.06  # how much to lower elevation at river cells
const SEA_LEVEL            := 0.0


func _init(sphere: VoronoiSphere, tectonics: PlateTectonics, seed: int) -> void:
	_sphere    = sphere
	_tectonics = tectonics

	_rng = RandomNumberGenerator.new()
	_rng.seed = seed

	_noise = FastNoiseLite.new()
	_noise.seed       = seed + 9988
	_noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	_noise.frequency  = 3.0

	var n: int = sphere.num_regions
	r_parent    = PackedInt32Array();    r_parent.resize(n);    r_parent.fill(-1)
	r_flow      = PackedInt32Array();    r_flow.resize(n);      r_flow.fill(1)  # each region counts itself
	r_flow_norm = PackedFloat32Array();  r_flow_norm.resize(n); r_flow_norm.fill(0.0)
	r_is_river  = PackedByteArray();     r_is_river.resize(n);  r_is_river.fill(0)


# --------------------------------------------------------------------------- #
# Public entry point
# --------------------------------------------------------------------------- #

func build_rivers() -> void:
	_build_flow_tree()
	_accumulate_flow()
	_normalise_flow()
	_carve_channels()


# --------------------------------------------------------------------------- #
# Step 1: Build downhill spanning tree
# Each land region drains into its lowest-elevation neighbor.
# Ocean regions are sinks (r_parent stays -1).
# --------------------------------------------------------------------------- #

func _build_flow_tree() -> void:
	for r in _sphere.num_regions:
		var elev : float = _tectonics.r_elevation[r]

		# Ocean regions are sinks
		if elev <= SEA_LEVEL:
			r_parent[r] = -1
			continue

		# Add tiny noise to break flat ties (prevents closed loops on plateaus)
		var pos := _sphere.get_region_pos(r)
		var noise_offset: float = _noise.get_noise_3d(pos.x, pos.y, pos.z) * 0.002

		var best_nb    := -1
		var best_elev  := elev + noise_offset  # must be lower than self

		for nb in (_sphere.region_neighbors[r] as PackedInt32Array):
			var nb_elev : float = _tectonics.r_elevation[nb]
			if nb_elev < best_elev:
				best_elev = nb_elev
				best_nb   = nb

		r_parent[r] = best_nb  # may be -1 if no lower neighbor (isolated peak → sink)


# --------------------------------------------------------------------------- #
# Step 2: Accumulate flow downstream (BFS in topological order)
# We sort regions by elevation descending (sources first), then propagate flow.
# --------------------------------------------------------------------------- #

func _accumulate_flow() -> void:
	var n: int = _sphere.num_regions

	# Build elevation-sorted order (descending = headwaters first)
	var order := Array()
	order.resize(n)
	for i in n:
		order[i] = i
	order.sort_custom(func(a, b): return _tectonics.r_elevation[a] > _tectonics.r_elevation[b])

	# Propagate
	for r in order:
		var p : int = r_parent[r]
		if p != -1:
			r_flow[p] += r_flow[r]


# --------------------------------------------------------------------------- #
# Step 3: Normalise
# --------------------------------------------------------------------------- #

func _normalise_flow() -> void:
	var max_flow: int = 1
	for r in _sphere.num_regions:
		if r_flow[r] > max_flow:
			max_flow = r_flow[r]

	var inv := 1.0 / float(max_flow)
	for r in _sphere.num_regions:
		r_flow_norm[r] = float(r_flow[r]) * inv
		if r_flow[r] >= RIVER_FLOW_THRESHOLD:
			r_is_river[r] = 1


# --------------------------------------------------------------------------- #
# Step 4: Carve river channels into elevation
# Regions that carry rivers are slightly depressed to create visible valleys.
# Depth is proportional to log(flow).
# --------------------------------------------------------------------------- #

func _carve_channels() -> void:
	for r in _sphere.num_regions:
		if r_is_river[r] == 0:
			continue
		var depth : int = RIVER_CARVE_DEPTH * log(float(r_flow[r])) / log(float(_sphere.num_regions))
		_tectonics.r_elevation[r] = max(SEA_LEVEL + 0.001, _tectonics.r_elevation[r] - depth)


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #

## Returns the normalised flow strength at region r (0=dry, 1=largest river).
func get_flow(r: int) -> float:
	return r_flow_norm[r]

## Traces the river path upstream from region r (up to max_steps hops).
func trace_upstream(r: int, max_steps: int = 200) -> PackedInt32Array:
	var path := PackedInt32Array()
	var cur   := r
	var steps: int = 0
	while cur != -1 and steps < max_steps:
		path.append(cur)
		# Find the highest-flow upstream child (reverse-tree traversal)
		var best_child: int = -1
		var best_flow  := 0
		for nb in (_sphere.region_neighbors[cur] as PackedInt32Array):
			if r_parent[nb] == cur and r_flow[nb] > best_flow:
				best_flow  = r_flow[nb]
				best_child = nb
		cur = best_child
		steps += 1
	return path