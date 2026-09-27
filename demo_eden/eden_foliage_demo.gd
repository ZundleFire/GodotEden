extends Node3D
## Wires eden_foliage's EdenTreeInstance/EdenBushInstance/EdenRockInstance into a real
## VoxelInstancer on top of EdenPlanetGeneratorV1 -- the "biome ecosystem" bridge Plans.txt
## Phase 7 describes but that didn't exist yet: eden_foliage only knew how to build a single
## tree/bush/rock, nothing placed them.
##
## VoxelInstanceGenerator has no biome-id concept, so this uses the terrain's own MIXEL4
## material paint (grass/rock/snow/sand/dirt/moss) as a biome proxy via
## voxel_texture_filter_array -- EdenPlanetGeneratorV1::_land_material_for() already maps each
## biome to one of these slots (forest->dirt, grassland/hills->grass, desert->sand,
## tropical->moss, tundra->snow), so filtering by material lines up with biome in practice.
##
## Terrain material reuses shaders/planet_land.gdshader (see eden_materials_showcase.gd) --
## already a flat-color-per-MIXEL4-material shader (no textures), so the biome patchwork is
## directly visible: it decodes CUSTOM1's packed material indices/weights and paints one flat
## RGB per index (grass/rock/snow/sand/dirt/moss/ocean_floor), then blends by slope/height.
##
## Captures 3 screenshots on a fixed frame schedule (streaming needs time to catch up after each
## camera jump, same reasoning as eden_stress_test.gd's warmup): one aerial context shot, then
## two low-altitude close-ups over a forest patch and a grassland patch specifically (found via
## _find_material_land(), not a latitude guess -- see its comment) so individual foliage
## instances are actually visible in frame, not just biome-colored ground from far away.
##
## ponytail: every instancer item is a VoxelInstanceLibrarySceneItem (one Node3D per spawned
## instance), not MultiMesh -- correct and simple, but the most expensive option at high density.
## Fine for this demo's scale; swap dense layers (grass/small rocks) to
## VoxelInstanceLibraryMultiMeshItem if profiling ever shows scene-item count is the bottleneck.

## EdenPlanetGeneratorV1's default params (heights, AND noise frequencies/tectonic_line_width_km)
## are tuned as a package for a ~40km-radius planet (see eden_stress_test.gd,
## eden_materials_showcase.gd) -- shrinking just the radius, or radius+heights, was tried and
## rejected: heights alone still left kilometer-tall spikes (amplitude >> radius); scaling
## heights down too just exposed that noise wavelengths and tectonic_line_width_km are ALSO
## tuned to 40km (a "coastline" transition zone that's a small fraction of a 40km planet becomes
## most of a 2km one), producing giant canyon-like seams no amount of height scaling fixed.
## Properly non-dimensionalizing every length-scale param for an arbitrary radius is a real task
## on its own, not a quick demo tweak -- so this uses the exact proven-working config from
## eden_materials_showcase.gd instead: full 40km radius, all defaults untouched, and
## generate_collisions=false (collision meshing was very likely why an earlier radius=8000 run
## with collisions left ON stayed stuck at ~13 mesh blocks for 1400+ frames).
const PLANET_RADIUS := 40000.0
const PLANET_SEED := 1

# EdenPlanetGeneratorV1.MAT_* are now bound engine constants (single source of truth) instead of
# hand-duplicated magic numbers here.
const MAT_GRASS := EdenPlanetGeneratorV1.MAT_GRASS
const MAT_ROCK := EdenPlanetGeneratorV1.MAT_ROCK
const MAT_SNOW := EdenPlanetGeneratorV1.MAT_SNOW
const MAT_SAND := EdenPlanetGeneratorV1.MAT_SAND
const MAT_DIRT := EdenPlanetGeneratorV1.MAT_DIRT
const MAT_MOSS := EdenPlanetGeneratorV1.MAT_MOSS

var gen: EdenPlanetGeneratorV1
var terrain: VoxelLodTerrain
var camera: Camera3D
var _noise_seed_counter := 0 # bumped once per _mat_generator() call so each species clusters independently

var land_material: ShaderMaterial

