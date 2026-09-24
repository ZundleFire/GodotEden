#include "eden_tree_generator.h"

#include "core/math/color.h"
#include "core/math/math_funcs.h"
#include "core/templates/vector.h"
#include "eden_tree_mesher.h"

namespace {

// One waypoint chain for the (possibly curved) trunk, plus the direction of each segment so
// branches can sample the trunk's actual local lean instead of assuming it's a straight pole.
struct TrunkChain {
	Vector<Vector3> points; // trunk_segments + 1 waypoints
	Vector<float> radii; // matching points
	Vector<Vector3> dirs; // one per segment
};

struct TrunkPoint {
	Vector3 pos;
	float radius = 0.0f;
	Vector3 dir = Vector3(0, 1, 0);
};

struct LeafParams {
	bool has_leaves = true;
	float hue_min = 0.0f, hue_max = 0.0f;
	float sat_min = 0.0f, sat_max = 0.0f;
	float val_min = 0.0f, val_max = 0.0f;
};

// Builds the trunk as p_shape->trunk_segments chained beams instead of one straight one, each
// tilted a little from the previous segment's direction (trunk_curve), so the trunk can lean or
// gently curve instead of always being a perfectly vertical pole. Caps are only drawn at the
// very base and very top; interior joins are uncapped so consecutive segments read as one
// continuous beam instead of showing a seam ring.
TrunkChain build_trunk(EdenTreeMesher &r_mesher, const Ref<RandomNumberGenerator> &p_rng, const Ref<EdenTreeShape> &p_shape) {
	const int seg_count = MAX(p_shape->get_trunk_segments(), 1);
	const float crown_radius = p_shape->get_trunk_base_radius() * p_shape->get_crown_radius_mul();
	const float seg_height = p_shape->get_trunk_height() / float(seg_count);
	const float curve = p_shape->get_trunk_curve();

	TrunkChain trunk;
	trunk.points.push_back(Vector3());
	trunk.radii.push_back(p_shape->get_trunk_base_radius());

	Vector3 dir = Vector3(0, 1, 0);
	Vector3 pos;
	for (int i = 0; i < seg_count; i++) {
		if (curve > 0.0f) {
			const Vector3 tilt_axis = Vector3(p_rng->randf_range(-1.0f, 1.0f), 0.0f, p_rng->randf_range(-1.0f, 1.0f));
			if (tilt_axis.length_squared() > 0.0001f) {
				const float tilt_angle = p_rng->randf_range(-curve, curve);
				dir = dir.rotated(tilt_axis.normalized(), tilt_angle).normalized();
			}
		}
		trunk.dirs.push_back(dir);
		pos += dir * seg_height;
		trunk.points.push_back(pos);
		const float t = float(i + 1) / float(seg_count);
		trunk.radii.push_back(Math::lerp(p_shape->get_trunk_base_radius(), crown_radius, t));
	}

	for (int i = 0; i < seg_count; i++) {
		r_mesher.add_segment_capped(
				trunk.points[i], trunk.points[i + 1], trunk.radii[i], trunk.radii[i + 1],
				i == 0, i == seg_count - 1);
	}

	return trunk;
}

// Interpolates position/radius/local direction along the chained trunk at height fraction p_t
// in [0, 1], so branches can fork off anywhere along a curved trunk and still tilt relative to
// the trunk's actual lean at that point.
TrunkPoint trunk_sample(const TrunkChain &p_trunk, float p_t) {
	const int seg_count = p_trunk.dirs.size();
	const float f = CLAMP(p_t, 0.0f, 1.0f) * float(seg_count);
	const int i = CLAMP(int(f), 0, seg_count - 1);
	const float local_t = f - float(i);

	TrunkPoint out;
	out.pos = p_trunk.points[i].lerp(p_trunk.points[i + 1], local_t);
	out.radius = Math::lerp(p_trunk.radii[i], p_trunk.radii[i + 1], local_t);
	out.dir = p_trunk.dirs[i];
	return out;
}

// Attaches one limb (branch or crown twig) growing out of the trunk at p_origin, tilted between
// p_origin_dir (the trunk's own lean there) and a random outward horizontal direction, plus its
// joint collar sized to the trunk's actual n-gon face inradius so it can't punch through the
// trunk's flat faces (see eden_tree_debug_log.md, Bug 3). Returns the limb's tip position.
Vector3 attach_limb(
		EdenTreeMesher &r_mesher, const Ref<RandomNumberGenerator> &p_rng, const Ref<EdenTreeShape> &p_shape,
		const Vector3 &p_origin, float p_origin_radius, const Vector3 &p_origin_dir, float p_t,
		float p_len_min, float p_len_max, float p_tilt_min, float p_tilt_max,
		float p_base_radius_mul_min, float p_base_radius_mul_max, float p_tip_radius_mul,
		float p_lift_min, float p_lift_max) {
	const float angle = p_rng->randf_range(0.0f, float(Math::TAU));
	const float tilt = p_rng->randf_range(p_tilt_min, p_tilt_max);
	const float lift = p_rng->randf_range(p_lift_min, p_lift_max);
	const Vector3 horiz = Vector3(Math::cos(angle), 0.0f, Math::sin(angle));
	Vector3 dir = (horiz * tilt + p_origin_dir * lift);
	if (dir.length_squared() < CMP_EPSILON2) {
		dir = p_origin_dir;
	} else {
		dir.normalize();
	}

	const float taper = Math::lerp(1.0f, 0.35f, CLAMP(p_t, 0.0f, 1.0f) * p_shape->get_branch_len_taper());
	const float length = p_rng->randf_range(p_len_min, p_len_max) * taper;

	const Vector3 tip = p_origin + dir * length;
	const float base_radius = p_origin_radius * p_rng->randf_range(p_base_radius_mul_min, p_base_radius_mul_max);
	const float tip_radius = base_radius * p_tip_radius_mul;

	// Only the tip cap can be seen: the limb starts on the parent's axis, so its start cap is inside
	// the parent, and the collar ends inside both the parent and the (wider) limb. Dome caps were
	// ~60% of a tree's triangles, most of them buried like this.
	r_mesher.add_segment_capped(p_origin, tip, base_radius, tip_radius, false, true);

	const float face_ratio = Math::cos(float(Math::PI) / float(p_shape->get_sides()));
	const float joint_radius = p_origin_radius * face_ratio * 0.92f;
	const Vector3 joint_end = p_origin + dir * (p_origin_radius * 0.2f);
	r_mesher.add_segment_capped(p_origin, joint_end, joint_radius, base_radius * 0.8f, false, false);

	return tip;
}

// Willow-only: a 3-segment ARCED branch (up-and-out, a transitional bend, then a near-vertical
// hanging fall) instead of one straight beam -- a single straight segment with a downward lift
// reads as "branch pointing at the ground," not a graceful droop, and even a 2-segment version
// bent too sharply to read as a cascade. Returns several points spread along the drooping
// (2nd+3rd) segments for leaves to attach to, not just the tip -- willow foliage hangs in
// strands along the whole branch, not as one clump at the end.
Vector<Vector3> attach_willow_limb(
		EdenTreeMesher &r_mesher, const Ref<RandomNumberGenerator> &p_rng, const Ref<EdenTreeShape> &p_shape,
		const Vector3 &p_origin, float p_origin_radius, const Vector3 &p_origin_dir, float p_t,
		float p_len_min, float p_len_max, float p_tilt_min, float p_tilt_max) {
	const float angle = p_rng->randf_range(0.0f, float(Math::TAU));
	const float tilt = p_rng->randf_range(p_tilt_min, p_tilt_max);
	const Vector3 horiz = Vector3(Math::cos(angle), 0.0f, Math::sin(angle));

	const float taper = Math::lerp(1.0f, 0.35f, CLAMP(p_t, 0.0f, 1.0f) * p_shape->get_branch_len_taper());
	const float total_length = p_rng->randf_range(p_len_min, p_len_max) * taper;

	// Fractions of total_length spent rising, transitioning, then hanging. The hanging segment
	// dominates (45-55% of the length) so the cascade actually reads, instead of a short flick
	// at the very end.
	const float frac1 = p_rng->randf_range(0.18f, 0.28f);
	const float frac2 = p_rng->randf_range(0.22f, 0.3f);

	// Segment 1: rises up and out from the trunk, same sense every other species' branches use.
	const float lift1 = p_rng->randf_range(0.55f, 0.85f);
	Vector3 dir1 = (horiz * tilt + p_origin_dir * lift1);
	if (dir1.length_squared() < CMP_EPSILON2) {
		dir1 = p_origin_dir;
	} else {
		dir1.normalize();
	}
	const Vector3 p1 = p_origin + dir1 * (total_length * frac1);

	// Segment 2: the bend -- lift crosses from upward to level to slightly downward.
	const float lift2 = p_rng->randf_range(-0.2f, 0.15f);
	Vector3 dir2 = (horiz * tilt * 0.7f + Vector3(0, 1, 0) * lift2);
	if (dir2.length_squared() < CMP_EPSILON2) {
		dir2 = horiz;
	} else {
		dir2.normalize();
	}
	const Vector3 p2 = p1 + dir2 * (total_length * frac2);

	// Segment 3: the actual hang -- steeply downward with only a little outward drift left,
	// like a real weeping willow strand falling nearly straight down.
	const float lift3 = p_rng->randf_range(-1.3f, -1.0f);
	Vector3 dir3 = (horiz * tilt * 0.15f + Vector3(0, 1, 0) * lift3);
	if (dir3.length_squared() < CMP_EPSILON2) {
		dir3 = Vector3(0, -1, 0);
	} else {
		dir3.normalize();
	}
	const Vector3 tip = p2 + dir3 * (total_length * (1.0f - frac1 - frac2));

	const float base_radius = p_origin_radius * p_rng->randf_range(0.35f, 0.5f);
	const float p1_radius = base_radius * 0.6f;
	const float p2_radius = base_radius * 0.3f;
	const float tip_radius = base_radius * 0.08f;

	// Buried caps dropped as in attach_limb; the elbow caps stay, they fill the gaps at the bends
	r_mesher.add_segment_capped(p_origin, p1, base_radius, p1_radius, false, true);
	r_mesher.add_segment(p1, p2, p1_radius, p2_radius);
	r_mesher.add_segment(p2, tip, p2_radius, tip_radius);

	const float face_ratio = Math::cos(float(Math::PI) / float(p_shape->get_sides()));
	const float joint_radius = p_origin_radius * face_ratio * 0.92f;
	const Vector3 joint_end = p_origin + dir1 * (p_origin_radius * 0.2f);
	r_mesher.add_segment_capped(p_origin, joint_end, joint_radius, base_radius * 0.8f, false, false);

	// Leaf strand points along the bend and the hang (not segment 1, which is bare like a real
	// willow's rising limb before the foliage cascade starts).
	Vector<Vector3> points;
	points.push_back(p1.lerp(p2, 0.35f));
	points.push_back(p1.lerp(p2, 0.75f));
	const float hang_ts[6] = { 0.16f, 0.32f, 0.48f, 0.64f, 0.8f, 1.0f };
	for (float lt : hang_ts) {
		points.push_back(p2.lerp(tip, lt));
	}
	return points;
}

// Small secondary branches spawned off a random point along a parent branch, scaled down from
// it -- opt-in per species via sub_branch_count_max (0 = off, the default, so species that don't
// set it are unaffected). Appends each sub-branch's tip to r_leaf_tips.
void spawn_sub_branches(
		EdenTreeMesher &r_mesher, const Ref<RandomNumberGenerator> &p_rng, const Ref<EdenTreeShape> &p_shape,
		Vector<Vector3> &r_leaf_tips, const Vector3 &p_branch_origin, const Vector3 &p_branch_tip,
		float p_origin_radius, float p_t) {
	if (p_shape->get_sub_branch_count_max() <= 0) {
		return;
	}
	const int count = p_rng->randi_range(p_shape->get_sub_branch_count_min(), p_shape->get_sub_branch_count_max());
	const Vector3 axis = p_branch_tip - p_branch_origin;
	const float branch_len = axis.length();
	if (count <= 0 || branch_len < CMP_EPSILON) {
		return;
	}
	const Vector3 branch_dir = axis / branch_len;

	for (int i = 0; i < count; i++) {
		const float st = p_rng->randf_range(0.35f, 0.9f);
		const Vector3 sub_origin = p_branch_origin.lerp(p_branch_tip, st);
		const float sub_origin_radius = MAX(p_origin_radius * (1.0f - st) * 0.6f, 0.01f);
		const Vector3 sub_tip = attach_limb(
				r_mesher, p_rng, p_shape,
				sub_origin, sub_origin_radius, branch_dir, p_t,
				branch_len * 0.3f, branch_len * 0.55f,
				p_shape->get_branch_tilt_min(), p_shape->get_branch_tilt_max(),
				0.5f, 0.7f, 0.3f,
				0.2f, 0.6f); // sub-branches lean outward/up regardless of the parent's own droop
		r_leaf_tips.push_back(sub_tip);
	}
}

// Species bark tone. A flat color, not a texture -- good enough to tell species apart at a
// glance, which is all this project needs right now.
Color trunk_color(int p_tree_type) {
	switch (p_tree_type) {
		case EdenTreeShape::TREE_PINE:
			return Color(0.28f, 0.20f, 0.14f);
		case EdenTreeShape::TREE_BIRCH:
			return Color(0.82f, 0.80f, 0.75f);
		case EdenTreeShape::TREE_WILLOW:
			return Color(0.36f, 0.30f, 0.20f);
		case EdenTreeShape::TREE_PALM:
			return Color(0.58f, 0.47f, 0.33f);
		case EdenTreeShape::TREE_DEAD:
			return Color(0.45f, 0.43f, 0.40f);
		case EdenTreeShape::TREE_FRUIT:
			return Color(0.38f, 0.27f, 0.15f);
		default:
			return Color(0.42f, 0.30f, 0.17f);
	}
}

// Small round fruit accents scattered sparsely near a subset of leaf tips -- deliberately much
// sparser than leaf clusters (0-2 per tip, not every tip) so they read as fruit dotted through
// the canopy, not another leaf layer. Returns a mesh with 0 surfaces if nothing was placed.
//
// Placed at ~0.85-1.25x leaf_radius from the tip (near where the leaf blobs' own outer surface
// sits -- see build_foliage_mesh's offset*0.6 + blob_radius up to 1.0x), not a small fraction of
// it: a small offset buries the fruit deep inside the leaf canopy where it's fully hidden, which
// is exactly the "no fruit visible on the tree" bug this was.
Ref<ArrayMesh> build_fruit_mesh(
		const Vector<Vector3> &p_tips, const Ref<RandomNumberGenerator> &p_rng, float p_leaf_radius) {
	EdenTreeMesher mesher;
	const float fruit_radius = p_leaf_radius * 0.22f;
	for (const Vector3 &tip : p_tips) {
		const int count = p_rng->randi_range(0, 2);
		for (int i = 0; i < count; i++) {
			Vector3 dir = Vector3(
					p_rng->randf_range(-1.0f, 1.0f), p_rng->randf_range(-0.3f, 0.6f), p_rng->randf_range(-1.0f, 1.0f));
			if (dir.length_squared() < CMP_EPSILON2) {
				dir = Vector3(0, 1, 0);
			} else {
				dir.normalize();
			}
			const Vector3 offset = dir * (p_leaf_radius * p_rng->randf_range(0.85f, 1.25f));
			mesher.add_leaf_blob(tip + offset, fruit_radius * p_rng->randf_range(0.85f, 1.15f), p_rng);
		}
	}
	return mesher.build_foliage_mesh();
}

// Leaf color range (HSV) for a species+season combo, plus whether the species has any leaves
// at all this season. Dead trees never have leaves, in any season. Evergreens (Pine, Palm)
// never drop theirs; every other species drops them in Winter.
LeafParams season_leaf_params(int p_tree_type, int p_season) {
	LeafParams out;
	if (p_tree_type == EdenTreeShape::TREE_DEAD) {
		out.has_leaves = false;
		return out;
	}

	const bool evergreen = p_tree_type == EdenTreeShape::TREE_PINE || p_tree_type == EdenTreeShape::TREE_PALM;
	if (!evergreen && p_season == EdenTreeShape::SEASON_WINTER) {
		out.has_leaves = false;
		return out;
	}

	if (evergreen) {
		out.hue_min = 0.32f;
		out.hue_max = 0.40f;
		out.sat_min = 0.45f;
		out.sat_max = 0.65f;
		if (p_season == EdenTreeShape::SEASON_WINTER) {
			out.val_min = 0.22f;
			out.val_max = 0.32f;
		} else {
			out.val_min = 0.28f;
			out.val_max = 0.42f;
		}
		return out;
	}

	switch (p_season) {
		case EdenTreeShape::SEASON_SPRING:
			out.hue_min = 0.27f;
			out.hue_max = 0.34f;
			out.sat_min = 0.50f;
			out.sat_max = 0.70f;
			out.val_min = 0.55f;
			out.val_max = 0.70f;
			break;
		case EdenTreeShape::SEASON_FALL:
			out.hue_min = 0.02f;
			out.hue_max = 0.13f;
			out.sat_min = 0.55f;
			out.sat_max = 0.85f;
			out.val_min = 0.45f;
			out.val_max = 0.65f;
			break;
		default: // SEASON_SUMMER
			out.hue_min = 0.22f;
			out.hue_max = 0.36f;
			out.sat_min = 0.45f;
			out.sat_max = 0.75f;
			out.val_min = 0.40f;
			out.val_max = 0.60f;
			break;
	}
	return out;
}

// Builds one merged mesh of leaf-blob clusters, a handful scattered around each tip point
// (branch tips + crown twig tips + main crown) so foliage reads as a loose canopy rather than
// one sphere per tip. Every blob goes through EdenTreeMesher::add_leaf_blob(), so leaf winding,
// flat normals, per-unique-vertex jitter and UVs all come from the same mesher that builds the
// trunk -- there is no second icosahedron table to drift out of sync.
Ref<ArrayMesh> build_foliage_mesh(
		const Vector<Vector3> &p_tips, const Ref<RandomNumberGenerator> &p_rng, float p_base_radius,
		const Ref<EdenTreeShape> &p_shape) {
	EdenTreeMesher mesher;
	mesher.set_leaf_jitter(p_shape->get_leaf_jitter());
	mesher.set_leaf_subdivisions(p_shape->get_leaf_subdivisions());

	// 1.0/1.0 (the default for most species) makes this identical to a round add_leaf_blob();
	// species with needle- or strand-like foliage (Pine, Willow) set a narrow width and tall
	// height instead.
	const float width_mul = p_shape->get_leaf_width_mul();
	const float height_mul = p_shape->get_leaf_height_mul();

	for (const Vector3 &tip : p_tips) {
		const int cluster_count = p_rng->randi_range(p_shape->get_leaf_cluster_min(), p_shape->get_leaf_cluster_max());
		for (int i = 0; i < cluster_count; i++) {
			const Vector3 offset = Vector3(
										p_rng->randf_range(-1.0f, 1.0f), p_rng->randf_range(-0.4f, 0.7f), p_rng->randf_range(-1.0f, 1.0f)) *
					p_base_radius * 0.6f;
			const float blob_radius = p_base_radius * p_rng->randf_range(0.6f, 1.0f);
			const Vector3 size = Vector3(blob_radius * width_mul, blob_radius * height_mul, blob_radius * width_mul);
			mesher.add_leaf_blob_elongated(tip + offset, size, p_rng);
		}
	}

	// Returns an ArrayMesh with 0 surfaces when there were no blobs.
	return mesher.build_foliage_mesh();
}

} // namespace

