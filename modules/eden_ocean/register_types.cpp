#include "register_types.h"

#include "core/object/class_db.h"
#include "eden_planet_ocean.h"

void initialize_eden_ocean_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(EdenPlanetOcean);
	}
}

void uninitialize_eden_ocean_module(ModuleInitializationLevel p_level) {
}
