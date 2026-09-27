extends SceneTree
## Throwaway audit: loads every panorama in res://Panoramics one at a time and reports its
## dimensions, so a bad or oversized file is found now rather than when someone cycles onto it.
## Run: godot --path demo_eden --headless --script res://_panorama_audit.gd

func _init() -> void:
	var folder := "res://Panoramics"
	var dir := DirAccess.open(folder)
	if dir == null:
		print("cannot open ", folder)
		quit(1)
		return

	var seen := {}
	var names: Array[String] = []
	for f in dir.get_files():
		var n := f.trim_suffix(".import")
		if n.get_extension().to_lower() not in ["png", "jpg", "jpeg", "webp", "exr", "hdr", "ktx", "dds"]:
			continue
		if seen.has(n):
			continue
		seen[n] = true
		names.append(n)
	names.sort()

	var ok := 0
	var bad := 0
	var worst := 0
	for n in names:
		var path := folder.path_join(n)
		var tex := ResourceLoader.load(path, "Texture2D", ResourceLoader.CACHE_MODE_IGNORE) as Texture2D
		if tex == null:
			print("  FAIL  %s" % n)
			bad += 1
			continue
		var w := tex.get_width()
		var h := tex.get_height()
		worst = maxi(worst, w)
		# Equirectangular must be 2:1; anything else will map wrong on the sphere.
		var ratio_note := "" if absf(float(w) / float(h) - 2.0) < 0.01 else "   <-- NOT 2:1, will distort"
		print("  ok    %-28s %5dx%-5d%s" % [n, w, h, ratio_note])
		ok += 1

	print("panorama audit: %d ok, %d failed, largest width %d" % [ok, bad, worst])
	quit(0 if bad == 0 else 1)
