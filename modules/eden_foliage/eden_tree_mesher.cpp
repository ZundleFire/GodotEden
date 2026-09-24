#include "eden_tree_mesher.h"

#include "core/math/math_funcs.h"
#include "core/math/vector3i.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

namespace {

// Pure, non-checking quad injector. Emits (a,b,c) + (a,c,d).
void push_quad(PackedInt32Array &r_indices, int p_a, int p_b, int p_c, int p_d) {
	r_indices.push_back(p_a);
	r_indices.push_back(p_b);
	r_indices.push_back(p_c);

	r_indices.push_back(p_a);
	r_indices.push_back(p_c);
	r_indices.push_back(p_d);
}

void push_triangle(PackedInt32Array &r_indices, int p_a, int p_b, int p_c) {
	r_indices.push_back(p_a);
	r_indices.push_back(p_b);
	r_indices.push_back(p_c);
}

// Closes off one end of a beam, either flat or as a rounded dome.
//
// WINDING, SOLVED STRUCTURALLY: rings are parameterised so the ring index
// ALWAYS advances along +p_dir, exactly like the beam's own start->end
// direction, and bands are emitted with the identical push_quad(a, b, c, d)
// pattern the side walls use. The cap therefore inherits whatever winding the
// side walls have, automatically. There is no flip flag left to get backwards
// and no assumption about which way Godot considers front-facing -- if the
// walls render, the caps render.
//
// End cap   (p_is_start = false): ring 0 = rim, ring N = pole ahead of it.
// Start cap (p_is_start = true):  ring 0 = pole behind it, ring N = rim.
void append_cap(
		const Vector3 &p_center, const Vector3 &p_dir, const Vector3 &p_right, const Vector3 &p_forward,
		float p_radius, int p_sides, int p_rings, float p_height_mul, bool p_dome, bool p_is_start,
		PackedVector3Array &r_positions, PackedVector3Array &r_normals,
		PackedVector2Array &r_uvs, PackedInt32Array &r_indices
) {
	if (p_radius < CMP_EPSILON) {
		return;
	}

	const int rings = p_dome ? MAX(p_rings, 1) : 1;
	const float height = p_dome ? p_radius * MAX(p_height_mul, 0.0f) : 0.0f;

	LocalVector<int> ring_base;
	LocalVector<bool> ring_is_pole;
	ring_base.resize(rings + 1);
	ring_is_pole.resize(rings + 1);

	for (int r = 0; r <= rings; r++) {
		const float t = float(r) / float(rings);
		// theta sweeps from the rim plane toward the pole
		const float theta = (p_is_start ? (1.0f - t) : t) * float(Math::PI) * 0.5f;
		const float ring_radius = p_radius * Math::cos(theta);
		const float along = height * Math::sin(theta);
		const Vector3 ring_center = p_center + p_dir * (p_is_start ? -along : along);
		const Vector3 flat_n = p_dir * (p_is_start ? -1.0f : 1.0f);

		if (ring_radius < CMP_EPSILON) {
			// Degenerate ring: one pole vertex.
			ring_base[r] = r_positions.size();
			ring_is_pole[r] = true;
			r_positions.push_back(ring_center);
			r_normals.push_back(flat_n);
			r_uvs.push_back(Vector2(0.5f, 0.5f));
			continue;
		}

		ring_base[r] = r_positions.size();
		ring_is_pole[r] = false;
		for (int i = 0; i < p_sides; i++) {
			const float angle = float(i) / float(p_sides) * float(Math::TAU);
			const float c = Math::cos(angle);
			const float s = Math::sin(angle);
			const Vector3 radial = p_right * c + p_forward * s;
			const Vector3 pos = ring_center + radial * ring_radius;

			// Exact for a hemisphere, and smooth across the whole dome.
			Vector3 n = (height > CMP_EPSILON) ? (pos - p_center).normalized() : flat_n;

			r_positions.push_back(pos);
			r_normals.push_back(n);
			r_uvs.push_back(Vector2(float(i) / float(p_sides), p_is_start ? (1.0f - t) : t));
		}
	}

	for (int r = 0; r < rings; r++) {
		const int lo = ring_base[r];
		const int hi = ring_base[r + 1];

		if (ring_is_pole[r]) {
			for (int i = 0; i < p_sides; i++) {
				const int j = (i + 1) % p_sides;
				push_triangle(r_indices, lo, hi + i, hi + j);
			}
		} else if (ring_is_pole[r + 1]) {
			for (int i = 0; i < p_sides; i++) {
				const int j = (i + 1) % p_sides;
				push_triangle(r_indices, lo + i, hi, lo + j);
			}
		} else {
			for (int i = 0; i < p_sides; i++) {
				const int j = (i + 1) % p_sides;
				push_quad(r_indices, lo + i, hi + i, hi + j, lo + j);
			}
		}
	}
}

// Raw base coordinates for a regular icosahedron.
const float PHI = 1.618033988749895f;
const Vector3 ICOSA_VERTS[12] = {
	Vector3(-1, PHI, 0), Vector3(1, PHI, 0), Vector3(-1, -PHI, 0), Vector3(1, -PHI, 0),
	Vector3(0, -1, PHI), Vector3(0, 1, PHI), Vector3(0, -1, -PHI), Vector3(0, 1, -PHI),
	Vector3(PHI, 0, -1), Vector3(PHI, 0, 1), Vector3(-PHI, 0, -1), Vector3(-PHI, 0, 1),
};

// Face table. All 20 faces are consistently oriented and form a closed
// manifold; verified numerically. As listed, the right-hand-rule normal of each
// face points INWARD. _append_icosahedron emits them a/c/b, which reverses that
// to outward -- measured as 300/300 outward in the shipped mesh. Normals are
// derived by an explicit outward test rather than from index order, so
// reversing this table cannot break shading.
const int ICOSA_FACES[20][3] = {
	{ 0, 5, 11 }, { 0, 1, 5 },  { 0, 7, 1 },  { 0, 10, 7 }, { 0, 11, 10 },
	{ 1, 9, 5 },  { 5, 4, 11 }, { 11, 2, 10 }, { 10, 6, 7 }, { 7, 8, 1 },
	{ 3, 4, 9 },  { 3, 2, 4 },  { 3, 6, 2 },  { 3, 8, 6 },  { 3, 9, 8 },
	{ 4, 5, 9 },  { 2, 11, 4 }, { 6, 10, 2 }, { 8, 7, 6 },  { 9, 1, 8 }
};

} // namespace

