#include "register_types.h"
#include "terrain_stamp.h"
#include "terrain_stamp_placer.h"
#include "core/object/class_db.h"

void initialize_eden_stamps_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(TerrainStamp);
	GDREGISTER_CLASS(TerrainStampPlacer);
}

void uninitialize_eden_stamps_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
