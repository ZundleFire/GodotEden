@tool
class_name EdenFoliage
extends VoxelInstancer
## Low-poly GPU foliage for a VoxelLodTerrain planet (EdenPlanetGeneratorV4), all MultiMesh. What spawns where is
## data: an EdenFoliageConfig of biomes (climate / slope / material regions), each with layers (trees, bushes, grass,
## rocks, wood) and their density, size range and placement. Edit it in the inspector; the foliage rebuilds.
## VoxelLodTerrain allows only ONE VoxelInstancer, so every layer is an item in this node's library.
##
## Biomes read the terrain's surface data (mesher surface_data_enabled) through the VoxelInstanceGenerator climate
## filter. The library is generated, never saved: it is detached around editor saves.
##
## ponytail: GDScript prototype. Port to an eden_foliage C++ node once settled.

## Biomes and their layers. Empty: the built-in default (EdenFoliageConfig.make_default()).
@export var config: EdenFoliageConfig:
	set(v):
		config = v
		_rebuild()

## Spawn foliage in the editor viewport too (it follows the editor camera).
@export var show_in_editor := true:
	set(v):
		show_in_editor = v
		_rebuild()
## Multiplies every layer's density (quick global thinning for slower machines).
@export_range(0.0, 4.0, 0.01) var density_scale := 1.0:
	set(v):
		density_scale = v
		_rebuild()
## Colliders (trunks, logs, rocks) exist only within this camera distance (m). None in the editor.
@export var collision_distance := 64.0:
	set(v):
		collision_distance = v
		_rebuild()

@export_group("Grass")
## Multiplies the density of every grass layer (on top of density_scale): the main grass cost on slow GPUs
@export_range(0.0, 4.0, 0.01) var grass_density_scale := 1.0:
	set(v):
		grass_density_scale = v
		_rebuild()
@export var blades_per_tuft := 7:
	set(v):
		blades_per_tuft = v
		_rebuild()
## Chunks of grass past this camera distance (m) draw a lighter tuft of far_blades_per_tuft blades (0 = off)
@export var grass_lod_distance := 25.0:
	set(v):
		grass_lod_distance = v
		_rebuild()
@export_range(1, 12) var far_blades_per_tuft := 4:
	set(v):
		far_blades_per_tuft = v
		_rebuild()
## Grass shrinks into the ground between these camera distances (m); whole chunks past the end stop drawing.
@export var grass_fade_start := 45.0:
	set(v):
		grass_fade_start = v
		_update_materials()
@export var grass_fade_end := 60.0:
	set(v):
		grass_fade_end = v
		_rebuild()
## Grass wind stops between these camera distances (m).
@export var grass_wind_fade_start := 20.0:
	set(v):
		grass_wind_fade_start = v
		_update_materials()
@export var grass_wind_fade_end := 35.0:
	set(v):
		grass_wind_fade_end = v
		_update_materials()

@export_group("Spacing")
## Keep foliage from overlapping: trees, logs, rocks and cacti stand only at the sites of a shared grid, each site
## taken by at most one of them; bushes and ferns use a finer grid and keep clear of the big sites. Per item, not
## per chunk, so it holds across detail levels. (VoxelInstanceGenerator EMIT_FROM_SITES)
@export var spacing_enabled := true:
	set(v):
		spacing_enabled = v
		_rebuild()
## Grid for trees, logs, rocks, cacti (m): two of them are never closer than about a third of this
@export_range(1.0, 20.0, 0.1) var big_spacing := 4.5:
	set(v):
		big_spacing = v
		_rebuild()
## Grid for bushes, ferns, berry bushes (m)
@export_range(0.5, 10.0, 0.1) var medium_spacing := 1.8:
	set(v):
		medium_spacing = v
		_rebuild()
## How far bushes and ferns keep from trunks and rocks (m)
@export_range(0.0, 10.0, 0.1) var medium_clearance := 1.4:
	set(v):
		medium_clearance = v
		_rebuild()

@export_group("Far Foliage")
## Trees and big rocks stay visible far away as voxelized copies on coarser terrain chunks (per layer: Far LOD).
@export var far_foliage_enabled := true:
	set(v):
		far_foliage_enabled = v
		_rebuild()
