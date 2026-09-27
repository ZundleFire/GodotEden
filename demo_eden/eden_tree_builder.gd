class_name EdenTreeBuilder
extends RefCounted
## Shared tree-geometry builder used by both the runtime spawn path (eden_tree_instance.gd --
## randomized per VoxelInstancer spawn) and the in-editor live preview (eden_tree_preview.gd --
## exact parameters via Inspector sliders). One code path so tuning the preview actually
## reflects what ends up in-game, instead of two meshers that quietly drift apart.
##
## The actual mesh generation (trunk/branch beams and leaf-blob icosahedra alike, including the
## checked-winding fix) lives in EdenTreeMesher (C++, modules/eden_foliage) -- this class is just
## tree-shape logic (where branches fork, where leaf clusters scatter) on top of it.

## Every tunable shape parameter, all defaulted so callers only set what they care about.
## build_random_shape() fills one in from a range for the runtime spawn path; the editor
## preview instead sets each field directly from its own Inspector sliders.
class Shape:
	var trunk_height := 3.0
	var trunk_base_radius := 0.2
	var crown_radius_mul := 0.35
	var branch_count := 3
	var fork_t_min := 0.55
	var fork_t_max := 0.85
	var branch_len_min := 0.8
	var branch_len_max := 1.6
	var branch_tilt_min := 0.5
	var branch_tilt_max := 1.0
	var sides := 7
	var leaf_radius_mul := 2.6
	var leaf_hue_min := 0.22
	var leaf_hue_max := 0.36
	var leaf_sat_min := 0.45
	var leaf_sat_max := 0.75
	var leaf_val_min := 0.40
	var leaf_val_max := 0.60
	var leaf_cluster_min := 3
	var leaf_cluster_max := 5


## The randomized-range defaults the runtime VoxelInstancer spawn path has always used --
## kept in one place instead of copied into every caller that wants "a normal-looking tree".
static func build_random_shape(p_rng: RandomNumberGenerator) -> Shape:
	var s := Shape.new()
	s.trunk_height = p_rng.randf_range(2.2, 4.0)
	s.trunk_base_radius = p_rng.randf_range(0.16, 0.26)
	s.branch_count = p_rng.randi_range(2, 4)
	return s


## Builds one tree from p_shape, consuming p_rng for every randomized choice (branch angles,
## leaf jitter, etc.) so the same seed always reproduces the same tree. Returns a Dictionary
## with "trunk_mesh" (ArrayMesh), "foliage_mesh" (ArrayMesh, may have 0 surfaces if there were
## no tips), and "leaf_color" (Color).
static func build(p_rng: RandomNumberGenerator, p_shape: Shape) -> Dictionary:
	var mesher := EdenTreeMesher.new()
	mesher.sides = p_shape.sides
	mesher.cap_ends = true

	var trunk_base := Vector3.ZERO
	var trunk_crown := Vector3(0, p_shape.trunk_height, 0)
	var crown_radius := p_shape.trunk_base_radius * p_shape.crown_radius_mul
	mesher.add_segment(trunk_base, trunk_crown, p_shape.trunk_base_radius, crown_radius)

	var leaf_tips: Array[Vector3] = [trunk_crown]

	# Branches fork off near the crown, each with its own joint connector ("elbow spine": one
	# extra overlapping beam biased toward the thicker/trunk side -- see EdenTreeMesher's own
	# doc comment for why this alone is enough to look seamless, no special joint geometry).
	for i in range(p_shape.branch_count):
		var fork_t := p_rng.randf_range(p_shape.fork_t_min, p_shape.fork_t_max)
		var fork_point := trunk_base.lerp(trunk_crown, fork_t)
		var fork_radius: float = lerp(p_shape.trunk_base_radius, crown_radius, fork_t)

		var angle := p_rng.randf_range(0.0, TAU)
		var tilt := p_rng.randf_range(p_shape.branch_tilt_min, p_shape.branch_tilt_max)
		var branch_len := p_rng.randf_range(p_shape.branch_len_min, p_shape.branch_len_max)
		var branch_dir := Vector3(
			cos(angle) * tilt, p_rng.randf_range(0.3, 0.7), sin(angle) * tilt
		).normalized()
		var branch_tip := fork_point + branch_dir * branch_len
		var branch_base_radius: float = fork_radius * p_rng.randf_range(0.55, 0.75)
		var branch_tip_radius: float = branch_base_radius * 0.3

		mesher.add_segment(fork_point, branch_tip, branch_base_radius, branch_tip_radius)

		var joint_end: Vector3 = fork_point + branch_dir * (fork_radius * 1.5)
		mesher.add_segment(fork_point, joint_end, fork_radius * 1.05, branch_base_radius)

		leaf_tips.append(branch_tip)

	var leaf_radius := p_shape.trunk_base_radius * p_shape.leaf_radius_mul
	_append_leaf_clusters(mesher, leaf_tips, p_rng, leaf_radius, p_shape)
	var foliage_mesh := mesher.build_foliage_mesh()

	var leaf_color := Color.from_hsv(
		p_rng.randf_range(p_shape.leaf_hue_min, p_shape.leaf_hue_max),
		p_rng.randf_range(p_shape.leaf_sat_min, p_shape.leaf_sat_max),
		p_rng.randf_range(p_shape.leaf_val_min, p_shape.leaf_val_max)
	)

	return {
		"trunk_mesh": mesher.build_mesh(),
		"foliage_mesh": foliage_mesh,
		"leaf_color": leaf_color,
	}


## Feeds p_mesher one leaf-blob cluster per tip point (branch tips + crown), a handful of
## jittered icosahedra scattered around each so foliage reads as a loose canopy rather than one
## sphere per tip. Actual blob geometry (and its winding fix) lives in EdenTreeMesher.add_leaf_blob.
static func _append_leaf_clusters(
	p_mesher: EdenTreeMesher, p_tips: Array[Vector3], p_rng: RandomNumberGenerator,
	p_base_radius: float, p_shape: Shape
) -> void:
	for tip in p_tips:
		var cluster_count := p_rng.randi_range(p_shape.leaf_cluster_min, p_shape.leaf_cluster_max)
		for i in range(cluster_count):
			var offset := Vector3(
				p_rng.randf_range(-1.0, 1.0), p_rng.randf_range(-0.4, 0.7), p_rng.randf_range(-1.0, 1.0)
			) * p_base_radius * 0.6
			var blob_radius := p_base_radius * p_rng.randf_range(0.6, 1.0)
			p_mesher.add_leaf_blob(tip + offset, blob_radius, p_rng)
