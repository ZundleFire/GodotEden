extends Node3D
## Walk the V4 planet: the probe scene with an EdenPlayer spawned on a temperate meadow near a forest. The player
## sets up the rest itself (terrain collision, its viewer, the HUD and weather keys: see EdenPlayer's Scene Setup).
## _ocean_editor_probe.tscn also has an EdenPlayer placed in it; this scene removes that one and picks a spawn.
## Started from the main menu, the planet is the chosen world's (EdenSession.seed), set before anything samples or
## streams it.

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
	if EdenSession.active:
		# The world's seed and settings, on a copy (the scene's generator is shared with the menu and the editor)
		terrain.generator = terrain.generator.duplicate()
		terrain.generator.seed = EdenSession.seed
		EdenWorldSettings.apply(terrain.generator, EdenSession.settings)
	# Procedural moons and parent planet, from the world's seed (the scene's own when run from the editor)
	EdenSkyBodies.apply_world(world, int(terrain.generator.seed))
	ambience = terrain.get_node_or_null("EdenAmbience")
	add_child(world)
	var spawn := _find_spawn()
	player = player_scene.instantiate()
	add_child(player)
	player.set_planet(terrain)
	player.global_position = spawn
	var music := get_node_or_null("/root/EdenMusic")
	if music:
		music.play_game()
	_show_loading()


## A loading screen over the world until the terrain under the player exists and they can move
func _show_loading() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 100
	add_child(layer)
	var cover := ColorRect.new()
	cover.color = Color(0.02, 0.03, 0.05)
	cover.set_anchors_preset(Control.PRESET_FULL_RECT)
	layer.add_child(cover)
	var label := Label.new()
	label.theme = EdenUITheme.theme()
	label.text = "LOADING %s..." % (EdenSession.world_name.to_upper() if EdenSession.active else "WORLD")
	label.add_theme_font_size_override("font_size", 32)
	label.set_anchors_preset(Control.PRESET_CENTER)
	label.grow_horizontal = Control.GROW_DIRECTION_BOTH
	label.grow_vertical = Control.GROW_DIRECTION_BOTH
	cover.add_child(label)
	while not player.ready_to_move:
		await get_tree().process_frame
	await get_tree().create_timer(0.5).timeout # (let the first meshes draw in)
	var tween := create_tween()
	tween.tween_property(cover, "modulate:a", 0.0, 0.6)
	await tween.finished
	layer.queue_free()


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
		if h < 15.0:
			continue
		var score := -absf(h - 120.0) * 0.01
		if float(s.temperature) < 0.4 or float(s.temperature) > 0.7 or float(s.moisture) < 0.5 or float(s.landform) > 0.3:
			score -= 1000.0 # any land will do if the world's settings leave a temperate meadow
			if score <= best_score:
				continue
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