## Multiplies the voxel resolution of the far copies (and of near trees' outer LOD): fewer triangles per far tree
@export_range(0.3, 2.0, 0.05) var far_resolution_scale := 1.0:
	set(v):
		far_resolution_scale = v
		_rebuild()
## Multiplies every layer's far distance.
@export_range(0.1, 4.0, 0.01) var far_distance_scale := 1.0:
	set(v):
		far_distance_scale = v
		_rebuild()
## Visibility cap: things are drawn out to this many metres per metre of their size (400 = about 2 px tall at
## 1080p), so a 2 m rock stops at 800 m while a 20 m tree reaches 8 km.
@export_range(50.0, 2000.0, 1.0) var far_visibility := 400.0:
	set(v):
		far_visibility = v
		_rebuild()

@export_group("Trees & Bushes")
## Trees and big bushes use their full mesh within this camera distance (m), a simplified one (about a fifth of the
## triangles) out to detail_distance x mid_detail_scale, and the voxelized far copy beyond. Per terrain chunk, so
## the switch happens a chunk at a time. 0 = full detail throughout.
@export_range(0.0, 1000.0, 1.0) var detail_distance := 60.0:
	set(v):
		detail_distance = v
		_rebuild()
@export_range(1.5, 8.0, 0.1) var mid_detail_scale := 3.0:
	set(v):
		mid_detail_scale = v
		_rebuild()
## Multiplies the size range of every tree layer in every biome (per-layer ranges are in the config).
@export_range(0.1, 5.0, 0.01, "or_greater") var tree_size_scale := 1.0:
	set(v):
		tree_size_scale = v
		_rebuild()
@export var tree_wind_fade_start := 60.0:
	set(v):
		tree_wind_fade_start = v
		_update_materials()
@export var tree_wind_fade_end := 90.0:
	set(v):
		tree_wind_fade_end = v
		_update_materials()

const MAX_INSTANCER_LOD := 7 # VoxelInstancer::MAX_LOD - 1
## Spacing groups (see Spacing): what shares the big grid, and the medium one; the rest scatter freely
const SPACED_BIG := [EdenFoliageLayer.Kind.OAK, EdenFoliageLayer.Kind.BIRCH, EdenFoliageLayer.Kind.WILLOW,
		EdenFoliageLayer.Kind.FRUIT_TREE, EdenFoliageLayer.Kind.PINE, EdenFoliageLayer.Kind.PALM, EdenFoliageLayer.Kind.DEAD_TREE,
		EdenFoliageLayer.Kind.LOG, EdenFoliageLayer.Kind.BOULDER, EdenFoliageLayer.Kind.ROCK_SLAB, EdenFoliageLayer.Kind.ROCK_SPIRE,
		EdenFoliageLayer.Kind.CACTUS]
const SPACED_MEDIUM := [EdenFoliageLayer.Kind.SHRUB, EdenFoliageLayer.Kind.FERN, EdenFoliageLayer.Kind.BERRY_BUSH,
		EdenFoliageLayer.Kind.DEAD_BUSH, EdenFoliageLayer.Kind.TALL_GRASS]
const SITE_SEEDS := {"big": 4501, "medium": 1802}
## Plants whose leaves follow the seasons (EdenCalendar); the rest (conifers, palms, cacti, dead wood, rocks) don't
const DECIDUOUS := [EdenFoliageLayer.Kind.OAK, EdenFoliageLayer.Kind.BIRCH, EdenFoliageLayer.Kind.WILLOW,
		EdenFoliageLayer.Kind.FRUIT_TREE, EdenFoliageLayer.Kind.SHRUB, EdenFoliageLayer.Kind.FERN,
		EdenFoliageLayer.Kind.BERRY_BUSH, EdenFoliageLayer.Kind.TALL_GRASS]
const MATERIAL_BITS := 6 # EdenFoliageLayer/Biome material flags: grass, rock, snow, sand, dirt, moss (MIXEL4 ids)

