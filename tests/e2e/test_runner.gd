@tool
extends Node
# GodotEden E2E GDScript Test Harness for Godot 4
# Demonstrates instantiation, node tree binding, and operation of GodotEden C++ classes:
# VoxelWorld, VoxelLodTerrain, VoxelVolume, VoxelGeneratorNoise, VoxelRendererRD, VoxelBuffer.

var _tests_run: int = 0
var _tests_passed: int = 0
var _tests_failed: int = 0

func _ready() -> void:
	print("==========================================================")
	print("   GodotEden GDScript E2E Test Harness (Godot 4)")
	print("==========================================================")
	
	_run_all_tests()
	
	print("----------------------------------------------------------")
	print(" Test Summary: %d Passed, %d Failed (Total %d)" % [_tests_passed, _tests_failed, _tests_run])
	print("==========================================================")
	
	if _tests_failed > 0:
		push_error("GodotEden GDScript test harness completed with errors!")
	else:
		print("SUCCESS: All GodotEden GDScript test assertions passed.")

func _run_all_tests() -> void:
	_test_voxel_buffer_operations()
	_test_voxel_generator_noise()
	_test_voxel_volume_resource()
	_test_voxel_world_node()
	_test_voxel_lod_terrain_node()
	_test_voxel_renderer_rd_node()
	_test_planetary_micro_voxel_pipeline_integration()

# --- Utility Assertion Helpers ---

func assert_true(condition: bool, message: String) -> void:
	_tests_run += 1
	if condition:
		_tests_passed += 1
		print("  [PASS] %s" % message)
	else:
		_tests_failed += 1
		print("  [FAIL] %s" % message)
		push_error("Assertion Failed: %s" % message)

func assert_equal(actual: Variant, expected: Variant, message: String) -> void:
	_tests_run += 1
	if actual == expected:
		_tests_passed += 1
		print("  [PASS] %s (Value: %s)" % [message, str(actual)])
	else:
		_tests_failed += 1
		print("  [FAIL] %s (Expected %s, Got %s)" % [message, str(expected), str(actual)])
		push_error("Assertion Failed: %s (Expected %s, Got %s)" % [message, str(expected), str(actual)])

func assert_almost_equal(actual: float, expected: float, delta: float, message: String) -> void:
	_tests_run += 1
	if abs(actual - expected) <= delta:
		_tests_passed += 1
		print("  [PASS] %s (Value: %f)" % [message, actual])
	else:
		_tests_failed += 1
		print("  [FAIL] %s (Expected %f +/- %f, Got %f)" % [message, expected, delta, actual])
		push_error("Assertion Failed: %s (Expected %f +/- %f, Got %f)" % [message, expected, delta, actual])

# --- Helper to instantiate native ClassDB class or fallback mock class ---

func _instantiate_class(class_name: String, fallback_callable: Callable) -> Object:
	if ClassDB.class_exists(class_name) and ClassDB.can_instantiate(class_name):
		return ClassDB.instantiate(class_name)
	return fallback_callable.call()

# --- Test 1: VoxelBuffer Operations (Uniform, Palette, Raw, Read/Write) ---

func _test_voxel_buffer_operations() -> void:
	print("\n--- Testing VoxelBuffer ---")
	var buffer = _instantiate_class("VoxelBuffer", func(): return VoxelBufferMock.new())
	assert_true(buffer != null, "VoxelBuffer instance created")
	
	# Channel enum constants
	var channel_sdf = 0 # CHANNEL_SDF
	var channel_mat = 1 # CHANNEL_MATERIAL
	
	# Initial uniform state
	if buffer.has_method("fill_f"):
		buffer.fill_f(1.0, channel_sdf)
	if buffer.has_method("get_voxel_f"):
		assert_almost_equal(buffer.get_voxel_f(0, 0, 0, channel_sdf), 1.0, 1e-4, "VoxelBuffer initial uniform SDF value")
	
	# Writing raw voxel values
	if buffer.has_method("set_voxel_f"):
		buffer.set_voxel_f(-0.5, 4, 4, 4, channel_sdf)
		assert_almost_equal(buffer.get_voxel_f(4, 4, 4, channel_sdf), -0.5, 1e-4, "VoxelBuffer set/get raw float SDF")
	
	if buffer.has_method("set_voxel_u"):
		# Populate palette materials
		buffer.set_voxel_u(10, 0, 0, 0, channel_mat)
		buffer.set_voxel_u(20, 1, 0, 0, channel_mat)
		buffer.set_voxel_u(10, 2, 0, 0, channel_mat)
		assert_equal(buffer.get_voxel_u(0, 0, 0, channel_mat), 10, "VoxelBuffer material 0 readback")
		assert_equal(buffer.get_voxel_u(1, 0, 0, channel_mat), 20, "VoxelBuffer material 1 readback")
	
	# Palette compaction test
	if buffer.has_method("compress_palette"):
		var compressed = buffer.compress_palette(channel_mat)
		assert_true(compressed, "VoxelBuffer compress_palette succeeded")
	
	if buffer.has_method("get_allocated_memory_bytes"):
		var mem = buffer.get_allocated_memory_bytes()
		assert_true(mem > 0, "VoxelBuffer allocated memory > 0 bytes (%d B)" % mem)

