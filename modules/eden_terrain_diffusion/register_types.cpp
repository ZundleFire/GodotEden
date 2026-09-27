#include "register_types.h"

#include "core/os/os.h"

#ifdef _WIN32
#include "onnx_linkage_test.h"
#endif

void initialize_eden_terrain_diffusion_module(ModuleInitializationLevel p_level) {
#ifdef _WIN32
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// Task #2 linkage-proof hook: run once, on demand, via
		//   godot.windows.editor.x86_64.exe --headless --run_onnx_test
		// See onnx_linkage_test.cpp for details. Not wired into the real
		// generator yet -- this only proves the ONNX Runtime vendoring +
		// SCons linkage + CUDA EP initialization work end to end.
		const List<String> cmdline_args = OS::get_singleton()->get_cmdline_args();
		for (const String &arg : cmdline_args) {
			if (arg == "--run_onnx_test") {
				eden::onnx_test::run_onnx_linkage_test();
				break;
			}
		}
	}
#endif
}

void uninitialize_eden_terrain_diffusion_module(ModuleInitializationLevel p_level) {
}
