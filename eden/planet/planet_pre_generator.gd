## planet_pre_generator.gd
## Runs the full offline pipeline to produce a PlanetData resource.
## Call generate() once at startup (or in a Thread) then hand PlanetData
## to PlanetGenerator.

class_name PlanetPreGenerator
extends RefCounted

# --------------------------------------------------------------------------- #
# Parameters
# --------------------------------------------------------------------------- #

@export var num_regions : int   = 8192   ## Voronoi region count
@export var num_plates  : int   = 24     ## Tectonic plates
@export var planet_radius: float = 40000.0  ## Metres
@export var seed        : int   = 12345
@export var jitter      : float = 0.45   ## Fibonacci sphere jitter [0,1]

# --------------------------------------------------------------------------- #
# Progress signal (emit from Thread so UI can show a bar)
# --------------------------------------------------------------------------- #

signal progress_updated(step: String, fraction: float)


# --------------------------------------------------------------------------- #
# Main entry point
# --------------------------------------------------------------------------- #

func generate() -> PlanetData:
	emit_signal("progress_updated", "Building Voronoi sphere …", 0.0)

	# --- Step 1: Voronoi sphere ---
	var sphere := VoronoiSphere.new()
	sphere.build(num_regions, seed, jitter)

	emit_signal("progress_updated", "Growing tectonic plates …", 0.1)

	# --- Step 2: Plate tectonics ---
	var tectonics := PlateTectonics.new(sphere, num_plates, seed)
	tectonics.grow_plates()

	emit_signal("progress_updated", "Classifying plate boundaries …", 0.35)
	tectonics.classify_boundaries()

	emit_signal("progress_updated", "Assigning elevation …", 0.45)
	tectonics.assign_elevation()

	emit_signal("progress_updated", "Computing climate …", 0.6)

	# --- Step 3: Biomes ---
	var biomes := BiomeClassifier.new(sphere, tectonics, seed)
	biomes.classify_all()

	emit_signal("progress_updated", "Building river network …", 0.75)

	# --- Step 4: Rivers ---
	var rivers := RiverSystem.new(sphere, tectonics, seed)
	rivers.build_rivers()

	emit_signal("progress_updated", "Packing planet data …", 0.9)

	# --- Step 5: Pack into PlanetData ---
	var data := _pack(sphere, tectonics, biomes, rivers)

	emit_signal("progress_updated", "Done.", 1.0)
	return data


# --------------------------------------------------------------------------- #
# Pack all results into a PlanetData resource
# --------------------------------------------------------------------------- #

func _pack(sphere: VoronoiSphere, tec: PlateTectonics, bio: BiomeClassifier, riv: RiverSystem) -> PlanetData:
	var data := PlanetData.new()

	data.num_regions   = sphere.num_regions
	data.planet_radius = planet_radius

	# Copy sphere geometry
	data.region_pos      = sphere.region_pos.duplicate()
	data.region_neighbors = []
	for i in sphere.num_regions:
		data.region_neighbors.append((sphere.region_neighbors[i] as PackedInt32Array).duplicate())

	# Tectonic
	data.r_plate        = tec.r_plate.duplicate()
	data.r_crust        = tec.r_crust.duplicate()
	data.r_elevation    = tec.r_elevation.duplicate()
	data.r_boundary_type = tec.r_boundary_type.duplicate()

	# Climate / biome
	data.r_temperature  = bio.r_temperature.duplicate()
	data.r_moisture     = bio.r_moisture.duplicate()
	data.r_biome        = bio.r_biome.duplicate()
	data.r_tex_slot     = bio.r_tex_slot.duplicate()

	# Rivers
	data.r_flow_norm    = riv.r_flow_norm.duplicate()
	data.r_is_river     = riv.r_is_river.duplicate()

	# Bucket grid
	_pack_bucket_grid(sphere, data)

	return data


func _pack_bucket_grid(sphere: VoronoiSphere, data: PlanetData) -> void:
	var bc: int = sphere.bucket_cols
	var br: int = sphere.bucket_rows
	data.bucket_cols = bc
	data.bucket_rows = br

	# Count how many regions per bucket
	var counts := PackedInt32Array(); counts.resize(bc * br); counts.fill(0)
	for r in sphere.num_regions:
		var pos := sphere.get_region_pos(r)
		var u   := (atan2(pos.z, pos.x) / TAU + 0.5)
		var v   := (asin(clamp(pos.y, -1.0, 1.0)) / PI + 0.5)
		var col : int = int(u * bc) % bc
		var row : int = clampi(int(v * br), 0, br - 1)
		counts[row * bc + col] += 1

	# Build offsets (prefix sum)
	data.bucket_offsets = PackedInt32Array(); data.bucket_offsets.resize(bc * br + 1)
	data.bucket_offsets[0] = 0
	for i in bc * br:
		data.bucket_offsets[i + 1] = data.bucket_offsets[i] + counts[i]

	# Fill region id list
	var total: int = data.bucket_offsets[bc * br]
	data.bucket_region_ids = PackedInt32Array(); data.bucket_region_ids.resize(total)

	var cursor: PackedInt32Array = counts.duplicate()
	cursor.fill(0)

	for r in sphere.num_regions:
		var pos := sphere.get_region_pos(r)
		var u   := (atan2(pos.z, pos.x) / TAU + 0.5)
		var v   := (asin(clamp(pos.y, -1.0, 1.0)) / PI + 0.5)
		var col : int = int(u * bc) % bc
		var row : int = clampi(int(v * br), 0, br - 1)
		var bi  : int = row * bc + col
		var idx : int = data.bucket_offsets[bi] + cursor[bi]
		data.bucket_region_ids[idx] = r
		cursor[bi] += 1