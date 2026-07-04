#ifndef ICO_CHUNK_STITCHER_H
#define ICO_CHUNK_STITCHER_H

#include "icosphere_mapper.h"
#include "core/object/ref_counted.h"
#include "core/variant/variant.h"

/// IcoChunkStitcher — Ensures seamless edges between adjacent triangular heightmap tiles.
///
/// After tile generation and erosion, edge samples may have diverged slightly
/// (erosion is per-tile). This class averages shared edge samples between
/// adjacent tiles to restore seamlessness.
///
/// Supports:
///   - Edge averaging (simple linear blend of shared boundary samples)
///   - Smooth blending (wider band, Hermite interpolation from edge into interior)
///   - Batch processing of all tiles in a single call
///
/// THREADING: stitch_all() processes edges sequentially (writes to shared data).
/// Individual tile reads are safe.

class IcoChunkStitcher : public RefCounted {
	GDCLASS(IcoChunkStitcher, RefCounted);

public:
	IcoChunkStitcher();

	/// Set the IcosphereMapper (must be build()-ed).
	void set_mapper(Ref<IcosphereMapper> p_mapper);

	/// Set the blend width (in samples) for smooth stitching.
	/// 0 = edge-only (just average the boundary row).
	/// N > 0 = blend N rows deep on each side with Hermite falloff.
	void set_blend_width(int p_width);
	int get_blend_width() const;

	/// Stitch all adjacent tile pairs.  Modifies height arrays in place.
	/// tile_data: Dictionary mapping face_index → PackedFloat32Array.
	/// resolution: the R used in fill_tile().
	void stitch_all(Dictionary p_tile_data, int p_resolution);

	/// Stitch a single edge between two tiles.  Useful for incremental updates.
	void stitch_edge(PackedFloat32Array &p_tile_a, PackedFloat32Array &p_tile_b,
		int p_face_a, int p_edge_a, int p_face_b, int p_edge_b,
		bool p_reversed, int p_resolution);

protected:
	static void _bind_methods();

private:
	Ref<IcosphereMapper> mapper;
	int blend_width = 4;

	/// Get the linear indices along an edge of a triangular tile.
	/// Edge 0: v0→v1 (row 0, j=0..R: samples at (0,j))
	/// Edge 1: v1→v2 (diagonal: samples at (i, R-i) for i=0..R)
	/// Edge 2: v2→v0 (column 0: samples at (i, 0) for i=0..R)
	/// Returns indices in order along the edge.
	static Vector<int> _get_edge_indices(int p_edge, int p_resolution);

	/// Get indices N rows deep from an edge (for smooth blending).
	/// Returns Vector<Vector<int>> where [0] is the edge row, [1] is 1 row in, etc.
	static Vector<Vector<int>> _get_blend_band(int p_edge, int p_resolution, int p_depth);
};

#endif // ICO_CHUNK_STITCHER_H
