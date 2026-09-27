extends SceneTree
## Headless regression check for EdenTreeMesher's (C++, modules/eden_foliage) winding fix. Checks
## two independent things per triangle, both of which have separately regressed before:
##   1. Its stored normal points away from the shape (a wrong-normal bug).
##   2. Its winding is the one this engine's rasterizer treats as front-facing when viewed from
##      outside -- POLYGON_FRONT_FACE_CLOCKWISE (see rendering_device_commons.h), i.e. the
##      OPPOSITE of OpenGL's usual CCW-is-front rule, so (b-a)x(c-a) must point *against* the
##      outward direction, not with it (see push_triangle_checked's doc comment in
##      eden_tree_mesher.cpp). Getting winding backwards while the normal stays correct is
##      exactly the bug that made CULL_BACK show the mesh's inside instead of its outside.
## Run with:
##   godot --headless --script res://test_eden_tree_builder.gd

func _initialize() -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 1

	var mesher := EdenTreeMesher.new()
	mesher.add_leaf_blob(Vector3.ZERO, 1.0, rng)
	var mesh: ArrayMesh = mesher.build_foliage_mesh()
	var arrays := mesh.surface_get_arrays(0)
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]

	var normal_bad := 0
	var winding_bad := 0
	var total := indices.size() / 3
	for i in range(0, indices.size(), 3):
		var a := positions[indices[i]]
		var b := positions[indices[i + 1]]
		var c := positions[indices[i + 2]]
		# Blob is centered on the local origin, so the centroid direction is the known outward
		# direction at that triangle.
		var outward := (a + b + c) / 3.0
		var n := normals[indices[i]]
		if n.dot(outward) < 0.0:
			normal_bad += 1
		if (b - a).cross(c - a).dot(outward) > 0.0:
			winding_bad += 1

	if normal_bad == 0 and winding_bad == 0 and total > 0:
		print("PASS: all %d leaf-blob triangles face outward with front-facing winding" % total)
	else:
		printerr(
			"FAIL: leaf blob -- %d/%d wrong normal, %d/%d wrong winding"
			% [normal_bad, total, winding_bad, total]
		)

	var trunk_bad := _check_trunk_mesher()
	quit(1 if (normal_bad > 0 or winding_bad > 0 or trunk_bad > 0) else 0)


## Isolates EdenTreeMesher (the C++ trunk/branch mesher) from all tree-shape logic: one plain
## vertical cylinder segment, axis known exactly (the Y axis from origin to tip), so "outward"
## can be computed directly from geometry instead of trusting the mesher's own winding-fix logic.
## Side-surface triangles' true outward direction is radial (away from the Y axis); the two flat
## end caps' true outward direction is axial (straight down / up) instead -- a cap triangle's
## "distance from the axis" is not its outward direction, so caps need their own case here.
func _check_trunk_mesher() -> int:
	var seg_start := Vector3.ZERO
	var seg_end := Vector3(0, 3.0, 0)

	var mesher := EdenTreeMesher.new()
	mesher.sides = 8
	mesher.cap_ends = true
	mesher.add_segment(seg_start, seg_end, 0.3, 0.3)
	var mesh: ArrayMesh = mesher.build_mesh()
	var arrays := mesh.surface_get_arrays(0)
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]

	var normal_bad := 0
	var winding_bad := 0
	var total := indices.size() / 3
	for i in range(0, indices.size(), 3):
		var a := positions[indices[i]]
		var b := positions[indices[i + 1]]
		var c := positions[indices[i + 2]]
		var n := normals[indices[i]]
		# A cap triangle always includes the cap's own center vertex, exactly at seg_start or
		# seg_end (see append_cap) -- side triangles never do.
		var is_cap := (
			a.is_equal_approx(seg_start) or b.is_equal_approx(seg_start) or c.is_equal_approx(seg_start)
			or a.is_equal_approx(seg_end) or b.is_equal_approx(seg_end) or c.is_equal_approx(seg_end)
		)
		var outward: Vector3
		if is_cap:
			var centroid_y := (a.y + b.y + c.y) / 3.0
			outward = Vector3(0, -1, 0) if centroid_y < (seg_start.y + seg_end.y) * 0.5 else Vector3(0, 1, 0)
		else:
			var centroid := (a + b + c) / 3.0
			outward = centroid - Vector3(0, centroid.y, 0)
		if outward.length() > 0.001:
			if n.dot(outward) < 0.0:
				normal_bad += 1
			if (b - a).cross(c - a).dot(outward) > 0.0:
				winding_bad += 1

	if normal_bad == 0 and winding_bad == 0 and total > 0:
		print("PASS: all %d trunk-mesher triangles face outward with front-facing winding" % total)
	else:
		printerr(
			"FAIL: trunk mesher -- %d/%d wrong normal, %d/%d wrong winding"
			% [normal_bad, total, winding_bad, total]
		)
	return normal_bad + winding_bad
