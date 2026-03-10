## plate_tectonics.gd
## Grows tectonic plates via cost-function BFS,
## classifies plate boundaries (convergent/divergent/transform),
## and assigns elevation to all regions.

class_name PlateTectonics
extends RefCounted

# --------------------------------------------------------------------------- #
# Enums
# --------------------------------------------------------------------------- #

enum CrustType { CONTINENTAL = 0, OCEANIC = 1, UNASSIGNED = 2 }
enum BoundaryType { NONE = 0, CONVERGENT = 1, DIVERGENT = 2, TRANSFORM = 3 }

# --------------------------------------------------------------------------- #
# Public data  (parallel arrays indexed by region_id)
# --------------------------------------------------------------------------- #

var num_regions: int

## Which plate each region belongs to (-1 = unassigned)
var r_plate: PackedInt32Array

## CONTINENTAL or OCEANIC
var r_crust: PackedInt32Array

## Normalized elevation [-1, 1]
var r_elevation: PackedFloat32Array

## Whether this region sits on a plate boundary
var r_at_boundary: PackedByteArray

## Boundary type (BoundaryType enum)
var r_boundary_type: PackedInt32Array

## Plate data
var num_plates: int
var plate_is_continental: PackedByteArray  # 1 = continental, 0 = oceanic plate seed
var plate_move_vec: PackedFloat32Array     # 3 floats per plate, unit vector on sphere

# --------------------------------------------------------------------------- #
# References
# --------------------------------------------------------------------------- #

var _sphere: VoronoiSphere
var _rng:    RandomNumberGenerator
var _noise:  FastNoiseLite  # For cost function modulation

# --------------------------------------------------------------------------- #
# Parameters  (tune as needed)
# --------------------------------------------------------------------------- #

const LAND_FRACTION        := 0.30   # ~30% land coverage
const DISTANCE_SCORE_BIAS  := 0.5
const BEARING_SCORE_BIAS   := 0.5
const MAX_COST             := 2.0
const CONVERGENT_THRESHOLD := 0.3
const DIVERGENT_THRESHOLD  := -0.3

# Elevation values assigned by boundary type
const ELEV_MOUNTAIN        := 0.85
const ELEV_ISLAND_ARC      := 0.45
const ELEV_RIFT            := -0.35
const ELEV_MID_OCEAN_RIDGE := 0.05
const ELEV_TRENCH          := -0.7
const ELEV_COAST_BASE      := 0.05
const ELEV_OCEAN_BASE      := -0.4
const ELEV_LAND_BASE       := 0.15


# --------------------------------------------------------------------------- #
# Construction
# --------------------------------------------------------------------------- #

func _init(sphere: VoronoiSphere, p_num_plates: int, seed: int) -> void:
	_sphere     = sphere
	num_regions = sphere.num_regions
	num_plates  = p_num_plates

	_rng = RandomNumberGenerator.new()
	_rng.seed = seed

	_noise = FastNoiseLite.new()
	_noise.seed         = seed + 7777
	_noise.noise_type   = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	_noise.frequency    = 1.5   # high frequency on unit sphere = fine boundary variation

	# Allocate arrays
	r_plate        = PackedInt32Array(); r_plate.resize(num_regions);        r_plate.fill(-1)
	r_crust        = PackedInt32Array(); r_crust.resize(num_regions);        r_crust.fill(CrustType.UNASSIGNED)
	r_elevation    = PackedFloat32Array(); r_elevation.resize(num_regions);  r_elevation.fill(0.0)
	r_at_boundary  = PackedByteArray(); r_at_boundary.resize(num_regions);  r_at_boundary.fill(0)
	r_boundary_type= PackedInt32Array(); r_boundary_type.resize(num_regions); r_boundary_type.fill(BoundaryType.NONE)

	plate_is_continental = PackedByteArray(); plate_is_continental.resize(num_plates)
	plate_move_vec       = PackedFloat32Array(); plate_move_vec.resize(num_plates * 3)


# --------------------------------------------------------------------------- #
# Step 1: Grow plates
# --------------------------------------------------------------------------- #

