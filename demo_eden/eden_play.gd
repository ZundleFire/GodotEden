extends Node3D
## Walk the V4 planet: the probe scene with an EdenPlayer spawned on a temperate meadow near a forest. The player
## sets up the rest itself (terrain collision, its viewer, the HUD and weather keys: see EdenPlayer's Scene Setup).
## _ocean_editor_probe.tscn also has an EdenPlayer placed in it; this scene removes that one and picks a spawn.

@export var world_scene: PackedScene = preload("res://_ocean_editor_probe.tscn")
@export var player_scene: PackedScene = preload("res://Character/eden_player.tscn")

var terrain: VoxelLodTerrain
var ambience: EdenAmbience
var player: EdenPlayer


func _ready() -> void:
	var world := world_scene.instantiate()
	var placed := world.get_node_or_null("EdenPlayer")
	if placed:
		world.remove_child(placed)
		placed.free()
	terrain = world.get_node("VoxelLodTerrain")
	ambience = terrain.get_node_or_null("EdenAmbience")
	add_child(world)
	var spawn := _find_spawn()
	player = player_scene.instantiate()
	add_child(player)
	player.set_planet(terrain)
	player.global_position = spawn


# A temperate, moist, gentle spot on land with forest nearby (golden-spiral search over the planet)
func _find_spawn() -> Vector3:
	var gen := terrain.generator
	var R: float = gen.planet_radius
	var foliage := terrain.get_node_or_null("EdenFoliage")
	var golden := PI * (3.0 - sqrt(5.0))
	var best := Vector3.UP * R
	var best_score := -INF
	for i in 3000:
		var y := 1.0 - float(i) / 2999.0 * 2.0
		var r := sqrt(maxf(0.0, 1.0 - y * y))
		var d := Vector3(cos(golden * i) * r, y, sin(golden * i) * r)
		var s: Dictionary = gen.sample_surface(d)
		var h := float(s.height)
		if h < 15.0 or float(s.temperature) < 0.4 or float(s.temperature) > 0.7 or float(s.moisture) < 0.5 or float(s.landform) > 0.3:
			continue
		var score := -absf(h - 120.0) * 0.01
		if foliage and foliage.has_method("get_forest_density"):
			var t := d.cross(Vector3.UP if absf(d.y) < 0.99 else Vector3.RIGHT).normalized()
			var o := d.rotated(t, 80.0 / R)
			var here: float = foliage.get_forest_density(d * (R + h))
			var near: float = foliage.get_forest_density(o * (R + float(gen.sample_surface(o).height)))
			score += near - here # a clearing next to a forest
		if score > best_score:
			best_score = score
			best = d * (R + h + 2.0)
	return best
