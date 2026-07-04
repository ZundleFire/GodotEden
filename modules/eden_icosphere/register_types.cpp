#include "register_types.h"
#include "ico_chunk_stitcher.h"
#include "ico_heightmap_filler.h"
#include "ico_voxel_block_filler.h"
#include "icosphere_mapper.h"

void initialize_eden_icosphere_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(IcosphereMapper);
		GDREGISTER_CLASS(IcoHeightmapFiller);
		GDREGISTER_CLASS(IcoChunkStitcher);
		GDREGISTER_CLASS(IcoVoxelBlockFiller);
	}
}

void uninitialize_eden_icosphere_module(ModuleInitializationLevel p_level) {
}
