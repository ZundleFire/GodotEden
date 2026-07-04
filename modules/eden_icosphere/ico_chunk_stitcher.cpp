#include "ico_chunk_stitcher.h"
#include "core/math/math_funcs.h"

IcoChunkStitcher::IcoChunkStitcher() {
}

void IcoChunkStitcher::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mapper", "mapper"), &IcoChunkStitcher::set_mapper);
	ClassDB::bind_method(D_METHOD("set_blend_width", "width"), &IcoChunkStitcher::set_blend_width);
	ClassDB::bind_method(D_METHOD("get_blend_width"), &IcoChunkStitcher::get_blend_width);
	ClassDB::bind_method(D_METHOD("stitch_all", "tile_data", "resolution"), &IcoChunkStitcher::stitch_all);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_width"), "set_blend_width", "get_blend_width");
}

void IcoChunkStitcher::set_mapper(Ref<IcosphereMapper> p_mapper) { mapper = p_mapper; }
void IcoChunkStitcher::set_blend_width(int p_width) { blend_width = MAX(0, p_width); }
int IcoChunkStitcher::get_blend_width() const { return blend_width; }

// ── Edge index calculation ────────────────────────────────────────────────────
// Triangle with resolution R has rows i=0..R.
// Row i has (R+1-i) samples starting at index: i*(R+1) - i*(i-1)/2.
//
// Edge 0: v0→v1 = row i=0, columns j=0..R (R+1 samples)
// Edge 1: v1→v2 = diagonal, samples at (i, R-i) for i=0..R
// Edge 2: v2→v0 = column j=0, rows i=R..0 (reversed so it goes v2→v0)

Vector<int> IcoChunkStitcher::_get_edge_indices(int p_edge, int p_resolution) {
	int R = p_resolution;
	Vector<int> indices;
	indices.resize(R + 1);

	switch (p_edge) {
		case 0: {
			// Row 0: index = j for j=0..R
			for (int j = 0; j <= R; j++) {
				indices.write[j] = j;
			}
		} break;
		case 1: {
			// Diagonal: sample (i, R-i) for i=0..R
			for (int i = 0; i <= R; i++) {
				int j = R - i;
				int row_start = i * (R + 1) - i * (i - 1) / 2;
				indices.write[i] = row_start + j;
			}
		} break;
		case 2: {
			// Column 0: sample (i, 0) for i=R..0 (v2→v0 direction)
			for (int k = 0; k <= R; k++) {
				int i = R - k;
				int row_start = i * (R + 1) - i * (i - 1) / 2;
				indices.write[k] = row_start; // j=0
			}
		} break;
	}

	return indices;
}

Vector<Vector<int>> IcoChunkStitcher::_get_blend_band(int p_edge, int p_resolution, int p_depth) {
	int R = p_resolution;
	Vector<Vector<int>> bands;
	bands.resize(p_depth + 1);

	// Band 0 = the edge itself.
	bands.write[0] = _get_edge_indices(p_edge, R);

	// Bands 1..depth = rows stepping inward from the edge.
	// The direction "inward" depends on which edge:
	//   Edge 0 (row 0): inward = increasing i
	//   Edge 1 (diagonal): inward = decreasing j (moving toward j=0)
	//   Edge 2 (column 0): inward = increasing j
	for (int d = 1; d <= p_depth; d++) {
		Vector<int> band;

		switch (p_edge) {
			case 0: {
				// Row d: samples at (d, j) for j=0..R-d
				int row_start = d * (R + 1) - d * (d - 1) / 2;
				int row_count = R + 1 - d;
				band.resize(row_count);
				for (int j = 0; j < row_count; j++) {
					band.write[j] = row_start + j;
				}
			} break;
			case 1: {
				// One row inside the diagonal: sample (i, R-i-d) for valid i
				int count = MAX(0, R + 1 - d);
				band.resize(count);
				for (int k = 0; k < count; k++) {
					int i = k;
					int j = R - i - d;
					if (j < 0) { band.resize(k); break; }
					int row_start = i * (R + 1) - i * (i - 1) / 2;
					band.write[k] = row_start + j;
				}
			} break;
			case 2: {
				// Column d: sample (i, d) for i=R-d..0
				int count = MAX(0, R + 1 - d);
				band.resize(count);
				for (int k = 0; k < count; k++) {
					int i = R - d - k;
					if (i < 0) { band.resize(k); break; }
					int row_start = i * (R + 1) - i * (i - 1) / 2;
					band.write[k] = row_start + d;
				}
			} break;
		}

		bands.write[d] = band;
	}

	return bands;
}

