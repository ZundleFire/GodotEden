extends SceneTree
## EdenPlanetOcean verification: screenshots from orbit and at the surface, plus a few pass/fail checks.
##   godot --path demo_eden --resolution 1280x720 --script res://_ocean_test.gd -- <out_dir>
##
## Terrain is a second EdenPlanetOcean with an opaque displaced-noise material (so it gets the same
## quadtree LOD for free); _height() below is the CPU twin of the terrain shader's noise, used to
## find open ocean, shallows and coastline to point cameras at.

const R := 20000.0
const TERRAIN_FREQ := 5.0
const TERRAIN_AMP := 900.0
const SEA_BIAS := -0.06

var _out := "user://ocean_test"
var _fails := 0
var _sun: DirectionalLight3D
var _atmo: EdenPlanetAtmosphere
var _cam: Camera3D
var _ocean: EdenPlanetOcean
var _terrain: EdenPlanetOcean


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		_out = args[0]
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	printerr("OCEAN: %s %s" % ["PASS" if ok else "FAIL", what])
	if not ok:
		_fails += 1


func _frames(n: int) -> void:
	for _i in n:
		await RenderingServer.frame_post_draw


func _save(name: String) -> Image:
	var img := root.get_texture().get_image()
	img.save_png(_out.path_join(name + ".png"))
	printerr("OCEAN: saved %s (leaves %d)" % [name, _ocean.get_leaf_count()])
	return img


# --- CPU twin of the terrain shader's noise ----------------------------------------------------
func _hash(x: int, y: int, z: int) -> float:
	var h := ((x * 73856093) ^ (y * 19349663) ^ (z * 83492791)) & 0xFFFFFFFF
	h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
	h = h ^ (h >> 16)
	return float(h & 0xFFFFFF) / 16777215.0


func _vnoise(p: Vector3) -> float:
	var i := p.floor()
	var f := p - i
	var u := f * f * (Vector3.ONE * 3.0 - 2.0 * f)
	var x := int(i.x)
	var y := int(i.y)
	var z := int(i.z)
	var a := lerpf(_hash(x, y, z), _hash(x + 1, y, z), u.x)
	var b := lerpf(_hash(x, y + 1, z), _hash(x + 1, y + 1, z), u.x)
	var c := lerpf(_hash(x, y, z + 1), _hash(x + 1, y, z + 1), u.x)
	var d := lerpf(_hash(x, y + 1, z + 1), _hash(x + 1, y + 1, z + 1), u.x)
	return lerpf(lerpf(a, b, u.y), lerpf(c, d, u.y), u.z)


func _height(dir: Vector3) -> float:
	var p := dir * TERRAIN_FREQ
	var s := 0.0
	var amp := 0.5
	for _o in 6:
		s += amp * (_vnoise(p) * 2.0 - 1.0)
		p *= 2.0
		amp *= 0.5
	return TERRAIN_AMP * (s + SEA_BIAS)


const TERRAIN_SHADER := """shader_type spatial;
render_mode world_vertex_coords, diffuse_burley;
uniform float radius;
uniform float freq;
uniform float amp;
uniform float sea_bias;
varying vec3 wpos;
varying float h;
float hash3(int x, int y, int z) {
	uint hh = (uint(x) * 73856093u) ^ (uint(y) * 19349663u) ^ (uint(z) * 83492791u);
	hh = (hh ^ (hh >> 13u)) * 1274126177u;
	hh = hh ^ (hh >> 16u);
	return float(hh & 16777215u) / 16777215.0;
}
float vnoise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = p - i;
	vec3 u = f * f * (3.0 - 2.0 * f);
	int x = int(i.x); int y = int(i.y); int z = int(i.z);
	float a = mix(hash3(x, y, z), hash3(x + 1, y, z), u.x);
	float b = mix(hash3(x, y + 1, z), hash3(x + 1, y + 1, z), u.x);
	float c = mix(hash3(x, y, z + 1), hash3(x + 1, y, z + 1), u.x);
	float d = mix(hash3(x, y + 1, z + 1), hash3(x + 1, y + 1, z + 1), u.x);
	return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}
void vertex() {
	vec3 d = normalize(VERTEX);
	vec3 p = d * freq;
	float s = 0.0;
	float a = 0.5;
	for (int o = 0; o < 6; o++) { s += a * (vnoise(p) * 2.0 - 1.0); p *= 2.0; a *= 0.5; }
	h = amp * (s + sea_bias);
	VERTEX = d * (radius + h);
	wpos = VERTEX;
}
void fragment() {
	vec3 n = normalize(cross(dFdy(wpos), dFdx(wpos)));
	if (dot(n, wpos) < 0.0) { n = -n; }
	NORMAL = normalize((VIEW_MATRIX * vec4(n, 0.0)).xyz);
	vec3 sand = vec3(0.76, 0.68, 0.5);
	vec3 seabed = mix(sand * 0.8, vec3(0.25, 0.3, 0.28), smoothstep(-2.0, -120.0, h));
	vec3 grass = vec3(0.22, 0.42, 0.14);
	vec3 rock = vec3(0.42, 0.38, 0.35);
	vec3 col = h < 0.0 ? seabed : mix(sand, grass, smoothstep(3.0, 15.0, h));
	col = mix(col, rock, smoothstep(250.0, 450.0, h));
	ALBEDO = col;
	ROUGHNESS = 0.95;
}"""


