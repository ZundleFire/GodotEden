#include "world_data_module.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "eden_planet_generator.h"

// ── SurfaceData ──────────────────────────────────────────────────────────────

void SurfaceData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_surface_normal", "v"), &SurfaceData::set_surface_normal);
	ClassDB::bind_method(D_METHOD("get_surface_normal"), &SurfaceData::get_surface_normal);
	ClassDB::bind_method(D_METHOD("set_height", "v"), &SurfaceData::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &SurfaceData::get_height);
	ClassDB::bind_method(D_METHOD("set_biome_type", "v"), &SurfaceData::set_biome_type);
	ClassDB::bind_method(D_METHOD("get_biome_type"), &SurfaceData::get_biome_type);
	ClassDB::bind_method(D_METHOD("set_temperature", "v"), &SurfaceData::set_temperature);
	ClassDB::bind_method(D_METHOD("get_temperature"), &SurfaceData::get_temperature);
	ClassDB::bind_method(D_METHOD("set_rainfall", "v"), &SurfaceData::set_rainfall);
	ClassDB::bind_method(D_METHOD("get_rainfall"), &SurfaceData::get_rainfall);
	ClassDB::bind_method(D_METHOD("set_water_depth", "v"), &SurfaceData::set_water_depth);
	ClassDB::bind_method(D_METHOD("get_water_depth"), &SurfaceData::get_water_depth);
	ClassDB::bind_method(D_METHOD("set_is_ocean", "v"), &SurfaceData::set_is_ocean);
	ClassDB::bind_method(D_METHOD("get_is_ocean"), &SurfaceData::get_is_ocean);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "surface_normal"), "set_surface_normal", "get_surface_normal");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height"), "set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "biome_type"), "set_biome_type", "get_biome_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temperature"), "set_temperature", "get_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rainfall"), "set_rainfall", "get_rainfall");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "water_depth"), "set_water_depth", "get_water_depth");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_ocean"), "set_is_ocean", "get_is_ocean");
	// Alias: BiomeClassifier (eden-project) reads `ocean_mask`; ADR-0005 names it
	// `is_ocean`. Expose both so neither consumer breaks.
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ocean_mask"), "set_is_ocean", "get_is_ocean");
}

// ── WorldConstants ───────────────────────────────────────────────────────────

void WorldConstants::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_axial_tilt_factor", "v"), &WorldConstants::set_axial_tilt_factor);
	ClassDB::bind_method(D_METHOD("get_axial_tilt_factor"), &WorldConstants::get_axial_tilt_factor);
	ClassDB::bind_method(D_METHOD("set_moon_count", "v"), &WorldConstants::set_moon_count);
	ClassDB::bind_method(D_METHOD("get_moon_count"), &WorldConstants::get_moon_count);
	ClassDB::bind_method(D_METHOD("set_moon_orbital_periods", "v"), &WorldConstants::set_moon_orbital_periods);
	ClassDB::bind_method(D_METHOD("get_moon_orbital_periods"), &WorldConstants::get_moon_orbital_periods);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "axial_tilt_factor"), "set_axial_tilt_factor", "get_axial_tilt_factor");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "moon_count"), "set_moon_count", "get_moon_count");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "moon_orbital_periods"), "set_moon_orbital_periods", "get_moon_orbital_periods");
}

// ── WorldDataModule ──────────────────────────────────────────────────────────

WorldDataModule *WorldDataModule::_singleton = nullptr;

WorldDataModule::WorldDataModule() {
	_singleton = this;
}

WorldDataModule::~WorldDataModule() {
	if (_singleton == this) {
		_singleton = nullptr;
	}
}

void WorldDataModule::set_generator(const Ref<EdenPlanetGenerator> &p_generator) {
	MutexLock lock(_mutex);
	_generator = p_generator;
}

Ref<EdenPlanetGenerator> WorldDataModule::get_generator() const {
	MutexLock lock(_mutex);
	return _generator;
}

void WorldDataModule::set_planet_center(const Vector3 &p_center) {
	MutexLock lock(_mutex);
	_planet_center = p_center;
}

Vector3 WorldDataModule::get_planet_center() const {
	MutexLock lock(_mutex);
	return _planet_center;
}

void WorldDataModule::set_worlds_dir(const String &p_dir) {
	MutexLock lock(_mutex);
	_worlds_dir = p_dir;
}

