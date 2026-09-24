#ifndef EDEN_TREE_MESHER_H
#define EDEN_TREE_MESHER_H

#include "core/math/random_number_generator.h"
#include "core/math/vector3.h"
#include "core/object/ref_counted.h"
#include "core/templates/vector.h"
#include "scene/resources/mesh.h"

class EdenTreeMesher : public RefCounted {
	GDCLASS(EdenTreeMesher, RefCounted);

public:
	enum CapStyle {
		CAP_NONE = 0, ///< open tube, nothing drawn at the ends
		CAP_FLAT = 1, ///< flat disc (the old behaviour)
		CAP_DOME = 2, ///< rounded hemisphere
	};

	EdenTreeMesher();

	void add_segment(Vector3 p_start, Vector3 p_end, float p_radius_start, float p_radius_end);
	/// Same, but with explicit per-end cap control. Use this when a segment
	/// starts or ends buried inside other geometry.
	void add_segment_capped(Vector3 p_start, Vector3 p_end, float p_radius_start, float p_radius_end,
			bool p_cap_start, bool p_cap_end);
	void clear();
	int get_segment_count() const;
	Ref<ArrayMesh> build_mesh() const;

	void set_sides(int p_sides);
	int get_sides() const;

	void set_cap_ends(bool p_enabled);
	bool get_cap_ends() const;

	void set_cap_style(int p_style);
	int get_cap_style() const;

	/// Latitude subdivisions in a dome cap. 1 collapses to a cone, 3-4 reads
	/// as round on a low-poly tree, above ~6 is wasted triangles.
	void set_cap_rings(int p_rings);
	int get_cap_rings() const;

	/// Dome height as a fraction of the beam radius. 1.0 is a true hemisphere,
	/// lower values give a squashed nub that hides flush against a trunk.
	void set_cap_height_mul(float p_mul);
	float get_cap_height_mul() const;

	void add_leaf_blob(const Vector3 &p_center, float p_radius, const Ref<RandomNumberGenerator> &p_rng);
	/// Same, but scaled per-axis instead of uniformly -- e.g. a tall, narrow leaf cluster
	/// (small x/z, large y) instead of a round blob. Uses the same smooth leaf_jitter/
	/// leaf_subdivisions as add_leaf_blob(), unlike EdenRockMesher's angular add_rock().
	void add_leaf_blob_elongated(const Vector3 &p_center, const Vector3 &p_size, const Ref<RandomNumberGenerator> &p_rng);
	Ref<ArrayMesh> build_foliage_mesh() const;

	void set_leaf_jitter(float p_jitter);
	float get_leaf_jitter() const;

	/// Icosphere subdivision level for leaf blobs. 0 = the base 20-triangle icosahedron
	/// (the original low-poly look); each +1 quadruples the triangle count (0=20, 1=80, 2=320,
	/// 3=1280, 4=5120) for a rounder, less faceted blob. Clamped to 0-4 -- beyond that the
	/// per-blob triangle count stops buying any visible roundness increase for the cost.
	void set_leaf_subdivisions(int p_level);
	int get_leaf_subdivisions() const;

	/// Adds one angular rock chunk: same base icosahedron as add_leaf_blob, but scaled
	/// per-axis (so rocks can be squat or elongated instead of always spherical) and jittered
	/// with a much larger, fixed range tuned for a broken-rock look rather than a round leaf
	/// blob -- unlike leaf jitter this isn't a tunable property, since "how angular a rock
	/// looks" isn't something callers have needed to vary yet.
	void add_rock(const Vector3 &p_center, const Vector3 &p_size, const Ref<RandomNumberGenerator> &p_rng);
	Ref<ArrayMesh> build_rock_mesh() const;

	/// Detects segments whose start point sits inside another segment's volume
	/// (branches meeting a trunk), drops their start cap, and pushes the start
	/// deeper in so the join reads as continuous instead of a disc on the bark.
	void set_auto_embed_branches(bool p_enabled);
	bool get_auto_embed_branches() const;

	/// How far to sink an embedded start, as a fraction of the host segment's
	/// radius at the contact point. 1.0 reaches the host's axis.
	void set_embed_depth_mul(float p_mul);
	float get_embed_depth_mul() const;

protected:
	static void _bind_methods();

private:
	struct Segment {
		Vector3 start;
		Vector3 end;
		float radius_start = 0.1f;
		float radius_end = 0.1f;
		bool cap_start = true;
		bool cap_end = true;
	};

	Vector<Segment> _segments;
	int _sides = 8;
	int _cap_style = CAP_DOME;
	int _cap_rings = 3;
	float _cap_height_mul = 1.0f;
	float _leaf_jitter = 0.15f;
	int _leaf_subdivisions = 0;
	bool _auto_embed_branches = true;
	float _embed_depth_mul = 1.0f;

	PackedVector3Array _leaf_positions;
	PackedVector3Array _leaf_normals;
	PackedVector2Array _leaf_uvs;
	PackedInt32Array _leaf_indices;

	PackedVector3Array _rock_positions;
	PackedVector3Array _rock_normals;
	PackedVector2Array _rock_uvs;
	PackedInt32Array _rock_indices;

	void _append_beam(
			const Segment &p_seg,
			PackedVector3Array &r_positions,
			PackedVector3Array &r_normals,
			PackedVector2Array &r_uvs,
			PackedInt32Array &r_indices
	) const;

	/// Returns a copy of _segments with embedded starts sunk in and their caps
	/// disabled. Leaves _segments untouched so repeated builds are stable.
	Vector<Segment> _prepared_segments() const;

	void _append_icosahedron(const Vector3 &p_center, const Vector3 &p_size, const Ref<RandomNumberGenerator> &p_rng);
	void _append_rock_icosahedron(const Vector3 &p_center, const Vector3 &p_size, const Ref<RandomNumberGenerator> &p_rng);
};

VARIANT_ENUM_CAST(EdenTreeMesher::CapStyle);

#endif // EDEN_TREE_MESHER_H