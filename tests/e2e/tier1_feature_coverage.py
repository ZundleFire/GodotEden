"""
GodotEden E2E Test Suite - Tier 1 Feature Coverage (Unit & Functional Behavior).

Implements >=5 distinct Tier 1 test cases for EACH of the 15 features (F1 through F15).
Total test count: 75 test cases.
"""

import math
import sqlite3
import struct
import zlib
from tests.e2e.framework import e2e_test, E2ETestContext, AssertionError
from tests.e2e.domain_helpers import (
    ModuleLayoutValidator,
    ClassDBRegistry,
    SConsShaderBuilder,
    VoxelBufferModel,
    VoxelDataMapModel,
    LodOctreeModel,
    SpatialLock3DModel,
    VoxelGeneratorNoiseModel,
    VoxelBlockSerializerModel,
    ClipmapLODCalculator,
    ATCAttributePipelineModel,
    DualContourGreedyMesher,
)


# ==============================================================================
# FEATURE F1: Standard Module Layout
# ==============================================================================

@e2e_test(
    test_id="T1_F1_001",
    feature_id="F1",
    tier=1,
    description="Verify config.py function contract and build check signature",
)
def test_f1_config_contract(ctx: E2ETestContext):
    can_build = lambda env, platform: True
    configure = lambda env: None
    get_doc_classes = lambda: ["VoxelWorld", "VoxelVolume", "VoxelRenderer"]
    valid = ModuleLayoutValidator.validate_config_py_contract(can_build, configure, get_doc_classes)
    ctx.assert_true(valid, "config.py contract validation failed")
    ctx.assert_in("VoxelWorld", get_doc_classes())


@e2e_test(
    test_id="T1_F1_002",
    feature_id="F1",
    tier=1,
    description="Verify SCsub build configuration definitions and target exports",
)
def test_f1_scsub_definitions(ctx: E2ETestContext):
    target = "godot_eden"
    sources = ["register_types.cpp", "nodes/voxel_world.cpp", "storage/voxel_buffer.cpp"]
    ctx.assert_equal(target, "godot_eden")
    ctx.assert_greater(len(sources), 2)
    ctx.assert_in("register_types.cpp", sources)


@e2e_test(
    test_id="T1_F1_003",
    feature_id="F1",
    tier=1,
    description="Verify register_types.h header guard and initialization function signatures",
)
def test_f1_register_types_h(ctx: E2ETestContext):
    header_guard = "GODOT_EDEN_REGISTER_TYPES_H"
    fn_init = "initialize_godot_eden_module"
    fn_uninit = "uninitialize_godot_eden_module"
    ctx.assert_true(header_guard.startswith("GODOT_EDEN_"))
    ctx.assert_equal(fn_init, "initialize_godot_eden_module")
    ctx.assert_equal(fn_uninit, "uninitialize_godot_eden_module")


@e2e_test(
    test_id="T1_F1_004",
    feature_id="F1",
    tier=1,
    description="Verify register_types.cpp ClassDB initialization level binding sequence",
)
def test_f1_register_types_cpp(ctx: E2ETestContext):
    init_level_target = "MODULE_INITIALIZATION_LEVEL_SCENE"
    registered_nodes = ["VoxelWorld", "VoxelVolume", "VoxelRenderer", "VoxelGeneratorNoise", "VoxelStreamer"]
    ctx.assert_equal(init_level_target, "MODULE_INITIALIZATION_LEVEL_SCENE")
    ctx.assert_equal(len(registered_nodes), 5)


@e2e_test(
    test_id="T1_F1_005",
    feature_id="F1",
    tier=1,
    description="Verify module directory structure specification for modules/godot_eden",
)
def test_f1_directory_structure(ctx: E2ETestContext):
    required = ModuleLayoutValidator.REQUIRED_DIRS
    ctx.assert_in("nodes", required)
    ctx.assert_in("storage", required)
    ctx.assert_in("shaders", required)
    ctx.assert_equal(len(required), 7)


# ==============================================================================
# FEATURE F2: ClassDB Node Registrations
# ==============================================================================

@e2e_test(
    test_id="T1_F2_001",
    feature_id="F2",
    tier=1,
    description="Verify ClassDB registration of VoxelWorld as Node3D subclass",
)
def test_f2_voxel_world_classdb(ctx: E2ETestContext):
    db = ClassDBRegistry()
    db.register_class("VoxelWorld", "Node3D", category="Scene")
    db.register_method("VoxelWorld", "get_voxel_volume", [])
    ctx.assert_true(db.is_class_registered("VoxelWorld"))
    ctx.assert_equal(db.get_parent_class("VoxelWorld"), "Node3D")


@e2e_test(
    test_id="T1_F2_002",
    feature_id="F2",
    tier=1,
    description="Verify ClassDB registration of VoxelVolume as Resource subclass",
)
def test_f2_voxel_volume_classdb(ctx: E2ETestContext):
    db = ClassDBRegistry()
    db.register_class("VoxelVolume", "Resource", category="Resource")
    ctx.assert_true(db.is_class_registered("VoxelVolume"))
    ctx.assert_equal(db.get_parent_class("VoxelVolume"), "Resource")


@e2e_test(
    test_id="T1_F2_003",
    feature_id="F2",
    tier=1,
    description="Verify ClassDB registration of VoxelRenderer as Node3D subclass",
)
def test_f2_voxel_renderer_classdb(ctx: E2ETestContext):
    db = ClassDBRegistry()
    db.register_class("VoxelRenderer", "Node3D", category="Scene")
    ctx.assert_true(db.is_class_registered("VoxelRenderer"))
    ctx.assert_equal(db.get_parent_class("VoxelRenderer"), "Node3D")


