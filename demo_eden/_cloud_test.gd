extends SceneTree
## Cloud style + weather verification harness.
##   godot --path demo_eden --resolution 960x540 --script res://_cloud_test.gd -- <out_dir>
##
## A. Proves the weather baker's cube face table matches the renderer (numerically, from readback).
## B. Renders clouds over real EdenPlanetGeneratorV3 data: biomes, the baked coverage map, orbit and
##    ground views, and a two-frame pixel diff showing the flow map actually moves the clouds.

const R := 40000.0
var _out := "user://cloud_test"


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		_out = args[0]
	_run.call_deferred()


func _run() -> void:
	DirAccess.make_dir_recursive_absolute(_out)
	await _verify_cube_table()
	await _weather_and_style()
	print("CLOUD: done")
	quit(0)


func _frames(n: int) -> void:
	for _i in n:
		await RenderingServer.frame_post_draw


func _save(name: String) -> Image:
	var img := root.get_texture().get_image()
	img.save_png(_out.path_join(name + ".png"))
	return img


# ------------------------------------------------------------------------------------------------
# A. Cube face table
# ------------------------------------------------------------------------------------------------
func _verify_cube_table() -> void:
	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var sky_mat := ShaderMaterial.new()
	var sh := Shader.new()
	# Each texel stores its own direction; wrong face orientation shows up as a large error.
	sh.code = """shader_type sky;
uniform samplerCube cube : filter_linear;
void sky() {
	vec3 enc = texture(cube, EYEDIR).rgb * 2.0 - 1.0;
	COLOR = abs(enc - EYEDIR) * 8.0;
}"""
	sky_mat.shader = sh
	sky_mat.set_shader_parameter("cube", EdenCloudShell.debug_direction_cubemap(32))
	var sky := Sky.new()
	sky.sky_material = sky_mat
	env.sky = sky
	env_node.environment = env
	root.add_child(env_node)

	var cam := Camera3D.new()
	cam.fov = 70.0
	root.add_child(cam)
	cam.current = true

	var views := {
		"+X": Vector3.RIGHT, "-X": Vector3.LEFT, "+Y": Vector3.UP,
		"-Y": Vector3.DOWN, "+Z": Vector3.BACK, "-Z": Vector3.FORWARD,
		"diag": Vector3(1, 0.7, -0.4).normalized(),
	}
	var worst := 0.0
	for key in views:
		var dir: Vector3 = views[key]
		var up := Vector3.UP if absf(dir.dot(Vector3.UP)) < 0.99 else Vector3.BACK
		cam.global_transform = Transform3D(Basis.looking_at(dir, up), Vector3.ZERO)
		await _frames(6)
		var img := _save("cube_table_" + key.replace("+", "p").replace("-", "m"))
		var face_max := 0.0
		for y in range(0, img.get_height(), 8):
			for x in range(0, img.get_width(), 8):
				var c := img.get_pixel(x, y)
				face_max = maxf(face_max, maxf(c.r, maxf(c.g, c.b)))
		worst = maxf(worst, face_max)
		print("CLOUD: cube table view %-4s max error pixel %.3f" % [key, face_max])
	# A correct table leaves only bilinear interpolation error (a few % of a direction, x8 -> dim).
	# A wrong face orientation saturates to 1.0.
	print("CLOUD: cube table %s (worst %.3f)" % ["PASS" if worst < 0.8 else "FAIL", worst])

	env_node.queue_free()
	cam.queue_free()
	await _frames(2)