var _grass_materials: Array[ShaderMaterial] = []
var _ring_materials := {} # [sways, inner, outer] -> ShaderMaterial
var _signature := 0
var _forest_mask: FastNoiseLite
var _next_check_ms := 0
var _batching := false
## Library item id -> EdenFoliageLayer.Kind of the layer it places (so a collider hit can tell a tree from a rock)
var item_kinds := {}
# Libraries just replaced, kept a moment: the renderer still draws with their materials for a frame or two
var _retired: Array[VoxelInstanceLibrary] = []


func _ready() -> void:
	up_mode = VoxelInstancer.UP_MODE_SPHERE
	if library == null: # a script may have assigned one before adding the node
		_rebuild()


func _notification(what: int) -> void:
	# Keep the generated library (meshes, shapes) out of the saved scene
	if what == NOTIFICATION_EDITOR_PRE_SAVE:
		library = null
	elif what == NOTIFICATION_EDITOR_POST_SAVE:
		_rebuild()


# Resource properties don't signal edits made deep inside arrays of sub-resources, so the editor polls a cheap
# signature of the config and rebuilds when it changes
func _process(_delta: float) -> void:
	if not Engine.is_editor_hint() or Time.get_ticks_msec() < _next_check_ms:
		return
	_next_check_ms = Time.get_ticks_msec() + 500
	var s := _config_signature()
	if s != _signature:
		_rebuild()


func _config_signature() -> int:
	var values := []
	var cfg := _get_config()
	values.append([cfg.forest_patch_size, cfg.forest_coverage, cfg.forest_edge, cfg.forest_seed])
	for b in cfg.biomes:
		if b == null:
			continue
		values.append(_resource_values(b))
		for l in b.layers:
			if l != null:
				values.append(_resource_values(l))
	return hash(values)


func _resource_values(r: Resource) -> Array:
	var out := []
	for p in r.get_property_list():
		if p.usage & PROPERTY_USAGE_SCRIPT_VARIABLE and p.name != "layers":
			out.append(r.get(p.name))
	return out


func _get_config() -> EdenFoliageConfig:
	if config == null:
		config = EdenFoliageConfig.make_default()
	return config


## Sets several properties (name -> value) and rebuilds once (EdenGraphics presets)
func apply_quality(values: Dictionary) -> void:
	var dirty := false
	_batching = true
	for k in values:
		if get(k) != values[k]:
			set(k, values[k])
			dirty = true
	_batching = false
	if dirty:
		_rebuild()


func _rebuild() -> void:
	if _batching or not is_inside_tree():
		return # setters fire while the scene loads; _ready builds once
	_signature = _config_signature()
	if Engine.is_editor_hint() and not show_in_editor:
		# Empty, not null: a VoxelInstancer without a library logs an assertion error every frame
		library = VoxelInstanceLibrary.new()
		return
	var old := library
	library = build_library()
	if old and is_inside_tree():
		_retired.append(old)
		get_tree().create_timer(1.0).timeout.connect(func(): _retired.erase(old))


