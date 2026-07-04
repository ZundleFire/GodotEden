#include "icosphere_mapper.h"
#include "core/math/math_funcs.h"
#include "core/templates/hash_map.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// ══════════════════════════════════════════════════════════════════════════════
// IcosphereMapper implementation
// ══════════════════════════════════════════════════════════════════════════════

IcosphereMapper::IcosphereMapper() {
}

// ── Bind methods ──────────────────────────────────────────────────────────────

void IcosphereMapper::_bind_methods() {
	ClassDB::bind_method(D_METHOD("build", "subdiv_level"), &IcosphereMapper::build);
	ClassDB::bind_method(D_METHOD("get_face_count"), &IcosphereMapper::get_face_count);
	ClassDB::bind_method(D_METHOD("get_face_vertices", "face_idx"), &IcosphereMapper::get_face_vertices);
	ClassDB::bind_method(D_METHOD("dir_to_face_uv", "dir"), &IcosphereMapper::dir_to_face_uv);
	ClassDB::bind_method(D_METHOD("face_uv_to_dir", "face", "u", "v"), &IcosphereMapper::face_uv_to_dir);
	ClassDB::bind_method(D_METHOD("get_face_neighbours", "face_idx"), &IcosphereMapper::get_face_neighbours);
	ClassDB::bind_method(D_METHOD("get_edge_adjacency", "face", "edge"), &IcosphereMapper::get_edge_adjacency);
	ClassDB::bind_static_method("IcosphereMapper", D_METHOD("get_samples_per_tile", "resolution"), &IcosphereMapper::get_samples_per_tile);
	ClassDB::bind_method(D_METHOD("sample_to_dir", "face", "i", "j", "resolution"), &IcosphereMapper::sample_to_dir);
	ClassDB::bind_method(D_METHOD("set_planet_radius", "radius"), &IcosphereMapper::set_planet_radius);
	ClassDB::bind_method(D_METHOD("get_planet_radius"), &IcosphereMapper::get_planet_radius);
	ClassDB::bind_method(D_METHOD("get_mesh_data"), &IcosphereMapper::get_mesh_data);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "planet_radius"), "set_planet_radius", "get_planet_radius");
}

// ── Build ─────────────────────────────────────────────────────────────────────

void IcosphereMapper::build(int p_subdiv_level) {
	ERR_FAIL_COND_MSG(p_subdiv_level < 0 || p_subdiv_level > 8,
		"Subdivision level must be 0-8");

	subdiv_level = p_subdiv_level;
	vertices.clear();
	faces.clear();
	edges.clear();

	_build_base_icosahedron();

	for (int i = 0; i < p_subdiv_level; i++) {
		_subdivide_once();
	}

	_build_adjacency();
	_build_spatial_accel();
}

// ── Base icosahedron ──────────────────────────────────────────────────────────