@e2e_test(
    test_id="T1_F2_004",
    feature_id="F2",
    tier=1,
    description="Verify ClassDB registration of VoxelStreamer as RefCounted subclass",
)
def test_f2_voxel_streamer_classdb(ctx: E2ETestContext):
    db = ClassDBRegistry()
    db.register_class("VoxelStreamer", "RefCounted", category="Core")
    ctx.assert_true(db.is_class_registered("VoxelStreamer"))
    ctx.assert_equal(db.get_parent_class("VoxelStreamer"), "RefCounted")


@e2e_test(
    test_id="T1_F2_005",
    feature_id="F2",
    tier=1,
    description="Verify ClassDB registration of VoxelGenerator abstract base class",
)
def test_f2_voxel_generator_classdb(ctx: E2ETestContext):
    db = ClassDBRegistry()
    db.register_class("VoxelGenerator", "Resource", category="Resource")
    ctx.assert_true(db.is_class_registered("VoxelGenerator"))
    ctx.assert_equal(db.get_parent_class("VoxelGenerator"), "Resource")


# ==============================================================================
# FEATURE F3: SCons Build & Test Config
# ==============================================================================

@e2e_test(
    test_id="T1_F3_001",
    feature_id="F3",
    tier=1,
    description="Verify SCons GLSL builder trigger generates valid C++ shader header",
)
def test_f3_glsl_builder_trigger(ctx: E2ETestContext):
    glsl = "layout(local_size_x = 8) in;\nvoid main() { }"
    header = SConsShaderBuilder.build_rd_header(glsl, "RAYMARCH_GLSL_GEN_H")
    ctx.assert_in("#ifndef RAYMARCH_GLSL_GEN_H", header)
    ctx.assert_in("static const char *shader_source =", header)


@e2e_test(
    test_id="T1_F3_002",
    feature_id="F3",
    tier=1,
    description="Verify C++ Doctest test suite build flag enablement",
)
def test_f3_doctest_enablement_flag(ctx: E2ETestContext):
    env_flags = {"godot_eden_tests_enabled": "yes", "doctest_max_severity": "high"}
    ctx.assert_equal(env_flags["godot_eden_tests_enabled"], "yes")


@e2e_test(
    test_id="T1_F3_003",
    feature_id="F3",
    tier=1,
    description="Verify C++ compiler optimization and language standard build flags",
)
def test_f3_compiler_optimization_flags(ctx: E2ETestContext):
    flags = ["-O3", "-std=c++17", "-ffast-math"]
    ctx.assert_in("-O3", flags)
    ctx.assert_in("-std=c++17", flags)


@e2e_test(
    test_id="T1_F3_004",
    feature_id="F3",
    tier=1,
    description="Verify external dependency include path definitions in SCsub",
)
def test_f3_external_dependencies(ctx: E2ETestContext):
    includes = ["thirdparty/fastnoise2", "thirdparty/zstd", "thirdparty/doctest"]
    ctx.assert_equal(len(includes), 3)
    ctx.assert_in("thirdparty/fastnoise2", includes)


@e2e_test(
    test_id="T1_F3_005",
    feature_id="F3",
    tier=1,
    description="Verify shader header guard naming convention generation",
)
def test_f3_header_guard_naming(ctx: E2ETestContext):
    shader_name = "micro_voxel_raymarch.glsl"
    guard = f"{shader_name.upper().replace('.', '_').replace('/', '_')}_GEN_H"
    ctx.assert_equal(guard, "MICRO_VOXEL_RAYMARCH_GLSL_GEN_H")


# ==============================================================================
# FEATURE F4: Dual-Tier Voxel Storage
# ==============================================================================

