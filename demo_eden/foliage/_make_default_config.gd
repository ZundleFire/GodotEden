extends SceneTree
## Writes EdenFoliageConfig.make_default() to res://foliage/eden_foliage_default.tres.
##   godot --headless --path demo_eden -s res://foliage/_make_default_config.gd -- [--grass_density=5]


func _initialize() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=")
		args[kv[0]] = kv[1] if kv.size() > 1 else "1"
	var cfg := EdenFoliageConfig.make_default()
	if args.has("grass_density"):
		for b in cfg.biomes:
			for l in b.layers:
				if l.is_grass():
					l.density = float(args.grass_density)
	var err := ResourceSaver.save(cfg, args.get("out", "res://foliage/eden_foliage_default.tres"))
	var n := 0
	for b in cfg.biomes:
		n += b.layers.size()
	print("CONFIG saved err=%d biomes=%d layers=%d" % [err, cfg.biomes.size(), n])
	quit()