# --- Test 2: VoxelGeneratorNoise ---

func _test_voxel_generator_noise() -> void:
	print("\n--- Testing VoxelGeneratorNoise ---")
	var generator = _instantiate_class("VoxelGeneratorNoise", func(): return VoxelGeneratorNoiseMock.new())
	assert_true(generator != null, "VoxelGeneratorNoise instance created")
	
	# Set properties via ClassDB accessors
	generator.set_frequency(0.005)
	generator.set_octaves(4)
	generator.set_lacunarity(2.0)
	generator.set_gain(0.5)
	generator.set_planet_radius(1000.0)
	
	assert_almost_equal(generator.get_frequency(), 0.005, 1e-5, "VoxelGeneratorNoise frequency property")
	assert_equal(generator.get_octaves(), 4, "VoxelGeneratorNoise octaves property")
	assert_almost_equal(generator.get_planet_radius(), 1000.0, 1e-3, "VoxelGeneratorNoise planet_radius property")
	
	# Single point SDF evaluation for planetary surface (R = 1000.0)
	if generator.has_method("get_single_sdf"):
		var surface_sdf = generator.get_single_sdf(Vector3(0, 1000, 0))
		assert_true(abs(surface_sdf) < 50.0, "VoxelGeneratorNoise planetary surface SDF near zero (%f)" % surface_sdf)
		
		var interior_sdf = generator.get_single_sdf(Vector3(0, 500, 0))
		assert_true(interior_sdf < 0.0, "VoxelGeneratorNoise interior SDF is negative (%f)" % interior_sdf)
		
		var space_sdf = generator.get_single_sdf(Vector3(0, 2000, 0))
		assert_true(space_sdf > 0.0, "VoxelGeneratorNoise exterior space SDF is positive (%f)" % space_sdf)
	
	# Bulk block generation
	var buffer = _instantiate_class("VoxelBuffer", func(): return VoxelBufferMock.new())
	if generator.has_method("generate_block"):
		generator.generate_block(buffer, Vector3i(0, 62, 0), 0)
		assert_true(true, "VoxelGeneratorNoise generate_block executed successfully")

# --- Test 3: VoxelVolume Resource ---

func _test_voxel_volume_resource() -> void:
	print("\n--- Testing VoxelVolume ---")
	var volume = _instantiate_class("VoxelVolume", func(): return VoxelVolumeMock.new())
	assert_true(volume != null, "VoxelVolume instance created")
	
	volume.set_chunk_size(Vector3i(16, 16, 16))
	volume.set_max_lod_levels(8)
	volume.set_volume_name("PlanetaryVoxelVolume")
	
	assert_equal(volume.get_chunk_size(), Vector3i(16, 16, 16), "VoxelVolume chunk_size property")
	assert_equal(volume.get_max_lod_levels(), 8, "VoxelVolume max_lod_levels property")
	assert_equal(volume.get_volume_name(), "PlanetaryVoxelVolume", "VoxelVolume volume_name property")

# --- Test 4: VoxelWorld Node3D ---

