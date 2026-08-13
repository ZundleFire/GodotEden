#include "register_types.h"
#include "core/config/engine.h"
#include "eden_planet_climate_profile.h"
#include "eden_planet_generator_v1.h"
#include "eden_planet_generator_v2.h"
#include "planet_tectonics.h"
#include "world_data_module.h"

static WorldDataModule *world_data_module = nullptr;

void initialize_eden_planet_gen_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(EdenPlanetClimateProfile);
		// V1: tectonics + climate + biomes + oceans + caves. The only variant with caves.
		GDREGISTER_CLASS(EdenPlanetGeneratorV1);
		// V2: same tectonics/climate pipeline, newer per-plate biome-region system, no caves.
		// Prefer this one unless you need caves (see each class's own doc comment).
		GDREGISTER_CLASS(EdenPlanetGeneratorV2);
		GDREGISTER_CLASS(PlanetTectonics);
		GDREGISTER_CLASS(SurfaceData);
		GDREGISTER_CLASS(WorldConstants);
		GDREGISTER_CLASS(WorldDataModule);

		// ADR-0005: GDScript reaches this via Engine.get_singleton("WorldDataModule").
		world_data_module = memnew(WorldDataModule);
		Engine::get_singleton()->add_singleton(Engine::Singleton("WorldDataModule", world_data_module));
	}
}

void uninitialize_eden_planet_gen_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE && world_data_module != nullptr) {
		Engine::get_singleton()->remove_singleton("WorldDataModule");
		memdelete(world_data_module);
		world_data_module = nullptr;
	}
}
