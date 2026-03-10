## biome_classifier.gd
## Maps (elevation, temperature, moisture) → biome enum + texture slot.
## Temperature decreases with altitude and latitude (distance from equator).
## Moisture is seeded from ocean proximity and prevailing-wind BFS.

class_name BiomeClassifier
extends RefCounted

# --------------------------------------------------------------------------- #
# Biome enum
# --------------------------------------------------------------------------- #

enum Biome {
	OCEAN           = 0,
	DEEP_OCEAN      = 1,
	BEACH           = 2,
	TUNDRA          = 3,
	SNOW            = 4,
	TAIGA           = 5,
	SHRUBLAND       = 6,
	GRASSLAND       = 7,
	TEMPERATE_FOREST= 8,
	TROPICAL_FOREST = 9,
	RAINFOREST      = 10,
	SAVANNA         = 11,
	DESERT          = 12,
	SCORCHED        = 13,
	BARE            = 14,
}

# Texture atlas slot (matches terrain_smooth.gdshader INDICES channels):
# 0=Grass, 1=Rock, 2=Snow, 3=Sand, 4=Dirt, 5=Moss
const BIOME_TEXTURE_SLOT := {
	Biome.OCEAN:            3,   # sand / seabed
	Biome.DEEP_OCEAN:       3,
	Biome.BEACH:            3,   # sand
	Biome.TUNDRA:           4,   # dirt / frozen dirt
	Biome.SNOW:             2,   # snow
	Biome.TAIGA:            5,   # moss / boreal undergrowth
	Biome.SHRUBLAND:        5,   # moss
	Biome.GRASSLAND:        0,   # grass
	Biome.TEMPERATE_FOREST: 0,   # grass
	Biome.TROPICAL_FOREST:  0,   # grass
	Biome.RAINFOREST:       5,   # rich moss / jungle floor
	Biome.SAVANNA:          0,   # dry grass
	Biome.DESERT:           3,   # sand
	Biome.SCORCHED:         1,   # rock / obsidian
	Biome.BARE:             1,   # bare rock
}

# --------------------------------------------------------------------------- #
# Public data (indexed by region_id)
# --------------------------------------------------------------------------- #

var r_temperature : PackedFloat32Array  # [0, 1]  0=cold, 1=hot
var r_moisture    : PackedFloat32Array  # [0, 1]  0=dry, 1=wet
var r_biome       : PackedInt32Array    # Biome enum
var r_tex_slot    : PackedInt32Array    # 0..5  → terrain shader INDICES slot

# --------------------------------------------------------------------------- #
# Internals
# --------------------------------------------------------------------------- #

var _sphere    : VoronoiSphere
var _tectonics : PlateTectonics
var _rng       : RandomNumberGenerator
var _noise     : FastNoiseLite

const SEA_LEVEL := 0.0        # elevation threshold
const POLAR_LATITUDE := 0.85  # cos(latitude) below which is polar zone


func _init(sphere: VoronoiSphere, tectonics: PlateTectonics, seed: int) -> void:
	_sphere    = sphere
	_tectonics = tectonics

	_rng = RandomNumberGenerator.new()
	_rng.seed = seed

	_noise = FastNoiseLite.new()
	_noise.seed            = seed + 4321
	_noise.noise_type      = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	_noise.frequency       = 2.5
	_noise.fractal_octaves = 3

	var n: int = sphere.num_regions
	r_temperature = PackedFloat32Array(); r_temperature.resize(n); r_temperature.fill(0.0)
	r_moisture    = PackedFloat32Array(); r_moisture.resize(n);    r_moisture.fill(0.0)
	r_biome       = PackedInt32Array();   r_biome.resize(n);       r_biome.fill(Biome.BARE)
	r_tex_slot    = PackedInt32Array();   r_tex_slot.resize(n);    r_tex_slot.fill(1)


# --------------------------------------------------------------------------- #
# Public entry point
# --------------------------------------------------------------------------- #

func classify_all() -> void:
	_compute_temperature()
	_compute_moisture()
	_assign_biomes()


# --------------------------------------------------------------------------- #
# Step 1: Temperature
# Decreases with latitude (abs(y) on unit sphere ≈ sin(lat))
# and altitude (elevation > 0 → further cooling)
# --------------------------------------------------------------------------- #

