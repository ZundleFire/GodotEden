@tool
class_name EdenFoliageLayer
extends Resource
## One thing that spawns inside an EdenFoliageBiome: what it is, how much, how big, and where exactly.
## Climate/slope/material limits here narrow the biome's own (they never widen them).

enum Kind {
	GRASS, DRY_GRASS,
	OAK, PINE, BIRCH, WILLOW, PALM, DEAD_TREE, FRUIT_TREE,
	SHRUB, FERN, DEAD_BUSH, TALL_GRASS, CACTUS, BERRY_BUSH,
	PEBBLES, BOULDER, ROCK_SLAB, ROCK_SPIRE,
	BRANCH, LOG,
}

@export var name := "Layer"
@export var enabled := true
@export var kind := Kind.BOULDER
## Distinct pre-built shapes; each is one draw call per terrain chunk.
@export_range(1, 8) var variants := 2

@export_group("Amount & Size")
## Instances per m² (split across variants), times the biome's density_scale.
@export_range(0.0, 20.0, 0.0001, "or_greater") var density := 0.01
## Uniform scale range, times the biome's size_scale.
@export_range(0.01, 50.0, 0.01, "or_greater") var scale_min := 0.8
@export_range(0.01, 50.0, 0.01, "or_greater") var scale_max := 1.3
## Forest-style clumps: > 0 thins instances between noise patches of about this size (m).
@export_range(0.0, 2000.0, 1.0) var clump_size := 0.0

@export_group("Where")
## Narrows the biome's climate ranges (0..1).
@export var temperature := Vector2(0.0, 1.0)
@export var moisture := Vector2(0.0, 1.0)
## Surface slope in degrees; narrows the biome's.
@export var slope := Vector2(0.0, 90.0)
## Ground materials (MIXEL4); none = the biome's.
@export_flags("Grass", "Rock", "Snow", "Sand", "Dirt", "Moss") var materials := 0

@export_group("Placement")
## 0 = aligned with the surface normal (rocks in cliffs), 1 = world-up relative to the planet.
@export_range(0.0, 1.0, 0.01) var vertical_alignment := 1.0
## Pushes instances into the ground (negative) along the normal, in metres at scale 1 (so as a fraction of a
## ~1 m rock: -0.2 buries a fifth of it at any size).
@export_range(-20.0, 5.0, 0.01) var sink := 0.0
## 0 = drawn as far as the chunk LOD exists; otherwise whole chunks stop drawing past this (m).
@export_range(0.0, 5000.0, 1.0) var draw_distance := 0.0
## Terrain LOD whose chunks this spawns on (0 = only the full-detail range around the camera).
@export_range(0, 3) var lod := 0
@export var collision := false
enum ShadowMode { AUTO, ON, OFF }
## Casts sun shadows. Auto: trees and rocks whose biggest size is at least 1.5 m -- small scatter's shadows are
## barely visible but cost a shadow pass per cascade (ferns, shrubs and pebbles were ~10 ms at sunset on a GTX 750 Ti).
@export var shadow_mode := ShadowMode.AUTO

@export_group("Far LOD")
enum FarMode { AUTO, ON, OFF }
## Keeps the layer visible past its normal range, on coarser terrain chunks, as voxelized low-poly copies.
## Auto: trees and rocks whose biggest size is at least 1.5 m (not grass, bushes, pebbles or wood).
@export var far_mode := FarMode.AUTO
## How far the layer stays visible (m).
@export_range(500.0, 20000.0, 1.0) var far_distance := 8000.0
## Up to here the far tiers use a finer voxel copy (2x far_resolution; rocks always use their 20-facet version); beyond it the normal
## one, and past 4x this a coarser one (far_resolution - 2).
@export_range(0.0, 4000.0, 1.0) var far_detail_distance := 512.0
## Voxel cells across the object's width (coarser = cheaper, blockier).
@export_range(2, 12) var far_resolution := 5
## Density multiplier per farther ring (each ring has 4x the area of the previous one).
@export_range(0.05, 1.0, 0.01) var far_density_falloff := 0.35
## Size multiplier per farther ring, so thinned-out forests keep their coverage.
@export_range(1.0, 2.0, 0.01) var far_scale_growth := 1.2

@export_group("Rock Look")
@export var rock_color := Color(0.46, 0.45, 0.42)
@export_range(0.0, 0.5, 0.01) var color_variation := 0.12
## Share of upward-facing facets covered in moss.
@export_range(0.0, 1.0, 0.01) var moss := 0.0
@export var moss_color := Color(0.24, 0.36, 0.14)
## Share of facets with lichen spots.
@export_range(0.0, 0.5, 0.01) var lichen := 0.0
@export var lichen_color := Color(0.72, 0.62, 0.3)
## Icosphere subdivisions: 1 = 80 facets, 2 = 320.
@export_range(0, 3) var facet_detail := 1
## Per-axis stretch of the rock shape (x, height, z).
@export var stretch := Vector3(1.0, 0.75, 1.0)
@export_range(0.0, 0.6, 0.01) var roughness := 0.25

@export_group("Grass Look")
@export var grass_base_color := Color(0.16, 0.33, 0.11)
@export var grass_tip_color := Color(0.32, 0.54, 0.20)


func is_tree() -> bool:
	return kind >= Kind.OAK and kind <= Kind.FRUIT_TREE


func is_grass() -> bool:
	return kind == Kind.GRASS or kind == Kind.DRY_GRASS


func is_rock() -> bool:
	return kind >= Kind.PEBBLES and kind <= Kind.ROCK_SPIRE


## Whether this layer gets far tiers, for a final size range (after the biome's size scale).
func has_far(max_size: float) -> bool:
	match far_mode:
		FarMode.ON:
			return not is_grass()
		FarMode.OFF:
			return false
	return is_tree() or (is_rock() and kind != Kind.PEBBLES and max_size >= 1.5)


## Whether instances cast sun shadows, for a final size range (after the biome's size scale).
func casts_shadow(max_size: float) -> bool:
	match shadow_mode:
		ShadowMode.ON:
			return true
		ShadowMode.OFF:
			return false
	return is_tree() or (is_rock() and kind != Kind.PEBBLES and max_size >= 1.5)


func sways() -> bool:
	return not is_rock() and kind != Kind.BRANCH and kind != Kind.LOG
