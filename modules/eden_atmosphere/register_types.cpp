#include "register_types.h"

#include "core/object/class_db.h"
#include "eden_atmosphere_post_effect.h"
#include "eden_cloud_shell.h"
#include "eden_planet_atmosphere.h"
#include "eden_planet_rings.h"

void initialize_eden_atmosphere_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(EdenPlanetAtmosphere);
	GDREGISTER_CLASS(EdenCloudShell);
	GDREGISTER_CLASS(EdenPlanetRings);
	GDREGISTER_CLASS(EdenAtmospherePostEffect);
}

void uninitialize_eden_atmosphere_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
