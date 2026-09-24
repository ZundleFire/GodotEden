#include "register_types.h"
#include "eden_bush_instance.h"
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
	}
}

void uninitialize_eden_foliage_module(ModuleInitializationLevel p_level) {
}