func _compute_temperature() -> void:
	for r in _sphere.num_regions:
		var pos  := _sphere.get_region_pos(r)
		var elev : float = _tectonics.r_elevation[r]

		# Latitude factor: 1.0 at equator (y≈0), 0.0 at poles (|y|=1)
		var lat_temp: float = 1.0 - abs(pos.y)

		# Altitude cooling: only above sea level
		var alt_temp: float = 0.0
		if elev > SEA_LEVEL:
			alt_temp = elev * 0.5  # high mountains are colder

		# Noise ±0.08
		var noise_val: float = _noise.get_noise_3d(pos.x, pos.y, pos.z) * 0.08

		r_temperature[r] = clamp(lat_temp - alt_temp + noise_val, 0.0, 1.0)


# --------------------------------------------------------------------------- #
# Step 2: Moisture
# Ocean regions emit full moisture; BFS spreads it inland with decay.
# Prevailing wind bias: moisture flows along +X axis (simplified).
# --------------------------------------------------------------------------- #

func _compute_moisture() -> void:
	var queue   := PackedInt32Array()
	var n       := _sphere.num_regions

	# Seed moisture from ocean-adjacent land regions
	for r in n:
		if _tectonics.r_elevation[r] < SEA_LEVEL:
			r_moisture[r] = 1.0
			queue.append(r)

	# BFS outward, decaying
	var qi: int = 0
	while qi < queue.size():
		var cur : int = queue[qi]; qi += 1
		for nb in (_sphere.region_neighbors[cur] as PackedInt32Array):
			var spread : float = r_moisture[cur] * 0.72
			if spread < 0.01:
				continue
			if spread > r_moisture[nb]:
				r_moisture[nb] = spread
				queue.append(nb)

	# Add noise ±0.1
	for r in n:
		var pos := _sphere.get_region_pos(r)
		var n2  := _noise.get_noise_3d(pos.x + 10.0, pos.y, pos.z) * 0.1
		r_moisture[r] = clamp(r_moisture[r] + n2, 0.0, 1.0)


# --------------------------------------------------------------------------- #
# Step 3: Whittaker biome table
# --------------------------------------------------------------------------- #

func _assign_biomes() -> void:
	for r in _sphere.num_regions:
		var elev : float = _tectonics.r_elevation[r]
		var temp : float = r_temperature[r]
		var mois : float = r_moisture[r]
		var biome: int

		if elev < SEA_LEVEL - 0.35:
			biome = Biome.DEEP_OCEAN
		elif elev < SEA_LEVEL:
			biome = Biome.OCEAN
		elif elev < SEA_LEVEL + 0.03:
			biome = Biome.BEACH
		else:
			biome = _whittaker(temp, mois, elev)

		r_biome[r]    = biome
		r_tex_slot[r] = BIOME_TEXTURE_SLOT[biome]


## Simplified Whittaker diagram lookup.
## temp [0=arctic, 1=tropical], mois [0=arid, 1=humid], elev [0..1]
func _whittaker(temp: float, mois: float, elev: float) -> int:
	# Alpine / permanent snow
	if elev > 0.7 and temp < 0.4:
		return Biome.SNOW

	# Very cold
	if temp < 0.15:
		if mois > 0.4:
			return Biome.TUNDRA
		return Biome.SCORCHED  # dry ice wasteland

	# Cold
	if temp < 0.35:
		if mois > 0.55:
			return Biome.TAIGA
		if mois > 0.3:
			return Biome.SHRUBLAND
		return Biome.TUNDRA

	# Temperate
	if temp < 0.6:
		if mois > 0.66:
			return Biome.TEMPERATE_FOREST
		if mois > 0.4:
			return Biome.GRASSLAND
		if mois > 0.2:
			return Biome.SHRUBLAND
		return Biome.DESERT

	# Warm to tropical
	if mois > 0.75:
		if temp > 0.80:
			return Biome.RAINFOREST
		return Biome.TROPICAL_FOREST
	if mois > 0.5:
		return Biome.TROPICAL_FOREST
	if mois > 0.3:
		return Biome.SAVANNA
	return Biome.DESERT


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #

func get_biome_name(biome: int) -> String:
	return Biome.keys()[biome] if biome >= 0 and biome < Biome.size() else "UNKNOWN"