String WorldDataModule::get_worlds_dir() const {
	MutexLock lock(_mutex);
	return _worlds_dir;
}

String WorldDataModule::_ewd_path(const String &p_world_id) const {
	return _worlds_dir.path_join(p_world_id).path_join("terrain.ewd");
}

Error WorldDataModule::write_ewd_header(int64_t p_world_seed, int64_t p_seed32, float p_axial_tilt,
		int64_t p_biome_region_seed_offset, int64_t p_moon_count, const Array &p_moon_orbital_periods) {
	MutexLock lock(_mutex);

	// Store in memory first — queries must work even if disk IO fails.
	_world_seed = p_world_seed;
	_seed32 = p_seed32;
	_axial_tilt = p_axial_tilt;
	_biome_region_seed_offset = p_biome_region_seed_offset;
	_moon_count = int(p_moon_count);
	_moon_orbital_periods.resize(p_moon_orbital_periods.size());
	for (int i = 0; i < p_moon_orbital_periods.size(); ++i) {
		_moon_orbital_periods.write[i] = float(p_moon_orbital_periods[i]);
	}
	_constants_valid = true;

	float radius = _planet_radius;
	if (_generator.is_valid()) {
		radius = _generator->get_planet_radius();
	}

	const String world_id = itos(p_world_seed);
	const String path = _ewd_path(world_id);
	const Error dir_err = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	ERR_FAIL_COND_V_MSG(dir_err != OK, dir_err, "WorldDataModule: cannot create " + path.get_base_dir());

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(f.is_null(), ERR_CANT_CREATE, "WorldDataModule: cannot write " + path);

	// ── EWD1 header, 64 bytes (ADR-0005 .ewd format) ──
	f->store_buffer((const uint8_t *)"EWD1", 4); // magic
	f->store_32(1); // format_version
	f->store_64((uint64_t)p_world_seed); // world_id bytes 0-7 (seed; no UUID yet)
	f->store_64(0); // world_id bytes 8-15
	f->store_float(radius); // planet_radius
	f->store_float(1.0f); // voxel_resolution (1 m/voxel)
	f->store_32(16); // chunk_size (voxels per chunk edge)
	f->store_32(0); // cell_count (procedural — no baked cell arrays yet)
	// reserved[24]: seed32, biome_region_seed_offset, 16 zero bytes
	f->store_32((uint32_t)p_seed32);
	f->store_32((uint32_t)p_biome_region_seed_offset);
	f->store_64(0);
	f->store_64(0);

	// ── World constants ──
	f->store_float(_axial_tilt);
	f->store_32((uint32_t)_moon_count);
	for (int i = 0; i < _moon_orbital_periods.size(); ++i) {
		f->store_float(_moon_orbital_periods[i]);
	}
	return OK;
}

Error WorldDataModule::load_world(const String &p_world_id, float p_planet_radius) {
	MutexLock lock(_mutex);
	_planet_radius = p_planet_radius;

	const String path = _ewd_path(p_world_id);
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_null()) {
		// Not fatal when constants were written this session (create-world flow).
		if (_constants_valid) {
			return OK;
		}
		ERR_FAIL_V_MSG(ERR_FILE_NOT_FOUND, "WorldDataModule: no terrain.ewd at " + path);
	}

	uint8_t magic[4];
	f->get_buffer(magic, 4);
	ERR_FAIL_COND_V_MSG(memcmp(magic, "EWD1", 4) != 0, ERR_FILE_CORRUPT,
			"WorldDataModule: bad magic in " + path);
	const uint32_t version = f->get_32();
	ERR_FAIL_COND_V_MSG(version != 1, ERR_FILE_UNRECOGNIZED,
			"WorldDataModule: unsupported .ewd version " + itos(version));

	_world_seed = (int64_t)f->get_64();
	f->get_64(); // world_id bytes 8-15
	const float file_radius = f->get_float();
	if (_planet_radius <= 0.0f) {
		_planet_radius = file_radius;
	}
	f->get_float(); // voxel_resolution
	f->get_32(); // chunk_size
	f->get_32(); // cell_count
	_seed32 = f->get_32();
	_biome_region_seed_offset = f->get_32();
	f->get_64();
	f->get_64(); // remaining reserved

	_axial_tilt = f->get_float();
	_moon_count = (int)f->get_32();
	ERR_FAIL_COND_V_MSG(_moon_count < 0 || _moon_count > 16, ERR_FILE_CORRUPT,
			"WorldDataModule: implausible moon_count in " + path);
	_moon_orbital_periods.resize(_moon_count);
	for (int i = 0; i < _moon_count; ++i) {
		_moon_orbital_periods.write[i] = f->get_float();
	}
	_constants_valid = true;
	return OK;
}

