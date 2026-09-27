extends VoxelLodTerrain

var format := VoxelFormat.new()
format.set_channel_depth(VoxelBuffer.CHANNEL_DATA6, VoxelBuffer.DEPTH_32_BIT)
terrain.format = format