func build_library() -> VoxelInstanceLibrary:
	var lib := VoxelInstanceLibrary.new()
	item_kinds.clear()
	_grass_materials.clear()
	_ring_materials.clear()
	var used := {}
	var sites := _site_ranges() if spacing_enabled else {}
	for biome in _get_config().biomes:
		if biome == null or not biome.enabled:
			continue
		for layer in biome.layers:
			if layer == null or not layer.enabled:
				continue
			var temperature := _intersect(biome.temperature, layer.temperature)
			var moisture := _intersect(biome.moisture, layer.moisture)
			var slope := _intersect(biome.slope, layer.slope)
			if temperature.x > temperature.y or moisture.x > moisture.y or slope.x > slope.y:
				continue # the layer's limits exclude the whole biome
			var materials := _material_ids(layer.materials if layer.materials != 0 else biome.materials)
			var density := layer.density * biome.density_scale * density_scale * (grass_density_scale if layer.is_grass() else 1.0)
			var size := Vector2(layer.scale_min, layer.scale_max) * biome.size_scale
			if layer.is_tree():
				size *= tree_size_scale
			var variants: int = 1 if layer.is_grass() else layer.variants
			var base := _alloc_ids(used, "%s/%s" % [biome.name, layer.name])
			var far := far_foliage_enabled and layer.has_far(size.y) and get_parent() is VoxelLodTerrain
			for v in variants:
				var g := _gen(density / variants, temperature, moisture, materials, slope)
				g.min_scale = size.x
				g.max_scale = size.y
				g.vertical_alignment = layer.vertical_alignment
				# Sink is in metres at scale 1: bigger instances sink proportionally deeper
				g.offset_along_normal = layer.sink * (size.x + size.y) * 0.5
				if layer.placement == EdenFoliageLayer.Placement.FOREST:
					_add_forest_mask(g)
				elif layer.clump_size > 0.0:
					_add_clumping(g, base, layer.clump_size)
				var site_key := "%s/%s" % [biome.name, layer.name]
				if sites.has(site_key):
					_use_sites(g, sites, sites[site_key], v, variants)
				var item_name := "%s/%s_%d" % [biome.name, layer.name, v]
				var item: VoxelInstanceLibraryMultiMeshItem
				if layer.is_grass():
					item = _grass_item(item_name, g, layer)
				else:
					var built: Dictionary = EdenFoliageMeshes.build(layer, v)
					# With far tiers, the near item ends where the first tier starts (its own LOD's range)
					var dd: float = layer.draw_distance if (layer.draw_distance > 0.0 or not far) else _lod_range(layer.lod)
					item = _item(item_name, built.mesh, g, layer.lod, built.shape if layer.collision else [], dd)
					if not layer.casts_shadow(size.y):
						item.cast_shadow = RenderingServer.SHADOW_CASTING_SETTING_OFF
					var deciduous: bool = layer.kind in DECIDUOUS
					if far: # exact per-instance cut where the far tiers take over (chunks only cut coarsely)
						item.material_override = _ring_material(layer.sways(), 0.0, dd, deciduous)
					elif layer.sways() and not deciduous:
						item.material_override = _ring_material(true, 0.0, 1e9, false)
					_add_detail_lods(item, layer, v, dd if dd > 0.0 else _lod_range(layer.lod) * 2.0)
				lib.add_item(base + v, item)
				item_kinds[base + v] = layer.kind
			if far:
				_add_far_tiers(lib, used, biome, layer, density, size, temperature, moisture, materials, slope)
	_update_materials()
	return lib