func _run() -> void:
	DirAccess.make_dir_recursive_absolute(_out)
	_build_scene()
	printerr("OCEAN: scene built")
	await _frames(10)

	# Find sites: deepest open ocean, and a coastline crossing next to it.
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var deep := Vector3.UP
	var deep_h := 1e9
	for _i in 4000:
		var d := Vector3(rng.randf_range(-1, 1), rng.randf_range(-0.6, 0.6), rng.randf_range(-1, 1)).normalized()
		var h := _height(d)
		if h < deep_h:
			deep_h = h
			deep = d
	var coast := Vector3.ZERO
	var shallow := Vector3.ZERO
	var axis := deep.cross(Vector3.UP).normalized()
	for step in 3000:
		var d := deep.rotated(axis, deg_to_rad(step * 0.02))
		var h := _height(d)
		if shallow == Vector3.ZERO and h > -25.0:
			shallow = d
		if h > 0.0:
			coast = d
			break
	printerr("OCEAN: deep site %s h=%.0f, coast %s, shallow %s" % [deep, deep_h, coast, shallow])
	_check(coast != Vector3.ZERO, "found a coastline to photograph")

	# ---------------- From space ----------------
	var site := deep
	_aim_sun(site, 50.0)
	await _frames(4)
	var to_sun := _sun.global_basis.z
	_place(site * (R * 3.2) + to_sun * R * 0.6, Vector3.ZERO, 40.0)
	await _frames(20)
	var leaves_orbit := _ocean.get_leaf_count()
	var img := _save("01_space_full_disk")
	await _perf("orbit")
	var c := img.get_pixel(img.get_width() / 2, img.get_height() / 2)
	_check(c.b > c.r, "day-side ocean reads blue from orbit (centre px %s)" % c)

	# Sun glint: camera placed at the mirror direction of the sun about the site normal.
	var mirror := (2.0 * to_sun.dot(site) * site - to_sun).normalized()
	_place(site * R + mirror * R * 1.6, site * R, 35.0)
	await _frames(20)
	_save("02_space_sun_glint")

	# Terminator + night side must not glow (the upstream shader emitted color_deep unlit).
	var night := -to_sun
	var side := to_sun.cross(Vector3.UP).normalized()
	_place(side * R * 3.0 + night * R * 0.8, Vector3.ZERO, 45.0)
	await _frames(20)
	_save("03_space_terminator")
	_place(night * R * 2.2, night * R, 40.0)
	_ocean.visible = false
	await _frames(10)
	var night_without := _lum(_save("04a_night_side_no_ocean"))
	_ocean.visible = true
	await _frames(10)
	var night_with := _lum(_save("04b_night_side_with_ocean"))
	_check(night_with < night_without + 0.03, "night-side ocean does not glow (lum %.3f vs %.3f w/o ocean)" % [night_with, night_without])

	# Low orbit, looking along the coast toward the horizon.
	_aim_sun(coast, 35.0)
	await _frames(4)
	var up := coast
	var fwd := (shallow - coast).normalized().cross(up).normalized()
	_place(coast * (R + 4000.0) - fwd * 3000.0, coast * R + fwd * 12000.0, 60.0)
	await _frames(30)
	_save("05_low_orbit_coast")

	# ---------------- At the surface ----------------
	_aim_sun(deep, 30.0)
	await _frames(4)
	to_sun = _sun.global_basis.z
	var tangent_sun := (to_sun - deep * to_sun.dot(deep)).normalized()
	var across := tangent_sun.cross(deep).normalized()
	var eye := deep * (R + 4.0)
	_place(eye, eye + across * 500.0 + deep * 5.0, 70.0)
	await _frames(40)
	_check(_ocean.get_leaf_count() > leaves_orbit, "LOD refines at the surface (%d leaves vs %d from orbit)" % [_ocean.get_leaf_count(), leaves_orbit])
	_save("06_surface_open_ocean")
	await _perf("surface")
	_place(eye, eye + tangent_sun * 500.0 + deep * 10.0, 70.0)
	await _frames(20)
	_save("07_surface_toward_sun")

	# Sunset glint.
	_aim_sun(deep, 4.0)
	await _frames(4)
	to_sun = _sun.global_basis.z
	tangent_sun = (to_sun - deep * to_sun.dot(deep)).normalized()
	_place(eye, eye + tangent_sun * 500.0 + deep * 12.0, 70.0)
	await _frames(40)
	_save("08_surface_sunset")

	# Shoreline: offshore in the shallows, looking at the beach.
	_aim_sun(coast, 40.0)
	await _frames(4)
	var toward_land := (coast - shallow).normalized()
	eye = shallow * (R + 6.0)
	_place(eye, coast * (R + 5.0), 70.0)
	await _frames(40)
	_save("09_surface_shoreline")
	# From the beach looking out to sea.
	eye = coast * (R + maxf(_height(coast), 0.0) + 3.0)
	_place(eye, shallow * R, 70.0)
	await _frames(30)
	_save("10_surface_from_beach")

	# Looking down into shallow water (refraction + depth tint over the seabed).
	eye = shallow * (R + 25.0)
	var fwd2 := toward_land.cross(shallow).normalized()
	_place(eye, shallow * R + fwd2 * 12.0, 70.0)
	await _frames(30)
	_save("11_surface_look_down_shallows")

	# Underwater, looking up (Snell's window).
	eye = deep * (R - 6.0)
	_place(eye, deep * R + fwd2 * 6.0, 80.0)
	await _frames(30)
	_save("12_underwater_looking_up")

	var mat := _ocean.material as ShaderMaterial

	# ---------------- Currents ----------------
	_check(_ocean.get_current_map() != null, "current cubemap baked")
	_aim_sun(deep, 50.0)
	await _frames(4)
	_place(deep * (R * 3.2) + _sun.global_basis.z * R * 0.6, Vector3.ZERO, 40.0)
	mat.set_shader_parameter("debug_show_currents", true)
	await _frames(15)
	_save("13_space_current_field")
	mat.set_shader_parameter("debug_show_currents", false)
	# Two open-ocean spots whose currents point most differently; both shot facing north from 30 m
	# with time frozen, so the swell's crest lines should visibly turn with the current.
	var spots: Array[Vector3] = []
	for _i in 60:
		var d := (deep + Vector3(rng.randf_range(-0.4, 0.4), rng.randf_range(-0.4, 0.4), rng.randf_range(-0.4, 0.4))).normalized()
		if _height(d) < -60.0 and _ocean.get_current_at(d * R).length() > 0.4:
			spots.append(d)
	var best := [deep, deep]
	var best_dot := 2.0
	var tangent_ok := true
	for a in spots:
		var ca := _ocean.get_current_at(a * R)
		tangent_ok = tangent_ok and absf(ca.normalized().dot(a)) < 0.05 and ca.length() <= 1.001
		for b in spots:
			var dd := ca.normalized().dot(_ocean.get_current_at(b * R).normalized())
			if dd < best_dot:
				best_dot = dd
				best = [a, b]
	_check(tangent_ok and spots.size() > 1, "get_current_at returns tangent flow, strength <= 1 (%d ocean samples)" % spots.size())
	_check(best_dot < 0.0, "currents vary across the ocean (most opposed pair dot %.2f)" % best_dot)
	mat.set_shader_parameter("freeze_time", true)
	for i in 2:
		var spot: Vector3 = best[i]
		var east := Vector3.UP.cross(spot).normalized()
		var north := spot.cross(east)
		var cur := _ocean.get_current_at(spot * R)
		_aim_sun(spot, 35.0)
		await _frames(4)
		_place(spot * (R + 30.0), spot * (R + 5.0) + north * 400.0, 70.0)
		await _frames(30)
		printerr("OCEAN: current at shot %d: heading %.0f deg from east toward north, strength %.2f" % [
			i, rad_to_deg(atan2(cur.dot(north), cur.dot(east))), cur.length()])
		_save("14_current_spot_%d_facing_north" % i)
	mat.set_shader_parameter("freeze_time", false)

	# ---------------- Low poly ----------------
	mat.set_shader_parameter("low_poly_normals", true)
	_aim_sun(deep, 30.0)
	await _frames(4)
	eye = deep * (R + 6.0)
	_place(eye, eye + across * 300.0 + deep * 2.0, 70.0)
	await _frames(30)
	_save("15_low_poly_surface")
	_place(deep * (R * 3.2) + _sun.global_basis.z * R * 0.6, Vector3.ZERO, 40.0)
	await _frames(20)
	_save("16_low_poly_orbit")
	mat.set_shader_parameter("low_poly_normals", false)

	# ---------------- Ripples ----------------
	# A 4 m box floating at sea level: waterline foam (found automatically, no group) should brighten
	# the water around it, and vanish when the box opts out with the ocean_ripple_ignore meta.
	var box := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = Vector3(4, 4, 4)
	box.mesh = bm
	root.add_child(box)
	box.global_position = deep * R
	mat.set_shader_parameter("freeze_time", true)
	_aim_sun(deep, 35.0)
	await _frames(4)
	var n_dir := deep.cross(Vector3.UP).normalized()
	_place(deep * (R + 30.0) - n_dir * 6.0, deep * R, 50.0)
	await _frames(20)
	var with_r := _save("18_ripples_around_box")
	box.set_meta("ocean_ripple_ignore", true)
	await _frames(6)
	var without_r := root.get_texture().get_image()
	var cx := with_r.get_width() / 2
	var cy := with_r.get_height() / 2
	var ring_px := 0
	for y in range(cy - 150, cy + 150, 3):
		for x in range(cx - 150, cx + 150, 3):
			if absf(with_r.get_pixel(x, y).get_luminance() - without_r.get_pixel(x, y).get_luminance()) > 0.05:
				ring_px += 1
	_check(ring_px > 200, "waterline foam draws around a floating box (%d changed pixels near it)" % ring_px)
	mat.set_shader_parameter("freeze_time", false)
	box.queue_free()

	# ---------------- LOD pop ----------------
	# Descend with time frozen; any LOD split shows up as a spike in frame-to-frame change on top of
	# the smooth change from the camera moving. Score = worst step / median step.
	mat.set_shader_parameter("freeze_time", true)
	var score_off := await _lod_pop_score(deep, across, false)
	var score_on := await _lod_pop_score(deep, across, true)
	printerr("OCEAN: LOD pop score (LOD-change step / plain step): blend off %.3f, blend on %.3f" % [score_off, score_on])
	# A score at or under 1.0 means an LOD change moves no more pixels than the camera does between
	# two ordinary frames -- there is no pop left for blending to reduce, and comparing the two
	# numbers is then just comparing noise. Only require blending to help when something is popping.
	_check(score_on <= 1.0 or score_on < score_off, "LOD blending reduces popping")
	mat.set_shader_parameter("freeze_time", false)

	printerr("OCEAN: done, %d failure(s)" % _fails)
	quit(1 if _fails > 0 else 0)


