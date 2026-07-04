#ifndef ICOSPHERE_MAPPER_H
#define ICOSPHERE_MAPPER_H

#include "core/math/vector2.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/variant/variant.h"

/// IcosphereMapper — Maps between 3D sphere directions and icosahedral triangle UV coords.
///
/// An icosahedron has 20 triangular faces. Each face is subdivided into a
/// power-of-2 grid forming equilateral-triangle tiles. Each tile stores a
/// 2D heightmap in barycentric parameterisation.
///
/// Subdivision level L produces 20 × 4^L tiles. At L=0, 20 tiles.
/// At L=3, 20 × 64 = 1280 tiles — each covering ~200 km² on a 40 km radius planet.
///
/// Key advantage over cube-map: no pole distortion, uniform tile area across
/// the entire sphere, and natural seamless stitching along shared edges.
///
/// The heightmap within each triangular tile uses a "triangular grid":
///   For resolution R, the triangle is parameterised with barycentric coords
///   (u, v) where u+v <= 1.  Samples are placed at:
///     (i/R, j/R) for i=0..R, j=0..R-i  → total samples = (R+1)(R+2)/2
///
///   This gives exactly (R+1)(R+2)/2 samples per tile, with shared vertices
///   along edges for seamless stitching.
///
/// THREADING: After build(), all data is read-only and thread-safe.

class IcosphereMapper : public RefCounted {
	GDCLASS(IcosphereMapper, RefCounted);

public:
	IcosphereMapper();

	/// Build the icosphere with given subdivision level.
	/// L=0: 20 faces (base icosahedron).
	/// L=1: 80 faces. L=2: 320. L=3: 1280. L=4: 5120.
	void build(int p_subdiv_level);

	/// Get the number of triangular faces.
	int get_face_count() const;

	/// Get the 3 vertex positions (on unit sphere) for a given face index.
	/// Returns PackedVector3Array of length 3.
	PackedVector3Array get_face_vertices(int p_face_idx) const;

	/// Map a unit-sphere direction to (face_index, u, v) barycentric coordinates.
	/// Returns Array [int face, float u, float v].
	/// u,v are barycentric: u ∈ [0,1], v ∈ [0,1-u], w = 1-u-v.
	Array dir_to_face_uv(const Vector3 &p_dir) const;

	/// Map (face_index, u, v) back to a unit-sphere direction.
	Vector3 face_uv_to_dir(int p_face, float p_u, float p_v) const;

	/// Get the 3 neighbouring face indices for a given face (sharing edges).
	/// Returns PackedInt32Array of length 3 (edge 0-1, edge 1-2, edge 2-0).
	PackedInt32Array get_face_neighbours(int p_face_idx) const;

	/// Get adjacency info for edge stitching.
	/// For face `p_face`, edge `p_edge` (0,1,2), returns:
	///   [neighbour_face, neighbour_edge, is_reversed]
	/// where is_reversed indicates if the shared edge runs in opposite direction.
	Array get_edge_adjacency(int p_face, int p_edge) const;

	/// Get the number of heightmap samples per triangular tile for given resolution.
	/// = (R+1)(R+2)/2
	static int get_samples_per_tile(int p_resolution);

	/// Convert barycentric (u,v) to a linear sample index within a tile of given resolution.
	/// Row i (u-row) has (R+1-i) samples. Index = sum_{k=0}^{i-1} (R+1-k) + j.
	static int bary_to_index(float p_u, float p_v, int p_resolution);

	/// Convert linear sample index back to barycentric (u,v).
	static Vector2 index_to_bary(int p_index, int p_resolution);

	/// Convert barycentric (u,v) within a face to a 3D unit-sphere direction.
	/// This is the per-sample version — maps grid index to world position.
	Vector3 sample_to_dir(int p_face, int p_i, int p_j, int p_resolution) const;

	/// Planet radius — used for world-space coordinate generation.
	void set_planet_radius(float p_radius);
	float get_planet_radius() const;

	/// Get all face vertex data as flat arrays for GPU upload.
	/// Returns Dictionary { "vertices": PackedVector3Array, "indices": PackedInt32Array }
	Dictionary get_mesh_data() const;

protected:
	static void _bind_methods();

private:
	struct Face {
		int v[3]; // indices into vertices array
		int neighbours[3]; // face indices sharing edge 0-1, 1-2, 2-0
		int neighbour_edges[3]; // which edge of the neighbour is shared
		bool neighbour_reversed[3]; // is the shared edge traversed in reverse?
	};

	struct Edge {
		int v0, v1; // vertex indices (v0 < v1 for canonical ordering)
		int face_a, edge_a; // first face and edge index
		int face_b, edge_b; // second face and edge index (set during build)
	};

	Vector<Vector3> vertices; // Unit-sphere vertices
	Vector<Face> faces;
	Vector<Edge> edges;
	int subdiv_level = 0;
	float planet_radius = 40000.0f;

	// Spatial acceleration: which face's circumscribed cone contains a direction.
	// We store face centres and precompute dot-product thresholds.
	Vector<Vector3> face_centres;
	Vector<float> face_dot_thresholds; // min dot product to be "inside" the face

	/// Build base icosahedron (20 faces, 12 vertices).
	void _build_base_icosahedron();

	/// Subdivide all faces once (each triangle → 4 triangles).
	void _subdivide_once();

	/// Build adjacency tables after final subdivision.
	void _build_adjacency();

	/// Build spatial acceleration structure (face centres + thresholds).
	void _build_spatial_accel();

	/// Get or create a vertex at the midpoint of edge (v0,v1), projected to unit sphere.
	int _get_midpoint(int v0, int v1, HashMap<uint64_t, int> &cache);

	/// Find which face contains a direction. Uses spatial acceleration.
	int _find_face(const Vector3 &dir) const;

	/// Compute barycentric coordinates of a point projected onto a triangle.
	static Vector3 _barycentric(const Vector3 &p, const Vector3 &a, const Vector3 &b, const Vector3 &c);
};

#endif // ICOSPHERE_MAPPER_H
