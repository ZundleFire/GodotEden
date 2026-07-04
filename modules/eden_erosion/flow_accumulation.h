#ifndef FLOW_ACCUMULATION_H
#define FLOW_ACCUMULATION_H

#include "core/object/ref_counted.h"
#include "core/variant/variant.h"

/// C++ accelerated D8 flow accumulation for heightmap chunks.
///
/// Computes drainage flow for a 2D heightmap grid using the D8
/// steepest-descent algorithm.  Each cell starts with 1 unit of "rain",
/// cells are processed in descending height order, and each cell routes
/// its accumulated flow to the steepest downhill D8 neighbour.
///
/// GDScript equivalent would take ~10-30 seconds per 512² chunk.
/// This C++ version runs in ~20-40 ms.
///
/// Usage from GDScript:
///   var flow := FlowAccumulation.new()
///   var flow_data: PackedFloat32Array = flow.compute(height_data, 512)

class FlowAccumulation : public RefCounted {
	GDCLASS(FlowAccumulation, RefCounted);

public:
	FlowAccumulation();

	/// Compute D8 flow accumulation for a square heightmap grid.
	/// @param p_height_data  PackedFloat32Array of resolution² height values.
	/// @param p_resolution   Grid width/height (e.g. 512).
	/// @returns PackedFloat32Array of resolution² flow values.
	PackedFloat32Array compute(PackedFloat32Array p_height_data, int p_resolution);

	/// Static helper — compute flow accumulation without creating an object.
	/// Used internally by HeightmapFiller if desired.
	static PackedFloat32Array compute_flow(const float *p_height, int p_resolution);

protected:
	static void _bind_methods();
};

#endif // FLOW_ACCUMULATION_H
