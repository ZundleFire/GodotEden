#include "flow_accumulation.h"
#include "core/os/os.h"
#include <algorithm>
#include <cstring>

FlowAccumulation::FlowAccumulation() {
}

PackedFloat32Array FlowAccumulation::compute(PackedFloat32Array p_height_data, int p_resolution) {
	const int N = p_resolution;
	const int total = N * N;

	PackedFloat32Array result;

	if (p_height_data.size() != total) {
		ERR_PRINT(vformat("FlowAccumulation::compute: height array size %d != expected %d",
				p_height_data.size(), total));
		result.resize(total);
		result.fill(0.0f);
		return result;
	}

	return compute_flow(p_height_data.ptr(), N);
}

PackedFloat32Array FlowAccumulation::compute_flow(const float *p_height, int p_resolution) {
	const int N = p_resolution;
	const int total = N * N;

	PackedFloat32Array result;
	result.resize(total);
	float *flow = result.ptrw();

	// Initialise: every cell contributes 1 unit of rain.
	for (int i = 0; i < total; i++) {
		flow[i] = 1.0f;
	}

	// ── Build sorted index array (descending height) ─────────────────────
	// Use a simple index sort. At 512² = 262K entries, std::sort takes ~5ms.
	Vector<int32_t> sorted;
	sorted.resize(total);
	int32_t *sorted_ptr = sorted.ptrw();
	for (int i = 0; i < total; i++) {
		sorted_ptr[i] = i;
	}

	// Sort descending by height.
	std::sort(sorted_ptr, sorted_ptr + total, [p_height](int32_t a, int32_t b) {
		return p_height[a] > p_height[b];
	});

	// ── D8 neighbour offsets ─────────────────────────────────────────────
	// N, NE, E, SE, S, SW, W, NW
	static const int dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
	static const int dy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
	static const float inv_dist[8] = {
		1.0f, 0.7071067811865f, 1.0f, 0.7071067811865f,
		1.0f, 0.7071067811865f, 1.0f, 0.7071067811865f
	};

	// ── Accumulate flow ──────────────────────────────────────────────────
	for (int si = 0; si < total; si++) {
		const int idx = sorted_ptr[si];
		const int cx = idx % N;
		const int cy = idx / N;
		const float h_here = p_height[idx];
		const float my_flow = flow[idx];

		// Find steepest downhill D8 neighbour.
		float best_slope = 0.0f;
		int best_idx = -1;

		for (int d = 0; d < 8; d++) {
			const int nx = cx + dx[d];
			const int ny = cy + dy[d];
			if (nx < 0 || nx >= N || ny < 0 || ny >= N) {
				continue;
			}
			const int nidx = ny * N + nx;
			const float slope = (h_here - p_height[nidx]) * inv_dist[d];
			if (slope > best_slope) {
				best_slope = slope;
				best_idx = nidx;
			}
		}

		// Route all flow to steepest downhill neighbour.
		if (best_idx >= 0) {
			flow[best_idx] += my_flow;
		}
	}

	return result;
}

// ── Binding ──────────────────────────────────────────────────────────────

void FlowAccumulation::_bind_methods() {
	ClassDB::bind_method(D_METHOD("compute", "height_data", "resolution"), &FlowAccumulation::compute);
}