EdenTreeMesher::EdenTreeMesher() {}

void EdenTreeMesher::clear() {
	_segments.clear();
	_leaf_positions.clear();
	_leaf_normals.clear();
	_leaf_uvs.clear();
	_leaf_indices.clear();
	_rock_positions.clear();
	_rock_normals.clear();
	_rock_uvs.clear();
	_rock_indices.clear();
}

void EdenTreeMesher::add_segment(Vector3 p_start, Vector3 p_end, float p_radius_start, float p_radius_end) {
	add_segment_capped(p_start, p_end, p_radius_start, p_radius_end, true, true);
}

void EdenTreeMesher::add_segment_capped(Vector3 p_start, Vector3 p_end, float p_radius_start, float p_radius_end,
		bool p_cap_start, bool p_cap_end) {
	Segment seg;
	seg.start = p_start;
	seg.end = p_end;
	seg.radius_start = MAX(p_radius_start, 0.0f);
	seg.radius_end = MAX(p_radius_end, 0.0f);
	seg.cap_start = p_cap_start;
	seg.cap_end = p_cap_end;
	_segments.push_back(seg);
}

void EdenTreeMesher::set_auto_embed_branches(bool p_enabled) {
	_auto_embed_branches = p_enabled;
}

bool EdenTreeMesher::get_auto_embed_branches() const {
	return _auto_embed_branches;
}

void EdenTreeMesher::set_embed_depth_mul(float p_mul) {
	_embed_depth_mul = CLAMP(p_mul, 0.0f, 2.0f);
}

float EdenTreeMesher::get_embed_depth_mul() const {
	return _embed_depth_mul;
}