# Far tiers: the layer again on each coarser terrain LOD, each shown only in its own ring (from the previous LOD's
# range to its own), so big things stay visible far out. Rings grow 4x in area per LOD, so every tier thins out
# (and grows a little to keep coverage). Detail steps down with distance (see EdenFoliageLayer.far_detail_distance). Positions come
# from each LOD's own chunks, so a far tree isn't the same tree as up close.
func _add_far_tiers(lib: VoxelInstanceLibrary, used: Dictionary, biome: EdenFoliageBiome, layer: EdenFoliageLayer,
		density: float, size: Vector2, temperature: Vector2, moisture: Vector2, materials: Array, slope: Vector2) -> void:
	var mesh0: Mesh = EdenFoliageMeshes.build(layer, 0).mesh
	var extent := mesh0.get_aabb().size
	var metres := maxf(extent.x, maxf(extent.y, extent.z)) * size.y
	var max_distance := minf(layer.far_distance * far_distance_scale, metres * far_visibility)
	for k in range(layer.lod + 1, MAX_INSTANCER_LOD + 1):
		var inner := _lod_range(k - 1)
		if inner >= max_distance:
			break
		var outer := minf(_lod_range(k), max_distance)
		var i := k - layer.lod - 1
		var d := density * pow(layer.far_density_falloff, i)
		var s := size * pow(layer.far_scale_growth, i)
		# Detail steps down per ring: a finer voxel copy inside far_detail_distance, the normal
		# one beyond, a coarser one past 4x that. One variant per tier: they're indistinguishable that far out.
		var detail := layer.far_detail_distance
		var res := layer.far_resolution * 2 if inner < detail else \
				(layer.far_resolution if inner < detail * 4.0 else maxi(layer.far_resolution - 2, 2))
		res = maxi(roundi(res * far_resolution_scale), 2)
		var variants := 1
		var base := _alloc_ids(used, "%s/%s/far%d" % [biome.name, layer.name, k])
		for v in variants:
			var g := _gen(d / variants, temperature, moisture, materials, slope)
			g.min_scale = s.x
			g.max_scale = s.y
			g.vertical_alignment = layer.vertical_alignment
			g.offset_along_normal = layer.sink * (s.x + s.y) * 0.5
			if layer.placement == EdenFoliageLayer.Placement.FOREST:
				_add_forest_mask(g)
			elif layer.clump_size > 0.0:
				_add_clumping(g, base, layer.clump_size)
			var mesh := EdenFoliageMeshes.build_far(layer, v, res)
			var item := VoxelInstanceLibraryMultiMeshItem.new()
			item.name = "%s/%s_far%d_%d" % [biome.name, layer.name, k, v]
			item.lod_index = k
			item.generator = g
			item.persistent = false
			item.cast_shadow = RenderingServer.SHADOW_CASTING_SETTING_OFF
			# Mesh LODs by chunk distance, as ratios of this LOD's range: nothing inside the ring (finer tiers draw
			# there), the mesh within it, hidden beyond
			item.mesh = EdenFoliageMeshes.empty_mesh()
			item.set_mesh(mesh, 1)
			item.hide_beyond_max_lod = true
			# Chunks are measured from their centre, so widen by half a chunk diagonal; the shader cuts exactly
			var r := _lod_range(k)
			var margin := (get_parent() as VoxelLodTerrain).mesh_block_size * (1 << k) * 0.87
			item.set("_mesh_lod_distance_ratios", PackedFloat32Array([maxf(inner - margin, 0.0) / r,
					(outer + margin) / r, 2.0, 2.0]))
			item.material_override = _ring_material(false, inner, outer, layer.kind in DECIDUOUS)
			lib.add_item(base + v, item)


# Which slice of each spacing grid's owner values every spaced layer gets ("biome/layer" -> [group, start,
# width]): width = its density x the grid's cell area, so its density is kept. Biomes that apply anywhere (no
# climate limits: rocks, cliffs) come first; the regional biomes follow, each from the same point, since their
# climates mostly exclude each other (a biome that would pass 1 is scaled down to fit). "_exclude_big" holds the
# end of everything the big grid can hold, which medium sites keep clear of.
func _site_ranges() -> Dictionary:
	var out := {}
	var cfg := _get_config()
	var grid := {"big": big_spacing, "medium": medium_spacing}
	var cursor := {"big": 0.0, "medium": 0.0}
	var ends := {"big": 0.0, "medium": 0.0}
	for pass_global in [true, false]:
		var base := cursor.duplicate()
		for b in cfg.biomes:
			if b == null or not b.enabled:
				continue
			var is_global: bool = b.temperature == Vector2(0.0, 1.0) and b.moisture == Vector2(0.0, 1.0)
			if is_global != pass_global:
				continue
			var at := cursor.duplicate() if pass_global else base.duplicate()
			var entries := []
			for l in b.layers:
				if l == null or not l.enabled:
					continue
				var group := "big" if l.kind in SPACED_BIG else ("medium" if l.kind in SPACED_MEDIUM else "")
				if group == "":
					continue
				# (medium sites lose some ground to the big sites' clearance: ask for a little more)
				var width: float = l.density * b.density_scale * density_scale * grid[group] * grid[group] * (1.3 if group == "medium" else 1.0)
				entries.append([l.name, group, width])
			for group in ["big", "medium"]:
				var total := 0.0
				for e in entries:
					if e[1] == group:
						total += e[2]
				var room: float = 1.0 - at[group]
				var fit := minf(1.0, room / total) if total > 0.0 else 1.0
				for e in entries:
					if e[1] == group:
						out["%s/%s" % [b.name, e[0]]] = [group, at[group], e[2] * fit]
						at[group] += e[2] * fit
				ends[group] = maxf(ends[group], at[group])
			if pass_global:
				cursor = at
	out["_exclude_big"] = ends.big
	return out