// Internal Whittaker biome (EdenPlanetGenerator::Biome) → ADR-0005 BiomeType int.
// Approximate by design: BiomeClassifier (GDScript) remains the authority and
// classifies from temperature/rainfall; this field is a convenience/debug hint.
static int _map_biome_to_adr(int p_internal) {
	switch (p_internal) {
		case EdenPlanetGenerator::BIOME_OCEAN:
			return 9; // OCEAN
		case EdenPlanetGenerator::BIOME_TUNDRA:
			return 8; // TUNDRA
		case EdenPlanetGenerator::BIOME_GRASSLAND:
			return 4; // TEMPERATE_GRASSLAND
		case EdenPlanetGenerator::BIOME_FOREST:
			return 3; // TEMPERATE_RAINFOREST
		case EdenPlanetGenerator::BIOME_TROPICAL:
			return 0; // TROPICAL_RAINFOREST
		case EdenPlanetGenerator::BIOME_DESERT:
			return 2; // HOT_DESERT
		case EdenPlanetGenerator::BIOME_HILLS_MEADOWS:
			return 5; // SHRUBLAND
		default:
			return 10; // UNKNOWN
	}
}

Ref<SurfaceData> WorldDataModule::get_surface_data_at(const Vector3 &p_pos) const {
	Ref<SurfaceData> sd;
	sd.instantiate();

	Ref<EdenPlanetGenerator> gen;
	Vector3 center;
	{
		MutexLock lock(_mutex);
		gen = _generator;
		center = _planet_center;
	}
	ERR_FAIL_COND_V_MSG(gen.is_null(), sd,
			"WorldDataModule: no generator wired — call EdenPlanetGenerator.setup() first");

	EdenPlanetGenerator::SurfaceSample s;
	if (!gen->sample_surface(p_pos - center, s)) {
		return sd; // setup() not completed — defaults (UNKNOWN biome)
	}

	sd->height = s.height;
	sd->temperature = s.temperature01;
	sd->rainfall = s.rainfall01;
	sd->is_ocean = s.is_ocean;
	// Sea level sits at planet radius; ocean floors have negative height.
	sd->water_depth = s.is_ocean ? MAX(0.0f, -s.height) : 0.0f;
	// ponytail: radial up — replace with a finite-difference heightfield normal
	// when the Movement system needs real slope data.
	sd->surface_normal = (p_pos - center).normalized();
	sd->biome_type = _map_biome_to_adr(s.biome);
	return sd;
}

Ref<WorldConstants> WorldDataModule::get_world_constants() const {
	Ref<WorldConstants> wc;
	wc.instantiate();
	MutexLock lock(_mutex);
	wc->axial_tilt_factor = _axial_tilt;
	wc->moon_count = _moon_count;
	wc->moon_orbital_periods = _moon_orbital_periods;
	return wc;
}

void WorldDataModule::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_generator", "generator"), &WorldDataModule::set_generator);
	ClassDB::bind_method(D_METHOD("get_generator"), &WorldDataModule::get_generator);
	ClassDB::bind_method(D_METHOD("set_planet_center", "center"), &WorldDataModule::set_planet_center);
	ClassDB::bind_method(D_METHOD("get_planet_center"), &WorldDataModule::get_planet_center);
	ClassDB::bind_method(D_METHOD("set_worlds_dir", "dir"), &WorldDataModule::set_worlds_dir);
	ClassDB::bind_method(D_METHOD("get_worlds_dir"), &WorldDataModule::get_worlds_dir);
	ClassDB::bind_method(
			D_METHOD("write_ewd_header", "world_seed", "seed32", "axial_tilt", "biome_region_seed_offset", "moon_count", "moon_orbital_periods"),
			&WorldDataModule::write_ewd_header);
	ClassDB::bind_method(D_METHOD("load_world", "world_id", "planet_radius"), &WorldDataModule::load_world);
	ClassDB::bind_method(D_METHOD("get_surface_data_at", "pos"), &WorldDataModule::get_surface_data_at);
	ClassDB::bind_method(D_METHOD("get_world_constants"), &WorldDataModule::get_world_constants);
}