# FIXED: the terrain material used to show a fine faceted checkerboard/speckle everywhere in LIT
# rendering, including on terrain nowhere near a real mountain. Three real bugs were found and
# fixed, each confirmed via isolated unlit/EMISSION readouts and direct elimination, not guesswork:
#   1. modules/voxel's Transvoxel MIXEL4 mesher (a fork -- see memory: voxel-module-is-forked)
#      picked each mesh CELL's top-4 materials by per-cell weight sum
#      (select_textures_4_per_voxel() in transvoxel_materials_mixel4.h), so neighboring cells
#      sharing the same active-material set could each keep a different 4th material whenever a
#      region has >4 distinct materials (coastline/biome/slope transitions easily do). Fixed with
#      a deterministic index-order tie-break.
#   2. This module's own _find_material_land() only decoded MIXEL4 slot 0, so it silently landed
#      on ocean/coastline points while believing it found forest. Fixed by adding
#      EdenPlanetGeneratorV1::sample_dominant_material() (properly decodes all 4 slots) as the
#      new single source of truth for this kind of query.
#   3. THE ACTUAL ROOT CAUSE of the visual checkerboard: EdenPlanetGeneratorV1's mountain/valley
#      erosion "gully" algorithm (eden_planet_generator_v1.cpp, the tect_line>0.001 block) is
#      intentionally high-frequency and discontinuous (a literal per-octave sign-flip) to carve
#      visible escarpment/gully character on real mountain/trench slopes -- but its old
#      `tect_line > 0.001` activation threshold was so permissive it ran at full spatial
#      frequency across almost any terrain even distantly near a tectonic line, just scaled down
#      in AMPLITUDE (not frequency) via mtn_mask. A small amplitude at a fixed high frequency
#      still produces a steep normal gradient, which read as faceted lighting noise once lit with
#      flat-colored MIXEL4 materials. Confirmed by directly zeroing mtn/valley -- an otherwise
#      checkerboarded, faceted-looking flat area went perfectly smooth. Fixed by fading the
#      erosion mask in smoothly over a much higher tect_line range, so it only activates near
#      genuinely real mountain/trench features.
# Ruled out along the way (all individually tested clean/no-effect, kept for the trail):
# material index, blend weights, slope value, specular, shader caching, LOD cross-fade dithering,
# anti-aliasing/debanding, the Transvoxel LOD-seam vertex correction, float64 for the SDF's
# r/alt computation, and the "neutral breakup" detail-noise term (this one WAS reduced during
# investigation but turned out not to be the cause -- kept at its original amplitude). Light
# direction was ALSO a separate real bug (was a static world-space rotation, now sphere-local)
# but not the cause of this specific artifact.
#
# IMPORTANT if debugging terrain material rendering further: VoxelLodTerrain.material
# (VoxelLodTerrain.md:231) duplicates a ShaderMaterial per chunk as chunks stream in, so
# set_shader_parameter() calls made AFTER _ready() do not reach chunks that already streamed in
# under an earlier value -- change a debug uniform's default in the .gdshader file itself and use
# a single shot, not a runtime override.
var _shots := [
	{"target_mat": -1, "altitude": 1200.0, "frame": 1400, "name": "res://foliage_demo_aerial.png"},
	{"target_mat": MAT_DIRT, "altitude": 180.0, "frame": 2100, "name": "res://foliage_demo_forest_closeup.png"},
	{"target_mat": MAT_GRASS, "altitude": 180.0, "frame": 2800, "name": "res://foliage_demo_grassland_closeup.png"},
]
var _shot_index := 0


func _ready() -> void:
	_build_terrain()
	_build_foliage()
	_build_lighting()
	for shot in _shots:
		shot.dir = _find_material_land(shot.target_mat)
	_position_camera(_shots[0])