# Descends toward the sea with time frozen and measures frame-to-frame image change, split by
# whether the step changed the LOD (patch count changed = a split or merge happened). Score = mean
# change on LOD-change steps / mean change on ordinary steps: ~1.0 means LOD changes are invisible
# against plain camera motion, >1 means they pop. Everything else that changes on its own is taken
# out: the terrain (its own, unmorphed LOD) and the atmosphere (the sky converges over frames);
# flat low-poly normals make any geometry change visible.
func _lod_pop_score(site: Vector3, fwd: Vector3, blend: bool) -> float:
	var m := _ocean.material as ShaderMaterial
	m.set_shader_parameter("lod_blend", blend)
	m.set_shader_parameter("low_poly_normals", true)
	_terrain.visible = false
	root.remove_child(_atmo)
	_sun.look_at_from_position(Vector3.ZERO, -(site + fwd).normalized(), Vector3.UP)
	var change_sum := 0.0
	var change_n := 0
	var plain_sum := 0.0
	var plain_n := 0
	var prev: Image = null
	var prev_leaves := -1
	for i in 160:
		var alt := 600.0 * pow(15.0 / 600.0, i / 159.0)
		var eye := site * (R + alt)
		_place(eye, site * R + fwd * (alt * 2.5 + 60.0), 60.0)
		var leaves := _ocean.get_leaf_count()
		await _frames(2)
		var img := root.get_texture().get_image()
		img.resize(192, 108, Image.INTERPOLATE_BILINEAR)
		if prev != null:
			var sum := 0.0
			for y in 108:
				for x in 192:
					var ca := img.get_pixel(x, y)
					var cb := prev.get_pixel(x, y)
					sum += absf(ca.r - cb.r) + absf(ca.g - cb.g) + absf(ca.b - cb.b)
			if leaves != prev_leaves:
				change_sum += sum
				change_n += 1
			else:
				plain_sum += sum
				plain_n += 1
		prev = img
		prev_leaves = leaves
	_terrain.visible = true
	root.add_child(_atmo)
	m.set_shader_parameter("low_poly_normals", false)
	var score := (change_sum / maxi(change_n, 1)) / maxf(plain_sum / maxi(plain_n, 1), 1e-6)
	printerr("OCEAN: LOD %s: %d LOD-change steps, %d plain steps, mean change ratio %.3f" % [
		"blended" if blend else "unblended", change_n, plain_n, score])
	return score


