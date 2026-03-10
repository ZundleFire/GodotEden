## voronoi_sphere.gd
## Builds a Voronoi region graph on a unit sphere.
## Uses Fibonacci sphere distribution + jitter for seed points,
## then computes neighbour relationships via nearest-point BFS expansion.
## Provides O(1) approximate spatial lookup via a bucket grid.
##
## All positions are on the UNIT sphere. Scale by planet_radius at query time.

class_name VoronoiSphere
extends RefCounted

# --------------------------------------------------------------------------- #
# Public data — populated after build()
# --------------------------------------------------------------------------- #

## Number of regions
var num_regions: int = 0

## Region centre positions on unit sphere (packed x0,y0,z0, x1,y1,z1, ...)
var region_pos: PackedFloat32Array  # length = num_regions * 3

## Neighbour lists. region_neighbors[r] = PackedInt32Array of neighbor region ids
var region_neighbors: Array  # Array[PackedInt32Array]

## Spatial bucket grid for fast lookup (see _build_bucket_grid)
var _buckets: Array           # Array[PackedInt32Array], indexed by bucket id
var _bucket_count_phi: int    # number of buckets along azimuth
var _bucket_count_theta: int  # number of buckets along elevation

# --------------------------------------------------------------------------- #
# Constants
# --------------------------------------------------------------------------- #
const GOLDEN_RATIO := 1.6180339887498948482
const TAU         := 6.2831853071795864769

# --------------------------------------------------------------------------- #
# Build
# --------------------------------------------------------------------------- #

func build(p_num_regions: int, seed: int, jitter: float = 0.45) -> void:
	num_regions = p_num_regions
	var rng := RandomNumberGenerator.new()
	rng.seed = seed

	# --- 1. Fibonacci sphere + jitter ---
	region_pos = PackedFloat32Array()
	region_pos.resize(num_regions * 3)

	for i in num_regions:
		# Fibonacci sphere point
		var theta := acos(1.0 - 2.0 * (i + 0.5) / num_regions)  # [0, PI]
		var phi   := TAU * i / GOLDEN_RATIO                        # [0, TAU)

		# Convert to cartesian
		var x := sin(theta) * cos(phi)
		var y := cos(theta)
		var z := sin(theta) * sin(phi)

		# Add jitter: random small rotation
		if jitter > 0.0:
			var jx := rng.randf_range(-1.0, 1.0) * jitter
			var jy := rng.randf_range(-1.0, 1.0) * jitter
			var jz := rng.randf_range(-1.0, 1.0) * jitter
			x += jx; y += jy; z += jz
			var len := sqrt(x * x + y * y + z * z)
			x /= len; y /= len; z /= len

		region_pos[i * 3 + 0] = x
		region_pos[i * 3 + 1] = y
		region_pos[i * 3 + 2] = z

	# --- 2. Build spatial bucket grid ---
	_build_bucket_grid()

	# --- 3. Build neighbor graph via closest-point expansion ---
	_build_neighbors()


# --------------------------------------------------------------------------- #
# Spatial bucket grid  (equirectangular bucketing on sphere)
# --------------------------------------------------------------------------- #

func _build_bucket_grid() -> void:
	# Choose bucket counts so each bucket covers ~sqrt(4pi/N) steradians
	_bucket_count_theta = max(4, int(sqrt(float(num_regions) * 0.5)))
	_bucket_count_phi   = _bucket_count_theta * 2
	var total_buckets   := _bucket_count_theta * _bucket_count_phi

	_buckets = []
	_buckets.resize(total_buckets)
	for b in total_buckets:
		_buckets[b] = PackedInt32Array()

	for i in num_regions:
		var bid := _bucket_id_for_region(i)
		(_buckets[bid] as PackedInt32Array).append(i)


func _bucket_id_for_region(r: int) -> int:
	var x : float = region_pos[r * 3 + 0]
	var y : float = region_pos[r * 3 + 1]
	var z : float = region_pos[r * 3 + 2]
	return _bucket_id_for_xyz(x, y, z)


func _bucket_id_for_xyz(x: float, y: float, z: float) -> int:
	var theta := acos(clamp(y, -1.0, 1.0))  # [0, PI]
	var phi   := atan2(z, x)                 # [-PI, PI]
	if phi < 0.0:
		phi += TAU

	var ti : int = clampi(int(theta / PI * _bucket_count_theta), 0, _bucket_count_theta - 1)
	var pi_ : int = clampi(int(phi / TAU * _bucket_count_phi), 0, _bucket_count_phi - 1)
	return ti * _bucket_count_phi + pi_