func grow_plates() -> void:
	# Pick plate seed regions (random, no duplicates)
	var used := {}
	var plate_seeds := PackedInt32Array()
	plate_seeds.resize(num_plates)

	for p in num_plates:
		var r: int = _rng.randi() % num_regions
		while used.has(r):
			r = (r + 1) % num_regions
		used[r] = true
		plate_seeds[p] = r

		# ~30% of plates start as continental
		var is_cont: bool = (_rng.randf() < 0.35)
		plate_is_continental[p] = 1 if is_cont else 0

		# Random movement vector (tangent to sphere at seed, normalized)
		var seed_pos := _sphere.get_region_pos(r)
		var rand_vec := Vector3(_rng.randf_range(-1, 1), _rng.randf_range(-1, 1), _rng.randf_range(-1, 1)).normalized()
		# Project onto tangent plane
		rand_vec = (rand_vec - seed_pos * rand_vec.dot(seed_pos)).normalized()
		plate_move_vec[p * 3 + 0] = rand_vec.x
		plate_move_vec[p * 3 + 1] = rand_vec.y
		plate_move_vec[p * 3 + 2] = rand_vec.z

		# Assign seed region
		r_plate[r] = p
		r_crust[r] = CrustType.CONTINENTAL if is_cont else CrustType.OCEANIC

	# ---------- PASS 1: Continental crust BFS ----------
	var land_target := int(num_regions * LAND_FRACTION)
	_bfs_grow(plate_seeds, CrustType.CONTINENTAL, land_target, 1.0)

	# ---------- PASS 2: Oceanic crust — expand remaining surface ----------
	# Re-seed from all boundary continental regions
	var ocean_seeds := PackedInt32Array()
	for r in num_regions:
		if r_crust[r] == CrustType.CONTINENTAL:
			for nb in (_sphere.region_neighbors[r] as PackedInt32Array):
				if r_crust[nb] == CrustType.UNASSIGNED:
					if not ocean_seeds.has(r):
						ocean_seeds.append(r)
					break

	_bfs_grow(ocean_seeds, CrustType.OCEANIC, num_regions, 0.5)  # lower cost penalty

	# Any still-unassigned regions → nearest plate
	for r in num_regions:
		if r_crust[r] == CrustType.UNASSIGNED:
			var best_nb := -1
			var best_d  := 1e38
			for nb in (_sphere.region_neighbors[r] as PackedInt32Array):
				if r_plate[nb] != -1:
					var d := _sphere.region_angular_distance(r, nb)
					if d < best_d:
						best_d  = d
						best_nb = nb
			if best_nb != -1:
				r_plate[r] = r_plate[best_nb]
				r_crust[r] = CrustType.OCEANIC


func _bfs_grow(seeds: PackedInt32Array, crust_type: int, target_count: int, cost_scale: float) -> void:
	# Priority queue implemented as a sorted array of [cost, region_id]
	# For ~20k regions this is acceptable; can be replaced with a heap for large N
	var queue: Array = []

	for r in seeds:
		if r_crust[r] == CrustType.UNASSIGNED and crust_type != CrustType.CONTINENTAL:
			pass  # Don't re-seed from assigned regions unless ocean pass
		queue.append([0.0, r])

	var assigned_count := 0
	for r in num_regions:
		if r_plate[r] != -1:
			assigned_count += 1

	while queue.size() > 0 and assigned_count < target_count:
		# Pop cheapest (sort once every N iterations for performance)
		queue.sort_custom(func(a, b): return a[0] < b[0])
		var item: Array = queue.pop_front() as Array
		var cost  := item[0] as float
		var cur_r := item[1] as int

		if r_crust[cur_r] != CrustType.UNASSIGNED:
			continue
		if r_plate[cur_r] == -1:
			continue

		# Mark this region
		r_crust[cur_r] = crust_type
		assigned_count += 1

		var plate : int = r_plate[cur_r]
		var plate_seed_r := _find_plate_seed(plate)
		var plate_pos := _sphere.get_region_pos(plate_seed_r)
		var mv: Vector3 = Vector3(plate_move_vec[plate * 3], plate_move_vec[plate * 3 + 1], plate_move_vec[plate * 3 + 2])

		# Expand to unassigned neighbors
		for nb in (_sphere.region_neighbors[cur_r] as PackedInt32Array):
			if r_crust[nb] != CrustType.UNASSIGNED:
				continue
			if r_plate[nb] != -1:
				continue

			# Assign this neighbor to current plate
			r_plate[nb] = plate

			# Compute cost
			var nb_pos := _sphere.get_region_pos(nb)
			var dist_score: float = _sphere.region_angular_distance(plate_seed_r, nb) / PI  # [0,1]
			var bearing := (nb_pos - plate_pos).normalized()
			var bearing_score: float = 1.0 - max(0.0, bearing.dot(mv))  # 0=aligned, 1=opposed
			var noise_val: float = (_noise.get_noise_3d(nb_pos.x, nb_pos.y, nb_pos.z) + 1.0) * 0.5  # [0,1]
			var c: float = (dist_score * DISTANCE_SCORE_BIAS + bearing_score * BEARING_SCORE_BIAS) * (1.0 - noise_val * 0.4) * cost_scale
			queue.append([c, nb])


func _find_plate_seed(plate: int) -> int:
	# Just return first region we find for this plate (approximate)
	for r in num_regions:
		if r_plate[r] == plate and r_crust[r] != CrustType.UNASSIGNED:
			return r
	return 0


# --------------------------------------------------------------------------- #
# Step 2: Classify boundaries
# --------------------------------------------------------------------------- #