# GPU ms of the whole frame with the ocean shown vs hidden; the difference is the ocean's cost.
func _perf(label: String) -> void:
	var vp := root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	var ms := [0.0, 0.0]
	for pass_i in 2:
		_ocean.visible = pass_i == 0
		await _frames(10)
		for _f in 60:
			await RenderingServer.frame_post_draw
			ms[pass_i] += RenderingServer.viewport_get_measured_render_time_gpu(vp) / 60.0
	_ocean.visible = true
	printerr("OCEAN: PERF %s gpu %.2f ms with ocean, %.2f without -> ocean %.2f ms (%d leaves)" % [
		label, ms[0], ms[1], ms[0] - ms[1], _ocean.get_leaf_count()])


func _lum(img: Image) -> float:
	var s := 0.0
	var n := 0
	for y in range(0, img.get_height(), 8):
		for x in range(0, img.get_width(), 8):
			var p := img.get_pixel(x, y)
			s += p.r * 0.2126 + p.g * 0.7152 + p.b * 0.0722
			n += 1
	return s / n


func _aim_sun(site: Vector3, elev_deg: float) -> void:
	var ph := _atmo.phase_for_sun_elevation(site, elev_deg)
	if ph >= 0.0:
		_atmo.sun_time_of_day = ph


func _place(pos: Vector3, target: Vector3, fov: float) -> void:
	var up := pos.normalized()
	var fwd := (target - pos).normalized()
	if absf(fwd.dot(up)) > 0.99:
		up = fwd.cross(Vector3.RIGHT).normalized()
	_cam.fov = fov
	_cam.global_transform = Transform3D(Basis.looking_at(fwd, up), pos)
	_ocean.update_lod()
	_terrain.update_lod()