func _use_sites(g: VoxelInstanceGenerator, sites: Dictionary, entry: Array, variant: int, variants: int) -> void:
	var group: String = entry[0]
	var width: float = entry[2] / variants
	var start: float = entry[1] + width * variant
	var terrain := get_parent() as VoxelLodTerrain
	var gen: Object = terrain.generator if terrain else null
	g.emit_mode = VoxelInstanceGenerator.EMIT_FROM_SITES
	g.site_spacing = big_spacing if group == "big" else medium_spacing
	g.site_seed = SITE_SEEDS[group]
	g.site_owner_range = Vector2(start, start + width)
	g.site_jitter = 0.35
	g.site_planet_radius = float(gen.get("planet_radius")) if gen and gen.get("planet_radius") != null else 0.0
	if group == "medium" and medium_clearance > 0.0:
		g.site_exclusion_spacing = big_spacing
		g.site_exclusion_seed = SITE_SEEDS.big
		g.site_exclusion_range = Vector2(0.0, sites["_exclude_big"])
		g.site_exclusion_radius = medium_clearance


# Ids seed placement: stable per name, so edits elsewhere don't reshuffle this layer. 8 slots (variants) per name.
func _alloc_ids(used: Dictionary, key: String) -> int:
	var base := (hash(key) & 0x7fffffff) % 8000 * 8
	while used.has(base):
		base = (base + 8) % 64000
	used[base] = true
	return base


# Distance where terrain LOD `lod` ends (VoxelLodTerrain::get_lod_distances): the instancer measures its mesh LOD
# ratios against it. Legacy octree doubles per LOD; clipbox adds the secondary distance.
func _lod_range(lod: int) -> float:
	var terrain := get_parent() as VoxelLodTerrain
	if terrain == null:
		return 128.0 * (1 << lod)
	if terrain.streaming_system != 0:
		return terrain.lod_distance + (terrain.secondary_lod_distance * (1 << lod) if lod > 0 else 0.0)
	return terrain.lod_distance * (1 << lod)


func _intersect(a: Vector2, b: Vector2) -> Vector2:
	return Vector2(maxf(a.x, b.x), minf(a.y, b.y))


func _material_ids(bits: int) -> Array:
	var ids := []
	for i in MATERIAL_BITS:
		if bits & (1 << i):
			ids.append(i)
	return ids


func _gen(density: float, temperature: Vector2, moisture: Vector2, materials: Array,
		slope: Vector2) -> VoxelInstanceGenerator:
	var g := VoxelInstanceGenerator.new()
	g.density = density
	# Area-based (per m²). FACES_FAST is per triangle, and mesh optimization makes flat ground few big triangles
	g.emit_mode = VoxelInstanceGenerator.EMIT_FROM_FACES
	g.random_rotation = true
	g.min_slope_degrees = slope.x
	g.max_slope_degrees = slope.y
	g.min_slope_falloff_degrees = 3.0 if slope.x > 0.0 else 0.0
	g.max_slope_falloff_degrees = 5.0 if slope.y < 90.0 else 0.0
	# Empty = any material. Cliffs need it: V4 marks rock by altitude, steep faces are rock only in the shader
	g.voxel_texture_filter_enabled = not materials.is_empty()
	g.voxel_texture_filter_array = PackedInt32Array(materials)
	g.surface_filter_enabled = true
	g.temperature_range = temperature
	g.moisture_range = moisture
	return g


# Forest clumps: a noise field thins instances between patches instead of a uniform sprinkle
# Forest layers share one mask (same seed and scale), so the trees, bushes and ferns of a forest line up
func _forest_noise() -> FastNoiseLite:
	var cfg := _get_config()
	if _forest_mask == null:
		_forest_mask = FastNoiseLite.new()
	_forest_mask.seed = 9000 + cfg.forest_seed
	_forest_mask.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	_forest_mask.frequency = 1.0 / maxf(cfg.forest_patch_size, 1.0)
	return _forest_mask


