extends SceneTree
## Writes settings/eden_graphics_presets.tres from EdenGraphicsPreset.defaults() (overwrites your edits: run only to
## reset the levels).
##   godot --headless --path demo_eden -s res://settings/_make_presets.gd


func _initialize() -> void:
	var res := EdenGraphicsPresets.new()
	res.presets = EdenGraphicsPreset.defaults()
	print("PRESETS save: ", ResourceSaver.save(res, "res://settings/eden_graphics_presets.tres"))
	quit()