# ------------------------------------------------------------------------------------------------
# B. Weather + style over real planet data
# ------------------------------------------------------------------------------------------------
func _weather_and_style() -> void:
	var t0 := Time.get_ticks_msec()
	var gen := EdenPlanetGeneratorV3.new()
	var climate: Image = gen.get_climate_debug_image(512, 256)
	var biomes: Image = gen.get_biome_debug_image(1024, 512)
	print("CLOUD: V3 climate %s, biomes %s, generated in %d ms" % [
		str(climate.get_size()) if climate else "null", str(biomes.get_size()) if biomes else "null",
		Time.get_ticks_msec() - t0])

	var env_node := WorldEnvironment.new()
	env_node.name = "Env"
	var env := Environment.new()
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env_node.environment = env
	root.add_child(env_node)
	var sun := DirectionalLight3D.new()
	sun.name = "Sun"
	root.add_child(sun)

	var planet_mat := ShaderMaterial.new()
	var psh := Shader.new()
	psh.code = """shader_type spatial;
render_mode diffuse_burley, specular_disabled;
uniform sampler2D biome_map : source_color, filter_linear, repeat_enable;
uniform samplerCube weather_map : filter_linear;
uniform int mode = 0; // 0 biomes, 1 coverage delta (red more / blue less)
varying vec3 world_dir;
void vertex() { world_dir = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	vec3 d = normalize(world_dir);
	// EdenPlanetGeneratorV3 equirect layout.
	float lat = asin(clamp(d.y, -1.0, 1.0));
	float lon = atan(d.z, d.x);
	vec3 col = texture(biome_map, vec2(lon / TAU + 0.5, 0.5 - lat / PI)).rgb;
	if (mode == 1) {
		float a = textureLod(weather_map, d, 0.0).a;
		col = vec3(max(a, 0.0) * 2.5, 0.08, max(-a, 0.0) * 2.5);
		EMISSION = col;
	}
	ALBEDO = col;
	ROUGHNESS = 1.0;
}"""
	planet_mat.shader = psh

	# Faithful coverage view: unshaded, so neither the sun, the tonemapper's hue shifts nor the fog can
	# recolour it. Red = more cloud than cloud_coverage, blue = less, black = no change.
	var coverage_mat := ShaderMaterial.new()
	var csh := Shader.new()
	csh.code = """shader_type spatial;
render_mode unshaded;
uniform samplerCube weather_map : filter_linear;
varying vec3 world_dir;
void vertex() { world_dir = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz; }
void fragment() {
	float a = textureLod(weather_map, normalize(world_dir), 0.0).a;
	ALBEDO = vec3(max(a, 0.0), 0.0, max(-a, 0.0)) * 2.5;
}"""
	coverage_mat.shader = csh
	planet_mat.set_shader_parameter("biome_map", ImageTexture.create_from_image(biomes))
	var sphere := SphereMesh.new()
	sphere.radius = R
	sphere.height = R * 2.0
	sphere.radial_segments = 256
	sphere.rings = 128
	var planet := MeshInstance3D.new()
	planet.mesh = sphere
	planet.material_override = planet_mat
	root.add_child(planet)

	var clouds := EdenCloudShell.new()
	clouds.name = "Clouds"
	clouds.planet_radius = R
	root.add_child(clouds)
	clouds.weather_source_image = climate

	var atmo := EdenPlanetAtmosphere.new()
	atmo.name = "Atmo"
	atmo.planet_radius = R
	atmo.day_length_seconds = 0.0
	atmo.light_rays_enabled = false
	atmo.star_intensity = 0.0
	root.add_child(atmo)
	atmo.sun_light_path = atmo.get_path_to(sun)
	atmo.environment_path = atmo.get_path_to(env_node)
	atmo.add_linked_material(clouds.get_material())

	var cam := Camera3D.new()
	cam.near = 5.0
	cam.far = 400000.0
	root.add_child(cam)
	cam.current = true

	# Let the bakes land (cells + weather happen on the first process frames).
	await _frames(10)
	planet_mat.set_shader_parameter("weather_map", clouds.get_weather_cubemap())
	coverage_mat.set_shader_parameter("weather_map", clouds.get_weather_cubemap())
	print("CLOUD: weather cubemap %s" % ("ok" if clouds.get_weather_cubemap() != null else "MISSING"))

	# --- Orbit views over three longitudes, with the sun roughly behind the camera.
	for lon_deg in [0.0, 120.0, 240.0]:
		var lon := deg_to_rad(lon_deg)
		var site := Vector3(cos(lon), 0.35, sin(lon)).normalized()
		_aim_sun_at(atmo, site, 55.0)
		cam.fov = 45.0
		cam.global_transform = Transform3D(Basis.looking_at(-site, Vector3.UP), site * (R + 70000.0))
		planet_mat.set_shader_parameter("mode", 0)
		clouds.visible = true
		await _frames(8)
		_save("orbit_clouds_lon%03d" % int(lon_deg))
		if int(lon_deg) == 0:
			clouds.visible = false
			await _frames(4)
			_save("orbit_diag_no_clouds")
			clouds.visible = true
			atmo.fog_enabled = false
			await _frames(4)
			_save("orbit_diag_no_fog")
			atmo.fog_enabled = true
		planet_mat.set_shader_parameter("mode", 1)
		clouds.visible = false
		atmo.fog_enabled = false
		planet.material_override = coverage_mat
		await _frames(4)
		_save("orbit_coverage_lon%03d" % int(lon_deg))
		planet.material_override = planet_mat
		atmo.fog_enabled = true
	clouds.visible = true
	planet_mat.set_shader_parameter("mode", 0)

	# --- Ground views: style under a high sun and at sunset.
	var ground_site := Vector3(0.8, 0.25, 0.55).normalized()
	for pose in [["ground_day", 50.0, 18.0], ["ground_sunset", 3.0, 8.0]]:
		_aim_sun_at(atmo, ground_site, pose[1])
		await _frames(2)
		var sd: Vector3 = atmo.get_sun_direction()
		var horiz := (sd - ground_site * sd.dot(ground_site)).normalized()
		# Look away from the sun for the day shot (lit faces), toward it at sunset (rims, glow).
		if pose[0] == "ground_day":
			horiz = -horiz
		var pitch := deg_to_rad(pose[2])
		cam.fov = 75.0
		cam.global_transform = Transform3D(
				Basis.looking_at((horiz * cos(pitch) + ground_site * sin(pitch)).normalized(), ground_site),
				ground_site * (R + 120.0))
		await _frames(8)
		_save(pose[0])

	# --- Style sweep at the daytime ground pose.
	_aim_sun_at(atmo, ground_site, 50.0)
	await _frames(2)
	var sdd: Vector3 = atmo.get_sun_direction()
	var away_d := -(sdd - ground_site * sdd.dot(ground_site)).normalized()
	cam.fov = 75.0
	cam.global_transform = Transform3D(
			Basis.looking_at((away_d * cos(0.55) + ground_site * sin(0.55)).normalized(), ground_site),
			ground_site * (R + 120.0))
	var sweep := [
		["style_a_defaults", 1.0, 12.0, 0.05, 0.06],
		["style_b_big_facets", 1.0, 5.0, 0.05, 0.08],
		["style_c_small_facets_crisp", 1.0, 14.0, 0.05, 0.04],
		["style_d_soft_reference", 0.0, 9.0, 0.05, 0.16],
		["style_e_strong_relief", 1.0, 9.0, 0.2, 0.08],
		["style_f_no_relief", 1.0, 9.0, 0.0, 0.08],
	]
	for st in sweep:
		clouds.cloud_facet_mix = st[1]
		clouds.cloud_facet_scale = st[2]
		clouds.cloud_facet_relief = st[3]
		clouds.cloud_edge_hardness = st[4]
		await _frames(6)
		_save(st[0])
	clouds.cloud_facet_mix = 1.0
	clouds.cloud_facet_scale = 12.0
	clouds.cloud_facet_relief = 0.05
	clouds.cloud_edge_hardness = 0.06

	# --- Flow: same pose, two moments; clouds must differ.
	_aim_sun_at(atmo, ground_site, 50.0)
	var sd2: Vector3 = atmo.get_sun_direction()
	var away := -(sd2 - ground_site * sd2.dot(ground_site)).normalized()
	cam.global_transform = Transform3D(
			Basis.looking_at((away * cos(0.5) + ground_site * sin(0.5)).normalized(), ground_site),
			ground_site * (R + 120.0))
	# Default motion settings. Drift on should clearly move the picture; with drift off, what is left
	# is clouds forming and dissolving, which must be much slower.
	await _frames(8)
	var m0 := _save("flow_move_t0")
	await _wait_ms(3000)
	var m1 := _save("flow_move_t3s")
	print("CLOUD: defaults, 3s: mean abs diff %.4f (transport + morph)" % _sky_diff(m0, m1))
	var drift_speed: float = clouds.cloud_drift_speed
	clouds.cloud_drift_speed = 0.0
	await _frames(4)
	var f0 := _save("flow_morph_t0")
	await _wait_ms(3000)
	var f1 := _save("flow_morph_t3s")
	print("CLOUD: drift off, 3s: mean abs diff %.4f (morph only, should be far smaller)" % _sky_diff(f0, f1))
	clouds.flow_speed = 0.0
	await _frames(4)
	var z0 := _save("flow_static_t0")
	await _wait_ms(3000)
	var z1 := _save("flow_static_t3s")
	print("CLOUD: all motion off, 3s: mean abs diff %.4f (noise floor)" % _sky_diff(z0, z1))
	clouds.flow_speed = 0.03
	clouds.flow_strength = 0.06
	await _frames(4)
	var o0 := _save("flow_morph_old_t0")
	await _wait_ms(3000)
	var o1 := _save("flow_morph_old_t3s")
	print("CLOUD: drift off, previous flow defaults, 3s: mean abs diff %.4f" % _sky_diff(o0, o1))
	clouds.flow_speed = 0.002
	clouds.flow_strength = 0.2
	clouds.cloud_drift_speed = drift_speed

	# --- Onion layering close-up: a tight FOV on a cloud silhouette against clear sky, so the rim
	# (thin, translucent, glowing) vs core (solid) structure from cloud_layer_erosion is visible.
	_aim_sun_at(atmo, ground_site, 35.0)
	var sd3: Vector3 = atmo.get_sun_direction()
	var toward_sun := (sd3 - ground_site * sd3.dot(ground_site)).normalized()
	cam.fov = 25.0
	cam.global_transform = Transform3D(
			Basis.looking_at((toward_sun * cos(0.4) + ground_site * sin(0.4)).normalized(), ground_site),
			ground_site * (R + 120.0))
	await _frames(10)
	_save("onion_closeup_toward_sun")
	cam.global_transform = Transform3D(
			Basis.looking_at((-toward_sun * cos(0.4) + ground_site * sin(0.4)).normalized(), ground_site),
			ground_site * (R + 120.0))
	await _frames(10)
	_save("onion_closeup_away_from_sun")
	cam.fov = 75.0

	# --- Sky/star/moon grading controls: sweep each new uniform far from its default so the effect
	# is unambiguous in a screenshot diff, one pose per setting.
	atmo.day_length_seconds = 0.0
	var night_phase: float = atmo.phase_for_sun_elevation(ground_site, -20.0)
	if night_phase >= 0.0:
		atmo.sun_time_of_day = night_phase
	cam.global_transform = Transform3D(Basis.looking_at(ground_site, horiz_up(ground_site)), ground_site * (R + 120.0))
	await _frames(6)
	_save("sky_controls_baseline_night")

	atmo.star_size = 0.12
	atmo.star_twinkle = 0.0
	await _frames(6)
	_save("sky_controls_star_small")
	atmo.star_size = 0.35

	atmo.star_size = 0.5
	await _frames(6)
	_save("sky_controls_star_big")
	atmo.star_size = 0.35

	atmo.star_color_variation = 1.0
	atmo.star_brightness_variation = 1.0
	await _frames(6)
	_save("sky_controls_star_varied")
	atmo.star_color_variation = 0.3
	atmo.star_brightness_variation = 0.75

	atmo.moon_contrast = 2.5
	atmo.moon_saturation = 0.0
	await _frames(10)
	_save("sky_controls_moon_high_contrast_bw")
	atmo.moon_contrast = 1.0
	atmo.moon_saturation = 1.0

	atmo.moon_opacity = 0.3
	await _frames(10)
	_save("sky_controls_moon_translucent")
	atmo.moon_opacity = 1.0

	if atmo.space_panorama != null:
		atmo.space_gamma = 2.5
		atmo.space_contrast = 1.8
		await _frames(6)
		_save("sky_controls_space_graded")
		atmo.space_gamma = 1.0
		atmo.space_contrast = 1.0

		atmo.space_opacity = 0.15
		await _frames(6)
		_save("sky_controls_space_faint")
		atmo.space_opacity = 1.0


func _wait_ms(ms: int) -> void:
	var t_start := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t_start < ms:
		await process_frame
	await _frames(2)


func _sky_diff(a: Image, b: Image) -> float:
	var diff := 0.0
	var n := 0
	for y in range(0, a.get_height() / 2, 6): # upper half: sky and clouds only
		for x in range(0, a.get_width(), 6):
			var ca := a.get_pixel(x, y)
			var cb := b.get_pixel(x, y)
			diff += absf(ca.r - cb.r) + absf(ca.g - cb.g) + absf(ca.b - cb.b)
			n += 1
	return diff / maxf(n, 1)


func horiz_up(site: Vector3) -> Vector3:
	var ref := Vector3.UP if absf(site.dot(Vector3.UP)) < 0.95 else Vector3.RIGHT
	return (ref - site * site.dot(ref)).normalized()


func _aim_sun_at(atmo: EdenPlanetAtmosphere, site: Vector3, elev_deg: float) -> void:
	var ph := atmo.phase_for_sun_elevation(site, elev_deg)
	if ph >= 0.0:
		atmo.sun_time_of_day = ph