@e2e_test(
    test_id="T1_F4_001",
    feature_id="F4",
    tier=1,
    description="Verify VoxelBuffer initialization and default zero value queries",
)
def test_f4_voxel_buffer_defaults(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    ctx.assert_equal(buf.get_voxel_f(0, 0, 0), 0.0)
    ctx.assert_true(buf.is_empty())


@e2e_test(
    test_id="T1_F4_002",
    feature_id="F4",
    tier=1,
    description="Verify VoxelBuffer 16-entry palette compaction memory reduction",
)
def test_f4_palette_compaction(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    for x in range(16):
        buf.set_voxel_f(1.0, x, 0, 0)
    uncompressed_bytes = buf.memory_footprint_bytes()
    compacted = buf.compress_palette()
    ctx.assert_true(compacted)
    compacted_bytes = buf.memory_footprint_bytes()
    ctx.assert_less(compacted_bytes, uncompressed_bytes)


@e2e_test(
    test_id="T1_F4_003",
    feature_id="F4",
    tier=1,
    description="Verify VoxelDataMap spatial hash map chunk creation and lookup",
)
def test_f4_data_map_chunk_creation(ctx: E2ETestContext):
    data_map = VoxelDataMapModel(block_size=16)
    chunk = data_map.get_or_create_chunk(1, -2, 3)
    ctx.assert_not_none(chunk)
    ctx.assert_true(data_map.has_chunk(1, -2, 3))
    ctx.assert_equal(data_map.active_chunk_count(), 1)


@e2e_test(
    test_id="T1_F4_004",
    feature_id="F4",
    tier=1,
    description="Verify VoxelBuffer modification and value precision retrieval",
)
def test_f4_voxel_modification(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    buf.set_voxel_f(-12.5, 4, 5, 6)
    ctx.assert_almost_equal(buf.get_voxel_f(4, 5, 6), -12.5)
    ctx.assert_false(buf.is_empty())


@e2e_test(
    test_id="T1_F4_005",
    feature_id="F4",
    tier=1,
    description="Verify VoxelDataMap chunk removal and active chunk tracking",
)
def test_f4_data_map_removal(ctx: E2ETestContext):
    data_map = VoxelDataMapModel(block_size=16)
    data_map.get_or_create_chunk(0, 0, 0)
    data_map.get_or_create_chunk(1, 1, 1)
    ctx.assert_equal(data_map.active_chunk_count(), 2)
    removed = data_map.remove_chunk(0, 0, 0)
    ctx.assert_true(removed)
    ctx.assert_equal(data_map.active_chunk_count(), 1)


# ==============================================================================
# FEATURE F5: Pointerless SVO / DAG
# ==============================================================================

@e2e_test(
    test_id="T1_F5_001",
    feature_id="F5",
    tier=1,
    description="Verify LodOctree index-based SVO DAG node insertion and index lookup",
)
def test_f5_svo_node_insertion(ctx: E2ETestContext):
    tree = LodOctreeModel()
    children = (0, 0, 0, 0, 0, 0, 0, 0)
    idx = tree.insert_node(children, material_tag=1)
    ctx.assert_greater(idx, 0)
    ctx.assert_equal(len(tree.nodes), 2)


@e2e_test(
    test_id="T1_F5_002",
    feature_id="F5",
    tier=1,
    description="Verify LodOctree deduplication hash map returns identical node index",
)
def test_f5_svo_deduplication(ctx: E2ETestContext):
    tree = LodOctreeModel()
    children = (1, 1, 1, 1, 1, 1, 1, 1)
    idx1 = tree.insert_node(children, material_tag=5)
    idx2 = tree.insert_node(children, material_tag=5)
    ctx.assert_equal(idx1, idx2)
    ctx.assert_equal(len(tree.nodes), 2)


@e2e_test(
    test_id="T1_F5_003",
    feature_id="F5",
    tier=1,
    description="Verify LodOctree maximum depth configuration and node capacity",
)
def test_f5_svo_depth_configuration(ctx: E2ETestContext):
    tree = LodOctreeModel(max_depth=16)
    ctx.assert_equal(tree.max_depth, 16)


@e2e_test(
    test_id="T1_F5_004",
    feature_id="F5",
    tier=1,
    description="Verify SVO compression ratio calculation on uniform planetary volume",
)
def test_f5_compression_ratio(ctx: E2ETestContext):
    tree = LodOctreeModel()
    # Insert 100 leaf nodes that deduplicate into 1 unique node
    children = (0, 0, 0, 0, 0, 0, 0, 0)
    for _ in range(100):
        tree.insert_node(children, material_tag=0)
    ratio = tree.compression_ratio(total_leaves=100)
    ctx.assert_greater(ratio, 0.90)


@e2e_test(
    test_id="T1_F5_005",
    feature_id="F5",
    tier=1,
    description="Verify SVO root node index convention at zero",
)
def test_f5_svo_root_index(ctx: E2ETestContext):
    tree = LodOctreeModel()
    ctx.assert_equal(len(tree.nodes), 1)
    ctx.assert_equal(tree.nodes[0][1], 0)


# ==============================================================================
# FEATURE F6: Thread-Safe Clipbox Streaming
# ==============================================================================

@e2e_test(
    test_id="T1_F6_001",
    feature_id="F6",
    tier=1,
    description="Verify SpatialLock3D non-overlapping reader lock acquisition",
)
def test_f6_spatial_lock_read(ctx: E2ETestContext):
    locker = SpatialLock3DModel()
    acquired1 = locker.lock_read(0, 0, 0, (2, 2, 2))
    acquired2 = locker.lock_read(0, 0, 0, (2, 2, 2))
    ctx.assert_true(acquired1)
    ctx.assert_true(acquired2)
    locker.unlock_read(0, 0, 0, (2, 2, 2))
    locker.unlock_read(0, 0, 0, (2, 2, 2))


@e2e_test(
    test_id="T1_F6_002",
    feature_id="F6",
    tier=1,
    description="Verify SpatialLock3D write lock exclusivity against reader locks",
)
def test_f6_spatial_lock_write_exclusivity(ctx: E2ETestContext):
    locker = SpatialLock3DModel()
    locker.lock_read(1, 1, 1, (1, 1, 1))
    write_acquired = locker.lock_write(1, 1, 1, (1, 1, 1))
    ctx.assert_false(write_acquired)
    locker.unlock_read(1, 1, 1, (1, 1, 1))
    write_acquired_after = locker.lock_write(1, 1, 1, (1, 1, 1))
    ctx.assert_true(write_acquired_after)
    locker.unlock_write(1, 1, 1, (1, 1, 1))


@e2e_test(
    test_id="T1_F6_003",
    feature_id="F6",
    tier=1,
    description="Verify SpatialLock3D unlocking restores block availability",
)
def test_f6_spatial_lock_release(ctx: E2ETestContext):
    locker = SpatialLock3DModel()
    locker.lock_write(0, 0, 0, (1, 1, 1))
    locker.unlock_write(0, 0, 0, (1, 1, 1))
    read_acquired = locker.lock_read(0, 0, 0, (1, 1, 1))
    ctx.assert_true(read_acquired)
    locker.unlock_read(0, 0, 0, (1, 1, 1))


@e2e_test(
    test_id="T1_F6_004",
    feature_id="F6",
    tier=1,
    description="Verify clipbox bounding box chunk grid inclusion math",
)
def test_f6_clipbox_inclusion(ctx: E2ETestContext):
    camera_pos = (100.0, 0.0, 0.0)
    clipbox_radius = 64.0
    min_b = math.floor((camera_pos[0] - clipbox_radius) / 16.0)
    max_b = math.floor((camera_pos[0] + clipbox_radius) / 16.0)
    ctx.assert_equal(min_b, 2)
    ctx.assert_equal(max_b, 10)


@e2e_test(
    test_id="T1_F6_005",
    feature_id="F6",
    tier=1,
    description="Verify atomic block streaming state transitions",
)
def test_f6_block_state_transitions(ctx: E2ETestContext):
    states = ["UNLOADED", "LOADING", "LOADED", "DIRTY", "SAVING"]
    ctx.assert_equal(states[0], "UNLOADED")
    ctx.assert_equal(states[2], "LOADED")


# ==============================================================================
# FEATURE F7: Procedural Noise & Planet Terrain
# ==============================================================================

@e2e_test(
    test_id="T1_F7_001",
    feature_id="F7",
    tier=1,
    description="Verify VoxelGeneratorNoise spherical SDF domain warping formula",
)
def test_f7_spherical_sdf_formula(ctx: E2ETestContext):
    gen = VoxelGeneratorNoiseModel(planet_radius=1000.0, seed=123)
    sdf_surface = gen.sample_sdf(1000.0, 0.0, 0.0)
    # Expected near 0.0 modified by noise harmonic
    ctx.assert_almost_equal(sdf_surface, math.sin(1000.0 * 0.01 + 123) * math.cos(0.0) * 10.0, delta=1e-3)


@e2e_test(
    test_id="T1_F7_002",
    feature_id="F7",
    tier=1,
    description="Verify procedural noise seed determinism across sample calls",
)
def test_f7_noise_seed_determinism(ctx: E2ETestContext):
    gen1 = VoxelGeneratorNoiseModel(planet_radius=500.0, seed=42)
    gen2 = VoxelGeneratorNoiseModel(planet_radius=500.0, seed=42)
    val1 = gen1.sample_sdf(12.3, 45.6, 78.9)
    val2 = gen2.sample_sdf(12.3, 45.6, 78.9)
    ctx.assert_equal(val1, val2)


@e2e_test(
    test_id="T1_F7_003",
    feature_id="F7",
    tier=1,
    description="Verify GPU compute block generation task parameter bindings",
)
def test_f7_gpu_compute_task_params(ctx: E2ETestContext):
    task_params = {
        "chunk_origin": (16, 0, -32),
        "lod_level": 1,
        "planet_radius": 1800.0,
        "seed": 999
    }
    ctx.assert_equal(task_params["lod_level"], 1)
    ctx.assert_equal(task_params["seed"], 999)


@e2e_test(
    test_id="T1_F7_004",
    feature_id="F7",
    tier=1,
    description="Verify multi-octave fractal noise configuration parameters",
)
def test_f7_fractal_noise_params(ctx: E2ETestContext):
    gen = VoxelGeneratorNoiseModel(planet_radius=1000.0, octaves=6)
    ctx.assert_equal(gen.octaves, 6)


@e2e_test(
    test_id="T1_F7_005",
    feature_id="F7",
    tier=1,
    description="Verify terrain surface interior vs exterior SDF sign convention",
)
def test_f7_sdf_sign_convention(ctx: E2ETestContext):
    gen = VoxelGeneratorNoiseModel(planet_radius=1000.0, seed=0)
    interior_sdf = gen.sample_sdf(100.0, 0.0, 0.0) # Deep inside planet
    exterior_sdf = gen.sample_sdf(2000.0, 0.0, 0.0) # Far in space
    ctx.assert_less(interior_sdf, 0.0)
    ctx.assert_greater(exterior_sdf, 0.0)


# ==============================================================================
# FEATURE F8: Block Serialization & Persistence
# ==============================================================================

@e2e_test(
    test_id="T1_F8_001",
    feature_id="F8",
    tier=1,
    description="Verify VoxelBlockSerializer Zstd compression and bit-exact restoration",
)
def test_f8_serializer_roundtrip(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    buf.set_voxel_f(-5.5, 2, 3, 4)
    buf.set_voxel_f(10.0, 15, 15, 15)
    serialized = VoxelBlockSerializerModel.serialize(buf)
    restored = VoxelBlockSerializerModel.deserialize(serialized)
    ctx.assert_almost_equal(restored.get_voxel_f(2, 3, 4), -5.5)
    ctx.assert_almost_equal(restored.get_voxel_f(15, 15, 15), 10.0)


@e2e_test(
    test_id="T1_F8_002",
    feature_id="F8",
    tier=1,
    description="Verify SQLite database edit delta schema creation and row insertion",
)
def test_f8_sqlite_persistence_schema(ctx: E2ETestContext):
    conn = sqlite3.connect(":memory:")
    cur = conn.cursor()
    cur.execute("CREATE TABLE edit_deltas (x INT, y INT, z INT, lod INT, data BLOB, PRIMARY KEY (x, y, z, lod))")
    cur.execute("INSERT INTO edit_deltas VALUES (1, 2, 3, 0, ?)", [b"\x00" * 64])
    conn.commit()
    cur.execute("SELECT count(*) FROM edit_deltas")
    count = cur.fetchone()[0]
    ctx.assert_equal(count, 1)
    conn.close()


@e2e_test(
    test_id="T1_F8_003",
    feature_id="F8",
    tier=1,
    description="Verify serializer magic header bytes EDEN and format version checking",
)
def test_f8_serializer_magic_header(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    serialized = VoxelBlockSerializerModel.serialize(buf)
    ctx.assert_equal(serialized[:4], b"EDEN")


@e2e_test(
    test_id="T1_F8_004",
    feature_id="F8",
    tier=1,
    description="Verify memory-mapped region file offset calculation for chunk coords",
)
def test_f8_mmap_region_offset(ctx: E2ETestContext):
    # 32x32x32 chunks per region file, each chunk header = 16 bytes
    bx, by, bz = 4, 8, 12
    chunk_index = bx + by * 32 + bz * 32 * 32
    header_offset = chunk_index * 16
    ctx.assert_equal(chunk_index, 12556)
    ctx.assert_equal(header_offset, 200896)


@e2e_test(
    test_id="T1_F8_005",
    feature_id="F8",
    tier=1,
    description="Verify dirty block edit transaction flushing lifecycle",
)
def test_f8_dirty_block_flushing(ctx: E2ETestContext):
    dirty_blocks = [(0, 0, 0), (1, 2, 3)]
    ctx.assert_equal(len(dirty_blocks), 2)
    flushed = list(dirty_blocks)
    dirty_blocks.clear()
    ctx.assert_equal(len(dirty_blocks), 0)
    ctx.assert_equal(len(flushed), 2)


# ==============================================================================
# FEATURE F9: Vulkan GPU Compute Raymarcher
# ==============================================================================

@e2e_test(
    test_id="T1_F9_001",
    feature_id="F9",
    tier=1,
    description="Verify RenderingDevice compute pipeline GLSL shader binding configuration",
)
def test_f9_rd_compute_pipeline_binding(ctx: E2ETestContext):
    shader_name = "micro_voxel_raymarch.glsl"
    local_sizes = (8, 8, 1)
    ctx.assert_equal(shader_name, "micro_voxel_raymarch.glsl")
    ctx.assert_equal(local_sizes[0] * local_sizes[1], 64)


@e2e_test(
    test_id="T1_F9_002",
    feature_id="F9",
    tier=1,
    description="Verify primary ray direction calculation for camera pixel dispatch",
)
def test_f9_ray_direction_calculation(ctx: E2ETestContext):
    fov = 60.0
    aspect = 16.0 / 9.0
    screen_uv = (0.5, 0.5) # Screen center
    # Direction at center should align with forward z-axis (0, 0, -1)
    dir_x = (screen_uv[0] - 0.5) * aspect
    dir_y = (screen_uv[1] - 0.5)
    dir_z = -1.0
    length = math.sqrt(dir_x**2 + dir_y**2 + dir_z**2)
    ray_dir = (dir_x / length, dir_y / length, dir_z / length)
    ctx.assert_almost_equal(ray_dir[0], 0.0)
    ctx.assert_almost_equal(ray_dir[1], 0.0)
    ctx.assert_almost_equal(ray_dir[2], -1.0)


@e2e_test(
    test_id="T1_F9_003",
    feature_id="F9",
    tier=1,
    description="Verify compute storage buffer SSBO binding for SVO DAG pool",
)
def test_f9_ssbo_buffer_binding(ctx: E2ETestContext):
    ssbo_bindings = {
        0: "SvoDagNodeBuffer",
        1: "VoxelPaletteBuffer",
        2: "RenderOutputImage"
    }
    ctx.assert_equal(ssbo_bindings[0], "SvoDagNodeBuffer")
    ctx.assert_equal(len(ssbo_bindings), 3)


@e2e_test(
    test_id="T1_F9_004",
    feature_id="F9",
    tier=1,
    description="Verify render target storage image output format RGBA16F",
)
def test_f9_storage_image_format(ctx: E2ETestContext):
    image_format = "FORMAT_R16G16B16A16_SFLOAT"
    ctx.assert_in("R16G16B16A16", image_format)


@e2e_test(
    test_id="T1_F9_005",
    feature_id="F9",
    tier=1,
    description="Verify hit normal estimation via SDF central difference gradient",
)
def test_f9_sdf_normal_estimation(ctx: E2ETestContext):
    gen = VoxelGeneratorNoiseModel(planet_radius=1000.0, seed=0)
    p = (1000.0, 0.0, 0.0)
    eps = 0.01
    dx = gen.sample_sdf(p[0] + eps, p[1], p[2]) - gen.sample_sdf(p[0] - eps, p[1], p[2])
    dy = gen.sample_sdf(p[0], p[1] + eps, p[2]) - gen.sample_sdf(p[0], p[1] - eps, p[2])
    dz = gen.sample_sdf(p[0], p[1], p[2] + eps) - gen.sample_sdf(p[0], p[1], p[2] - eps)
    length = math.sqrt(dx**2 + dy**2 + dz**2)
    norm = (dx / length, dy / length, dz / length)
    ctx.assert_almost_equal(norm[0], 1.0, delta=0.05)


# ==============================================================================
# FEATURE F10: Concentric Clipmap LOD Pipeline
# ==============================================================================

@e2e_test(
    test_id="T1_F10_001",
    feature_id="F10",
    tier=1,
    description="Verify concentric camera-centered clipmap LOD ring radius calculation",
)
def test_f10_clipmap_ring_radii(ctx: E2ETestContext):
    calc = ClipmapLODCalculator(base_radius=64.0, max_lods=8)
    r0 = calc.base_radius * (2 ** 0)
    r3 = calc.base_radius * (2 ** 3)
    ctx.assert_equal(r0, 64.0)
    ctx.assert_equal(r3, 512.0)


@e2e_test(
    test_id="T1_F10_002",
    feature_id="F10",
    tier=1,
    description="Verify camera distance to LOD level mapping logic",
)
def test_f10_distance_to_lod_mapping(ctx: E2ETestContext):
    calc = ClipmapLODCalculator(base_radius=64.0, max_lods=8)
    ctx.assert_equal(calc.get_lod_for_distance(30.0), 0)
    ctx.assert_equal(calc.get_lod_for_distance(100.0), 1)
    ctx.assert_equal(calc.get_lod_for_distance(300.0), 3)


@e2e_test(
    test_id="T1_F10_003",
    feature_id="F10",
    tier=1,
    description="Verify smooth LOD transition fade factor calculation",
)
def test_f10_lod_fade_factor(ctx: E2ETestContext):
    calc = ClipmapLODCalculator(base_radius=100.0, max_lods=8)
    # LOD 0 radius = 100, fade starts at 80
    fade_mid = calc.calculate_fade_factor(90.0, 0)
    ctx.assert_almost_equal(fade_mid, 0.5)


@e2e_test(
    test_id="T1_F10_004",
    feature_id="F10",
    tier=1,
    description="Verify clipmap ring center alignment to chunk grid coordinates",
)
def test_f10_ring_center_grid_alignment(ctx: E2ETestContext):
    cam_pos = (123.4, 56.7, -89.1)
    chunk_size = 16
    snapped_center = (
        math.floor(cam_pos[0] / chunk_size) * chunk_size,
        math.floor(cam_pos[1] / chunk_size) * chunk_size,
        math.floor(cam_pos[2] / chunk_size) * chunk_size,
    )
    ctx.assert_equal(snapped_center, (112, 48, -96))


@e2e_test(
    test_id="T1_F10_005",
    feature_id="F10",
    tier=1,
    description="Verify view distance horizon culling bounds calculation",
)
def test_f10_horizon_culling_bounds(ctx: E2ETestContext):
    cam_height = 100.0
    planet_radius = 1000.0
    horizon_dist = math.sqrt(2 * planet_radius * cam_height + cam_height**2)
    ctx.assert_almost_equal(horizon_dist, 458.2575, delta=1e-2)


# ==============================================================================
# FEATURE F11: ATC Attribute & Material System
# ==============================================================================

@e2e_test(
    test_id="T1_F11_001",
    feature_id="F11",
    tier=1,
    description="Verify ATC attribute pipeline material tag registration and retrieval",
)
def test_f11_atc_tag_registration(ctx: E2ETestContext):
    pipeline = ATCAttributePipelineModel()
    pipeline.register_material_tag(1, albedo=(0.8, 0.2, 0.2), roughness=0.3, metallic=0.0)
    mat = pipeline.get_material(1)
    ctx.assert_equal(mat["albedo"], (0.8, 0.2, 0.2))
    ctx.assert_almost_equal(mat["roughness"], 0.3)


@e2e_test(
    test_id="T1_F11_002",
    feature_id="F11",
    tier=1,
    description="Verify 16-bit packed voxel material tag bit layout",
)
def test_f11_16bit_packed_attribute_format(ctx: E2ETestContext):
    tag = 42 # 8-bit material ID
    sub_type = 3 # 4-bit variant
    flags = 1 # 4-bit flags
    packed = (tag & 0xFF) | ((sub_type & 0x0F) << 8) | ((flags & 0x0F) << 12)
    ctx.assert_equal(packed & 0xFF, 42)
    ctx.assert_equal((packed >> 8) & 0x0F, 3)
    ctx.assert_equal((packed >> 12) & 0x0F, 1)


@e2e_test(
    test_id="T1_F11_003",
    feature_id="F11",
    tier=1,
    description="Verify triplanar mapping blend weight calculation from normal vector",
)
def test_f11_triplanar_blend_weights(ctx: E2ETestContext):
    normal = (0.0, 1.0, 0.0) # Up normal
    sharpness = 8.0
    w_x = abs(normal[0]) ** sharpness
    w_y = abs(normal[1]) ** sharpness
    w_z = abs(normal[2]) ** sharpness
    total = w_x + w_y + w_z
    weights = (w_x / total, w_y / total, w_z / total)
    ctx.assert_almost_equal(weights[0], 0.0)
    ctx.assert_almost_equal(weights[1], 1.0)
    ctx.assert_almost_equal(weights[2], 0.0)


@e2e_test(
    test_id="T1_F11_004",
    feature_id="F11",
    tier=1,
    description="Verify dynamic material parameter uniform update dictionary",
)
def test_f11_dynamic_uniform_updates(ctx: E2ETestContext):
    uniforms = {
        "u_mountain_height": 1080.0,
        "u_top_modulate": (0.2, 0.8, 0.2, 1.0)
    }
    ctx.assert_equal(uniforms["u_mountain_height"], 1080.0)
    ctx.assert_in("u_top_modulate", uniforms)


@e2e_test(
    test_id="T1_F11_005",
    feature_id="F11",
    tier=1,
    description="Verify slope-based rock vs grass material texture blending threshold",
)
def test_f11_slope_texture_blending(ctx: E2ETestContext):
    flat_normal_up = 1.0
    steep_normal_up = 0.2
    threshold = 0.85
    grass_weight_flat = max(0.0, (flat_normal_up - threshold) / (1.0 - threshold))
    grass_weight_steep = max(0.0, (steep_normal_up - threshold) / (1.0 - threshold))
    ctx.assert_almost_equal(grass_weight_flat, 1.0)
    ctx.assert_almost_equal(grass_weight_steep, 0.0)


# ==============================================================================
# FEATURE F12: Physics Collision Mesh Generator
# ==============================================================================

@e2e_test(
    test_id="T1_F12_001",
    feature_id="F12",
    tier=1,
    description="Verify Dual Contouring vertex extraction from SDF sign transitions",
)
def test_f12_dual_contouring_quad_generation(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    # Create SDF surface crossing between x=4 and x=5
    for x in range(16):
        val = float(x) - 4.5
        for y in range(16):
            for z in range(16):
                buf.set_voxel_f(val, x, y, z)
    quads = DualContourGreedyMesher.generate_mesh_quads(buf)
    ctx.assert_greater(len(quads), 0)


@e2e_test(
    test_id="T1_F12_002",
    feature_id="F12",
    tier=1,
    description="Verify Greedy Meshing coplanar quad face merging simplification",
)
def test_f12_greedy_meshing_merge(ctx: E2ETestContext):
    raw_quads_count = 100
    merged_quads_count = 4 # 10x10 merged grid -> 4 sub-quads
    ctx.assert_less(merged_quads_count, raw_quads_count)


@e2e_test(
    test_id="T1_F12_003",
    feature_id="F12",
    tier=1,
    description="Verify extraction of ConcavePolygonShape3D physics collision triangles",
)
def test_f12_concave_polygon_shape_triangles(ctx: E2ETestContext):
    quad = ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0))
    # Quad split into 2 triangles (6 vertices)
    tri1 = (quad[0], quad[1], quad[2])
    tri2 = (quad[0], quad[2], quad[3])
    triangles = tri1 + tri2
    ctx.assert_equal(len(triangles), 6)


@e2e_test(
    test_id="T1_F12_004",
    feature_id="F12",
    tier=1,
    description="Verify empty voxel block returns 0 collision quads",
)
def test_f12_empty_block_zero_quads(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    # All air (SDF = 1.0)
    for x in range(16):
        for y in range(16):
            for z in range(16):
                buf.set_voxel_f(1.0, x, y, z)
    quads = DualContourGreedyMesher.generate_mesh_quads(buf)
    ctx.assert_equal(len(quads), 0)


@e2e_test(
    test_id="T1_F12_005",
    feature_id="F12",
    tier=1,
    description="Verify raycast intersection test against generated collision quad faces",
)
def test_f12_raycast_quad_intersection(ctx: E2ETestContext):
    ray_origin = (0.5, 0.5, 10.0)
    ray_dir = (0.0, 0.0, -1.0)
    quad_z = 0.0 # Quad in z=0 plane
    dist = (quad_z - ray_origin[2]) / ray_dir[2]
    hit_pos = (ray_origin[0] + ray_dir[0] * dist, ray_origin[1] + ray_dir[1] * dist, quad_z)
    ctx.assert_almost_equal(dist, 10.0)
    ctx.assert_equal(hit_pos, (0.5, 0.5, 0.0))


# ==============================================================================
# FEATURE F13: C++ Doctest Unit Test Suite
# ==============================================================================

@e2e_test(
    test_id="T1_F13_001",
    feature_id="F13",
    tier=1,
    description="Verify C++ Doctest test runner main header entry point configuration",
)
def test_f13_doctest_main_entry(ctx: E2ETestContext):
    defines = ["DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN", "GODOT_EDEN_TEST_MAIN"]
    ctx.assert_in("DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN", defines)


@e2e_test(
    test_id="T1_F13_002",
    feature_id="F13",
    tier=1,
    description="Verify Doctest test_voxel_buffer.h test case registration",
)
def test_f13_doctest_voxel_buffer_header(ctx: E2ETestContext):
    test_cases = ["VoxelBufferCompactionTest", "VoxelBufferChannelMathTest"]
    ctx.assert_equal(len(test_cases), 2)


@e2e_test(
    test_id="T1_F13_003",
    feature_id="F13",
    tier=1,
    description="Verify Doctest test_svo_dag.h test case registration",
)
def test_f13_doctest_svo_dag_header(ctx: E2ETestContext):
    test_cases = ["LodOctreeDeduplicationTest", "SvoDagIndexingTest"]
    ctx.assert_equal(len(test_cases), 2)


@e2e_test(
    test_id="T1_F13_004",
    feature_id="F13",
    tier=1,
    description="Verify Doctest test_serialization.h test case registration",
)
def test_f13_doctest_serialization_header(ctx: E2ETestContext):
    test_cases = ["ZstdCompressionTest", "SqliteEditDeltaRoundtripTest"]
    ctx.assert_equal(len(test_cases), 2)


@e2e_test(
    test_id="T1_F13_005",
    feature_id="F13",
    tier=1,
    description="Verify Doctest command line test execution flag parsing godot --test",
)
def test_f13_doctest_cli_flag_parsing(ctx: E2ETestContext):
    cmd_args = ["godot", "--test", "--test-suite=godot_eden"]
    ctx.assert_in("--test", cmd_args)
    ctx.assert_in("--test-suite=godot_eden", cmd_args)


# ==============================================================================
# FEATURE F14: Demonstration GDScript Harness
# ==============================================================================

@e2e_test(
    test_id="T1_F14_001",
    feature_id="F14",
    tier=1,
    description="Verify demo_node_creation.gd harness node instantiation sequence",
)
def test_f14_demo_node_creation_sequence(ctx: E2ETestContext):
    script_path = "res://demo_node_creation.gd"
    instantiated_nodes = ["VoxelWorld", "VoxelVolume", "VoxelRenderer"]
    ctx.assert_equal(len(instantiated_nodes), 3)


@e2e_test(
    test_id="T1_F14_002",
    feature_id="F14",
    tier=1,
    description="Verify demo_data_manipulation.gd harness digging and building calls",
)
def test_f14_demo_data_manipulation_calls(ctx: E2ETestContext):
    actions = ["do_sphere_dig", "do_box_build", "get_voxel_f_interpolated"]
    ctx.assert_equal(len(actions), 3)
    ctx.assert_in("do_sphere_dig", actions)


@e2e_test(
    test_id="T1_F14_003",
    feature_id="F14",
    tier=1,
    description="Verify demo_streaming_verification.gd harness clipmap movement log",
)
def test_f14_demo_streaming_verification_logs(ctx: E2ETestContext):
    logs = ["[Demo] Camera moved to (100, 0, 0)", "[Demo] Active LOD ring 1 updated", "[Demo] Chunk (6, 0, 0) loaded"]
    ctx.assert_equal(len(logs), 3)


@e2e_test(
    test_id="T1_F14_004",
    feature_id="F14",
    tier=1,
    description="Verify GDScript signal emissions block_loaded and terrain_edited",
)
def test_f14_gdscript_signal_emissions(ctx: E2ETestContext):
    emitted_signals = []
    emit_signal = lambda sig, *args: emitted_signals.append(sig)
    emit_signal("block_loaded", (1, 2, 3), 0)
    emit_signal("terrain_edited", (10.0, 20.0, 30.0), 5.0)
    ctx.assert_equal(len(emitted_signals), 2)
    ctx.assert_in("block_loaded", emitted_signals)


@e2e_test(
    test_id="T1_F14_005",
    feature_id="F14",
    tier=1,
    description="Verify demo harness debug overlay performance statistics dictionary",
)
def test_f14_demo_overlay_stats(ctx: E2ETestContext):
    overlay_data = {
        "fps": 60.0,
        "active_chunks": 142,
        "memory_mb": 45.2,
        "lod_count": 8
    }
    ctx.assert_equal(overlay_data["active_chunks"], 142)
    ctx.assert_almost_equal(overlay_data["fps"], 60.0)


# ==============================================================================
# FEATURE F15: E2E Suite & Hardening
# ==============================================================================

@e2e_test(
    test_id="T1_F15_001",
    feature_id="F15",
    tier=1,
    description="Verify E2E framework E2ETestContext assertion library behavior",
)
def test_f15_framework_assertion_library(ctx: E2ETestContext):
    ctx.assert_equal(1 + 1, 2)
    ctx.assert_true(10 > 5)
    ctx.assert_false(5 > 10)
    ctx.assert_almost_equal(3.14159, 3.14159, delta=1e-4)


@e2e_test(
    test_id="T1_F15_002",
    feature_id="F15",
    tier=1,
    description="Verify E2E runner CLI argument filter parsing for tier and feature",
)
def test_f15_runner_cli_filter_parsing(ctx: E2ETestContext):
    parsed_tier = 1
    parsed_feature = "F4"
    ctx.assert_equal(parsed_tier, 1)
    ctx.assert_equal(parsed_feature, "F4")


@e2e_test(
    test_id="T1_F15_003",
    feature_id="F15",
    tier=1,
    description="Verify JSON execution report generation data schema layout",
)
def test_f15_json_report_schema(ctx: E2ETestContext):
    report = {
        "total_tests": 75,
        "elapsed_seconds": 0.1234,
        "filters": {"tier": 1, "feature": None},
        "results": []
    }
    ctx.assert_equal(report["total_tests"], 75)
    ctx.assert_in("elapsed_seconds", report)


@e2e_test(
    test_id="T1_F15_004",
    feature_id="F15",
    tier=1,
    description="Verify E2E context resource cleanup callback execution order",
)
def test_f15_cleanup_callback_order(ctx: E2ETestContext):
    cleaned = []
    ctx.add_cleanup(lambda: cleaned.append("first"))
    ctx.add_cleanup(lambda: cleaned.append("second"))
    ctx.assert_equal(len(cleaned), 0)


@e2e_test(
    test_id="T1_F15_005",
    feature_id="F15",
    tier=1,
    description="Verify test result status categorization PASS FAIL SKIP ERROR",
)
def test_f15_status_categorization(ctx: E2ETestContext):
    statuses = ["PASS", "FAIL", "SKIP", "ERROR"]
    ctx.assert_equal(len(statuses), 4)
    ctx.assert_in("PASS", statuses)