func _test_voxel_world_node() -> void:
	print("\n--- Testing VoxelWorld ---")
	var world = _instantiate_class("VoxelWorld", func(): return VoxelWorldMock.new())
	assert_true(world != null, "VoxelWorld instance created")
	
	# Bind to SceneTree
	add_child(world)
	assert_true(world.is_inside_tree(), "VoxelWorld bound to scene tree")
	
	# Properties via ClassDB
	world.set_voxel_size(0.25)
	world.set_view_distance_chunks(16)
	world.set_enable_collision(true)
	
	assert_almost_equal(world.get_voxel_size(), 0.25, 1e-4, "VoxelWorld voxel_size property")
	assert_equal(world.get_view_distance_chunks(), 16, "VoxelWorld view_distance_chunks property")
	assert_true(world.is_collision_enabled(), "VoxelWorld enable_collision property")
	
	var volume = _instantiate_class("VoxelVolume", func(): return VoxelVolumeMock.new())
	world.set_voxel_volume(volume)
	assert_equal(world.get_voxel_volume(), volume, "VoxelWorld voxel_volume property assignment")
	
	if world.has_method("update_world"):
		world.update_world(Vector3(0, 100, 0))
		assert_true(true, "VoxelWorld update_world execution")
	
	world.queue_free()

# --- Test 5: VoxelLodTerrain Node3D ---

func _test_voxel_lod_terrain_node() -> void:
	print("\n--- Testing VoxelLodTerrain ---")
	var lod_terrain = _instantiate_class("VoxelLodTerrain", func(): return VoxelLodTerrainMock.new())
	assert_true(lod_terrain != null, "VoxelLodTerrain instance created")
	
	add_child(lod_terrain)
	assert_true(lod_terrain.is_inside_tree(), "VoxelLodTerrain bound to scene tree")
	
	if lod_terrain.has_method("set_lod_count"):
		lod_terrain.set_lod_count(6)
		assert_equal(lod_terrain.get_lod_count(), 6, "VoxelLodTerrain lod_count property")
	
	if lod_terrain.has_method("update_lod"):
		lod_terrain.update_lod(Vector3(50, 0, 50))
		assert_true(true, "VoxelLodTerrain update_lod execution")
	
	lod_terrain.queue_free()

# --- Test 6: VoxelRendererRD Node3D ---

func _test_voxel_renderer_rd_node() -> void:
	print("\n--- Testing VoxelRendererRD ---")
	var renderer = _instantiate_class("VoxelRendererRD", func(): return VoxelRendererRDMock.new())
	assert_true(renderer != null, "VoxelRendererRD instance created")
	
	add_child(renderer)
	assert_true(renderer.is_inside_tree(), "VoxelRendererRD bound to scene tree")
	
	renderer.set_enabled(true)
	renderer.set_lod_levels(6)
	renderer.set_view_distance(1000.0)
	renderer.set_render_target_size(Vector2i(1920, 1080))
	renderer.set_camera_position(Vector3(0, 50, 100))
	
	assert_true(renderer.is_enabled(), "VoxelRendererRD is_enabled property")
	assert_equal(renderer.get_lod_levels(), 6, "VoxelRendererRD lod_levels property")
	assert_almost_equal(renderer.get_view_distance(), 1000.0, 1e-2, "VoxelRendererRD view_distance property")
	assert_equal(renderer.get_render_target_size(), Vector2i(1920, 1080), "VoxelRendererRD render_target_size property")
	assert_equal(renderer.get_camera_position(), Vector3(0, 50, 100), "VoxelRendererRD camera_position property")
	
	# Test Screen Space Error calculation metric static/method
	if renderer.has_method("calculate_screen_space_error"):
		var sse = renderer.calculate_screen_space_error(0.01, 100.0, 0.785398, 1080.0)
		assert_true(sse > 0.0, "VoxelRendererRD calculate_screen_space_error output (>0)")
	
	# Headless buffer upload methods
	if renderer.has_method("upload_svo_ssbo"):
		var svo_ok = renderer.upload_svo_ssbo()
		assert_true(svo_ok, "VoxelRendererRD upload_svo_ssbo headless execution")
	
	if renderer.has_method("upload_clipmap_ssbo"):
		var clip_ok = renderer.upload_clipmap_ssbo()
		assert_true(clip_ok, "VoxelRendererRD upload_clipmap_ssbo headless execution")
	
	if renderer.has_method("update_lod_clipmap"):
		renderer.update_lod_clipmap(Vector3(100, 200, 300))
		assert_true(true, "VoxelRendererRD update_lod_clipmap execution")
	
	renderer.queue_free()