func classify_boundaries() -> void:
	for r in num_regions:
		for nb in (_sphere.region_neighbors[r] as PackedInt32Array):
			if r_plate[nb] == r_plate[r]:
				continue
			# This pair spans a plate boundary
			r_at_boundary[r] = 1
			r_at_boundary[nb] = 1

			var p1 : int = r_plate[r]
			var p2 : int = r_plate[nb]

			# Movement vectors for each plate
			var mv1: Vector3 = Vector3(plate_move_vec[p1 * 3], plate_move_vec[p1 * 3 + 1], plate_move_vec[p1 * 3 + 2])
			var mv2: Vector3 = Vector3(plate_move_vec[p2 * 3], plate_move_vec[p2 * 3 + 1], plate_move_vec[p2 * 3 + 2])

			# Boundary normal: direction from r toward nb
			var pos_r  := _sphere.get_region_pos(r)
			var pos_nb := _sphere.get_region_pos(nb)
			var boundary_normal := (pos_nb - pos_r).normalized()

			# How much do the plates' movements push toward / away from boundary?
			var approach: float = mv1.dot(boundary_normal) - mv2.dot(boundary_normal)

			var btype: int
			if approach > CONVERGENT_THRESHOLD:
				btype = BoundaryType.CONVERGENT
			elif approach < DIVERGENT_THRESHOLD:
				btype = BoundaryType.DIVERGENT
			else:
				btype = BoundaryType.TRANSFORM

			# Only upgrade (CONVERGENT > DIVERGENT > TRANSFORM > NONE)
			if btype > r_boundary_type[r]:   r_boundary_type[r] = btype
			if btype > r_boundary_type[nb]:  r_boundary_type[nb] = btype


# --------------------------------------------------------------------------- #
# Step 3: Assign elevation
# --------------------------------------------------------------------------- #

func assign_elevation() -> void:
	# --- 3a. Set boundary elevation ---
	for r in num_regions:
		if r_at_boundary[r] == 0:
			continue

		var crust : int = r_crust[r]
		var btype : int = r_boundary_type[r]

		# Find neighbor's crust type across boundary
		var other_crust := crust  # default
		for nb in (_sphere.region_neighbors[r] as PackedInt32Array):
			if r_plate[nb] != r_plate[r]:
				other_crust = r_crust[nb]
				break

		match btype:
			BoundaryType.CONVERGENT:
				if crust == CrustType.CONTINENTAL and other_crust == CrustType.CONTINENTAL:
					r_elevation[r] = ELEV_MOUNTAIN
				elif crust == CrustType.CONTINENTAL and other_crust == CrustType.OCEANIC:
					r_elevation[r] = ELEV_MOUNTAIN
				elif crust == CrustType.OCEANIC and other_crust == CrustType.CONTINENTAL:
					r_elevation[r] = ELEV_TRENCH
				else:  # ocean + ocean
					r_elevation[r] = ELEV_ISLAND_ARC
			BoundaryType.DIVERGENT:
				if crust == CrustType.CONTINENTAL:
					r_elevation[r] = ELEV_RIFT
				else:
					r_elevation[r] = ELEV_MID_OCEAN_RIDGE
			BoundaryType.TRANSFORM:
				# Slight noise variation — leave at plate base
				r_elevation[r] = ELEV_LAND_BASE if crust == CrustType.CONTINENTAL else ELEV_OCEAN_BASE

	# --- 3b. Propagate elevation to interior via BFS distance field ---
	# Interior regions inherit boundary elevations weighted by inverse distance,
	# then blend toward the plate's base elevation with distance.
	var elevation_sum   := PackedFloat32Array(); elevation_sum.resize(num_regions);   elevation_sum.fill(0.0)
	var elevation_weight := PackedFloat32Array(); elevation_weight.resize(num_regions); elevation_weight.fill(0.0)

	# Seed from boundary regions
	var queue: Array = []
	for r in num_regions:
		if r_at_boundary[r] == 1:
			elevation_sum[r]    = r_elevation[r]
			elevation_weight[r] = 10.0  # high weight = boundary controls strongly
			queue.append(r)

	# BFS outward from boundaries
	var visited := PackedByteArray(); visited.resize(num_regions); visited.fill(0)
	for r in queue:
		visited[r] = 1

	var qi := 0
	while qi < queue.size():
		var cur : int = queue[qi]; qi += 1
		for nb in (_sphere.region_neighbors[cur] as PackedInt32Array):
			var w : float = elevation_weight[cur] * 0.7  # weight decays with distance
			if w < 0.01:
				continue
			elevation_weight[nb] += w
			elevation_sum[nb]    += r_elevation[cur] * w
			if visited[nb] == 0:
				visited[nb] = 1
				queue.append(nb)

	# --- 3c. Set final elevation ---
	var noise2 := FastNoiseLite.new()
	noise2.seed       = _rng.randi()
	noise2.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise2.frequency  = 2.0
	noise2.fractal_octaves = 4

	for r in num_regions:
		var pos := _sphere.get_region_pos(r)
		var base_elev: float
		if elevation_weight[r] > 0.0:
			base_elev = elevation_sum[r] / elevation_weight[r]
		else:
			base_elev = ELEV_LAND_BASE if r_crust[r] == CrustType.CONTINENTAL else ELEV_OCEAN_BASE

		# Add fractal noise (±0.15 amplitude)
		var n: float = noise2.get_noise_3d(pos.x, pos.y, pos.z) * 0.15

		r_elevation[r] = clamp(base_elev + n, -1.0, 1.0)