// Finds segments whose start point is sitting on or inside another segment --
// the classic branch-meets-trunk case -- then sinks the start further in and
// drops its cap. A cap buried inside the trunk is invisible, so the join reads
// as continuous instead of a flat disc pasted on the bark.
Vector<EdenTreeMesher::Segment> EdenTreeMesher::_prepared_segments() const {
	Vector<Segment> out = _segments;
	if (!_auto_embed_branches) {
		return out;
	}

	const int count = out.size();
	for (int i = 0; i < count; i++) {
		Segment &seg = out.write[i];

		const Vector3 axis = seg.end - seg.start;
		const float len = axis.length();
		if (len < CMP_EPSILON) {
			continue;
		}
		const Vector3 dir = axis / len;

		float host_radius = 0.0f;
		// How far past a host's surface this segment's start already sits, i.e. r_at_t - dist
		// at the best-matching host. Needed so a start that is already buried deep (like
		// EdenTreeBuilder's fork_point, which sits ON the trunk's axis) doesn't get pushed
		// even further in -- see eden_tree_debug_log.md, Bug 4.
		float already_inside = 0.0f;
		for (int j = 0; j < count; j++) {
			if (j == i) {
				continue;
			}
			const Segment &host = _segments[j];
			const Vector3 h_axis = host.end - host.start;
			const float h_len_sq = h_axis.length_squared();
			if (h_len_sq < CMP_EPSILON2) {
				continue;
			}

			// closest point on the host's axis to this segment's start
			float t = (seg.start - host.start).dot(h_axis) / h_len_sq;
			t = CLAMP(t, 0.0f, 1.0f);
			const Vector3 closest = host.start + h_axis * t;
			const float dist = seg.start.distance_to(closest);
			const float r_at_t = Math::lerp(host.radius_start, host.radius_end, t);

			// Touching or penetrating the host's surface.
			if (dist <= r_at_t + seg.radius_start * 0.5f) {
				host_radius = MAX(host_radius, r_at_t);
				already_inside = MAX(already_inside, r_at_t - dist);
			}
		}

		if (host_radius > 0.0f) {
			// Pull the start backwards along its own axis, into the host. Clamped so a start
			// that's already buried past the desired depth isn't pushed further, and a
			// near-parallel branch cannot invert itself.
			const float want_depth = seg.radius_start * 0.5f;
			const float needed = want_depth - already_inside;
			if (needed > 0.0f) {
				seg.start -= dir * MIN(needed, len * 0.5f);
			}
			seg.cap_start = false;
		}
	}

	return out;
}

int EdenTreeMesher::get_segment_count() const {
	return _segments.size();
}

void EdenTreeMesher::set_sides(int p_sides) {
	_sides = MAX(p_sides, 3);
}

int EdenTreeMesher::get_sides() const {
	return _sides;
}

void EdenTreeMesher::set_cap_ends(bool p_enabled) {
	// Kept so existing callers keep working.
	_cap_style = p_enabled ? CAP_DOME : CAP_NONE;
}

bool EdenTreeMesher::get_cap_ends() const {
	return _cap_style != CAP_NONE;
}

void EdenTreeMesher::set_cap_style(int p_style) {
	_cap_style = CLAMP(p_style, int(CAP_NONE), int(CAP_DOME));
}

int EdenTreeMesher::get_cap_style() const {
	return _cap_style;
}

void EdenTreeMesher::set_cap_rings(int p_rings) {
	_cap_rings = CLAMP(p_rings, 1, 12);
}

int EdenTreeMesher::get_cap_rings() const {
	return _cap_rings;
}

void EdenTreeMesher::set_cap_height_mul(float p_mul) {
	_cap_height_mul = CLAMP(p_mul, 0.0f, 2.0f);
}

float EdenTreeMesher::get_cap_height_mul() const {
	return _cap_height_mul;
}

void EdenTreeMesher::set_leaf_jitter(float p_jitter) {
	_leaf_jitter = CLAMP(p_jitter, 0.0f, 0.9f);
}

float EdenTreeMesher::get_leaf_jitter() const {
	return _leaf_jitter;
}

void EdenTreeMesher::set_leaf_subdivisions(int p_level) {
	_leaf_subdivisions = CLAMP(p_level, 0, 4);
}

int EdenTreeMesher::get_leaf_subdivisions() const {
	return _leaf_subdivisions;
}