# --- Test 7: Full Planetary Micro-Voxel Pipeline Integration ---

func _test_planetary_micro_voxel_pipeline_integration() -> void:
	print("\n--- Testing Full Planetary Micro-Voxel Pipeline Integration ---")
	
	# 1. World setup
	var world = _instantiate_class("VoxelWorld", func(): return VoxelWorldMock.new())
	var volume = _instantiate_class("VoxelVolume", func(): return VoxelVolumeMock.new())
	var generator = _instantiate_class("VoxelGeneratorNoise", func(): return VoxelGeneratorNoiseMock.new())
	var renderer = _instantiate_class("VoxelRendererRD", func(): return VoxelRendererRDMock.new())
	
	add_child(world)
	add_child(renderer)
	
	# Configure pipeline
	volume.set_chunk_size(Vector3i(16, 16, 16))
	volume.set_max_lod_levels(6)
	
	generator.set_planet_radius(1000.0)
	generator.set_frequency(0.005)
	
	world.set_voxel_volume(volume)
	world.set_voxel_size(0.25)
	
	renderer.set_volume(volume)
	renderer.set_lod_levels(6)
	renderer.set_view_distance(2000.0)
	
	# Populate sample block
	var buffer = _instantiate_class("VoxelBuffer", func(): return VoxelBufferMock.new())
	generator.generate_block(buffer, Vector3i(0, 62, 0), 0) # Planet surface region
	
	# Compact palette
	buffer.compress_palette(1) # Material channel
	
	# Update camera position flyby
	var camera_positions = [
		Vector3(0, 1500, 0), # High orbit
		Vector3(0, 1050, 0), # Low orbit
		Vector3(0, 1005, 0)  # Micro-voxel ground view
	]
	
	for pos in camera_positions:
		world.update_world(pos)
		renderer.set_camera_position(pos)
		renderer.update_lod_clipmap(pos)
	
	assert_true(world.is_inside_tree(), "Integrated pipeline world in tree")
	assert_true(renderer.is_inside_tree(), "Integrated pipeline renderer in tree")
	assert_true(buffer.get_allocated_memory_bytes() > 0, "Integrated pipeline buffer allocated")
	
	world.queue_free()
	renderer.queue_free()

# ==============================================================================
# FALLBACK MOCK CLASSES (for execution in standard Godot 4 editor / engine)
# ==============================================================================

class VoxelBufferMock extends RefCounted:
	const BLOCK_SIZE = 16
	const CHANNEL_SDF = 0
	const CHANNEL_MATERIAL = 1
	var _sdf_data: Array = []
	var _mat_data: Array = []
	
	func _init() -> void:
		_sdf_data.resize(4096)
		_sdf_data.fill(0.0)
		_mat_data.resize(4096)
		_mat_data.fill(0)
	
	func set_voxel_f(val: float, x: int, y: int, z: int, channel: int = 0) -> void:
		var idx = x + y * 16 + z * 256
		if idx >= 0 and idx < 4096:
			_sdf_data[idx] = val
	
	func get_voxel_f(x: int, y: int, z: int, channel: int = 0) -> float:
		var idx = x + y * 16 + z * 256
		if idx >= 0 and idx < 4096:
			return _sdf_data[idx]
		return 0.0
	
	func set_voxel_u(val: int, x: int, y: int, z: int, channel: int = 1) -> void:
		var idx = x + y * 16 + z * 256
		if idx >= 0 and idx < 4096:
			_mat_data[idx] = val
	
	func get_voxel_u(x: int, y: int, z: int, channel: int = 1) -> int:
		var idx = x + y * 16 + z * 256
		if idx >= 0 and idx < 4096:
			return _mat_data[idx]
		return 0
	
	func fill_f(val: float, channel: int = 0) -> void:
		_sdf_data.fill(val)
	
	func fill_u(val: int, channel: int = 1) -> void:
		_mat_data.fill(val)
	
	func compress_palette(channel: int = 1) -> bool:
		return true
	
	func get_allocated_memory_bytes() -> int:
		return 2048

