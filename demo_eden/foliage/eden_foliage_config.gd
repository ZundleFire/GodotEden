@tool
class_name EdenFoliageConfig
extends Resource
## Every biome EdenFoliage spawns. Save it as a .tres to share between scenes; edit biomes and layers in the
## inspector, EdenFoliage rebuilds on change.

@export var biomes: Array[EdenFoliageBiome] = []

@export_group("Forests")
## Forest layers (placement Forest) grow only inside these patches: one noise mask shared by all of them, and read
## by EdenAmbience (through EdenFoliage.get_forest_density) to place forest sounds and falling leaves.
## Typical size of a forest patch (m).
@export_range(50.0, 20000.0, 1.0) var forest_patch_size := 900.0
## Share of the land (where the climate allows forest) covered by forest.
@export_range(0.0, 1.0, 0.01) var forest_coverage := 0.45
## Width of the thinning edge: 0 = hard forest edge, higher = trees thin out gradually into the clearing.
@export_range(0.01, 1.0, 0.01) var forest_edge := 0.25
@export var forest_seed := 7


static func _layer(name: String, kind: EdenFoliageLayer.Kind, density: float, scale: Vector2,
		props := {}) -> EdenFoliageLayer:
	var l := EdenFoliageLayer.new()
	l.name = name
	l.kind = kind
	l.density = density
	l.scale_min = scale.x
	l.scale_max = scale.y
	if l.is_tree():
		l.lod = 1
		l.variants = 3
		l.collision = true
		l.clump_size = 150.0
		l.slope = Vector2(0.0, 30.0)
	elif l.is_grass():
		l.variants = 1
		l.slope = Vector2(0.0, 35.0)
		l.vertical_alignment = 0.6
	for k in props:
		l.set(k, props[k])
	return l


static func _biome(name: String, temperature: Vector2, moisture: Vector2, layers: Array, props := {}) -> EdenFoliageBiome:
	var b := EdenFoliageBiome.new()
	b.name = name
	b.temperature = temperature
	b.moisture = moisture
	b.layers.assign(layers)
	for k in props:
		b.set(k, props[k])
	return b


## Dense forest layers for a climate biome (placement Forest): close-set trees with bushes and ferns under them.
## Also appended to existing configs by foliage/_add_forest_layers.gd.
static func forest_layers(biome_name: String) -> Array:
	var K := EdenFoliageLayer.Kind
	const GRASS := 1
	const SNOW := 4
	const SAND := 8
	const DIRT := 16
	const MOSS := 32
	var veg := GRASS | DIRT | MOSS
	var f := {"placement": EdenFoliageLayer.Placement.FOREST}
	# Forest trees: full meshes only on LOD 0 chunks (~130 m), voxelized far copies beyond, thinning faster than
	# meadow trees -- a forest is ~5x the trees of open country, and at full detail to 256 m it cost 40 ms
	var tree := f.merged({"lod": 0, "far_detail_distance": 256.0, "far_density_falloff": 0.28})
	var under := f.merged({"variants": 1})
	match biome_name:
		"Temperate":
			return [
				_layer("Forest oak", K.OAK, 0.012, Vector2(2.2, 3.4), tree.merged({"materials": veg, "temperature": Vector2(0.42, 1.0)})),
				_layer("Forest birch", K.BIRCH, 0.011, Vector2(2.0, 3.2), tree.merged({"materials": veg, "temperature": Vector2(0.0, 0.6)})),
				_layer("Forest willow", K.WILLOW, 0.005, Vector2(2.0, 2.8), tree.merged({"materials": veg, "moisture": Vector2(0.7, 1.0)})),
				_layer("Undergrowth", K.SHRUB, 0.04, Vector2(0.7, 1.5), under.merged({"materials": veg, "draw_distance": 70.0, "variants": 2})),
				_layer("Forest ferns", K.FERN, 0.05, Vector2(0.7, 1.4), under.merged({"materials": GRASS | MOSS | DIRT, "draw_distance": 45.0})),
				_layer("Forest berries", K.BERRY_BUSH, 0.012, Vector2(0.7, 1.2), under.merged({"materials": veg, "draw_distance": 60.0})),
			]
		"Boreal":
			return [
				_layer("Forest pine", K.PINE, 0.022, Vector2(2.2, 3.8), tree.merged({"materials": veg | SNOW})),
				_layer("Undergrowth", K.SHRUB, 0.025, Vector2(0.6, 1.2), under.merged({"materials": veg, "draw_distance": 70.0})),
				_layer("Forest ferns", K.FERN, 0.03, Vector2(0.6, 1.2), under.merged({"materials": GRASS | MOSS | DIRT, "moisture": Vector2(0.45, 1.0), "draw_distance": 45.0})),
			]
		"Tropical":
			return [
				_layer("Jungle palm", K.PALM, 0.012, Vector2(2.0, 3.2), tree.merged({"materials": veg | SAND})),
				_layer("Jungle fruit tree", K.FRUIT_TREE, 0.01, Vector2(1.8, 2.8), tree.merged({"materials": veg})),
				_layer("Jungle undergrowth", K.SHRUB, 0.045, Vector2(0.8, 1.6), under.merged({"materials": veg, "draw_distance": 70.0, "variants": 2})),
				_layer("Jungle ferns", K.FERN, 0.06, Vector2(0.8, 1.6), under.merged({"materials": GRASS | MOSS | DIRT, "draw_distance": 45.0})),
			]
	return []