func _forest_threshold() -> float:
	return (_get_config().forest_coverage - 0.5) * 0.8


func _add_forest_mask(g: VoxelInstanceGenerator) -> void:
	g.noise = _forest_noise()
	g.noise_dimension = VoxelInstanceGenerator.DIMENSION_3D
	g.noise_threshold = _forest_threshold()
	g.noise_falloff = _get_config().forest_edge


## How much forest grows at a world position (0..1): the instancer's own keep probability for forest layers
## there (same noise, threshold and falloff), zero where no biome with forest layers fits the climate or under
## the sea. EdenAmbience places forest sounds and falling leaves with it.
func get_forest_density(world_position: Vector3) -> float:
	var terrain := get_parent() as VoxelLodTerrain
	if terrain == null:
		return 0.0
	if _forest_mask == null:
		_forest_noise()
	var local := world_position - (terrain.global_position if terrain.is_inside_tree() else terrain.position)
	var n := _forest_mask.get_noise_3dv(local) + _forest_threshold()
	var keep := clampf(n / maxf(_get_config().forest_edge, 1e-3), 0.0, 1.0)
	keep *= keep
	if keep <= 0.0 or not terrain.generator or not terrain.generator.has_method("sample_surface"):
		return 0.0
	var s: Dictionary = terrain.generator.sample_surface(local.normalized())
	if float(s.get("height", 0.0)) <= 0.0 or not _get_config().forest_climate(float(s.temperature), float(s.moisture)):
		return 0.0
	return keep


func _add_clumping(g: VoxelInstanceGenerator, noise_seed: int, patch_size: float) -> void:
	var noise := FastNoiseLite.new()
	noise.seed = 4000 + noise_seed # shared by a layer's variants
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.frequency = 1.0 / maxf(patch_size, 1.0)
	g.noise = noise
	g.noise_dimension = VoxelInstanceGenerator.DIMENSION_3D
	g.noise_threshold = -0.1
	g.noise_falloff = 0.4


func _item(item_name: String, mesh: Mesh, gen: VoxelInstanceGenerator, lod: int,
		shapes: Array = [], draw_distance := 0.0) -> VoxelInstanceLibraryMultiMeshItem:
	var item := VoxelInstanceLibraryMultiMeshItem.new()
	item.name = item_name
	item.lod_index = lod
	item.generator = gen
	item.persistent = false
	item.mesh = mesh
	if not shapes.is_empty() and not Engine.is_editor_hint(): # no physics in the editor: bodies only cost time
		item.collision_shapes = shapes
		item.collision_distance = collision_distance
	if draw_distance > 0.0:
		_limit_draw_distance(item, draw_distance)
	return item


# Mesh LODs by chunk distance for heavy meshes (trees, big bushes): full within detail_distance, simplified to
# detail_distance x mid_detail_scale, voxelized beyond, hidden past `end` (the item's draw distance)
func _add_detail_lods(item: VoxelInstanceLibraryMultiMeshItem, layer: EdenFoliageLayer, variant: int, end: float) -> void:
	var terrain := get_parent() as VoxelLodTerrain
	if terrain == null or detail_distance <= 0.0 or detail_distance >= end or _tris(item.mesh) <= 400:
		return
	# Half a chunk diagonal of slack: chunks are measured from their centre
	var margin := terrain.mesh_block_size * (1 << item.lod_index) * 0.45
	var r := _lod_range(item.lod_index)
	var mid := detail_distance * mid_detail_scale
	item.set_mesh(EdenFoliageMeshes.build_simplified(layer, variant, 0.2), 1)
	var ratios := PackedFloat32Array([(detail_distance + margin) / r])
	if mid < end:
		item.set_mesh(EdenFoliageMeshes.build_far(layer, variant, maxi(roundi(8 * far_resolution_scale), 2)), 2)
		ratios.append((mid + margin) / r)
	ratios.append(minf((end + margin) / r, 2.0))
	while ratios.size() < 4:
		ratios.append(2.0)
	item.hide_beyond_max_lod = true
	item.set("_mesh_lod_distance_ratios", ratios)