Ref<EdenTreeShape> EdenTreeGenerator::build_random_shape(const Ref<RandomNumberGenerator> &p_rng) {
	return species_shape(p_rng->randi_range(0, 6), p_rng);
}

Ref<EdenTreeShape> EdenTreeGenerator::species_shape(int p_tree_type, const Ref<RandomNumberGenerator> &p_rng) {
	Ref<EdenTreeShape> s;
	s.instantiate();
	s->set_tree_type(p_tree_type);

	switch (p_tree_type) {
		case EdenTreeShape::TREE_PINE:
			// Tall, straight, conical: many short branches low down tapering to almost nothing
			// near the apex, dense small needle-like blobs, evergreen.
			s->set_trunk_height(p_rng->randf_range(3.8f, 6.0f));
			s->set_trunk_base_radius(p_rng->randf_range(0.15f, 0.22f));
			s->set_crown_radius_mul(p_rng->randf_range(0.22f, 0.30f));
			s->set_trunk_curve(0.03f);
			s->set_branch_count(p_rng->randi_range(10, 16));
			s->set_fork_t_min(0.22f);
			s->set_fork_t_max(0.95f);
			s->set_branch_tilt_min(0.3f);
			s->set_branch_tilt_max(0.55f);
			s->set_branch_len_min(0.5f);
			s->set_branch_len_max(1.1f);
			s->set_branch_len_taper(0.75f);
			s->set_crown_twig_count_min(4);
			s->set_crown_twig_count_max(7);
			s->set_sides(6);
			s->set_leaf_radius_mul(p_rng->randf_range(1.8f, 2.4f));
			s->set_leaf_cluster_min(6);
			s->set_leaf_cluster_max(9);
			s->set_leaf_jitter(0.12f);
			// Needle-like: narrow and a little elongated instead of round pom-poms.
			s->set_leaf_width_mul(0.5f);
			s->set_leaf_height_mul(1.4f);
			s->set_sub_branch_count_min(0);
			s->set_sub_branch_count_max(2);
			break;
		case EdenTreeShape::TREE_BIRCH:
			// Tall and thin, sparse high branches, airy foliage.
			s->set_trunk_height(p_rng->randf_range(4.0f, 6.5f));
			s->set_trunk_base_radius(p_rng->randf_range(0.10f, 0.16f));
			s->set_crown_radius_mul(p_rng->randf_range(0.35f, 0.45f));
			s->set_trunk_curve(0.16f);
			s->set_branch_count(p_rng->randi_range(5, 9));
			s->set_fork_t_min(0.45f);
			s->set_fork_t_max(0.92f);
			s->set_branch_tilt_min(0.7f);
			s->set_branch_tilt_max(1.0f);
			s->set_branch_len_min(0.6f);
			s->set_branch_len_max(1.3f);
			s->set_branch_len_taper(0.2f);
			s->set_sides(6);
			s->set_leaf_radius_mul(p_rng->randf_range(2.6f, 3.2f));
			s->set_leaf_cluster_min(2);
			s->set_leaf_cluster_max(4);
			s->set_leaf_jitter(0.28f);
			s->set_sub_branch_count_min(1);
			s->set_sub_branch_count_max(2);
			break;
		case EdenTreeShape::TREE_WILLOW:
			// Broad, weeping canopy: branches leave the trunk going up-and-out then arc downward
			// (see attach_willow_limb -- a real 2-segment bend, not just a downward-biased
			// straight beam, which read as "branch pointing at the ground" instead of drooping).
			// Leaves are narrow and tall (strand-like) and scattered along each branch's
			// drooping segment rather than clumped only at the tip.
			s->set_trunk_height(p_rng->randf_range(3.5f, 5.5f));
			s->set_trunk_base_radius(p_rng->randf_range(0.16f, 0.24f));
			s->set_crown_radius_mul(p_rng->randf_range(0.6f, 0.8f));
			s->set_trunk_curve(0.12f);
			// More, thinner strands covering the trunk from low down to near the top.
			s->set_branch_count(p_rng->randi_range(16, 24));
			s->set_fork_t_min(0.15f);
			s->set_fork_t_max(0.92f);
			s->set_branch_tilt_min(0.4f);
			s->set_branch_tilt_max(0.7f);
			s->set_branch_len_min(1.1f);
			s->set_branch_len_max(2.3f);
			s->set_branch_len_taper(0.1f);
			s->set_sides(5);
			s->set_leaf_radius_mul(p_rng->randf_range(1.1f, 1.4f));
			s->set_leaf_cluster_min(1);
			s->set_leaf_cluster_max(2);
			s->set_leaf_jitter(0.18f);
			// Thinner still than the previous pass -- narrower width, taller aspect.
			s->set_leaf_width_mul(0.28f);
			s->set_leaf_height_mul(2.0f);
			s->set_sub_branch_count_min(2);
			s->set_sub_branch_count_max(4);
			break;
		case EdenTreeShape::TREE_PALM:
			// Tall bare trunk, no side branches at all -- just a crown of long fronds (crown
			// twigs) clustered right at the apex.
			s->set_trunk_height(p_rng->randf_range(4.5f, 7.0f));
			s->set_trunk_base_radius(p_rng->randf_range(0.14f, 0.20f));
			s->set_crown_radius_mul(p_rng->randf_range(0.45f, 0.6f));
			s->set_trunk_curve(0.05f);
			s->set_branch_count(0);
			s->set_crown_twig_count_min(6);
			s->set_crown_twig_count_max(10);
			s->set_crown_twig_len_min(0.5f);
			s->set_crown_twig_len_max(0.9f);
			s->set_sides(8);
			s->set_leaf_radius_mul(p_rng->randf_range(1.4f, 1.8f));
			s->set_leaf_cluster_min(2);
			s->set_leaf_cluster_max(3);
			s->set_leaf_jitter(0.15f);
			break;
		case EdenTreeShape::TREE_DEAD:
			// A bare, skeletal snag -- reuses Oak-like branch spread but is always leafless (see
			// season_leaf_params) regardless of season.
			s->set_trunk_height(p_rng->randf_range(2.5f, 4.5f));
			s->set_trunk_base_radius(p_rng->randf_range(0.15f, 0.25f));
			s->set_crown_radius_mul(p_rng->randf_range(0.4f, 0.6f));
			s->set_trunk_curve(0.14f);
			s->set_branch_count(p_rng->randi_range(5, 9));
			s->set_fork_t_min(0.15f);
			s->set_fork_t_max(0.9f);
			s->set_branch_tilt_min(0.5f);
			s->set_branch_tilt_max(1.0f);
			s->set_branch_len_min(0.5f);
			s->set_branch_len_max(1.2f);
			s->set_branch_len_taper(0.1f);
			s->set_crown_twig_count_min(2);
			s->set_crown_twig_count_max(5);
			s->set_sides(6);
			s->set_sub_branch_count_min(1);
			s->set_sub_branch_count_max(3);
			break;
		case EdenTreeShape::TREE_FRUIT:
			// Short, dense, rounded canopy -- shorter and twiggier than Oak, like an orchard
			// tree. Fruit accents are added separately in build() (see build_fruit_mesh()), not
			// tunable here since only this species uses them.
			s->set_trunk_height(p_rng->randf_range(2.0f, 3.2f));
			s->set_trunk_base_radius(p_rng->randf_range(0.15f, 0.22f));
			s->set_crown_radius_mul(p_rng->randf_range(0.55f, 0.75f));
			s->set_trunk_curve(0.09f);
			s->set_branch_count(p_rng->randi_range(6, 9));
			s->set_fork_t_min(0.25f);
			s->set_fork_t_max(0.85f);
			s->set_branch_tilt_min(0.6f);
			s->set_branch_tilt_max(1.0f);
			s->set_branch_len_min(0.5f);
			s->set_branch_len_max(1.1f);
			s->set_branch_len_taper(0.1f);
			s->set_sides(6);
			s->set_leaf_radius_mul(p_rng->randf_range(2.0f, 2.6f));
			s->set_leaf_cluster_min(3);
			s->set_leaf_cluster_max(5);
			s->set_leaf_jitter(0.2f);
			s->set_sub_branch_count_min(2);
			s->set_sub_branch_count_max(4);
			break;
		default: // TREE_OAK
			// Broad rounded canopy, branches spread across most of the trunk's height.
			s->set_trunk_height(p_rng->randf_range(2.6f, 4.0f));
			s->set_trunk_base_radius(p_rng->randf_range(0.18f, 0.28f));
			s->set_crown_radius_mul(p_rng->randf_range(0.5f, 0.7f));
			s->set_trunk_curve(0.10f);
			s->set_branch_count(p_rng->randi_range(4, 7));
			s->set_fork_t_min(0.2f);
			s->set_fork_t_max(0.88f);
			s->set_branch_tilt_min(0.6f);
			s->set_branch_tilt_max(1.1f);
			s->set_branch_len_min(0.7f);
			s->set_branch_len_max(1.5f);
			s->set_branch_len_taper(0.0f);
			s->set_sides(7);
			s->set_leaf_radius_mul(p_rng->randf_range(2.4f, 3.0f));
			s->set_leaf_cluster_min(3);
			s->set_leaf_cluster_max(5);
			s->set_leaf_jitter(0.2f);
			s->set_sub_branch_count_min(1);
			s->set_sub_branch_count_max(3);
			break;
	}
	return s;
}