## Returns the index of the Voronoi region whose centre is closest to the given
## unit-sphere direction vector. O(1) amortised (checks current bucket + 8 neighbors).
func find_nearest_region(dir: Vector3) -> int:
	var x := dir.x; var y := dir.y; var z := dir.z
	var theta := acos(clamp(y, -1.0, 1.0))
	var phi   := atan2(z, x)
	if phi < 0.0:
		phi += TAU

	var ti : int = clampi(int(theta / PI * _bucket_count_theta), 0, _bucket_count_theta - 1)
	var pi_ : int = clampi(int(phi / TAU * _bucket_count_phi), 0, _bucket_count_phi - 1)

	var best_r    := -1
	var best_dist := 1e38

	# Check 3x3 bucket neighborhood
	for dt in range(-1, 2):
		var nt := ti + dt
		if nt < 0 or nt >= _bucket_count_theta:
			continue
		for dp in range(-1, 2):
			var np : int = (pi_ + dp + _bucket_count_phi) % _bucket_count_phi
			var bid := nt * _bucket_count_phi + np
			for r in (_buckets[bid] as PackedInt32Array):
				var rx : float = region_pos[r * 3 + 0]
				var ry : float = region_pos[r * 3 + 1]
				var rz : float = region_pos[r * 3 + 2]
				var dx := x - rx; var dy := y - ry; var dz := z - rz
				var d  := dx * dx + dy * dy + dz * dz
				if d < best_dist:
					best_dist = d
					best_r    = r

	# Fallback: linear scan if bucket neighborhood gave nothing
	if best_r == -1:
		for r in num_regions:
			var rx : float = region_pos[r * 3 + 0]
			var ry : float = region_pos[r * 3 + 1]
			var rz : float = region_pos[r * 3 + 2]
			var dx := x - rx; var dy := y - ry; var dz := z - rz
			var d  := dx * dx + dy * dy + dz * dz
			if d < best_dist:
				best_dist = d
				best_r    = r

	return best_r


# --------------------------------------------------------------------------- #
# Neighbor graph
# --------------------------------------------------------------------------- #

## For each region, find the ~6 nearest other regions and call them neighbors.
## We do this by checking all regions in nearby buckets and keeping the closest K.
func _build_neighbors(k_neighbors: int = 8) -> void:
	region_neighbors = []
	region_neighbors.resize(num_regions)

	for r in num_regions:
		var x : float = region_pos[r * 3 + 0]
		var y : float = region_pos[r * 3 + 1]
		var z : float = region_pos[r * 3 + 2]

		var theta := acos(clamp(y, -1.0, 1.0))
		var phi   := atan2(z, x)
		if phi < 0.0:
			phi += TAU

		var ti : int = clampi(int(theta / PI * _bucket_count_theta), 0, _bucket_count_theta - 1)
		var pi_ : int = clampi(int(phi / TAU * _bucket_count_phi), 0, _bucket_count_phi - 1)

		# Collect candidates from 3x3 bucket neighborhood
		var candidates: Array = []
		for dt in range(-2, 3):
			var nt := ti + dt
			if nt < 0 or nt >= _bucket_count_theta:
				continue
			for dp in range(-2, 3):
				var np : int = (pi_ + dp + _bucket_count_phi) % _bucket_count_phi
				var bid := nt * _bucket_count_phi + np
				for c in (_buckets[bid] as PackedInt32Array):
					if c == r:
						continue
					var cx : float = region_pos[c * 3 + 0]
					var cy : float = region_pos[c * 3 + 1]
					var cz : float = region_pos[c * 3 + 2]
					var dx := x - cx; var dy := y - cy; var dz := z - cz
					candidates.append([dx * dx + dy * dy + dz * dz, c])

		candidates.sort_custom(func(a, b): return a[0] < b[0])

		var nbrs := PackedInt32Array()
		var count : float = min(k_neighbors, candidates.size())
		for i in count:
			nbrs.append(candidates[i][1])
		region_neighbors[r] = nbrs


## Get position of region r as a Vector3
func get_region_pos(r: int) -> Vector3:
	return Vector3(
		region_pos[r * 3 + 0],
		region_pos[r * 3 + 1],
		region_pos[r * 3 + 2]
	)


## Spherical great-circle distance between two region centers (radians)
func region_angular_distance(a: int, b: int) -> float:
	var ax: float = region_pos[a * 3 + 0]; var ay: float = region_pos[a * 3 + 1]; var az: float = region_pos[a * 3 + 2]
	var bx: float = region_pos[b * 3 + 0]; var by: float = region_pos[b * 3 + 1]; var bz: float = region_pos[b * 3 + 2]
	var dot : float = clamp(ax * bx + ay * by + az * bz, -1.0, 1.0)
	return acos(dot)