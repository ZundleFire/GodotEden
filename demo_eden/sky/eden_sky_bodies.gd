class_name EdenSkyBodies
## A world's sky bodies from its seed: which painted, animated surface the parent planet and the two moons get (the
## sky shader draws them live; see planet_sky.gdshader's bs_* functions, in the look of the EDEN_Assets
## Moon_Planet_Textures maps), the rings' band pattern and which space panorama is behind it all.
##   Parent planet (EdenParentPlanet.parent_planet_style): 1 gas giant, 2 primordial, 3 ocean, 4 terrestrial,
##     5 tropical, 6 tundral, 7 wetlands
##   Moons (EdenPlanetAtmosphere.moon_style / moonb_style): 1 rock, 2 ice, 3 rust

const PLANET_STYLES := ["", "gas giant", "primordial", "ocean", "terrestrial", "tropical", "tundral", "wetlands"]
const MOON_STYLES := ["", "rock", "ice", "rust"]
const PANORAMA_DIR := "res://Panoramics"


## {planet, moon, moonb} styles, and a seed for each (the palette, the land, the craters); the rings' seed and a
## panorama pick
static func world_bodies(world_seed: int) -> Dictionary:
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(world_seed * 7919 + 17)
	return {
		"planet": rng.randi_range(1, 7), "planet_seed": float(rng.randi() % 1000),
		"moon": rng.randi_range(1, 3), "moon_seed": float(rng.randi() % 1000),
		"moonb": rng.randi_range(1, 3), "moonb_seed": float(rng.randi() % 1000),
		"ring_seed": float(rng.randi() % 1000), "panorama": rng.randi(),
	}


## Gives a world scene (the probe scene's root) its look from the seed: moons, parent planet, rings and space
## panorama. A texture assigned in the scene would override a painted surface, so it is cleared. With show_parent
## false the parent planet is hidden (the main menu shows only the planet itself).
static func apply_world(world: Node, world_seed: int, show_parent := true) -> void:
	var b := world_bodies(world_seed)
	var atmosphere := _first(world, "EdenPlanetAtmosphere")
	var parent_planet := _first(world, "EdenParentPlanet")
	var rings := _first(world, "EdenPlanetRings")
	var space := _first(world, "EdenSpaceEnvironment")
	if atmosphere:
		atmosphere.set("moon_texture", null)
		atmosphere.set("moon_style", b.moon)
		atmosphere.set("moon_style_seed", b.moon_seed)
		atmosphere.set("moonb_texture", null)
		atmosphere.set("moonb_style", b.moonb)
		atmosphere.set("moonb_style_seed", b.moonb_seed)
	if parent_planet:
		parent_planet.set("parent_planet_texture", null)
		parent_planet.set("parent_planet_style", b.planet)
		parent_planet.set("parent_planet_seed", b.planet_seed)
		parent_planet.set("parent_planet_enabled", show_parent)
	if rings:
		rings.set("ring_seed", b.ring_seed)
	if space:
		# No folder scan (it would load the first panorama just to replace it): pick one and load only that
		space.set("space_texture_dir", "")
		space.set("use_placeholder_panorama", false)
		var files := _panoramas()
		if not files.is_empty():
			space.set("space_panorama", load(files[b.panorama % files.size()]))


static func _first(world: Node, type: String) -> Node:
	var n := world.find_children("*", type, true, false)
	return n[0] if n else null


## res://Panoramics images, sorted (an exported build lists them with a .import suffix)
static func _panoramas() -> PackedStringArray:
	var out := PackedStringArray()
	for f in DirAccess.get_files_at(PANORAMA_DIR):
		f = f.trim_suffix(".import")
		if f.get_extension().to_lower() in ["png", "jpg", "jpeg", "webp", "exr", "hdr"] and not out.has(f):
			out.append(PANORAMA_DIR.path_join(f))
	out.sort()
	return out
