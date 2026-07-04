## terrain_test.gd
## Root scene script. Runs PlanetPreGenerator in a thread then wires planet_data.
@tool
extends Node3D

@onready var _terrain : VoxelLodTerrain = get_node("VoxelLodTerrain")
@onready var _player  : CharacterBody3D = get_node("Player")
var _thread : Thread = null

func _ready() -> void:
	if Engine.is_editor_hint():
		# Godot auto-disables run_stream_in_editor for script-based generators.
		# Re-enable it so terrain is visible in the editor viewport.
		_terrain.run_stream_in_editor = true
		return
	_player.global_position = Vector3(0.0, planet_radius() + 600.0, 0.0)
	_thread = Thread.new()
	_thread.start(_run_pregen)

func planet_radius() -> float:
	var gen := _terrain.generator as PlanetGenerator
	return gen.planet_radius if gen else 40000.0

func _run_pregen() -> void:
	var gen := _terrain.generator as PlanetGenerator
	if gen == null:
		push_error("terrain_test: generator is not a PlanetGenerator")
		return
	var pregen := PlanetPreGenerator.new()
	pregen.num_regions   = 12000
	pregen.num_plates    = 24
	pregen.planet_radius = gen.planet_radius
	pregen.seed          = 42
	pregen.jitter        = 0.45
	pregen.progress_updated.connect(func(s, f): call_deferred("_on_progress", s, f))
	var data := pregen.generate()
	call_deferred("_on_pregen_done", data)

func _on_progress(step: String, frac: float) -> void:
	print("[PlanetPreGen] %.0f%%  %s" % [frac * 100.0, step])

func _on_pregen_done(data: PlanetData) -> void:
	var gen := _terrain.generator as PlanetGenerator
	gen.planet_data = data
	gen.set_noise_seeds(42)
	_player.global_position = Vector3(0.0, data.planet_radius + 200.0, 0.0)
	print("[PlanetTest] Pre-generation complete.")
	_thread.wait_to_finish()
	_thread = null
