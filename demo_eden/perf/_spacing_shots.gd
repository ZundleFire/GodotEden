extends SceneTree
## Before/after stills of foliage spacing: the same view from above with EdenFoliage.spacing_enabled off, then on.
##   godot --path demo_eden --resolution 1280x720 -s res://perf/_spacing_shots.gd -- --out=<dir>

const SETTLE_FRAMES := 400

var play: Node
var player: EdenPlayer
var out := "user://"
var step := 0
var wait := 0


func _initialize() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			out = a.trim_prefix("--out=")
	play = load("res://eden_play.tscn").instantiate()
	root.add_child(play)
	current_scene = play


func _process(_d: float) -> bool:
	if player == null:
		for n in play.find_children("*", "CharacterBody3D", true, false):
			if n is EdenPlayer:
				player = n
		return false
	if not player.ready_to_move:
		return false
	wait += 1
	if wait < SETTLE_FRAMES:
		return false
	wait = 0
	var foliage: EdenFoliage = player.miner._foliage
	match step:
		0:
			var cal := player.calendar
			cal.running = false
			cal.set_date(1, 5, 2, 12.0) # summer, then local noon
			cal.days -= (cal.local_hour(player.up_direction) - 12.0) / 24.0
			cal._to_fields()
			cal._apply()
			for n in root.find_children("*", "CanvasLayer", true, false):
				n.visible = false
			player._pitch = -0.85
			player.camera_distance = 16.0
			foliage.spacing_enabled = false
		1:
			root.get_texture().get_image().save_jpg(out.path_join("spacing_off.jpg"), 0.9)
			foliage.spacing_enabled = true
		2:
			root.get_texture().get_image().save_jpg(out.path_join("spacing_on.jpg"), 0.9)
			print("SPACING done")
			quit()
			return true
	step += 1
	return false
