extends SceneTree
## Prints the foliage config: every biome's climate/slope/material limits and its layers, the effective limits of
## each layer (biome ∩ layer), so spawning rules can be checked at a glance.
##   godot --headless --path demo_eden -s res://foliage/_dump_config.gd [-- res://path/config.tres]

const MATS := ["grass", "rock", "snow", "sand", "dirt", "moss"]


static func _mats(bits: int) -> String:
	if bits == 0:
		return "any"
	var out := []
	for i in MATS.size():
		if bits & (1 << i):
			out.append(MATS[i])
	return ",".join(out)


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var cfg: EdenFoliageConfig = load(args[0] if args.size() > 0 else "res://foliage/eden_foliage_default.tres")
	for b in cfg.biomes:
		print("BIOME %-12s T %s M %s slope %s mats %s dens x%.2f %s" % [b.name, b.temperature, b.moisture, b.slope,
				_mats(b.materials), b.density_scale, "" if b.enabled else "(off)"])
		for l in b.layers:
			var kind: String = EdenFoliageLayer.Kind.keys()[l.kind]
			var t := Vector2(maxf(b.temperature.x, l.temperature.x), minf(b.temperature.y, l.temperature.y))
			var m := Vector2(maxf(b.moisture.x, l.moisture.x), minf(b.moisture.y, l.moisture.y))
			print("   %-16s %-8s dens %.4f T %s M %s mats %s place %d dd %.0f" % [l.name, kind, l.density, t, m,
					_mats(l.materials if l.materials != 0 else b.materials), l.placement, l.draw_distance])
	quit()