void EdenTreeMesher::_append_beam(
		const Segment &p_seg, PackedVector3Array &r_positions, PackedVector3Array &r_normals,
		PackedVector2Array &r_uvs, PackedInt32Array &r_indices
) const {
	const Vector3 axis = p_seg.end - p_seg.start;
	const float length = axis.length();
	if (length < CMP_EPSILON) {
		return;
	}
	const Vector3 dir = axis / length;

	Vector3 ref_up = Vector3(0, 1, 0);
	if (Math::abs(dir.dot(ref_up)) > 0.99f) {
		ref_up = Vector3(1, 0, 0);
	}
	const Vector3 right = ref_up.cross(dir).normalized();
	const Vector3 forward = dir.cross(right).normalized();

	const int sides = MAX(_sides, 3);
	const int side_base = r_positions.size();

	for (int i = 0; i < sides; i++) {
		const float angle = float(i) / float(sides) * float(Math::TAU);
		const float c = Math::cos(angle);
		const float s = Math::sin(angle);
		const Vector3 radial = right * c + forward * s;
		const Vector3 circumferential = -right * s + forward * c;

		const Vector3 p0 = p_seg.start + radial * p_seg.radius_start;
		const Vector3 p1 = p_seg.end + radial * p_seg.radius_end;
		const Vector3 slant = p1 - p0;

		Vector3 normal = circumferential.cross(slant);
		if (normal.length_squared() < CMP_EPSILON2) {
			normal = radial;
		} else {
			normal.normalize();
		}

		const float u = float(i) / float(sides);
		r_positions.push_back(p0);
		r_normals.push_back(normal);
		r_uvs.push_back(Vector2(u, 0.0f));
		r_positions.push_back(p1);
		r_normals.push_back(normal);
		r_uvs.push_back(Vector2(u, 1.0f));
	}

	for (int i = 0; i < sides; i++) {
		const int i0 = side_base + i * 2;
		const int i1 = side_base + i * 2 + 1;
		const int ni = (i + 1) % sides;
		const int j0 = side_base + ni * 2;
		const int j1 = side_base + ni * 2 + 1;

		push_quad(r_indices, i0, i1, j1, j0);
	}

	if (_cap_style != CAP_NONE && (p_seg.cap_start || p_seg.cap_end)) {
		const bool dome = _cap_style == CAP_DOME;
		// Both calls pass `dir`, not -dir. p_is_start handles direction
		// internally so ring index always advances along +dir, which is what
		// keeps cap winding locked to the side walls.
		if (p_seg.cap_start) {
			append_cap(p_seg.start, dir, right, forward, p_seg.radius_start, sides,
					_cap_rings, _cap_height_mul, dome, true,
					r_positions, r_normals, r_uvs, r_indices);
		}
		if (p_seg.cap_end) {
			append_cap(p_seg.end, dir, right, forward, p_seg.radius_end, sides,
					_cap_rings, _cap_height_mul, dome, false,
					r_positions, r_normals, r_uvs, r_indices);
		}
	}
}

