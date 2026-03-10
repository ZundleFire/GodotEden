## planet_data.gd
## Immutable snapshot of all per-region planet data produced by PlanetPreGenerator.
## This is what PlanetGenerator queries at runtime for each voxel block.
## All arrays are parallel and indexed by region_id (0..num_regions-1).

class_name PlanetData
extends Resource

# --------------------------------------------------------------------------- #
# Sphere geometry
# --------------------------------------------------------------------------- #

## Number of Voronoi regions
@export var num_regions: int = 0

## Planet radius in metres (voxels).  Default 40 km.
@export var planet_radius: float = 40000.0

## Packed xyz positions of region centroids on the unit sphere.
## Access: Vector3(region_pos[r*3], region_pos[r*3+1], region_pos[r*3+2])
@export var region_pos: PackedFloat32Array

## Neighbor adjacency list — Array of PackedInt32Array, one per region.
@export var region_neighbors: Array  # Array[PackedInt32Array]

# --------------------------------------------------------------------------- #
# Tectonic data
# --------------------------------------------------------------------------- #

## Plate index per region
@export var r_plate: PackedInt32Array

## Crust type: 0=CONTINENTAL, 1=OCEANIC
@export var r_crust: PackedInt32Array

## Normalised elevation [-1, 1]
@export var r_elevation: PackedFloat32Array

## Boundary type: 0=NONE, 1=CONVERGENT, 2=DIVERGENT, 3=TRANSFORM
@export var r_boundary_type: PackedInt32Array

# --------------------------------------------------------------------------- #
# Climate data
# --------------------------------------------------------------------------- #

## Temperature [0=arctic, 1=tropical]
@export var r_temperature: PackedFloat32Array

## Moisture [0=arid, 1=humid]
@export var r_moisture: PackedFloat32Array

# --------------------------------------------------------------------------- #
# Biome data
# --------------------------------------------------------------------------- #

## BiomeClassifier.Biome enum value per region
@export var r_biome: PackedInt32Array

## Terrain shader texture slot (0..5) per region
@export var r_tex_slot: PackedInt32Array

# --------------------------------------------------------------------------- #
# River data
# --------------------------------------------------------------------------- #

## Flow accumulation normalised [0, 1]
@export var r_flow_norm: PackedFloat32Array

## 1 if this region carries a river
@export var r_is_river: PackedByteArray

# --------------------------------------------------------------------------- #
# Spatial lookup (equirectangular bucket grid mirrored from VoronoiSphere)
# --------------------------------------------------------------------------- #

## Bucket grid dimensions (cols x rows)
@export var bucket_cols: int = 0
@export var bucket_rows: int = 0

## Flat bucket -> region list.  Packed as:
## bucket_offsets[bucket_idx]..bucket_offsets[bucket_idx+1] into bucket_region_ids.
@export var bucket_offsets   : PackedInt32Array   # length = bucket_cols*bucket_rows + 1
@export var bucket_region_ids: PackedInt32Array   # all region ids, sorted by bucket

# --------------------------------------------------------------------------- #
# Runtime query
# --------------------------------------------------------------------------- #

## Find the nearest Voronoi region for a direction vector (unit vector from planet centre).
## This is an O(1) lookup via the bucket grid.
func find_nearest_region(dir: Vector3) -> int:
	if bucket_cols == 0 or bucket_rows == 0:
		return _find_nearest_linear(dir)

	# Map dir -> (u, v) equirectangular in [0, 1)
	var u: float = atan2(dir.z, dir.x) / TAU + 0.5
	var v: float = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5

	var col0: int = int(u * bucket_cols)
	var row0: int = int(v * bucket_rows)

	var best_r  : int   = 0
	var best_dot: float = -2.0

	for dc: int in [-1, 0, 1]:
		for dr: int in [-1, 0, 1]:
			var bcol: int = (col0 + dc + bucket_cols) % bucket_cols
			var brow: int = clampi(row0 + dr, 0, bucket_rows - 1)
			var bi  : int = brow * bucket_cols + bcol

			var bstart: int = bucket_offsets[bi]
			var bend  : int = bucket_offsets[bi + 1]
			for i: int in range(bstart, bend):
				var r  : int   = bucket_region_ids[i]
				var rx : float = region_pos[r * 3 + 0]
				var ry : float = region_pos[r * 3 + 1]
				var rz : float = region_pos[r * 3 + 2]
				var d  : float = dir.x * rx + dir.y * ry + dir.z * rz
				if d > best_dot:
					best_dot = d
					best_r   = r

	return best_r


func _find_nearest_linear(dir: Vector3) -> int:
	var best_r  : int   = 0
	var best_dot: float = -2.0
	for r: int in num_regions:
		var rx: float = region_pos[r * 3 + 0]
		var ry: float = region_pos[r * 3 + 1]
		var rz: float = region_pos[r * 3 + 2]
		var d : float = dir.x * rx + dir.y * ry + dir.z * rz
		if d > best_dot:
			best_dot = d
			best_r   = r
	return best_r


## Convenience: get the region position as a Vector3
func get_region_pos(r: int) -> Vector3:
	return Vector3(region_pos[r * 3], region_pos[r * 3 + 1], region_pos[r * 3 + 2])
