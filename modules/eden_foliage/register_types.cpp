#include "register_types.h"
#include "eden_bush_instance.h"
#include "eden_foliage.h"
#include "eden_foliage_config.h"
#include "eden_foliage_meshes.h"
#include "eden_rock_instance.h"
#include "eden_tree_generator.h"
#include "eden_tree_instance.h"
#include "eden_tree_mesher.h"
#include "eden_tree_shape.h"

void initialize_eden_foliage_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(EdenTreeMesher);
		GDREGISTER_CLASS(EdenTreeShape);
		GDREGISTER_CLASS(EdenTreeGenerator);
		GDREGISTER_CLASS(EdenTreeInstance);
		GDREGISTER_CLASS(EdenRockInstance);
		GDREGISTER_CLASS(EdenBushInstance);
		GDREGISTER_CLASS(EdenFoliageLayer);
		GDREGISTER_CLASS(EdenFoliageBiome);
		GDREGISTER_CLASS(EdenFoliageConfig);
		GDREGISTER_ABSTRACT_CLASS(EdenFoliageMeshes);
		GDREGISTER_CLASS(EdenFoliage);
	}
}

void uninitialize_eden_foliage_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		EdenFoliageMeshes::clear_cache(); // cached meshes and materials must go before the servers
	}
}
