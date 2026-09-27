@tool
class_name EdenProbe
extends RefCounted
## Answers "what is at this point of the planet": picks the V4 terrain along a ray and gathers everything the
## generator, the stored voxels, the terrain mesh and the foliage config know about the hit. Used by the
## eden_debug_probe editor plugin; plain static code so a runtime probe can reuse it.

const MATERIALS := ["grass", "rock", "snow", "sand", "dirt", "moss", "ocean floor"]
const BIOMES := ["deep ocean", "shallow ocean", "coast", "river", "tropical", "forest", "plains", "desert", "tundra",
		"mountain", "snow"]
const MAX_RELIEF := 8000.0 # m above/below planet_radius the surface can reach


## First VoxelLodTerrain with an EdenPlanetGeneratorV4 under `root`, or null.
static func find_planet(root: Node) -> VoxelLodTerrain:
	if root == null:
		return null
	if root is VoxelLodTerrain and root.generator is EdenPlanetGeneratorV4:
		return root
	for c in root.get_children():
		var t := find_planet(c)
		if t:
			return t
	return null


## Ray against the generator's surface (radius + height along a direction), in terrain-local space. Works at any
## distance, loaded or not, without colliders. Returns the local hit point, or null.
static func raycast(terrain: VoxelLodTerrain, origin_world: Vector3, dir_world: Vector3, max_distance := 120000.0):
	var gen: EdenPlanetGeneratorV4 = terrain.generator
	var to_local := terrain.global_transform.affine_inverse()
	var o := to_local * origin_world
	var d := (to_local.basis * dir_world).normalized()
	var R: float = gen.planet_radius
	# Enter the relief shell first: nothing to hit outside it
	var outer := R + MAX_RELIEF
	var t := 0.0
	if o.length() > outer:
		var b := o.dot(d)
		var disc := b * b - (o.length_squared() - outer * outer)
		if disc < 0.0 or -b - sqrt(disc) < 0.0:
			return null
		t = -b - sqrt(disc)
	var prev_t := t
	var f := _surface_distance(gen, o + d * t)
	var steps := 0
	while f > 0.0:
		prev_t = t
		# Height-field "distance" isn't a true SDF on slopes: step a fraction of it
		t += maxf(f * 0.4, 0.5)
		if t > max_distance or steps > 4000:
			return null
		f = _surface_distance(gen, o + d * t)
		steps += 1
	var lo := prev_t
	var hi := t
	for i in 24:
		var mid := (lo + hi) * 0.5
		if _surface_distance(gen, o + d * mid) > 0.0:
			lo = mid
		else:
			hi = mid
	return o + d * hi


static func _surface_distance(gen: EdenPlanetGeneratorV4, p: Vector3) -> float:
	return p.length() - gen.planet_radius - float(gen.sample_surface(p).height)


## Everything known about the local point `p` (a raycast hit): Array of [section title, [[label, value], ...]].
static func describe(terrain: VoxelLodTerrain, p: Vector3, camera_world: Vector3) -> Array:
	var gen: EdenPlanetGeneratorV4 = terrain.generator
	var R: float = gen.planet_radius
	var up := p.normalized()
	var s: Dictionary = gen.sample_surface(up)
	var height := float(s.height)
	var slope := _slope_degrees(gen, up)
	var world := terrain.global_transform * p
	var sections := []

	var lat := rad_to_deg(asin(clampf(up.y, -1.0, 1.0)))
	var lon := rad_to_deg(atan2(up.x, up.z))
	sections.append(["Location", [
		["world", "(%.1f, %.1f, %.1f)" % [world.x, world.y, world.z]],
		["distance", "%s m" % _n(world.distance_to(camera_world), 1)],
		["above sea", "%s m" % _n(height - gen.sea_level, 1)],
		["lat / lon", "%.3f° %s, %.3f° %s" % [absf(lat), "N" if lat >= 0 else "S", absf(lon), "E" if lon >= 0 else "W"]],
		["slope", "%.1f°" % slope],
	]])

	var landform := float(s.get("landform", 1.0))
	sections.append(["Generator (V4)", [
		["height", "%s m" % _n(height, 1)],
		["landform", "%.2f  %s" % [landform, "lowland" if landform < 0.2 else ("mountain range" if landform > 0.8 else "foothills")]],
		["temperature", _temperature(float(s.temperature), up, terrain)],
		["moisture", "%d %%  (%.2f; deserts under 20 %%, rainforest over 70 %%)" % [roundi(100.0 * s.moisture), s.moisture]],
		["ridge / erosion", "%.2f / %.2f" % [s.ridge, s.erosion]],
		["biome", _name(BIOMES, int(s.biome_id))],
		["material", _name(MATERIALS, int(s.get("material", -1)))],
	]])

	var voxel := _voxel_data(terrain, p, up)
	sections.append(["Voxel data", voxel.rows])
	sections.append(["Terrain mesh", [["finest LOD here", _mesh_lod(terrain, p)]]])

	var climate := {"temperature": float(s.temperature), "moisture": float(s.moisture)}
	if voxel.has("temperature"): # foliage filters read the stored surface data, prefer it
		climate = {"temperature": voxel.temperature, "moisture": voxel.moisture}
	var foliage := terrain.get_node_or_null("EdenFoliage")
	if foliage and foliage.get("config"):
		var mat_ids: Array = voxel.get("materials", [int(s.get("material", 0))])
		sections.append(["Foliage here", _foliage(foliage.config, climate, slope, mat_ids)])
	return sections