# Samples candidate directions spread across the whole sphere and returns the one whose
# DOMINANT rendered material (via EdenPlanetGeneratorV1.sample_dominant_material -- properly
# decodes all 4 MIXEL4 slots and picks the highest-weight one, matching what the mesher actually
# renders) best matches target_mat (-1 = any land).
#
# An earlier version of this function hand-decoded only indices&0xF/weights&0xF (MIXEL4 slot 0,
# the "land material" slot) directly from a VoxelBuffer probe. That's wrong whenever land weight
# is legitimately 0 at that point (e.g. anywhere the dominant material is actually sand or ocean)
# -- slot 0 still reports SOME land material identity even with zero weight, so the search was
# silently scoring on a near-meaningless number and, confirmed via direct probing, kept landing
# on ocean/coastline points while believing it had found forest. sample_dominant_material() fixes
# the decode at the source so this can't happen again.
func _find_material_land(target_mat: int) -> Vector3:
	const N := 40
	var best_dir := Vector3(0, 1, 0)
	var best_score := -INF
	var golden_angle := PI * (3.0 - sqrt(5.0))
	for i in range(N):
		var y := 1.0 - (float(i) / float(N - 1)) * 2.0
		var radius_at_y := sqrt(max(0.0, 1.0 - y * y))
		var theta := golden_angle * i
		var d := Vector3(cos(theta) * radius_at_y, y, sin(theta) * radius_at_y).normalized()
		var r := _bisect_surface_radius(d)
		var alt := r - PLANET_RADIUS
		if alt <= 5.0: # underwater/ocean floor -- not a landing spot
			continue
		var dominant := gen.sample_dominant_material(d)
		var mat_match := 1.0 if (target_mat < 0 or dominant == target_mat) else -1.0
		# Mid-range altitude (150-400m): comfortably above the beach band and comfortably below
		# typical mountain elevation, so slope/height overlays don't dominate a close-up shot.
		var alt_penalty := absf(alt - 250.0) / 400.0
		var score := mat_match - alt_penalty
		if score > best_score:
			best_score = score
			best_dir = d
	return best_dir


func _build_terrain() -> void:
	gen = EdenPlanetGeneratorV1.new()
	gen.planet_radius = PLANET_RADIUS
	gen.seed_val = PLANET_SEED
	# One material id per voxel + shader-side blending, instead of MIXEL4's 4-ids-plus-4-quantized-
	# weights per voxel. In MIXEL4 each voxel carries its own weight mix AND the mesher re-picks a
	# per-cell top-4, so adjacent voxels disagree about the blend and that disagreement renders as
	# fine material speckle on flat-coloured terrain. TEXTURES_SINGLE_S4 has no per-voxel weight
	# field to disagree about: the mesher derives weights geometrically from which material each
	# cell corner holds and where the isosurface crosses the edge, so a cell whose corners all share
	# a material emits it pure (the common case) and only real boundaries blend.
	gen.single_material_mode = true
	gen.setup()

	var mesher := VoxelMesherTransvoxel.new()
	mesher.set_texturing_mode(VoxelMesherTransvoxel.TEXTURES_SINGLE_S4)

	terrain = VoxelLodTerrain.new()
	terrain.generator = gen
	terrain.mesher = mesher
	# TEXTURES_SINGLE_S4 asserts 8-bit indices (see transvoxel_materials_single_common.h:49 --
	# its 16-bit branch is unreachable behind that assert), and VoxelFormat defaults
	# indices_depth to 16-bit. sdf_depth is left at its default.
	var fmt := VoxelFormat.new()
	fmt.indices_depth = VoxelBuffer.DEPTH_8_BIT
	terrain.format = fmt
	# lod_distance drives FOLIAGE VISIBILITY, not just terrain detail: VoxelInstancer only spawns
	# an item on chunks at its lod_index (0-1 for everything in _build_foliage), so at the previous
	# lod_distance=500 no instances existed at all beyond 500m -- which is why earlier shots showed
	# bare terrain. 1500 keeps trees/bushes visible across the useful part of the frame while still
	# streaming in reasonable time (LOD0-everywhere at 4000 needed ~300k data blocks).
	# Raise the items' lod_index instead if you want foliage visible further out more cheaply.
	terrain.lod_distance = 1500.0
	terrain.lod_count = 5
	terrain.generate_collisions = false
	land_material = ShaderMaterial.new()
	land_material.shader = load("res://shaders/planet_land.gdshader")
	land_material.set_shader_parameter("u_planet_radius", PLANET_RADIUS)
	land_material.set_shader_parameter("detail_strength", 0.0)
	terrain.material = land_material
	add_child(terrain)


func _make_scene(node: Node3D) -> PackedScene:
	var packed := PackedScene.new()
	packed.pack(node)
	node.free()
	return packed