func _build_scene() -> void:
	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env_node.environment = env
	root.add_child(env_node)
	_sun = DirectionalLight3D.new()
	root.add_child(_sun)

	var tmat := ShaderMaterial.new()
	var tsh := Shader.new()
	tsh.code = TERRAIN_SHADER
	tmat.shader = tsh
	tmat.set_shader_parameter("radius", R)
	tmat.set_shader_parameter("freq", TERRAIN_FREQ)
	tmat.set_shader_parameter("amp", TERRAIN_AMP)
	tmat.set_shader_parameter("sea_bias", SEA_BIAS)
	var terrain := EdenPlanetOcean.new()
	_terrain = terrain
	terrain.name = "Terrain"
	terrain.planet_radius = R
	terrain.min_leaf_world_size = 32.0
	terrain.skirt_depth_ratio = 0.5
	terrain.material = tmat
	terrain.horizon_culling = false # mountains past the sea horizon are still visible
	terrain.current_enabled = false
	root.add_child(terrain)

	_ocean = EdenPlanetOcean.new()
	_ocean.name = "Ocean"
	_ocean.planet_radius = R
	root.add_child(_ocean)

	_atmo = EdenPlanetAtmosphere.new()
	_atmo.planet_radius = R
	_atmo.day_length_seconds = 0.0
	_atmo.light_rays_enabled = false
	_atmo.auto_create_planet_mesh = false # its placeholder sphere sits exactly at sea level
	root.add_child(_atmo)
	_atmo.sun_light_path = _atmo.get_path_to(_sun)
	_atmo.environment_path = _atmo.get_path_to(env_node)

	_cam = Camera3D.new()
	_cam.near = 0.5
	_cam.far = 400000.0
	_cam.position = Vector3(0, 0, R * 3.0) # never start inside the planet
	root.add_child(_cam)
	_cam.current = true
