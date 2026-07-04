#include "register_types.h"
#include "core/config/engine.h"
#include "eden_planet_climate_profile.h"
#include "eden_planet_generator.h"
#include "eden_planet_generator_clean.h"
#include "eden_planet_generator_v6_native.h"
#include "planet_tectonics.h"
#include "world_data_module.h"

static WorldDataModule *world_data_module = nullptr;

void initialize_eden_planet_gen_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(EdenPlanetClimateProfile);
		GDREGISTER_CLASS(EdenPlanetGenerator);
		GDREGISTER_CLASS(EdenPlanetGeneratorClean);
		GDREGISTER_CLASS(EdenPlanetGeneratorV6Native);
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
