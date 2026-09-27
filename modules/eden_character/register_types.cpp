#include "register_types.h"

#include "eden_animator.h"
#include "eden_gait.h"

void initialize_eden_character_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_ABSTRACT_CLASS(EdenGait);
		GDREGISTER_CLASS(EdenAnimator);
	}
}

void uninitialize_eden_character_module(ModuleInitializationLevel p_level) {
}
