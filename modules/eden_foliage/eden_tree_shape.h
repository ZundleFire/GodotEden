#ifndef EDEN_TREE_SHAPE_H
#define EDEN_TREE_SHAPE_H

#include "core/object/ref_counted.h"

// Plain tunable-parameter bag for EdenTreeGenerator::build(). Every field has a sensible
// species default (see EdenTreeGenerator::species_shape), and every field is a bound property
// so GDScript tooling (the editor preview bench) can override individual fields without any of
// the tree-shape *algorithm* itself living in script -- that part is all in EdenTreeGenerator.
class EdenTreeShape : public RefCounted {
	GDCLASS(EdenTreeShape, RefCounted);

public:
	enum TreeType { TREE_OAK = 0, TREE_PINE = 1, TREE_BIRCH = 2, TREE_WILLOW = 3, TREE_PALM = 4, TREE_DEAD = 5, TREE_FRUIT = 6 };
	enum Season { SEASON_SPRING = 0, SEASON_SUMMER = 1, SEASON_FALL = 2, SEASON_WINTER = 3 };

	int tree_type = TREE_OAK;
	int season = SEASON_SUMMER;

	float trunk_height = 3.0f;
	float trunk_base_radius = 0.2f;
	float crown_radius_mul = 0.35f;
	// Trunk is built as this many chained beams instead of one straight one, so it can carry a
	// gentle lean/curve (see trunk_curve) instead of always being a perfectly vertical pole.
	int trunk_segments = 4;
	// Max random tilt (radians) applied per trunk segment, accumulating into a lean/curve along
	// the trunk's height. 0 = perfectly straight.
	float trunk_curve = 0.10f;

	int branch_count = 3;
	float fork_t_min = 0.55f;
	float fork_t_max = 0.85f;
	float branch_len_min = 0.8f;
	float branch_len_max = 1.6f;
	float branch_tilt_min = 0.5f;
	float branch_tilt_max = 1.0f;
	// Shrinks branch length toward the top of the trunk (fork_t near 1.0). 0 = no shrink, 1 =
	// branches near the top are ~35% the length of ones at the base. Gives conical species
	// (pine) their taper without separate short/long branch ranges.
	float branch_len_taper = 0.0f;
	// How much a branch's direction is pulled toward the trunk's own "up" vs. away from it.
	// Positive = branches sweep upward from their fork point (the default, most species).
	// Negative = branches sweep DOWNWARD instead, for a drooping/weeping silhouette (willow).
	float branch_lift_min = 0.3f;
	float branch_lift_max = 0.7f;

	// Small twigs scattered near the very top of the trunk, mostly pointing upward -- a real
	// crown doesn't stop at the last big branch, it thins out into little upward shoots.
	int crown_twig_count_min = 2;
	int crown_twig_count_max = 4;
	float crown_twig_len_min = 0.2f;
	float crown_twig_len_max = 0.45f;

	int sides = 7;
	float leaf_radius_mul = 2.6f;
	int leaf_cluster_min = 3;
	int leaf_cluster_max = 5;
	// Radial noise on leaf blob vertices, as a fraction of blob radius.
	float leaf_jitter = 0.2f;
	// Icosphere subdivision level per leaf blob -- see EdenTreeMesher::set_leaf_subdivisions().
	// 0 keeps the original low-poly look; higher values round the blobs off at the cost of more
	// triangles (quadruples per level).
	int leaf_subdivisions = 0;
	// Per-axis scale on every leaf blob: width_mul scales x/z (horizontal), height_mul scales y
	// (vertical). 1.0/1.0 = round (the original look). Species with needle- or strand-like
	// foliage (Pine, Willow) use a narrow width and tall height instead.
	float leaf_width_mul = 1.0f;
	float leaf_height_mul = 1.0f;

	// Small secondary branches spawned off a random point along each main branch, scaled down
	// from it. 0/0 (the default) means no sub-branches at all, matching the original look for
	// species that don't opt in.
	int sub_branch_count_min = 0;
	int sub_branch_count_max = 0;

#define EDEN_SHAPE_ACCESSORS_INT(m_name) \
	void set_##m_name(int p_v) { m_name = p_v; } \
	int get_##m_name() const { return m_name; }
#define EDEN_SHAPE_ACCESSORS_FLOAT(m_name) \
	void set_##m_name(float p_v) { m_name = p_v; } \
	float get_##m_name() const { return m_name; }

	EDEN_SHAPE_ACCESSORS_INT(tree_type)
	EDEN_SHAPE_ACCESSORS_INT(season)
	EDEN_SHAPE_ACCESSORS_FLOAT(trunk_height)
	EDEN_SHAPE_ACCESSORS_FLOAT(trunk_base_radius)
	EDEN_SHAPE_ACCESSORS_FLOAT(crown_radius_mul)
	EDEN_SHAPE_ACCESSORS_INT(trunk_segments)
	EDEN_SHAPE_ACCESSORS_FLOAT(trunk_curve)
	EDEN_SHAPE_ACCESSORS_INT(branch_count)
	EDEN_SHAPE_ACCESSORS_FLOAT(fork_t_min)
	EDEN_SHAPE_ACCESSORS_FLOAT(fork_t_max)
	EDEN_SHAPE_ACCESSORS_FLOAT(branch_len_min)
	EDEN_SHAPE_ACCESSORS_FLOAT(branch_len_max)
	EDEN_SHAPE_ACCESSORS_FLOAT(branch_tilt_min)
	EDEN_SHAPE_ACCESSORS_FLOAT(branch_tilt_max)
	EDEN_SHAPE_ACCESSORS_FLOAT(branch_len_taper)
	EDEN_SHAPE_ACCESSORS_FLOAT(branch_lift_min)
	EDEN_SHAPE_ACCESSORS_FLOAT(branch_lift_max)
	EDEN_SHAPE_ACCESSORS_INT(crown_twig_count_min)
	EDEN_SHAPE_ACCESSORS_INT(crown_twig_count_max)
	EDEN_SHAPE_ACCESSORS_FLOAT(crown_twig_len_min)
	EDEN_SHAPE_ACCESSORS_FLOAT(crown_twig_len_max)
	EDEN_SHAPE_ACCESSORS_INT(sides)
	EDEN_SHAPE_ACCESSORS_FLOAT(leaf_radius_mul)
	EDEN_SHAPE_ACCESSORS_INT(leaf_cluster_min)
	EDEN_SHAPE_ACCESSORS_INT(leaf_cluster_max)
	EDEN_SHAPE_ACCESSORS_FLOAT(leaf_jitter)
	EDEN_SHAPE_ACCESSORS_INT(leaf_subdivisions)
	EDEN_SHAPE_ACCESSORS_FLOAT(leaf_width_mul)
	EDEN_SHAPE_ACCESSORS_FLOAT(leaf_height_mul)
	EDEN_SHAPE_ACCESSORS_INT(sub_branch_count_min)
	EDEN_SHAPE_ACCESSORS_INT(sub_branch_count_max)

#undef EDEN_SHAPE_ACCESSORS_INT
#undef EDEN_SHAPE_ACCESSORS_FLOAT

protected:
	static void _bind_methods();
};

VARIANT_ENUM_CAST(EdenTreeShape::TreeType);
VARIANT_ENUM_CAST(EdenTreeShape::Season);

#endif // EDEN_TREE_SHAPE_H
