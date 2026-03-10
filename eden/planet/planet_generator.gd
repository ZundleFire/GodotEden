## planet_generator.gd
## VoxelGeneratorScript subclass that produces SDF voxels for a spherical planet.
##
## USAGE:
##   var gen := PlanetGenerator.new()
##   gen.planet_data = PlanetPreGenerator.new().generate()
##   var terrain := VoxelLodTerrain.new()
##   terrain.generator = gen
##
## The generator uses the PlanetData region lookup to find the biome at each
## voxel position, then applies a layered SDF:
##   sdf = |pos| - planet_radius - terrain_height(biome, noise)
## Negative SDF = solid, positive = air.

class_name PlanetGenerator
extends VoxelGeneratorScript

# --------------------------------------------------------------------------- #
# Parameters
# --------------------------------------------------------------------------- #

## Pre-generated planet data (set before the terrain node is added to the tree)
var planet_data: PlanetData

## Radius of the planet in metres (used when planet_data is not set)
@export var planet_radius: float = 40000.0

## Sea level fraction of planet_radius (0 = exactly at surface ring)
@export var sea_level_bias: float = 0.0

## Global amplitude multiplier on terrain height variation
@export var height_amplitude: float = 800.0  # metres

## Per-biome height scale multipliers [Biome enum index → float]
var biome_height_scale: Array = [
	0.8,   # OCEAN
	0.5,   # DEEP_OCEAN
	0.3,   # BEACH
	0.6,   # TUNDRA
	0.9,   # SNOW
	0.7,   # TAIGA
	0.7,   # SHRUBLAND
	0.8,   # GRASSLAND
	0.85,  # TEMPERATE_FOREST
	0.9,   # TROPICAL_FOREST
	1.0,   # RAINFOREST
	0.75,  # SAVANNA
	0.5,   # DESERT
	0.4,   # SCORCHED
	0.5,   # BARE
]

# --------------------------------------------------------------------------- #
# Internal noise layers (set up in _init / _ready equivalent)
# --------------------------------------------------------------------------- #

var _noise_macro  : FastNoiseLite   # continent-scale features
var _noise_meso   : FastNoiseLite   # hills / valleys
var _noise_micro  : FastNoiseLite   # surface roughness

var _initialized := false


# --------------------------------------------------------------------------- #
# Lifecycle
# --------------------------------------------------------------------------- #

func _init() -> void:
	_setup_noise()


func _setup_noise() -> void:
	_noise_macro = FastNoiseLite.new()
	_noise_macro.noise_type      = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	_noise_macro.frequency       = 0.00005   # very large features on a 40 km planet
	_noise_macro.fractal_octaves = 4
	_noise_macro.fractal_gain    = 0.5
	_noise_macro.seed            = 111

	_noise_meso = FastNoiseLite.new()
	_noise_meso.noise_type      = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	_noise_meso.frequency       = 0.0004
	_noise_meso.fractal_octaves = 5
	_noise_meso.fractal_gain    = 0.45
	_noise_meso.seed            = 222

	_noise_micro = FastNoiseLite.new()
	_noise_micro.noise_type      = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	_noise_micro.frequency       = 0.003
	_noise_micro.fractal_octaves = 3
	_noise_micro.fractal_gain    = 0.4
	_noise_micro.seed            = 333

	_initialized = true


# --------------------------------------------------------------------------- #
# VoxelGeneratorScript virtual
# --------------------------------------------------------------------------- #

func _generate_block(out_buffer: VoxelBuffer, origin_in_voxels: Vector3i, lod: int) -> void:
	if not _initialized:
		_setup_noise()

	var block_size := out_buffer.get_size()
	var lod_scale  := float(1 << lod)  # voxel → world-space multiplier at this LOD

	# We'll need planet_data to be set
	var has_data := planet_data != null

	var radius := planet_data.planet_radius if has_data else planet_radius

	var bx := block_size.x
	var by := block_size.y
	var bz := block_size.z

	for z in bz:
		for y in by:
			for x in bx:
				# World-space voxel centre
				var wx := float(origin_in_voxels.x + x) * lod_scale
				var wy := float(origin_in_voxels.y + y) * lod_scale
				var wz := float(origin_in_voxels.z + z) * lod_scale

				# Distance from planet centre
				var dist := sqrt(wx * wx + wy * wy + wz * wz)

				# Direction (unit vector)
				var dir := Vector3.ZERO
				var tex_slot := 1  # default = rock
				var elev_scale := 0.5

				if dist > 0.01:
					dir = Vector3(wx, wy, wz) / dist

					if has_data:
						var r := planet_data.find_nearest_region(dir)
						tex_slot   = planet_data.r_tex_slot[r]
						var biome  := planet_data.r_biome[r]
						elev_scale = biome_height_scale[biome] if biome < biome_height_scale.size() else 0.6

				# Terrain height from noise layers
				var h := _sample_height(wx, wy, wz, elev_scale)

				# SDF: negative inside solid, positive in air
				var sdf := dist - (radius + h + sea_level_bias)

				# Clamp to [-1, 1] SDF range for the buffer
				sdf = clamp(sdf / 4.0, -1.0, 1.0)  # /4 = ~4-voxel transition band

				out_buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer.CHANNEL_SDF)

				# Set material (only write for near-surface voxels to avoid
				# unnecessary writes deep underground or high in the air)
				if sdf < 0.9 and sdf > -0.9:
					out_buffer.set_voxel(tex_slot, x, y, z, VoxelBuffer.CHANNEL_INDICES)


# --------------------------------------------------------------------------- #
# Height sampling
# --------------------------------------------------------------------------- #

## Returns terrain height offset in metres.
## Negative height → below sea level shell, positive → above.
func _sample_height(x: float, y: float, z: float, scale: float) -> float:
	var macro := _noise_macro.get_noise_3d(x, y, z)          # [-1, 1]
	var meso  := _noise_meso.get_noise_3d(x, y, z) * 0.3
	var micro := _noise_micro.get_noise_3d(x, y, z) * 0.08

	return (macro + meso + micro) * height_amplitude * scale


# --------------------------------------------------------------------------- #
# Utility: expose noise seeds for re-seeding from PlanetData seed
# --------------------------------------------------------------------------- #

func set_noise_seeds(s: int) -> void:
	_noise_macro.seed = s
	_noise_meso.seed  = s + 1111
	_noise_micro.seed = s + 2222
