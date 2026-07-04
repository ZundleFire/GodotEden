#ifndef EDEN_WORLD_DATA_MODULE_H
#define EDEN_WORLD_DATA_MODULE_H

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"

class EdenPlanetGenerator;

// ADR-0005: World Data Storage and Runtime Query.
// SurfaceData / WorldConstants are the GDScript-visible result types;
// WorldDataModule is the engine singleton GDScript reaches via
// Engine.get_singleton("WorldDataModule") (wrapped by the WorldQuery Autoload).

// Per-cell world data returned by WorldDataModule::get_surface_data_at().
// Usage (GDScript):
//   var sd = Engine.get_singleton("WorldDataModule").get_surface_data_at(global_position)
//   if sd.is_ocean: ...
class SurfaceData : public RefCounted {
	GDCLASS(SurfaceData, RefCounted)
public:
	Vector3 surface_normal = Vector3(0, 1, 0); // outward surface normal; == planet_up at flat terrain
	float height = 0.0f; // terrain offset from planet radius, world units
	int biome_type = 10; // ADR BiomeType int (10 = UNKNOWN); BiomeClassifier stays authoritative
	float temperature = 0.0f; // normalized 0 (coldest) .. 1 (hottest)
	float rainfall = 0.0f; // normalized 0 (driest) .. 1 (max)
	float water_depth = 0.0f; // 0 on land; depth below sea level when submerged
	bool is_ocean = false;

	void set_surface_normal(const Vector3 &v) { surface_normal = v; }
	Vector3 get_surface_normal() const { return surface_normal; }
	void set_height(float v) { height = v; }
	float get_height() const { return height; }
	void set_biome_type(int v) { biome_type = v; }
	int get_biome_type() const { return biome_type; }
	void set_temperature(float v) { temperature = v; }
	float get_temperature() const { return temperature; }
	void set_rainfall(float v) { rainfall = v; }
	float get_rainfall() const { return rainfall; }
	void set_water_depth(float v) { water_depth = v; }
	float get_water_depth() const { return water_depth; }
	void set_is_ocean(bool v) { is_ocean = v; }
	bool get_is_ocean() const { return is_ocean; }

protected:
	static void _bind_methods();
};

// World-scale constants seeded at world creation (read by Calendar / Weather).
// Usage (GDScript):
//   var wc = Engine.get_singleton("WorldDataModule").get_world_constants()
//   var tilt: float = wc.axial_tilt_factor
class WorldConstants : public RefCounted {
	GDCLASS(WorldConstants, RefCounted)
public:
	float axial_tilt_factor = 0.0f; // 0.0–1.5; drives seasonal day-length variation
	int moon_count = 0; // 0–3
	PackedFloat32Array moon_orbital_periods; // in-game days per moon

	void set_axial_tilt_factor(float v) { axial_tilt_factor = v; }
	float get_axial_tilt_factor() const { return axial_tilt_factor; }
	void set_moon_count(int v) { moon_count = v; }
	int get_moon_count() const { return moon_count; }
	void set_moon_orbital_periods(const PackedFloat32Array &v) { moon_orbital_periods = v; }
	PackedFloat32Array get_moon_orbital_periods() const { return moon_orbital_periods; }

protected:
	static void _bind_methods();
};

// Engine singleton owning runtime world-data queries and .ewd persistence.
// Not a scene node — registered via Engine::add_singleton in register_types.cpp.
// The active EdenPlanetGenerator auto-wires itself here from setup(); tests and
// tools may inject one explicitly with set_generator().
//
// Thread safety: all public methods lock an internal mutex around shared state;
// terrain sampling itself uses the generator's own RWLock (shared with worker
// threads). get_surface_data_at allocates one SurfaceData per call by ADR design —
// per-entity query rates, not per-voxel.
class WorldDataModule : public Object {
	GDCLASS(WorldDataModule, Object)
public:
	static WorldDataModule *get_singleton() { return _singleton; }

	WorldDataModule();
	~WorldDataModule();

	// The generator used to answer surface queries. Auto-set by
	// EdenPlanetGenerator::setup(); last setup() wins.
	void set_generator(const Ref<EdenPlanetGenerator> &p_generator);
	Ref<EdenPlanetGenerator> get_generator() const;

	// ADR-0001: planet center must come from the registered GravityBody3D —
	// game code sets this at world load. Defaults to origin.
	void set_planet_center(const Vector3 &p_center);
	Vector3 get_planet_center() const;

	// Directory that holds worlds/<world_id>/terrain.ewd. Default "user://worlds".
	void set_worlds_dir(const String &p_dir);
	String get_worlds_dir() const;

	// Writes worlds/<world_seed>/terrain.ewd (EWD1 header + world constants) and
	// stores the constants in memory. Called once by WorldBootstrap at world
	// creation (ADR-0013). Example (GDScript):
	//   module.write_ewd_header(world_seed, seed32, tilt, biome_offset, moons, periods)
	Error write_ewd_header(int64_t p_world_seed, int64_t p_seed32, float p_axial_tilt,
			int64_t p_biome_region_seed_offset, int64_t p_moon_count, const Array &p_moon_orbital_periods);

	// Loads worlds/<world_id>/terrain.ewd into memory and records the planet
	// radius for queries. Missing file is not fatal if constants were already
	// written this session. Example: module.load_world(str(world_seed), radius)
	Error load_world(const String &p_world_id, float p_planet_radius);

	// Per-cell world data at a world-space position (synchronous; callable from
	// _physics_process at per-entity rates). Example:
	//   var sd = module.get_surface_data_at(global_position)
	Ref<SurfaceData> get_surface_data_at(const Vector3 &p_pos) const;

	// World-scale constants from the loaded/written .ewd header.
	Ref<WorldConstants> get_world_constants() const;

protected:
	static void _bind_methods();

private:
	static WorldDataModule *_singleton;

	String _ewd_path(const String &p_world_id) const;

	mutable Mutex _mutex;
	Ref<EdenPlanetGenerator> _generator;
	Vector3 _planet_center;
	float _planet_radius = 0.0f;
	String _worlds_dir = "user://worlds";

	// .ewd header state (in-memory copy).
	int64_t _world_seed = 0;
	int64_t _seed32 = 0;
	int64_t _biome_region_seed_offset = 0;
	float _axial_tilt = 0.0f;
	int _moon_count = 0;
	PackedFloat32Array _moon_orbital_periods;
	bool _constants_valid = false;
};

#endif // EDEN_WORLD_DATA_MODULE_H