Dictionary EdenTreeGenerator::build(const Ref<RandomNumberGenerator> &p_rng, const Ref<EdenTreeShape> &p_shape) {
	ERR_FAIL_COND_V(p_rng.is_null() || p_shape.is_null(), Dictionary());

	EdenTreeMesher mesher;
	mesher.set_sides(p_shape->get_sides());
	mesher.set_cap_ends(true);
	mesher.set_auto_embed_branches(false);

	const TrunkChain trunk = build_trunk(mesher, p_rng, p_shape);
	const Vector3 trunk_crown = trunk.points[trunk.points.size() - 1];

	Vector<Vector3> leaf_tips;
	leaf_tips.push_back(trunk_crown);

	// Branches fork off anywhere across the trunk's height (species-controlled range), each
	// with its own joint connector ("elbow spine": one extra overlapping beam biased toward the
	// thicker/trunk side).
	for (int i = 0; i < p_shape->get_branch_count(); i++) {
		const float t = p_rng->randf_range(p_shape->get_fork_t_min(), p_shape->get_fork_t_max());
		const TrunkPoint s = trunk_sample(trunk, t);

		if (p_shape->get_tree_type() == EdenTreeShape::TREE_WILLOW) {
			const Vector<Vector3> willow_points = attach_willow_limb(
					mesher, p_rng, p_shape,
					s.pos, s.radius, s.dir, t,
					p_shape->get_branch_len_min(), p_shape->get_branch_len_max(),
					p_shape->get_branch_tilt_min(), p_shape->get_branch_tilt_max());
			for (const Vector3 &pt : willow_points) {
				leaf_tips.push_back(pt);
			}
			if (willow_points.size() > 0) {
				spawn_sub_branches(mesher, p_rng, p_shape, leaf_tips, s.pos, willow_points[willow_points.size() - 1], s.radius, t);
			}
		} else {
			const Vector3 tip = attach_limb(
					mesher, p_rng, p_shape,
					s.pos, s.radius, s.dir, t,
					p_shape->get_branch_len_min(), p_shape->get_branch_len_max(),
					p_shape->get_branch_tilt_min(), p_shape->get_branch_tilt_max(),
					0.55f, 0.75f, 0.3f,
					p_shape->get_branch_lift_min(), p_shape->get_branch_lift_max());
			leaf_tips.push_back(tip);
			spawn_sub_branches(mesher, p_rng, p_shape, leaf_tips, s.pos, tip, s.radius, t);
		}
	}

	// Small upward twigs clustered right at the apex, thinner and more vertical than the main
	// branches -- a bare straight-to-a-point crown reads as unnaturally simple.
	const int twig_count = p_rng->randi_range(p_shape->get_crown_twig_count_min(), p_shape->get_crown_twig_count_max());
	for (int i = 0; i < twig_count; i++) {
		const float t = p_rng->randf_range(0.88f, 0.99f);
		const TrunkPoint s = trunk_sample(trunk, t);
		const Vector3 tip = attach_limb(
				mesher, p_rng, p_shape,
				s.pos, s.radius, s.dir, t,
				p_shape->get_crown_twig_len_min(), p_shape->get_crown_twig_len_max(),
				0.15f, 0.4f,
				0.3f, 0.45f, 0.4f,
				0.3f, 0.7f); // twigs always shoot upward regardless of species droop
		leaf_tips.push_back(tip);
	}

	const LeafParams leaf_params = season_leaf_params(p_shape->get_tree_type(), p_shape->get_season());
	Ref<ArrayMesh> foliage_mesh;
	Color leaf_color;
	if (leaf_params.has_leaves) {
		const float leaf_radius = p_shape->get_trunk_base_radius() * p_shape->get_leaf_radius_mul();
		foliage_mesh = build_foliage_mesh(leaf_tips, p_rng, leaf_radius, p_shape);
		leaf_color = Color::from_hsv(
				p_rng->randf_range(leaf_params.hue_min, leaf_params.hue_max),
				p_rng->randf_range(leaf_params.sat_min, leaf_params.sat_max),
				p_rng->randf_range(leaf_params.val_min, leaf_params.val_max));
	} else {
		// Deciduous species in Winter: bare tree, no foliage mesh at all.
		EdenTreeMesher empty_mesher;
		foliage_mesh = empty_mesher.build_foliage_mesh();
		leaf_color = Color(1, 1, 1);
	}

	// Fruit: only TREE_FRUIT, only when it actually has leaves this season, and only in
	// Summer/Fall -- real fruit trees blossom in Spring and go bare in Winter, fruit sits
	// between those two.
	Ref<ArrayMesh> fruit_mesh;
	Color fruit_color(1, 1, 1);
	const bool wants_fruit = p_shape->get_tree_type() == EdenTreeShape::TREE_FRUIT && leaf_params.has_leaves &&
			(p_shape->get_season() == EdenTreeShape::SEASON_SUMMER || p_shape->get_season() == EdenTreeShape::SEASON_FALL);
	if (wants_fruit) {
		const float leaf_radius = p_shape->get_trunk_base_radius() * p_shape->get_leaf_radius_mul();
		fruit_mesh = build_fruit_mesh(leaf_tips, p_rng, leaf_radius);
		fruit_color = Color::from_hsv(p_rng->randf_range(0.0f, 0.05f), p_rng->randf_range(0.75f, 0.9f), p_rng->randf_range(0.55f, 0.75f));
	} else {
		EdenTreeMesher empty_mesher;
		fruit_mesh = empty_mesher.build_foliage_mesh();
	}

	Dictionary out;
	out["trunk_mesh"] = mesher.build_mesh();
	out["foliage_mesh"] = foliage_mesh;
	out["leaf_color"] = leaf_color;
	out["trunk_color"] = trunk_color(p_shape->get_tree_type());
	out["fruit_mesh"] = fruit_mesh;
	out["fruit_color"] = fruit_color;
	return out;
}

void EdenTreeGenerator::_bind_methods() {
	ClassDB::bind_static_method("EdenTreeGenerator", D_METHOD("build_random_shape", "rng"), &EdenTreeGenerator::build_random_shape);
	ClassDB::bind_static_method("EdenTreeGenerator", D_METHOD("species_shape", "tree_type", "rng"), &EdenTreeGenerator::species_shape);
	ClassDB::bind_static_method("EdenTreeGenerator", D_METHOD("build", "rng", "shape"), &EdenTreeGenerator::build);
}
