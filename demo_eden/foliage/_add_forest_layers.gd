extends SceneTree
## Adds EdenFoliageConfig.forest_layers() to the matching biomes of an existing config, keeping every other edit
## in it (unlike _make_default_config.gd, which rewrites the whole thing). Forest layers already there are replaced.
##   godot --headless --path demo_eden -s res://foliage/_add_forest_layers.gd -- [--config=res://foliage/eden_foliage_default.tres]


func _initialize() -> void:
	var path := "res://foliage/eden_foliage_default.tres"
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--config="):
			path = a.trim_prefix("--config=")
	var cfg: EdenFoliageConfig = load(path)
	var added := 0
	for b in cfg.biomes:
		var fresh := EdenFoliageConfig.forest_layers(b.name)
		var names := {}
		for l in fresh:
			names[l.name] = true
		# Replace earlier forest layers of the same name (so re-running applies new defaults to them)
		var kept: Array[EdenFoliageLayer] = []
		for l in b.layers:
			if not (names.has(l.name) and l.placement == EdenFoliageLayer.Placement.FOREST):
				kept.append(l)
		kept.append_array(fresh)
		b.layers = kept
		added += fresh.size()
	# Saving from a script (no editor filesystem) drops the file's UID, which scenes reference it by: copy the
	# original header's uid back in
	var header := FileAccess.get_file_as_string(path).get_slice("
", 0)
	var err := ResourceSaver.save(cfg, path)
	var uid_at := header.find(" uid=")
	if uid_at >= 0:
		var text := FileAccess.get_file_as_string(path)
		var first := text.get_slice("
", 0)
		if not first.contains(" uid="):
			text = first.trim_suffix("]") + header.substr(uid_at).trim_suffix("]") + "]" + text.substr(first.length())
			FileAccess.open(path, FileAccess.WRITE).store_string(text)
	print("FOREST set %d layers to %s err=%d" % [added, path, err])
	quit()