static func _slope_degrees(gen: EdenPlanetGeneratorV4, up: Vector3) -> float:
	var R: float = gen.planet_radius
	var t := up.cross(Vector3.UP if absf(up.y) < 0.99 else Vector3.RIGHT).normalized()
	var b := up.cross(t)
	const STEP := 2.0
	var pc := up * (R + float(gen.sample_surface(up).height))
	var da := up.rotated(b, STEP / R)
	var db := up.rotated(t, STEP / R)
	var pa := da * (R + float(gen.sample_surface(da).height))
	var pb := db * (R + float(gen.sample_surface(db).height))
	var n := (pa - pc).cross(pb - pc).normalized()
	return rad_to_deg(acos(clampf(absf(n.dot(up)), 0.0, 1.0)))


# The voxel just under the surface, generated with the terrain's own VoxelFormat: exactly what the mesher gets, and
# through CUSTOM1/CUSTOM2 what the shader and foliage read. (VoxelTool can't give this: without a stream the terrain
# keeps no voxels after meshing, and its per-voxel fallback uses an 8-bit DATA6 the generator won't write to.)
static func _voxel_data(terrain: VoxelLodTerrain, p: Vector3, up: Vector3) -> Dictionary:
	var pos := Vector3i((p - up * 0.5).floor())
	var buf := VoxelBuffer.new()
	var fmt: VoxelFormat = terrain.format
	for ch in [VoxelBuffer.CHANNEL_SDF, VoxelBuffer.CHANNEL_INDICES, VoxelBuffer.CHANNEL_WEIGHTS, VoxelBuffer.CHANNEL_DATA6]:
		if fmt:
			buf.set_channel_depth(ch, fmt.get_channel_depth(ch))
	buf.create(2, 2, 2)
	terrain.generator.generate_block(buf, Vector3(pos), 0)
	var out := {}
	var edited := ""
	var vt := terrain.get_voxel_tool()
	if vt.is_area_editable(AABB(Vector3(pos), Vector3.ONE)):
		vt.channel = VoxelBuffer.CHANNEL_SDF
		var stored := vt.get_voxel_f(pos)
		if absf(stored - buf.get_voxel_f(0, 0, 0, VoxelBuffer.CHANNEL_SDF)) > 0.01:
			edited = "  (edited: %.3f)" % stored
	var rows := [["voxel", "(%d, %d, %d) at LOD 0" % [pos.x, pos.y, pos.z]]]
	rows.append(["sdf", "%.3f%s" % [buf.get_voxel_f(0, 0, 0, VoxelBuffer.CHANNEL_SDF), edited]])
	var idx := int(buf.get_voxel(0, 0, 0, VoxelBuffer.CHANNEL_INDICES))
	var w := int(buf.get_voxel(0, 0, 0, VoxelBuffer.CHANNEL_WEIGHTS))
	var weights := [(w & 0x0f) << 4, w & 0xf0, (w >> 4) & 0xf0, (w >> 8) & 0xf0] # mixel4.h, max 240
	var total := 0
	for x in weights:
		total += x
	var parts := []
	var mats := []
	for i in 4:
		var m := (idx >> (4 * i)) & 0x0f
		if weights[i] > 0:
			parts.append("%s %d%%" % [_name(MATERIALS, m), roundi(100.0 * weights[i] / maxf(total, 1))])
			mats.append(m)
	rows.append(["materials", ", ".join(parts) if parts else "none"])
	out.materials = mats
	var sd := int(buf.get_voxel(0, 0, 0, VoxelBuffer.CHANNEL_DATA6))
	if sd != 0:
		out.temperature = ((sd >> 24) & 0xff) / 255.0
		out.moisture = ((sd >> 16) & 0xff) / 255.0
		rows.append(["surface data", "erosion %.2f  ridge %.2f" % [(sd & 0xff) / 255.0, ((sd >> 8) & 0xff) / 255.0 * 2.0 - 1.0]])
		rows.append(["", "moisture %d %%  temperature %.1f °C (annual mean)" % [roundi(out.moisture * 100.0), EdenCalendar.celsius(out.temperature)]])
	else:
		rows.append(["surface data", "none (terrain format has no 32-bit DATA6)"])
	out.rows = rows
	return out


