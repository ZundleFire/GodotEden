extends Node3D
## Probes sample_dominant_material()/sample_biome_at() to find and zoom into any remaining
## material mottling (BIOME_MATERIAL_PLAN.md Step 1). Multi-stage: coarse-to-fine, each stage
## re-centers on the row with the most material flips (dense flips = real mottling; a single
## clean region boundary only flips once or twice), so we land inside real speckle instead of
## the first ordinary region edge encountered. Uses proper per-point surface bisection along a
## ROTATED direction for each offset (not a flat position offset).

const PLANET_RADIUS := 40000.0
const PLANET_SEED := 1

func _ready() -> void:
	var gen := EdenPlanetGeneratorV1.new()
	gen.planet_radius = PLANET_RADIUS
	gen.seed_val = PLANET_SEED
	gen.setup()

	var best_dir := _find_material_land(gen, EdenPlanetGeneratorV1.MAT_DIRT)
	print("PROBE: base dir=", best_dir)

	var up := best_dir
	var ref := Vector3(0, 1, 0) if absf(up.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
	var tangent_a := up.cross(ref).normalized()
	var tangent_b := up.cross(tangent_a).normalized()

	var center_a := 0.0
	var center_b := 0.0
	# Zoom stages: (half_width_cells, step_meters). Each stage scans a
	# (2*half+1)x(2*half+1) grid centered on the previous stage's densest row/col.
	var stages := [[100, 20.0], [40, 2.0], [10, 1.0]]
	for stage_i in range(stages.size()):
		var half: int = stages[stage_i][0]
		var step: float = stages[stage_i][1]
		var result := _scan(gen, up, tangent_a, tangent_b, center_a, center_b, half, step)
		var best_flips: int = result.flips
		print("PROBE stage ", stage_i, " (step=", step, "m, span=", (2 * half) * step, "m): max flips/row=", best_flips, " center=(", result.a, ",", result.b, ")")
		if best_flips <= 1:
			if stage_i == 0:
				print("PROBE: no dense material mottling found even in the widest scan; region is clean here.")
				get_tree().quit()
				return
			break
		center_a = result.a
		center_b = result.b

	print("PROBE: final 1m zoom around a=", center_a, " b=", center_b)
	for row in range(-10, 11):
		var mat_line := ""
		var biome_line := ""
		var b_off := center_b + float(row) * 1.0
		for col in range(-10, 11):
			var a_off := center_a + float(col) * 1.0
			var d := _offset_dir(up, tangent_a, tangent_b, a_off, b_off)
			var mat := gen.sample_dominant_material(d)
			var biome := gen.sample_biome_at(d)
			mat_line += "%X" % mat
			biome_line += "%X" % biome
		print("PROBE 1M ROW ", row, " MAT:   ", mat_line)
		print("PROBE 1M ROW ", row, " BIOME: ", biome_line)

	get_tree().quit()


## Scans a (2*half+1)^2 grid of dominant-material samples centered at (center_a, center_b) with
## spacing `step`, and returns the row (or column, whichever is denser) with the most adjacent
## material disagreements, as {a, b, flips}.
func _scan(gen: EdenPlanetGeneratorV1, up: Vector3, tangent_a: Vector3, tangent_b: Vector3, center_a: float, center_b: float, half: int, step: float) -> Dictionary:
	var rows: Array = []
	for row in range(-half, half + 1):
		var b_off := center_b + float(row) * step
		var row_mats: Array[int] = []
		for col in range(-half, half + 1):
			var a_off := center_a + float(col) * step
			var d := _offset_dir(up, tangent_a, tangent_b, a_off, b_off)
			row_mats.append(gen.sample_dominant_material(d))
		rows.append(row_mats)

	var best_row := -1
	var best_col := -1
	var best_flips := 0
	for row in range(rows.size()):
		var row_mats: Array = rows[row]
		var flips := 0
		var last_flip_col := -1
		for col in range(1, row_mats.size()):
			if row_mats[col] != row_mats[col - 1]:
				flips += 1
				last_flip_col = col
		if flips > best_flips:
			best_flips = flips
			best_row = row
			best_col = last_flip_col

	if best_row == -1:
		return {"a": center_a, "b": center_b, "flips": 0}
	return {
		"a": center_a + float(best_col - half) * step,
		"b": center_b + float(best_row - half) * step,
		"flips": best_flips,
	}


func _offset_dir(up: Vector3, tangent_a: Vector3, tangent_b: Vector3, a_off: float, b_off: float) -> Vector3:
	var ang_a := a_off / PLANET_RADIUS
	var ang_b := b_off / PLANET_RADIUS
	return (up + tangent_a * sin(ang_a) + tangent_b * sin(ang_b)).normalized()


func _find_material_land(gen: EdenPlanetGeneratorV1, target_mat: int) -> Vector3:
	const N := 40
	var best_dir := Vector3(0, 1, 0)
	var best_score := -INF
	var golden_angle := PI * (3.0 - sqrt(5.0))
	for i in range(N):
		var y := 1.0 - (float(i) / float(N - 1)) * 2.0
		var radius_at_y := sqrt(max(0.0, 1.0 - y * y))
		var theta := golden_angle * i
		var d := Vector3(cos(theta) * radius_at_y, y, sin(theta) * radius_at_y).normalized()
		var r := _bisect(gen, d)
		var alt := r - PLANET_RADIUS
		if alt <= 5.0:
			continue
		var dominant := gen.sample_dominant_material(d)
		var mat_match := 1.0 if dominant == target_mat else -1.0
		var alt_penalty := absf(alt - 250.0) / 400.0
		var score := mat_match - alt_penalty
		if score > best_score:
			best_score = score
			best_dir = d
	return best_dir


func _bisect(gen: EdenPlanetGeneratorV1, dir: Vector3) -> float:
	var lo := PLANET_RADIUS - 4000.0
	var hi := PLANET_RADIUS + 4000.0
	for i in range(24):
		var mid := (lo + hi) * 0.5
		var buf := VoxelBuffer.new()
		buf.create(2, 2, 2)
		var p: Vector3 = dir * mid
		var origin := Vector3i(round(p.x), round(p.y), round(p.z))
		gen.generate_block(buf, Vector3(origin), 0)
		if buf.get_voxel_f(0, 0, 0, VoxelBuffer.CHANNEL_SDF) > 0.0:
			hi = mid
		else:
			lo = mid
	return (lo + hi) * 0.5