static func _tris(m: Mesh) -> int:
	var n := 0
	for s in m.get_surface_count():
		var idx = m.surface_get_arrays(s)[Mesh.ARRAY_INDEX]
		n += (idx.size() if idx is PackedInt32Array else 0) / 3
	return n


# Stops drawing whole chunks whose centre is beyond `meters` (plus half a chunk diagonal, so chunks straddling the
# limit still draw). The instancer measures it as a ratio of the terrain's distance for the item's LOD.
func _limit_draw_distance(item: VoxelInstanceLibraryMultiMeshItem, meters: float) -> void:
	var terrain := get_parent() as VoxelLodTerrain
	if terrain == null:
		return
	var lod := item.lod_index
	var lod_range := _lod_range(lod)
	var margin := terrain.mesh_block_size * (1 << lod) * 0.87
	item.hide_beyond_max_lod = true
	# Whole-array setter: the per-LOD one clamps LOD0 to the LOD1 default (0.35)
	var r := minf((meters + margin) / lod_range, 2.0)
	item.set("_mesh_lod_distance_ratios", PackedFloat32Array([r, 2.0, 2.0, 2.0]))


func _update_materials() -> void:
	for m in _grass_materials:
		m.set_shader_parameter("u_fade_start", grass_fade_start)
		m.set_shader_parameter("u_fade_end", grass_fade_end)
		m.set_shader_parameter("u_wind_fade_start", grass_wind_fade_start)
		m.set_shader_parameter("u_wind_fade_end", grass_wind_fade_end)
	for tm: ShaderMaterial in [EdenFoliageMeshes.get_material(true)] + _ring_materials.values():
		tm.set_shader_parameter("u_wind_fade_start", tree_wind_fade_start)
		tm.set_shader_parameter("u_wind_fade_end", tree_wind_fade_end)


# The plant shader drawing only instances between camera distances inner..outer; without sway for rocks and wood.
func _ring_material(sways: bool, inner: float, outer: float, deciduous := false) -> ShaderMaterial:
	var key := [sways, inner, outer, deciduous]
	if not _ring_materials.has(key):
		var m := ShaderMaterial.new()
		m.shader = (EdenFoliageMeshes.get_material(true) as ShaderMaterial).shader
		if not sways:
			m.set_shader_parameter("u_wind_strength", 0.0)
		m.set_shader_parameter("u_ring", Vector2(inner, outer))
		m.set_shader_parameter("u_deciduous", deciduous)
		_ring_materials[key] = m
	return _ring_materials[key]


func _grass_item(item_name: String, gen: VoxelInstanceGenerator,
		layer: EdenFoliageLayer) -> VoxelInstanceLibraryMultiMeshItem:
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/eden_grass_lowpoly.gdshader")
	var b := layer.grass_base_color
	var t := layer.grass_tip_color
	mat.set_shader_parameter("u_base_color", Vector3(b.r, b.g, b.b))
	mat.set_shader_parameter("u_tip_color", Vector3(t.r, t.g, t.b))
	_grass_materials.append(mat)
	# Chunks past the fade stop drawing (the shader already shrank their tufts to nothing)
	var item := _item(item_name, EdenFoliageMeshes.tuft(blades_per_tuft, mat), gen, 0, [], grass_fade_end)
	item.cast_shadow = RenderingServer.SHADOW_CASTING_SETTING_OFF
	# Mesh LOD per chunk: the full tuft near the camera, a lighter one further out (grass is vertex-bound: the
	# far chunks are most of the blades on screen)
	var terrain := get_parent() as VoxelLodTerrain
	if terrain and grass_lod_distance > 0.0 and grass_lod_distance < grass_fade_end and far_blades_per_tuft < blades_per_tuft:
		var margin := terrain.mesh_block_size * 0.87
		var r := _lod_range(0)
		item.set_mesh(EdenFoliageMeshes.tuft(far_blades_per_tuft, mat), 1)
		item.set("_mesh_lod_distance_ratios", PackedFloat32Array([(grass_lod_distance + margin) / r,
				minf((grass_fade_end + margin) / r, 2.0), 2.0, 2.0]))
	return item
