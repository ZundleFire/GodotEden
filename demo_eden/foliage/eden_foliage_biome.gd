@tool
class_name EdenFoliageBiome
extends Resource
## A region of the planet and what grows there. Climate comes from the terrain's surface data (V4 generator:
## temperature ~0.9 equator, ~0.75 subtropics, ~0.4 mid latitudes, ~0.15 subarctic, ~0 poles; moisture 0..1).
## Regions that aren't climates (cliffs, mountain rock) are biomes too: set a slope range or ground materials.

@export var name := "Biome"
@export var enabled := true
@export var temperature := Vector2(0.0, 1.0)
@export var moisture := Vector2(0.0, 1.0)
## Surface slope in degrees.
@export var slope := Vector2(0.0, 90.0)
## Ground materials (MIXEL4); none = any.
@export_flags("Grass", "Rock", "Snow", "Sand", "Dirt", "Moss") var materials := 0
## Multiplies every layer's density.
@export_range(0.0, 10.0, 0.01, "or_greater") var density_scale := 1.0
## Multiplies every layer's size range.
@export_range(0.01, 10.0, 0.01, "or_greater") var size_scale := 1.0
@export var layers: Array[EdenFoliageLayer] = []
