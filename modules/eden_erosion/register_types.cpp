#include "register_types.h"
#include "flow_accumulation.h"
#include "gpu_erosion.h"
#include "gpu_heightmap_filler.h"
#include "heightmap_filler.h"
#include "hydraulic_erosion.h"
#include "voxel_block_filler.h"

void initialize_eden_erosion_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(HydraulicErosion);
		GDREGISTER_CLASS(GPUErosion);
		GDREGISTER_CLASS(HeightmapFiller);
		GDREGISTER_CLASS(FlowAccumulation);
		GDREGISTER_CLASS(GPUHeightmapFiller);
		GDREGISTER_CLASS(VoxelBlockFiller);
	}
}

void uninitialize_eden_erosion_module(ModuleInitializationLevel p_level) {
}