# The climate's annual mean in °C, and today's (the season, when the scene has an EdenCalendar)
static func _temperature(t: float, up: Vector3, terrain: Node) -> String:
	var text := "%.1f °C annual mean  (%.3f)" % [EdenCalendar.celsius(t), t]
	var root := terrain.get_tree().edited_scene_root if terrain.get_tree() and Engine.is_editor_hint() else terrain.get_tree().current_scene
	var cal: EdenCalendar = null
	for n in root.find_children("*", "Node", true, false) if root else []:
		if n is EdenCalendar:
			cal = n
			break
	if cal:
		text += ";  now %.1f °C (%s, %s)" % [EdenCalendar.celsius(t + cal.season_offset(up)), cal.season_at(up), cal.date_string()]
	return text


static func _mesh_lod(terrain: VoxelLodTerrain, p: Vector3) -> String:
	for lod in terrain.lod_count:
		var bs := float(terrain.mesh_block_size << lod)
		var c := Vector3i((p / bs).floor())
		for dz in range(-1, 2):
			for dy in range(-1, 2):
				for dx in range(-1, 2):
					if terrain.debug_get_mesh_block_info(c + Vector3i(dx, dy, dz), lod).get("meshed", false):
						return "LOD %d  (%d m chunks)" % [lod, int(bs)]
	return "none loaded"


# Which config biomes match here, and which of their layers can spawn (with the reason for the ones that can't)
static func _foliage(config: Resource, climate: Dictionary, slope: float, mat_ids: Array) -> Array:
	var rows := []
	var t: float = climate.temperature
	var m: float = climate.moisture
	for b in config.biomes:
		if b == null or not b.enabled:
			continue
		var why := _reject(b, t, m, slope, mat_ids)
		if not why.is_empty():
			continue
		var ok := []
		var no := []
		for l in b.layers:
			if l == null or not l.enabled:
				continue
			var lw := _reject(l, t, m, slope, mat_ids)
			if lw.is_empty():
				ok.append(l.name)
			else:
				no.append("%s (%s)" % [l.name, lw])
		rows.append([b.name, ", ".join(ok) if ok else "no layer fits"])
		if no:
			rows.append(["", "not: " + ", ".join(no)])
	if rows.is_empty():
		rows.append(["", "no biome matches (%.0f °C, moisture %d %%, slope %.0f°)" % [EdenCalendar.celsius(t), roundi(m * 100.0), slope]])
	return rows


static func _reject(r: Resource, t: float, m: float, slope: float, mat_ids: Array) -> String:
	if t < r.temperature.x or t > r.temperature.y:
		return "needs %.0f to %.0f °C" % [EdenCalendar.celsius(r.temperature.x), EdenCalendar.celsius(r.temperature.y)]
	if m < r.moisture.x or m > r.moisture.y:
		return "needs moisture %d-%d %%" % [roundi(r.moisture.x * 100.0), roundi(r.moisture.y * 100.0)]
	if slope < r.slope.x or slope > r.slope.y:
		return "slope %.0f-%.0f°" % [r.slope.x, r.slope.y]
	if r.materials != 0:
		for id in mat_ids:
			if r.materials & (1 << id):
				return ""
		return "material"
	return ""


static func _name(names: Array, i: int) -> String:
	return names[i] if i >= 0 and i < names.size() else "#%d" % i


static func _n(v: float, decimals: int) -> String:
	return String.num(v, decimals)