void IcosphereMapper::_build_base_icosahedron() {
	// 12 vertices of a regular icosahedron inscribed in unit sphere.
	// Golden ratio for vertex placement.
	const float PHI = (1.0f + Math::sqrt(5.0f)) / 2.0f;
	const float inv_len = 1.0f / Math::sqrt(1.0f + PHI * PHI);

	// Vertices: all permutations of (0, ±1, ±φ), normalised.
	vertices.push_back(Vector3(-1, PHI, 0).normalized());  // 0
	vertices.push_back(Vector3(1, PHI, 0).normalized());   // 1
	vertices.push_back(Vector3(-1, -PHI, 0).normalized()); // 2
	vertices.push_back(Vector3(1, -PHI, 0).normalized());  // 3
	vertices.push_back(Vector3(0, -1, PHI).normalized());  // 4
	vertices.push_back(Vector3(0, 1, PHI).normalized());   // 5
	vertices.push_back(Vector3(0, -1, -PHI).normalized()); // 6
	vertices.push_back(Vector3(0, 1, -PHI).normalized());  // 7
	vertices.push_back(Vector3(PHI, 0, -1).normalized());  // 8
	vertices.push_back(Vector3(PHI, 0, 1).normalized());   // 9
	vertices.push_back(Vector3(-PHI, 0, -1).normalized()); // 10
	vertices.push_back(Vector3(-PHI, 0, 1).normalized());  // 11

	// 20 faces of the icosahedron (CCW winding).
	const int ico_faces[20][3] = {
		{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
		{1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
		{3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
		{4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
	};

	faces.resize(20);
	for (int i = 0; i < 20; i++) {
		faces.write[i].v[0] = ico_faces[i][0];
		faces.write[i].v[1] = ico_faces[i][1];
		faces.write[i].v[2] = ico_faces[i][2];
		faces.write[i].neighbours[0] = -1;
		faces.write[i].neighbours[1] = -1;
		faces.write[i].neighbours[2] = -1;
	}
}

// ── Subdivision ───────────────────────────────────────────────────────────────

int IcosphereMapper::_get_midpoint(int v0, int v1, HashMap<uint64_t, int> &cache) {
	// Canonical edge key: smaller index first.
	int lo = MIN(v0, v1);
	int hi = MAX(v0, v1);
	uint64_t key = ((uint64_t)lo << 32) | (uint64_t)hi;

	HashMap<uint64_t, int>::Iterator it = cache.find(key);
	if (it != cache.end()) {
		return it->value;
	}

	// Create new vertex at midpoint, projected to unit sphere.
	Vector3 mid = (vertices[v0] + vertices[v1]).normalized();
	int idx = vertices.size();
	vertices.push_back(mid);
	cache.insert(key, idx);
	return idx;
}

void IcosphereMapper::_subdivide_once() {
	HashMap<uint64_t, int> midpoint_cache;
	Vector<Face> new_faces;
	new_faces.resize(faces.size() * 4);

	for (int i = 0; i < faces.size(); i++) {
		const Face &f = faces[i];
		int a = f.v[0];
		int b = f.v[1];
		int c = f.v[2];

		// Midpoints of each edge.
		int ab = _get_midpoint(a, b, midpoint_cache);
		int bc = _get_midpoint(b, c, midpoint_cache);
		int ca = _get_midpoint(c, a, midpoint_cache);

		// 4 sub-triangles (same winding as parent).
		int base = i * 4;
		new_faces.write[base + 0].v[0] = a;
		new_faces.write[base + 0].v[1] = ab;
		new_faces.write[base + 0].v[2] = ca;

		new_faces.write[base + 1].v[0] = ab;
		new_faces.write[base + 1].v[1] = b;
		new_faces.write[base + 1].v[2] = bc;

		new_faces.write[base + 2].v[0] = ca;
		new_faces.write[base + 2].v[1] = bc;
		new_faces.write[base + 2].v[2] = c;

		new_faces.write[base + 3].v[0] = ab;
		new_faces.write[base + 3].v[1] = bc;
		new_faces.write[base + 3].v[2] = ca;

		// Neighbours will be rebuilt after subdivision.
		for (int j = 0; j < 4; j++) {
			new_faces.write[base + j].neighbours[0] = -1;
			new_faces.write[base + j].neighbours[1] = -1;
			new_faces.write[base + j].neighbours[2] = -1;
		}
	}

	faces = new_faces;
}

// ── Adjacency ─────────────────────────────────────────────────────────────────

void IcosphereMapper::_build_adjacency() {
	// Build edge → face mapping.
	// Each edge (v0,v1) with v0<v1 is shared by exactly 2 faces.
	HashMap<uint64_t, int> edge_face_map; // key → packed (face_a << 16 | edge_a) for first occurrence

	edges.clear();

	for (int fi = 0; fi < faces.size(); fi++) {
		Face &f = faces.write[fi];
		for (int e = 0; e < 3; e++) {
			int v0 = f.v[e];
			int v1 = f.v[(e + 1) % 3];
			int lo = MIN(v0, v1);
			int hi = MAX(v0, v1);
			uint64_t key = ((uint64_t)lo << 32) | (uint64_t)hi;

			HashMap<uint64_t, int>::Iterator it = edge_face_map.find(key);
			if (it == edge_face_map.end()) {
				// First face touching this edge.
				int packed = (fi << 16) | e;
				edge_face_map.insert(key, packed);
			} else {
				// Second face — link both as neighbours.
				int packed_a = it->value;
				int face_a = packed_a >> 16;
				int edge_a = packed_a & 0xFFFF;

				faces.write[face_a].neighbours[edge_a] = fi;
				faces.write[face_a].neighbour_edges[edge_a] = e;
				f.neighbours[e] = face_a;
				f.neighbour_edges[e] = edge_a;

				// Check if edge is reversed between the two faces.
				// Edge (v0,v1) in face_a and (v0',v1') in fi:
				// If they share the same vertex order, reversed=false; else reversed=true.
				int a_v0 = faces[face_a].v[edge_a];
				int a_v1 = faces[face_a].v[(edge_a + 1) % 3];
				int b_v0 = f.v[e];
				int b_v1 = f.v[(e + 1) % 3];
				bool reversed = (a_v0 == b_v1 && a_v1 == b_v0);
				faces.write[face_a].neighbour_reversed[edge_a] = reversed;
				f.neighbour_reversed[e] = reversed;

				// Store edge info.
				Edge edge;
				edge.v0 = lo;
				edge.v1 = hi;
				edge.face_a = face_a;
				edge.edge_a = edge_a;
				edge.face_b = fi;
				edge.edge_b = e;
				edges.push_back(edge);
			}
		}
	}
}

// ── Spatial acceleration ──────────────────────────────────────────────────────

void IcosphereMapper::_build_spatial_accel() {
	face_centres.resize(faces.size());
	face_dot_thresholds.resize(faces.size());

	for (int i = 0; i < faces.size(); i++) {
		const Face &f = faces[i];
		Vector3 centre = (vertices[f.v[0]] + vertices[f.v[1]] + vertices[f.v[2]]).normalized();
		face_centres.write[i] = centre;

		// Threshold = min dot product of the centre with any vertex.
		// A point is "near" this face if dot(dir, centre) >= threshold - margin.
		float min_dot = 1.0f;
		for (int j = 0; j < 3; j++) {
			float d = centre.dot(vertices[f.v[j]]);
			if (d < min_dot) {
				min_dot = d;
			}
		}
		// Add a small margin for numerical safety.
		face_dot_thresholds.write[i] = min_dot - 0.01f;
	}
}

// ── Face finding ──────────────────────────────────────────────────────────────

int IcosphereMapper::_find_face(const Vector3 &dir) const {
	// Fast path: find the face whose centre has the highest dot product with dir.
	float best_dot = -2.0f;
	int best_face = 0;

	const Vector3 *centres = face_centres.ptr();
	int n = faces.size();

	for (int i = 0; i < n; i++) {
		float d = dir.dot(centres[i]);
		if (d > best_dot) {
			best_dot = d;
			best_face = i;
		}
	}

	return best_face;
}

// ── Barycentric coordinates ───────────────────────────────────────────────────

Vector3 IcosphereMapper::_barycentric(const Vector3 &p, const Vector3 &a, const Vector3 &b, const Vector3 &c) {
	// Compute barycentric coordinates of p w.r.t. triangle (a,b,c) on the sphere.
	// Project p onto the plane of (a,b,c), then compute standard barycentrics.
	Vector3 v0 = b - a;
	Vector3 v1 = c - a;
	Vector3 v2 = p - a;

	float d00 = v0.dot(v0);
	float d01 = v0.dot(v1);
	float d11 = v1.dot(v1);
	float d20 = v2.dot(v0);
	float d21 = v2.dot(v1);

	float denom = d00 * d11 - d01 * d01;
	if (Math::abs(denom) < 1e-12f) {
		return Vector3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);
	}

	float inv_denom = 1.0f / denom;
	float v = (d11 * d20 - d01 * d21) * inv_denom; // weight of vertex b
	float w = (d00 * d21 - d01 * d20) * inv_denom; // weight of vertex c
	float u = 1.0f - v - w; // weight of vertex a

	return Vector3(u, v, w);
}

// ── Public API ────────────────────────────────────────────────────────────────

int IcosphereMapper::get_face_count() const {
	return faces.size();
}

void IcosphereMapper::set_planet_radius(float p_radius) {
	planet_radius = p_radius;
}

float IcosphereMapper::get_planet_radius() const {
	return planet_radius;
}

PackedVector3Array IcosphereMapper::get_face_vertices(int p_face_idx) const {
	ERR_FAIL_INDEX_V(p_face_idx, faces.size(), PackedVector3Array());
	PackedVector3Array result;
	result.resize(3);
	const Face &f = faces[p_face_idx];
	result.write[0] = vertices[f.v[0]];
	result.write[1] = vertices[f.v[1]];
	result.write[2] = vertices[f.v[2]];
	return result;
}

Array IcosphereMapper::dir_to_face_uv(const Vector3 &p_dir) const {
	ERR_FAIL_COND_V_MSG(faces.size() == 0, Array(), "Must call build() first");

	Vector3 dir = p_dir.normalized();
	int face_idx = _find_face(dir);
	const Face &f = faces[face_idx];

	Vector3 bary = _barycentric(dir,
		vertices[f.v[0]], vertices[f.v[1]], vertices[f.v[2]]);

	// Clamp to valid barycentric range.
	float u = CLAMP(bary.y, 0.0f, 1.0f); // weight of v1 → "u" in triangular grid
	float v = CLAMP(bary.z, 0.0f, 1.0f - u); // weight of v2 → "v"

	Array result;
	result.resize(3);
	result[0] = face_idx;
	result[1] = u;
	result[2] = v;
	return result;
}

Vector3 IcosphereMapper::face_uv_to_dir(int p_face, float p_u, float p_v) const {
	ERR_FAIL_INDEX_V(p_face, faces.size(), Vector3(0, 1, 0));

	const Face &f = faces[p_face];
	float w = 1.0f - p_u - p_v;

	// Interpolate on the sphere: weighted sum of vertex directions, renormalised.
	Vector3 pos = vertices[f.v[0]] * w + vertices[f.v[1]] * p_u + vertices[f.v[2]] * p_v;
	return pos.normalized();
}

Vector3 IcosphereMapper::sample_to_dir(int p_face, int p_i, int p_j, int p_resolution) const {
	ERR_FAIL_INDEX_V(p_face, faces.size(), Vector3(0, 1, 0));

	float u = (float)p_i / (float)p_resolution;
	float v = (float)p_j / (float)p_resolution;
	return face_uv_to_dir(p_face, u, v);
}

PackedInt32Array IcosphereMapper::get_face_neighbours(int p_face_idx) const {
	ERR_FAIL_INDEX_V(p_face_idx, faces.size(), PackedInt32Array());
	PackedInt32Array result;
	result.resize(3);
	const Face &f = faces[p_face_idx];
	result.write[0] = f.neighbours[0];
	result.write[1] = f.neighbours[1];
	result.write[2] = f.neighbours[2];
	return result;
}

Array IcosphereMapper::get_edge_adjacency(int p_face, int p_edge) const {
	ERR_FAIL_INDEX_V(p_face, faces.size(), Array());
	ERR_FAIL_COND_V(p_edge < 0 || p_edge > 2, Array());

	const Face &f = faces[p_face];
	Array result;
	result.resize(3);
	result[0] = f.neighbours[p_edge];
	result[1] = f.neighbour_edges[p_edge];
	result[2] = f.neighbour_reversed[p_edge];
	return result;
}

int IcosphereMapper::get_samples_per_tile(int p_resolution) {
	return (p_resolution + 1) * (p_resolution + 2) / 2;
}

int IcosphereMapper::bary_to_index(float p_u, float p_v, int p_resolution) {
	int R = p_resolution;
	int i = CLAMP((int)Math::round(p_u * R), 0, R);
	int j = CLAMP((int)Math::round(p_v * R), 0, R - i);

	// Row i starts at: sum_{k=0}^{i-1} (R+1-k) = i*(R+1) - i*(i-1)/2
	int row_start = i * (R + 1) - i * (i - 1) / 2;
	return row_start + j;
}

Vector2 IcosphereMapper::index_to_bary(int p_index, int p_resolution) {
	int R = p_resolution;
	// Find which row (i) this index belongs to.
	int cumulative = 0;
	int i = 0;
	for (; i <= R; i++) {
		int row_size = R + 1 - i;
		if (cumulative + row_size > p_index) {
			break;
		}
		cumulative += row_size;
	}
	int j = p_index - cumulative;
	return Vector2((float)i / (float)R, (float)j / (float)R);
}

Dictionary IcosphereMapper::get_mesh_data() const {
	PackedVector3Array mesh_verts;
	PackedInt32Array mesh_indices;

	mesh_verts.resize(vertices.size());
	for (int i = 0; i < vertices.size(); i++) {
		mesh_verts.write[i] = vertices[i];
	}

	mesh_indices.resize(faces.size() * 3);
	for (int i = 0; i < faces.size(); i++) {
		mesh_indices.write[i * 3 + 0] = faces[i].v[0];
		mesh_indices.write[i * 3 + 1] = faces[i].v[1];
		mesh_indices.write[i * 3 + 2] = faces[i].v[2];
	}

	Dictionary result;
	result["vertices"] = mesh_verts;
	result["indices"] = mesh_indices;
	return result;
}