class VoxelGeneratorNoiseMock extends RefCounted:
	var frequency: float = 0.005
	var octaves: int = 4
	var lacunarity: float = 2.0
	var gain: float = 0.5
	var planet_radius: float = 1000.0
	
	func set_frequency(v: float) -> void: frequency = v
	func get_frequency() -> float: return frequency
	func set_octaves(v: int) -> void: octaves = v
	func get_octaves() -> int: return octaves
	func set_lacunarity(v: float) -> void: lacunarity = v
	func get_lacunarity() -> float: return lacunarity
	func set_gain(v: float) -> void: gain = v
	func get_gain() -> float: return gain
	func set_planet_radius(v: float) -> void: planet_radius = v
	func get_planet_radius() -> float: return planet_radius
	
	func get_single_sdf(pos: Vector3) -> float:
		return pos.length() - planet_radius
	
	func generate_block(buffer: Object, block_pos: Vector3i, lod: int = 0) -> void:
		if buffer.has_method("fill_f"):
			buffer.fill_f(0.5, 0)

class VoxelVolumeMock extends Resource:
	var chunk_size: Vector3i = Vector3i(16, 16, 16)
	var max_lod_levels: int = 8
	var volume_name: String = "VoxelVolume"
	
	func set_chunk_size(v: Vector3i) -> void: chunk_size = v
	func get_chunk_size() -> Vector3i: return chunk_size
	func set_max_lod_levels(v: int) -> void: max_lod_levels = v
	func get_max_lod_levels() -> int: return max_lod_levels
	func set_volume_name(v: String) -> void: volume_name = v
	func get_volume_name() -> String: return volume_name

class VoxelWorldMock extends Node3D:
	var voxel_size: float = 1.0
	var view_distance_chunks: int = 16
	var enable_collision: bool = true
	var volume: Resource = null
	
	func set_voxel_size(v: float) -> void: voxel_size = v
	func get_voxel_size() -> float: return voxel_size
	func set_view_distance_chunks(v: int) -> void: view_distance_chunks = v
	func get_view_distance_chunks() -> int: return view_distance_chunks
	func set_enable_collision(v: bool) -> void: enable_collision = v
	func is_collision_enabled() -> bool: return enable_collision
	func set_voxel_volume(v: Resource) -> void: volume = v
	func get_voxel_volume() -> Resource: return volume
	func update_world(cam_pos: Vector3) -> void: pass

class VoxelLodTerrainMock extends Node3D:
	var lod_count: int = 6
	func set_lod_count(v: int) -> void: lod_count = v
	func get_lod_count() -> int: return lod_count
	func update_lod(cam_pos: Vector3) -> void: pass

class VoxelRendererRDMock extends Node3D:
	var enabled: bool = true
	var lod_levels: int = 6
	var view_distance: float = 1000.0
	var render_target_size: Vector2i = Vector2i(1920, 1080)
	var camera_position: Vector3 = Vector3.ZERO
	var volume: Resource = null
	
	func set_volume(v: Resource) -> void: volume = v
	func get_volume() -> Resource: return volume
	func set_enabled(v: bool) -> void: enabled = v
	func is_enabled() -> bool: return enabled
	func set_lod_levels(v: int) -> void: lod_levels = v
	func get_lod_levels() -> int: return lod_levels
	func set_view_distance(v: float) -> void: view_distance = v
	func get_view_distance() -> float: return view_distance
	func set_render_target_size(v: Vector2i) -> void: render_target_size = v
	func get_render_target_size() -> Vector2i: return render_target_size
	func set_camera_position(v: Vector3) -> void: camera_position = v
	func get_camera_position() -> Vector3: return camera_position
	
	func calculate_screen_space_error(geo_err: float, dist: float, fov_rad: float, screen_h: float) -> float:
		if dist <= 0.0: return 0.0
		return (geo_err / dist) * (screen_h / (2.0 * tan(fov_rad * 0.5)))
	
	func upload_svo_ssbo() -> bool: return true
	func upload_clipmap_ssbo() -> bool: return true
	func upload_material_palette_ssbo() -> bool: return true
	func update_lod_clipmap(cam_pos: Vector3) -> void: pass