# emit_mode EMIT_FROM_FACES_FAST, random_rotation and scale jitter are sane defaults for every
# biome layer here; only density/slope/material/clustering differ per call site.
#
# cluster_scale_m is the rough diameter (in meters) of one noise clump -- e.g. one stand of
# trees or one patch of grass -- converted to a FastNoiseLite frequency internally. Each call
# gets its own noise seed (via _noise_seed_counter) so species don't all cluster in the exact
# same spots. Pass cluster_scale_m <= 0.0 to skip clustering entirely (uniform density within
# the material/slope band) -- used for boulders, which shouldn't clump like living things do.
func _mat_generator(mat: int, density: float, min_slope: float, max_slope: float, cluster_scale_m: float = 80.0) -> VoxelInstanceGenerator:
	var g := VoxelInstanceGenerator.new()
	g.density = density
	g.emit_mode = VoxelInstanceGenerator.EMIT_FROM_FACES_FAST
	g.min_slope_degrees = min_slope
	g.max_slope_degrees = max_slope
	g.voxel_texture_filter_enabled = true
	g.voxel_texture_filter_array = PackedInt32Array([mat])
	g.random_rotation = true
	g.min_scale = 0.8
	g.max_scale = 1.3

	if cluster_scale_m > 0.0:
		_noise_seed_counter += 1
		var noise := FastNoiseLite.new()
		noise.seed = PLANET_SEED * 1000 + _noise_seed_counter
		noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
		noise.frequency = 1.0 / cluster_scale_m
		g.noise = noise
		g.noise_dimension = VoxelInstanceGenerator.DIMENSION_3D
		# threshold=0 keeps roughly half the noise range as "in a clump"; falloff softens the
		# clump edge instead of a hard on/off boundary between dense patch and empty ground.
		g.noise_threshold = 0.0
		g.noise_falloff = 0.4

	return g


func _scene_item(scene: PackedScene, generator: VoxelInstanceGenerator, lod_index: int) -> VoxelInstanceLibrarySceneItem:
	var item := VoxelInstanceLibrarySceneItem.new()
	item.scene = scene
	item.generator = generator
	item.lod_index = lod_index
	return item


func _build_foliage() -> void:
	var lib := VoxelInstanceLibrary.new()
	var id := 0

	# Forest biome -> oak/birch on dirt (EdenPlanetGeneratorV1::_land_material_for: FOREST->MAT_DIRT).
	for tree_type in [EdenTreeShape.TREE_OAK, EdenTreeShape.TREE_BIRCH]:
		var tree := EdenTreeInstance.new()
		tree.tree_type = tree_type + 1 # 0 means "random" on this node
		tree.season = EdenTreeShape.SEASON_SUMMER
		var gen_item := _mat_generator(MAT_DIRT, 0.15, 0.0, 35.0)
		lib.add_item(id, _scene_item(_make_scene(tree), gen_item, 1))
		id += 1

	# Grassland/hills -> sparse oak + tall-grass ground cover (both map to MAT_GRASS).
	var grass_tree := EdenTreeInstance.new()
	grass_tree.tree_type = EdenTreeShape.TREE_OAK + 1
	grass_tree.season = EdenTreeShape.SEASON_SUMMER
	lib.add_item(id, _scene_item(_make_scene(grass_tree), _mat_generator(MAT_GRASS, 0.03, 0.0, 30.0), 1))
	id += 1

	var tall_grass := EdenBushInstance.new()
	tall_grass.bush_type = EdenBushInstance.BUSH_TALL_GRASS
	lib.add_item(id, _scene_item(_make_scene(tall_grass), _mat_generator(MAT_GRASS, 0.4, 0.0, 40.0, 25.0), 0))
	id += 1

	# Desert -> cacti on sand.
	var cactus := EdenBushInstance.new()
	cactus.bush_type = EdenBushInstance.BUSH_CACTUS
	lib.add_item(id, _scene_item(_make_scene(cactus), _mat_generator(MAT_SAND, 0.05, 0.0, 25.0), 0))
	id += 1

	# Tropical -> palms on moss.
	var palm := EdenTreeInstance.new()
	palm.tree_type = EdenTreeShape.TREE_PALM + 1
	palm.season = EdenTreeShape.SEASON_SUMMER
	lib.add_item(id, _scene_item(_make_scene(palm), _mat_generator(MAT_MOSS, 0.1, 0.0, 30.0), 1))
	id += 1

	# Tundra -> sparse winter pine on snow.
	var pine := EdenTreeInstance.new()
	pine.tree_type = EdenTreeShape.TREE_PINE + 1
	pine.season = EdenTreeShape.SEASON_WINTER
	lib.add_item(id, _scene_item(_make_scene(pine), _mat_generator(MAT_SNOW, 0.02, 0.0, 25.0), 1))
	id += 1

	# Boulders on bare rock/cliffs, independent of biome.
	var boulder := EdenRockInstance.new()
	boulder.rock_type = EdenRockInstance.ROCK_BOULDER
	lib.add_item(id, _scene_item(_make_scene(boulder), _mat_generator(MAT_ROCK, 0.08, 20.0, 80.0, 0.0), 1))
	id += 1

	var instancer := VoxelInstancer.new()
	instancer.up_mode = VoxelInstancer.UP_MODE_SPHERE
	instancer.library = lib
	terrain.add_child(instancer)