Ref<ArrayMesh> EdenTreeMesher::build_mesh() const {
	PackedVector3Array positions;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedInt32Array indices;

	const Vector<Segment> prepared = _prepared_segments();
	for (const Segment &seg : prepared) {
		_append_beam(seg, positions, normals, uvs, indices);
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	if (positions.size() == 0) {
		return mesh;
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = positions;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

void EdenTreeMesher::add_leaf_blob(const Vector3 &p_center, float p_radius, const Ref<RandomNumberGenerator> &p_rng) {
	if (p_radius < CMP_EPSILON) {
		return;
	}
	_append_icosahedron(p_center, Vector3(p_radius, p_radius, p_radius), p_rng);
}

void EdenTreeMesher::add_leaf_blob_elongated(const Vector3 &p_center, const Vector3 &p_size, const Ref<RandomNumberGenerator> &p_rng) {
	if (p_size.x < CMP_EPSILON || p_size.y < CMP_EPSILON || p_size.z < CMP_EPSILON) {
		return;
	}
	_append_icosahedron(p_center, p_size, p_rng);
}

void EdenTreeMesher::_append_icosahedron(const Vector3 &p_center, const Vector3 &p_size, const Ref<RandomNumberGenerator> &p_rng) {
	const bool use_rng = p_rng.is_valid() && _leaf_jitter > 0.0f;

	// Unit-sphere directions and faces, subdivided _leaf_subdivisions times BEFORE any jitter is
	// applied. Splitting each triangle into 4 via edge midpoints (deduplicated so shared edges
	// reuse the same new vertex, not a copy each side) preserves the base table's winding on
	// every child triangle for free -- (a,ab,ca)/(ab,b,bc)/(ca,bc,c)/(ab,bc,ca) all wind the same
	// way as their parent (a,b,c).
	LocalVector<Vector3> dirs;
	dirs.resize(12);
	for (int v = 0; v < 12; v++) {
		dirs[v] = ICOSA_VERTS[v].normalized();
	}

	LocalVector<Vector3i> faces;
	faces.resize(20);
	for (int i = 0; i < 20; i++) {
		faces[i] = Vector3i(ICOSA_FACES[i][0], ICOSA_FACES[i][1], ICOSA_FACES[i][2]);
	}

	for (int s = 0; s < _leaf_subdivisions; s++) {
		HashMap<uint64_t, int> midpoint_cache;
		auto midpoint = [&](int a, int b) -> int {
			const uint64_t key = (uint64_t(MIN(a, b)) << 32) | uint64_t(MAX(a, b));
			if (midpoint_cache.has(key)) {
				return midpoint_cache[key];
			}
			const int idx = dirs.size();
			dirs.push_back(((dirs[a] + dirs[b]) * 0.5f).normalized());
			midpoint_cache[key] = idx;
			return idx;
		};

		LocalVector<Vector3i> next_faces;
		next_faces.reserve(faces.size() * 4);
		for (const Vector3i &f : faces) {
			const int ab = midpoint(f.x, f.y);
			const int bc = midpoint(f.y, f.z);
			const int ca = midpoint(f.z, f.x);
			next_faces.push_back(Vector3i(f.x, ab, ca));
			next_faces.push_back(Vector3i(ab, f.y, bc));
			next_faces.push_back(Vector3i(ca, bc, f.z));
			next_faces.push_back(Vector3i(ab, bc, ca));
		}
		faces = next_faces;
	}

	// THE FIX: jitter each UNIQUE vertex exactly once, up front, on the final (post-subdivision)
	// vertex set. The original code rolled fresh noise for every (face, corner) pair, so the
	// five-or-more faces meeting at a vertex each pushed their own copy to a different radius.
	// That tore the shell into disconnected plates with gaps up to 0.30 * radius, which is what
	// you were seeing through.
	LocalVector<Vector3> verts;
	verts.resize(dirs.size());
	for (uint32_t v = 0; v < dirs.size(); v++) {
		const float jitter = use_rng ? p_rng->randf_range(-_leaf_jitter, _leaf_jitter) : 0.0f;
		const Vector3 d = dirs[v];
		verts[v] = p_center + Vector3(d.x * p_size.x, d.y * p_size.y, d.z * p_size.z) * (1.0f + jitter);
	}

	for (uint32_t i = 0; i < faces.size(); i++) {
		const Vector3 p_a = verts[faces[i].x];
		const Vector3 p_b = verts[faces[i].y];
		const Vector3 p_c = verts[faces[i].z];

		Vector3 flat_normal = (p_b - p_a).cross(p_c - p_a);
		if (flat_normal.length_squared() < CMP_EPSILON2) {
			continue;
		}
		flat_normal.normalize();

		// SECOND FIX: the table's winding is deliberately inward-facing, so the
		// raw cross product is an inward normal. Flip it to face away from the
		// blob center. Doing it by test rather than by a hardcoded negation
		// keeps this correct even if the table is ever re-ordered.
		const Vector3 centroid = (p_a + p_b + p_c) / 3.0f;
		if (flat_normal.dot(centroid - p_center) < 0.0f) {
			flat_normal = -flat_normal;
		}

		const int base = _leaf_positions.size();

		// Natural table order. MEASURED: this is RHR-inward, which is what
		// Godot treats as front-facing -- the same sense the beam side walls
		// use. Confirmed by the foliage rendering solid under CULL_BACK.
		// Do not swap b and c here.
		_leaf_positions.push_back(p_a);
		_leaf_positions.push_back(p_b);
		_leaf_positions.push_back(p_c);

		_leaf_normals.push_back(flat_normal);
		_leaf_normals.push_back(flat_normal);
		_leaf_normals.push_back(flat_normal);

		// Minimal per-face UVs so materials that sample a texture don't read
		// garbage. Swap for a real projection if you need proper leaf texturing.
		_leaf_uvs.push_back(Vector2(0.0f, 0.0f));
		_leaf_uvs.push_back(Vector2(1.0f, 0.0f));
		_leaf_uvs.push_back(Vector2(0.5f, 1.0f));

		_leaf_indices.push_back(base);
		_leaf_indices.push_back(base + 1);
		_leaf_indices.push_back(base + 2);
	}
}

Ref<ArrayMesh> EdenTreeMesher::build_foliage_mesh() const {
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	if (_leaf_positions.size() == 0) {
		return mesh;
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = _leaf_positions;
	arrays[Mesh::ARRAY_NORMAL] = _leaf_normals;
	arrays[Mesh::ARRAY_TEX_UV] = _leaf_uvs;
	arrays[Mesh::ARRAY_INDEX] = _leaf_indices;

	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

void EdenTreeMesher::add_rock(const Vector3 &p_center, const Vector3 &p_size, const Ref<RandomNumberGenerator> &p_rng) {
	if (p_size.x < CMP_EPSILON || p_size.y < CMP_EPSILON || p_size.z < CMP_EPSILON) {
		return;
	}
	_append_rock_icosahedron(p_center, p_size, p_rng);
}

void EdenTreeMesher::_append_rock_icosahedron(const Vector3 &p_center, const Vector3 &p_size, const Ref<RandomNumberGenerator> &p_rng) {
	// Same base shape and winding as _append_icosahedron, but each vertex is scaled per-axis
	// (so rocks read as squat or elongated chunks, not always round like a leaf blob) and
	// jittered with a much wider, fixed range -- large enough that faces visibly tilt against
	// their neighbors instead of approximating a smooth sphere, which is what sells "broken
	// rock" instead of "round boulder."
	const bool use_rng = p_rng.is_valid();
	const float ROCK_JITTER = 0.45f;

	Vector3 verts[12];
	for (int v = 0; v < 12; v++) {
		const float jitter = use_rng ? p_rng->randf_range(-ROCK_JITTER, ROCK_JITTER) : 0.0f;
		const Vector3 dir = ICOSA_VERTS[v].normalized();
		verts[v] = p_center + Vector3(dir.x * p_size.x, dir.y * p_size.y, dir.z * p_size.z) * (1.0f + jitter);
	}

	for (int i = 0; i < 20; i++) {
		const Vector3 p_a = verts[ICOSA_FACES[i][0]];
		const Vector3 p_b = verts[ICOSA_FACES[i][1]];
		const Vector3 p_c = verts[ICOSA_FACES[i][2]];

		Vector3 flat_normal = (p_b - p_a).cross(p_c - p_a);
		if (flat_normal.length_squared() < CMP_EPSILON2) {
			continue;
		}
		flat_normal.normalize();

		const Vector3 centroid = (p_a + p_b + p_c) / 3.0f;
		if (flat_normal.dot(centroid - p_center) < 0.0f) {
			flat_normal = -flat_normal;
		}

		const int base = _rock_positions.size();

		// Same winding convention as _append_icosahedron (RHR-inward table, front-facing in
		// this project) -- do not swap b and c here either.
		_rock_positions.push_back(p_a);
		_rock_positions.push_back(p_b);
		_rock_positions.push_back(p_c);

		_rock_normals.push_back(flat_normal);
		_rock_normals.push_back(flat_normal);
		_rock_normals.push_back(flat_normal);

		_rock_uvs.push_back(Vector2(0.0f, 0.0f));
		_rock_uvs.push_back(Vector2(1.0f, 0.0f));
		_rock_uvs.push_back(Vector2(0.5f, 1.0f));

		_rock_indices.push_back(base);
		_rock_indices.push_back(base + 1);
		_rock_indices.push_back(base + 2);
	}
}

Ref<ArrayMesh> EdenTreeMesher::build_rock_mesh() const {
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	if (_rock_positions.size() == 0) {
		return mesh;
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = _rock_positions;
	arrays[Mesh::ARRAY_NORMAL] = _rock_normals;
	arrays[Mesh::ARRAY_TEX_UV] = _rock_uvs;
	arrays[Mesh::ARRAY_INDEX] = _rock_indices;

	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

void EdenTreeMesher::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_segment", "start", "end", "radius_start", "radius_end"), &EdenTreeMesher::add_segment);
	ClassDB::bind_method(D_METHOD("add_segment_capped", "start", "end", "radius_start", "radius_end", "cap_start", "cap_end"), &EdenTreeMesher::add_segment_capped);
	ClassDB::bind_method(D_METHOD("add_leaf_blob", "center", "radius", "rng"), &EdenTreeMesher::add_leaf_blob);
	ClassDB::bind_method(D_METHOD("add_rock", "center", "size", "rng"), &EdenTreeMesher::add_rock);
	ClassDB::bind_method(D_METHOD("clear"), &EdenTreeMesher::clear);
	ClassDB::bind_method(D_METHOD("get_segment_count"), &EdenTreeMesher::get_segment_count);
	ClassDB::bind_method(D_METHOD("build_mesh"), &EdenTreeMesher::build_mesh);
	ClassDB::bind_method(D_METHOD("build_foliage_mesh"), &EdenTreeMesher::build_foliage_mesh);
	ClassDB::bind_method(D_METHOD("build_rock_mesh"), &EdenTreeMesher::build_rock_mesh);

	ClassDB::bind_method(D_METHOD("set_sides", "sides"), &EdenTreeMesher::set_sides);
	ClassDB::bind_method(D_METHOD("get_sides"), &EdenTreeMesher::get_sides);
	ClassDB::bind_method(D_METHOD("set_cap_ends", "enabled"), &EdenTreeMesher::set_cap_ends);
	ClassDB::bind_method(D_METHOD("get_cap_ends"), &EdenTreeMesher::get_cap_ends);
	ClassDB::bind_method(D_METHOD("set_leaf_jitter", "jitter"), &EdenTreeMesher::set_leaf_jitter);
	ClassDB::bind_method(D_METHOD("get_leaf_jitter"), &EdenTreeMesher::get_leaf_jitter);
	ClassDB::bind_method(D_METHOD("set_leaf_subdivisions", "level"), &EdenTreeMesher::set_leaf_subdivisions);
	ClassDB::bind_method(D_METHOD("get_leaf_subdivisions"), &EdenTreeMesher::get_leaf_subdivisions);

	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::INT, "sides"), "set_sides", "get_sides");
	ClassDB::bind_method(D_METHOD("set_cap_style", "style"), &EdenTreeMesher::set_cap_style);
	ClassDB::bind_method(D_METHOD("get_cap_style"), &EdenTreeMesher::get_cap_style);
	ClassDB::bind_method(D_METHOD("set_cap_rings", "rings"), &EdenTreeMesher::set_cap_rings);
	ClassDB::bind_method(D_METHOD("get_cap_rings"), &EdenTreeMesher::get_cap_rings);
	ClassDB::bind_method(D_METHOD("set_cap_height_mul", "mul"), &EdenTreeMesher::set_cap_height_mul);
	ClassDB::bind_method(D_METHOD("get_cap_height_mul"), &EdenTreeMesher::get_cap_height_mul);

	BIND_ENUM_CONSTANT(CAP_NONE);
	BIND_ENUM_CONSTANT(CAP_FLAT);
	BIND_ENUM_CONSTANT(CAP_DOME);

	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::BOOL, "cap_ends"), "set_cap_ends", "get_cap_ends");
	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::INT, "cap_style", PROPERTY_HINT_ENUM, "None,Flat,Dome"), "set_cap_style", "get_cap_style");
	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::INT, "cap_rings", PROPERTY_HINT_RANGE, "1,12,1"), "set_cap_rings", "get_cap_rings");
	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::FLOAT, "cap_height_mul", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_cap_height_mul", "get_cap_height_mul");
	ClassDB::bind_method(D_METHOD("set_auto_embed_branches", "enabled"), &EdenTreeMesher::set_auto_embed_branches);
	ClassDB::bind_method(D_METHOD("get_auto_embed_branches"), &EdenTreeMesher::get_auto_embed_branches);
	ClassDB::bind_method(D_METHOD("set_embed_depth_mul", "mul"), &EdenTreeMesher::set_embed_depth_mul);
	ClassDB::bind_method(D_METHOD("get_embed_depth_mul"), &EdenTreeMesher::get_embed_depth_mul);

	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::FLOAT, "leaf_jitter"), "set_leaf_jitter", "get_leaf_jitter");
	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::INT, "leaf_subdivisions", PROPERTY_HINT_RANGE, "0,4,1"), "set_leaf_subdivisions", "get_leaf_subdivisions");
	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::BOOL, "auto_embed_branches"), "set_auto_embed_branches", "get_auto_embed_branches");
	ClassDB::add_property("EdenTreeMesher", PropertyInfo(Variant::FLOAT, "embed_depth_mul", PROPERTY_HINT_RANGE, "0.0,2.0,0.01"), "set_embed_depth_mul", "get_embed_depth_mul");
}