// ── Edge stitching ────────────────────────────────────────────────────────────

void IcoChunkStitcher::stitch_edge(PackedFloat32Array &p_tile_a, PackedFloat32Array &p_tile_b,
		int p_face_a, int p_edge_a, int p_face_b, int p_edge_b,
		bool p_reversed, int p_resolution) {
	Vector<int> edge_a = _get_edge_indices(p_edge_a, p_resolution);
	Vector<int> edge_b = _get_edge_indices(p_edge_b, p_resolution);

	int n = MIN(edge_a.size(), edge_b.size());
	float *da = p_tile_a.ptrw();
	float *db = p_tile_b.ptrw();

	// Average the shared edge samples.
	for (int k = 0; k < n; k++) {
		int idx_a = edge_a[k];
		int idx_b = p_reversed ? edge_b[n - 1 - k] : edge_b[k];

		float avg = (da[idx_a] + db[idx_b]) * 0.5f;
		da[idx_a] = avg;
		db[idx_b] = avg;
	}

	// Smooth blending into interior if blend_width > 0.
	if (blend_width > 0) {
		Vector<Vector<int>> bands_a = _get_blend_band(p_edge_a, p_resolution, blend_width);
		Vector<Vector<int>> bands_b = _get_blend_band(p_edge_b, p_resolution, blend_width);

		for (int d = 1; d <= blend_width; d++) {
			if (d >= bands_a.size() || d >= bands_b.size()) break;

			const Vector<int> &band_a = bands_a[d];
			const Vector<int> &band_b = bands_b[d];
			int bn = MIN(band_a.size(), band_b.size());

			// Hermite falloff: 1.0 at edge → 0.0 at blend_width
			float t = (float)d / (float)(blend_width + 1);
			float weight = 1.0f - t * t * (3.0f - 2.0f * t); // smoothstep

			for (int k = 0; k < bn; k++) {
				int ia = band_a[k];
				int ib = p_reversed ? band_b[bn - 1 - k] : band_b[k];

				float avg = (da[ia] + db[ib]) * 0.5f;
				da[ia] = Math::lerp(da[ia], avg, weight);
				db[ib] = Math::lerp(db[ib], avg, weight);
			}
		}
	}
}

// ── Stitch all edges ──────────────────────────────────────────────────────────

void IcoChunkStitcher::stitch_all(Dictionary p_tile_data, int p_resolution) {
	ERR_FAIL_COND_MSG(mapper.is_null(), "Mapper not set.");

	int n_faces = mapper->get_face_count();

	// Process each edge once.  We iterate all faces and only process edges
	// where face_a < face_b to avoid double-processing.
	for (int fa = 0; fa < n_faces; fa++) {
		if (!p_tile_data.has(fa)) continue;

		PackedFloat32Array tile_a = p_tile_data[fa];

		for (int e = 0; e < 3; e++) {
			Array adj = mapper->get_edge_adjacency(fa, e);
			int fb = adj[0];
			int eb = adj[1];
			bool reversed = adj[2];

			if (fb < 0 || fb <= fa) continue; // skip if no neighbour or already processed
			if (!p_tile_data.has(fb)) continue;

			PackedFloat32Array tile_b = p_tile_data[fb];

			stitch_edge(tile_a, tile_b, fa, e, fb, eb, reversed, p_resolution);

			// Write back (PackedFloat32Array is COW, so we must reassign).
			p_tile_data[fa] = tile_a;
			p_tile_data[fb] = tile_b;
		}
	}
}