var light: DirectionalLight3D


func _build_lighting() -> void:
	# Exposure matched to eden_materials_showcase.gd, which renders this same generator+shader
	# correctly. The previous values here (light_energy 3.0, ambient 1.2, and no tonemap_mode so
	# Godot defaulted to TONE_MAPPER_LINEAR) blew the terrain out to near-white: linear tonemapping
	# clips hard at 1.0, and with flat MIXEL4 albedos around 0.2-0.9 the direct term alone already
	# reached ~1.8 before ambient added another ~0.66 on top. ACES rolls the highlights off
	# instead of clipping, so material colors stay distinguishable at full sun.
	# 1.0 (not the reference scene's 1.6): planet_land.gdshader's albedos were rebalanced into a
	# physical range, so full sun should land near albedo itself rather than multiply past 1.0.
	light = DirectionalLight3D.new()
	light.light_energy = 1.0
	add_child(light)

	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.4, 0.55, 0.75)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.55, 0.65)
	env.ambient_light_energy = 0.25
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env_node.environment = env
	add_child(env_node)


func _bisect_surface_radius(dir: Vector3) -> float:
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


func _position_camera(shot: Dictionary) -> void:
	land_material.set_shader_parameter("u_debug_mode", shot.get("debug_mode", 0))
	print("EDEN_FOLIAGE_DEMO: u_debug_mode set to ", shot.get("debug_mode", 0),
			" (readback=", land_material.get_shader_parameter("u_debug_mode"), ")")
	var up_dir: Vector3 = shot.dir
	var surface_r := _bisect_surface_radius(up_dir)
	var surface_point := up_dir * surface_r

	# Sun direction relative to THIS location's local up, not a fixed world rotation (matches
	# eden_materials_showcase.gd/eden_stress_test.gd) -- a static world-space light rotation put
	# the sun at a near-grazing angle relative to the ground at some sphere locations, which
	# mathematically amplifies otherwise-invisible per-triangle normal noise into a visible
	# checkerboard via how steep cos(angle) is near 90 degrees. Confirmed via elimination: this
	# was the one remaining variable after material index/weights/slope/specular all tested clean.
	var sun_from := (up_dir * 0.65 + Vector3(0.3, 0.85, 0.25)).normalized()
	var light_ref := Vector3.UP if absf(sun_from.y) < 0.99 else Vector3.RIGHT
	light.look_at(-sun_from, light_ref)

	if camera == null:
		camera = Camera3D.new()
		camera.far = 6000.0
		camera.current = true
		add_child(camera)
		var viewer := VoxelViewer.new()
		viewer.view_distance = 900.0 # matches eden_materials_showcase.gd, proven to stream fast enough
		camera.add_child(viewer)

	camera.position = surface_point + up_dir * shot.altitude
	var reference := Vector3(0, 1, 0) if absf(up_dir.dot(Vector3(0, 1, 0))) < 0.99 else Vector3(1, 0, 0)
	var fwd := up_dir.cross(reference).normalized()
	# Aim at a point ON the surface ahead (not still up at camera altitude) so the camera
	# actually looks down-and-forward at terrain instead of out toward the horizon/sky.
	camera.look_at(surface_point + fwd * shot.altitude * 2.0, up_dir)
	print("EDEN_FOLIAGE_DEMO: camera moved to dir=", up_dir, " surface_r=", surface_r, " altitude=", shot.altitude)


func _process(_delta: float) -> void:
	if _shot_index >= _shots.size():
		return
	var frame := Engine.get_process_frames()
	if frame % 60 == 0:
		print("EDEN_FOLIAGE_DEMO: frame=", frame, " mesh_blocks=", terrain.debug_get_mesh_block_count(),
				" data_blocks=", terrain.debug_get_data_block_count())
	var shot: Dictionary = _shots[_shot_index]
	if frame < shot.frame:
		return

	var img := get_viewport().get_texture().get_image()
	img.save_png(shot.name)
	print("EDEN_FOLIAGE_DEMO: saved ", shot.name)

	_shot_index += 1
	if _shot_index < _shots.size():
		_position_camera(_shots[_shot_index])
	else:
		get_tree().quit()