## Whether any enabled biome has forest layers, and the climate at a spot falls inside it.
func forest_climate(temperature: float, moisture: float) -> bool:
	for b in biomes:
		if b == null or not b.enabled or temperature < b.temperature.x or temperature > b.temperature.y \
				or moisture < b.moisture.x or moisture > b.moisture.y:
			continue
		for l in b.layers:
			if l != null and l.enabled and l.placement == EdenFoliageLayer.Placement.FOREST:
				return true
	return false


## The built-in setup: climate biomes plus Cliffs, Mountain Rock and Grassland Rocks regions.
static func make_default() -> EdenFoliageConfig:
	var K := EdenFoliageLayer.Kind
	const GRASS := 1
	const ROCK := 2
	const SNOW := 4
	const SAND := 8
	const DIRT := 16
	const MOSS := 32
	var veg := GRASS | DIRT | MOSS
	var c := EdenFoliageConfig.new()
	var small_rocks := {"materials": veg | SAND | ROCK, "slope": Vector2(0, 40), "sink": -0.15, "moss": 0.25,
			"lichen": 0.05, "draw_distance": 90.0}
	c.biomes.assign([
		_biome("Temperate", Vector2(0.35, 0.8), Vector2(0.35, 1.0), [
			_layer("Grass", K.GRASS, 8.0, Vector2(0.7, 1.4), {"materials": GRASS}),
			_layer("Oak", K.OAK, 0.0035, Vector2(2.2, 3.2), {"materials": veg, "temperature": Vector2(0.42, 1.0)}),
			_layer("Birch", K.BIRCH, 0.003, Vector2(2.0, 3.0), {"materials": veg, "temperature": Vector2(0.0, 0.6)}),
			_layer("Willow", K.WILLOW, 0.002, Vector2(2.0, 2.8), {"materials": veg, "moisture": Vector2(0.72, 1.0)}),
			_layer("Fruit tree", K.FRUIT_TREE, 0.0015, Vector2(1.6, 2.4), {"materials": veg, "temperature": Vector2(0.55, 1.0)}),
			_layer("Shrub", K.SHRUB, 0.02, Vector2(0.7, 1.3), {"materials": veg, "draw_distance": 110.0}),
			_layer("Fern", K.FERN, 0.03, Vector2(0.7, 1.3), {"materials": GRASS | MOSS, "moisture": Vector2(0.6, 1.0), "variants": 1, "draw_distance": 70.0}),
			_layer("Berry bush", K.BERRY_BUSH, 0.006, Vector2(0.7, 1.2), {"materials": veg, "temperature": Vector2(0.0, 0.7), "variants": 1, "draw_distance": 90.0}),
			_layer("Branches", K.BRANCH, 0.012, Vector2(0.8, 1.2), {"materials": veg, "vertical_alignment": 0.2, "draw_distance": 60.0}),
			_layer("Logs", K.LOG, 0.002, Vector2(0.8, 1.2), {"materials": veg, "vertical_alignment": 0.2, "slope": Vector2(0, 25), "collision": true, "draw_distance": 150.0}),
		]),
		_biome("Boreal", Vector2(0.06, 0.4), Vector2(0.25, 1.0), [
			_layer("Grass", K.GRASS, 5.0, Vector2(0.6, 1.1), {"materials": GRASS, "grass_base_color": Color(0.17, 0.3, 0.14), "grass_tip_color": Color(0.36, 0.48, 0.25)}),
			_layer("Pine", K.PINE, 0.007, Vector2(2.2, 3.4), {"materials": veg | SNOW}),
			_layer("Birch", K.BIRCH, 0.0015, Vector2(2.0, 3.0), {"materials": veg, "temperature": Vector2(0.25, 1.0)}),
			_layer("Shrub", K.SHRUB, 0.012, Vector2(0.6, 1.1), {"materials": veg, "draw_distance": 110.0}),
			_layer("Branches", K.BRANCH, 0.012, Vector2(0.8, 1.2), {"materials": veg, "vertical_alignment": 0.2, "draw_distance": 60.0}),
			_layer("Logs", K.LOG, 0.003, Vector2(0.8, 1.2), {"materials": veg, "vertical_alignment": 0.2, "slope": Vector2(0, 25), "collision": true, "draw_distance": 150.0}),
		]),
		_biome("Tropical", Vector2(0.72, 1.0), Vector2(0.45, 1.0), [
			_layer("Grass", K.GRASS, 8.0, Vector2(0.8, 1.5), {"materials": GRASS}),
			_layer("Palm", K.PALM, 0.004, Vector2(1.8, 2.8), {"materials": veg | SAND}),
			_layer("Fruit tree", K.FRUIT_TREE, 0.002, Vector2(1.6, 2.4), {"materials": veg}),
			_layer("Fern", K.FERN, 0.04, Vector2(0.8, 1.5), {"materials": GRASS | MOSS, "variants": 1, "draw_distance": 70.0}),
			_layer("Shrub", K.SHRUB, 0.02, Vector2(0.8, 1.4), {"materials": veg, "draw_distance": 110.0}),
		]),
		_biome("Dry", Vector2(0.45, 1.0), Vector2(0.0, 0.38), [
			# Deserts and steppe: sparse clumps of dry grass, none at all in the driest land (the terrain is drawn as sand
			# there; its material channel still says grass, so the moisture limit does the work)
			_layer("Dry grass", K.DRY_GRASS, 0.3, Vector2(0.6, 1.2), {"materials": GRASS, "moisture": Vector2(0.26, 1.0), "clump_size": 14.0, "grass_base_color": Color(0.36, 0.33, 0.14), "grass_tip_color": Color(0.66, 0.58, 0.30)}),
			_layer("Dead tree", K.DEAD_TREE, 0.0004, Vector2(1.6, 2.6), {"materials": veg | SAND, "clump_size": 0.0}),
			_layer("Dead bush", K.DEAD_BUSH, 0.01, Vector2(0.7, 1.3), {"materials": veg | SAND, "draw_distance": 110.0}),
			_layer("Cactus", K.CACTUS, 0.004, Vector2(0.7, 1.4), {"materials": SAND | DIRT | GRASS, "temperature": Vector2(0.7, 1.0), "variants": 1, "draw_distance": 150.0}),
		]),
		_biome("Tundra", Vector2(0.0, 0.1), Vector2(0.0, 1.0), [
			_layer("Dead tree", K.DEAD_TREE, 0.0005, Vector2(1.4, 2.2), {"materials": veg | SNOW, "clump_size": 0.0}),
			_layer("Pine", K.PINE, 0.0008, Vector2(1.2, 2.0), {"materials": veg | SNOW}),
			_layer("Stones", K.BOULDER, 0.004, Vector2(0.4, 1.2), small_rocks.merged({"moss": 0.0, "lichen": 0.15, "variants": 3}, true)),
		]),
		# Regions, any climate
		_biome("Grassland Rocks", Vector2(0.0, 1.0), Vector2(0.0, 1.0), [
			_layer("Pebbles", K.PEBBLES, 0.02, Vector2(0.3, 0.8), {"materials": veg | SAND | ROCK, "slope": Vector2(0, 45), "variants": 1, "draw_distance": 50.0}),
			_layer("Stones", K.BOULDER, 0.003, Vector2(0.3, 0.9), small_rocks.merged({"variants": 3}, true)),
			_layer("Boulders", K.BOULDER, 0.0004, Vector2(1.0, 2.5), small_rocks.merged({"variants": 3, "lod": 1, "sink": -0.12, "collision": true, "draw_distance": 0.0, "facet_detail": 2, "moss": 0.4}, true)),
		], {"materials": veg | SAND}),
		_biome("Mountain Rock", Vector2(0.0, 1.0), Vector2(0.0, 1.0), [
			_layer("Boulder field", K.BOULDER, 0.0015, Vector2(1.5, 5.0), {"variants": 3, "lod": 1, "sink": -0.15, "collision": true, "facet_detail": 1, "lichen": 0.1}),
		], {"materials": ROCK | SNOW, "slope": Vector2(0, 35)}),
		_biome("Cliffs", Vector2(0.0, 1.0), Vector2(0.0, 1.0), [
			# Big slabs set into the face read as rock strata; a few boulders and spires break the outline
			_layer("Cliff slabs", K.ROCK_SLAB, 0.0003, Vector2(18.0, 55.0), {"variants": 3, "lod": 1, "vertical_alignment": 0.0, "sink": -0.18, "collision": true, "facet_detail": 1, "roughness": 0.18}),
			_layer("Cliff boulders", K.BOULDER, 0.0005, Vector2(8.0, 25.0), {"variants": 2, "lod": 1, "vertical_alignment": 0.0, "sink": -0.15, "collision": true, "facet_detail": 1, "roughness": 0.3}),
			_layer("Spires", K.ROCK_SPIRE, 0.00004, Vector2(15.0, 40.0), {"variants": 2, "lod": 1, "vertical_alignment": 0.4, "sink": -0.2, "collision": true, "slope": Vector2(30, 80)}),
		], {"slope": Vector2(35, 90)}),
	])
	for b in c.biomes:
		b.layers.append_array(forest_layers(b.name))
	return c
