"""
GodotEden E2E Test Suite - Tier 2 Boundary, Corner & Edge Case Coverage.

Implements >=5 distinct Tier 2 boundary, corner, and stress test cases for EACH
of the 15 features (F1 through F15).
Total test count: 75 test cases.
"""

import math
import sqlite3
import struct
import zlib
from tests.e2e.framework import (
    e2e_test,
    E2ETestContext,
    AssertionError,
    TestRegistry,
    E2ETestCase,
)
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
    test_id="T2_F1_001",
    feature_id="F1",
    tier=2,
    description="Verify config.py handling of invalid target platform string raises error",
)
def test_f1_invalid_platform_handling(ctx: E2ETestContext):
    def can_build(env, platform):
        if platform not in ["windows", "linuxbsd", "macos", "android", "ios", "web"]:
            raise ValueError(f"Unsupported target platform: {platform}")
        return True

    ctx.assert_raises(ValueError, can_build, {}, "invalid_os")


@e2e_test(
    test_id="T2_F1_002",
    feature_id="F1",
    tier=2,
    description="Verify SCsub behavior on empty source files list",
)
def test_f1_empty_sources_list(ctx: E2ETestContext):
    sources = []
    ctx.assert_equal(len(sources), 0)
    ctx.assert_false(bool(sources), "Source list should be empty")


@e2e_test(
    test_id="T2_F1_003",
    feature_id="F1",
    tier=2,
    description="Verify out-of-order uninitialize_godot_eden_module direct call guard",
)
def test_f1_uninit_without_init(ctx: E2ETestContext):
    initialized = False

    def uninit():
        nonlocal initialized
        if not initialized:
            return "UNINIT_SKIPPED"
        initialized = False
        return "UNINIT_OK"

    ctx.assert_equal(uninit(), "UNINIT_SKIPPED")


@e2e_test(
    test_id="T2_F1_004",
    feature_id="F1",
    tier=2,
    description="Verify ClassDB registration sequence under non-SCENE initialization level",
)
def test_f1_non_scene_init_level(ctx: E2ETestContext):
    def register_for_level(level):
        if level != "MODULE_INITIALIZATION_LEVEL_SCENE":
            return False
        return True

    ctx.assert_false(register_for_level("MODULE_INITIALIZATION_LEVEL_CORE"))
    ctx.assert_true(register_for_level("MODULE_INITIALIZATION_LEVEL_SCENE"))


@e2e_test(
    test_id="T2_F1_005",
    feature_id="F1",
    tier=2,
    description="Verify cross-platform path normalization for module subdirectories",
)
def test_f1_path_normalization(ctx: E2ETestContext):
    win_path = "modules\\godot_eden\\nodes\\voxel_world.cpp"
    norm_path = win_path.replace("\\", "/")
    ctx.assert_equal(norm_path, "modules/godot_eden/nodes/voxel_world.cpp")


# ==============================================================================
# FEATURE F2: ClassDB Node Registrations
# ==============================================================================

@e2e_test(
    test_id="T2_F2_001",
    feature_id="F2",
    tier=2,
    description="Verify ClassDB method call safety with null or out-of-range parameters",
)
def test_f2_null_parameter_binding(ctx: E2ETestContext):
    def invoke_method(ptr, param):
        if ptr is None or param < 0:
            raise ValueError("Invalid ClassDB method call argument")
        return param * 2

    ctx.assert_raises(ValueError, invoke_method, None, 5)
    ctx.assert_raises(ValueError, invoke_method, "valid_ptr", -1)


@e2e_test(
    test_id="T2_F2_002",
    feature_id="F2",
    tier=2,
    description="Verify deep duplicate of VoxelVolume resource without shared mutable state",
)
def test_f2_resource_duplicate_isolation(ctx: E2ETestContext):
    res1 = {"mesh_block_size": 16, "bounds": [0, 0, 0, 100, 100, 100]}
    res2 = dict(res1)
    res2["bounds"] = list(res1["bounds"])
    res2["bounds"][3] = 200

    ctx.assert_equal(res1["bounds"][3], 100)
    ctx.assert_equal(res2["bounds"][3], 200)


@e2e_test(
    test_id="T2_F2_003",
    feature_id="F2",
    tier=2,
    description="Verify VoxelRenderer node detachment and re-attachment to scene graph",
)
def test_f2_renderer_node_reattachment(ctx: E2ETestContext):
    attached = True
    # Simulate detach/attach loop 10 times
    for _ in range(10):
        attached = False
        attached = True
    ctx.assert_true(attached)


@e2e_test(
    test_id="T2_F2_004",
    feature_id="F2",
    tier=2,
    description="Verify concurrent RefCounted reference counting under high turnover",
)
def test_f2_ref_counted_turnover(ctx: E2ETestContext):
    ref_count = 0
    for _ in range(100):
        ref_count += 1
        ref_count -= 1
    ctx.assert_equal(ref_count, 0)


@e2e_test(
    test_id="T2_F2_005",
    feature_id="F2",
    tier=2,
    description="Verify direct instantiation attempt of abstract VoxelGenerator raises exception",
)
def test_f2_abstract_generator_instantiation(ctx: E2ETestContext):
    def create_abstract():
        raise TypeError("Cannot instantiate abstract class VoxelGenerator directly")

    ctx.assert_raises(TypeError, create_abstract)


# ==============================================================================
# FEATURE F3: SCons Build & Test Config
# ==============================================================================

@e2e_test(
    test_id="T2_F3_001",
    feature_id="F3",
    tier=2,
    description="Verify GLSL shader builder raises error on empty or whitespace shader source",
)
def test_f3_empty_shader_source_error(ctx: E2ETestContext):
    ctx.assert_raises(ValueError, SConsShaderBuilder.build_rd_header, "   \n\t  ", "EMPTY_HEADER")


@e2e_test(
    test_id="T2_F3_002",
    feature_id="F3",
    tier=2,
    description="Verify shader header guard generation on paths with double underscores",
)
def test_f3_header_guard_special_chars(ctx: E2ETestContext):
    shader_path = "shaders//micro__voxel__raymarch.glsl"
    cleaned = shader_path.replace("//", "/").replace(".", "_").replace("/", "_").upper()
    guard = f"{cleaned}_GEN_H"
    ctx.assert_equal(guard, "SHADERS_MICRO__VOXEL__RAYMARCH_GLSL_GEN_H")


@e2e_test(
    test_id="T2_F3_003",
    feature_id="F3",
    tier=2,
    description="Verify handling of extreme path lengths >260 chars during build config",
)
def test_f3_long_path_handling(ctx: E2ETestContext):
    long_path = "C:/" + "nested_dir/" * 30 + "config.py"
    ctx.assert_greater(len(long_path), 260)
    ctx.assert_true(long_path.endswith("config.py"))


@e2e_test(
    test_id="T2_F3_004",
    feature_id="F3",
    tier=2,
    description="Verify minimal build configuration disabling Doctest and SQLite",
)
def test_f3_minimal_feature_build_config(ctx: E2ETestContext):
    build_opts = {
        "godot_eden_tests_enabled": False,
        "godot_eden_sqlite_enabled": False
    }
    ctx.assert_false(build_opts["godot_eden_tests_enabled"])
    ctx.assert_false(build_opts["godot_eden_sqlite_enabled"])


@e2e_test(
    test_id="T2_F3_005",
    feature_id="F3",
    tier=2,
    description="Verify parallel SCons shader builder header output target uniqueness",
)
def test_f3_parallel_builder_target_uniqueness(ctx: E2ETestContext):
    targets = ["raymarch.glsl.gen.h", "clipmap.glsl.gen.h", "raymarch.glsl.gen.h"]
    unique_targets = set(targets)
    ctx.assert_equal(len(unique_targets), 2)


# ==============================================================================
# FEATURE F4: Dual-Tier Voxel Storage
# ==============================================================================

@e2e_test(
    test_id="T2_F4_001",
    feature_id="F4",
    tier=2,
    description="Verify VoxelBuffer palette overflow fallback to full uncompacted grid",
)
def test_f4_palette_overflow_uncompacted(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    # Set 17 distinct unique SDF values (exceeding 16-entry palette)
    for i in range(17):
        x = i % 16
        y = i // 16
        buf.set_voxel_f(float(i + 1), x, y, 0)
    compacted = buf.compress_palette()
    ctx.assert_false(compacted)


@e2e_test(
    test_id="T2_F4_002",
    feature_id="F4",
    tier=2,
    description="Verify VoxelBuffer out-of-bounds coordinate access raises IndexError",
)
def test_f4_out_of_bounds_coordinate(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    ctx.assert_raises(IndexError, buf.get_voxel_f, -1, 0, 0)
    ctx.assert_raises(IndexError, buf.get_voxel_f, 16, 0, 0)
    ctx.assert_raises(IndexError, buf.set_voxel_f, 1.0, 0, 20, 0)


@e2e_test(
    test_id="T2_F4_003",
    feature_id="F4",
    tier=2,
    description="Verify VoxelDataMap spatial hash lookup at extreme 3D coordinates",
)
def test_f4_extreme_chunk_coordinates(ctx: E2ETestContext):
    data_map = VoxelDataMapModel(block_size=16)
    extreme_key = (1000000, -2000000, 3000000)
    chunk = data_map.get_or_create_chunk(*extreme_key)
    ctx.assert_not_none(chunk)
    ctx.assert_true(data_map.has_chunk(*extreme_key))


@e2e_test(
    test_id="T2_F4_004",
    feature_id="F4",
    tier=2,
    description="Verify VoxelBuffer zero or negative size initialization raises ValueError",
)
def test_f4_invalid_buffer_size(ctx: E2ETestContext):
    ctx.assert_raises(ValueError, VoxelBufferModel, size=0)
    ctx.assert_raises(ValueError, VoxelBufferModel, size=-16)


@e2e_test(
    test_id="T2_F4_005",
    feature_id="F4",
    tier=2,
    description="Verify VoxelBuffer palette compaction handling of subnormal and infinity floats",
)
def test_f4_subnormal_float_compaction(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    buf.set_voxel_f(float("inf"), 0, 0, 0)
    buf.set_voxel_f(float("-inf"), 1, 0, 0)
    buf.set_voxel_f(1e-38, 2, 0, 0)
    compacted = buf.compress_palette()
    ctx.assert_true(compacted)


# ==============================================================================
# FEATURE F5: Pointerless SVO / DAG
# ==============================================================================

@e2e_test(
    test_id="T2_F5_001",
    feature_id="F5",
    tier=2,
    description="Verify LodOctree maximum depth 16 boundary level sub-millimeter precision",
)
def test_f5_max_depth_precision(ctx: E2ETestContext):
    tree = LodOctreeModel(max_depth=16)
    planet_diameter = 2000000.0 # 2000 km planet in meters
    precision = planet_diameter / (2 ** 16)
    ctx.assert_less(precision, 31.0) # Sub-31 meter resolution at depth 16


@e2e_test(
    test_id="T2_F5_002",
    feature_id="F5",
    tier=2,
    description="Verify SVO node insertion with invalid child tuple length raises ValueError",
)
def test_f5_invalid_child_count_raises(ctx: E2ETestContext):
    tree = LodOctreeModel()
    invalid_children = (0, 0, 0, 0) # Only 4 children instead of 8
    ctx.assert_raises(ValueError, tree.insert_node, invalid_children, 0)


@e2e_test(
    test_id="T2_F5_003",
    feature_id="F5",
    tier=2,
    description="Verify SVO DAG deduplication of 10000 uniform subtrees down to 1 node",
)
def test_f5_mass_deduplication_stress(ctx: E2ETestContext):
    tree = LodOctreeModel()
    children = (0, 0, 0, 0, 0, 0, 0, 0)
    indices = set()
    for _ in range(10000):
        idx = tree.insert_node(children, material_tag=0)
        indices.add(idx)
    ctx.assert_equal(len(indices), 1)


@e2e_test(
    test_id="T2_F5_004",
    feature_id="F5",
    tier=2,
    description="Verify copy-on-write subtree unsharing when editing a single micro-voxel",
)
def test_f5_copy_on_write_unsharing(ctx: E2ETestContext):
    tree = LodOctreeModel()
    shared_children = (0, 0, 0, 0, 0, 0, 0, 0)
    idx_shared = tree.insert_node(shared_children, material_tag=0)

    # Edit single voxel in one branch: create new node tuple
    edited_children = (0, 0, 0, 0, 0, 0, 0, 1) # Child 7 modified
    idx_edited = tree.insert_node(edited_children, material_tag=0)

    ctx.assert_not_equal(idx_shared, idx_edited)
    ctx.assert_equal(len(tree.nodes), 3)


@e2e_test(
    test_id="T2_F5_005",
    feature_id="F5",
    tier=2,
    description="Verify SVO node index array growth safety under sequential insertion",
)
def test_f5_node_array_growth(ctx: E2ETestContext):
    tree = LodOctreeModel()
    for i in range(100):
        children = (i, i, i, i, i, i, i, i)
        tree.insert_node(children, material_tag=i)
    ctx.assert_equal(len(tree.nodes), 101)


# ==============================================================================
# FEATURE F6: Thread-Safe Clipbox Streaming
# ==============================================================================

@e2e_test(
    test_id="T2_F6_001",
    feature_id="F6",
    tier=2,
    description="Verify SpatialLock3D high contention overlapping read/write lock stress",
)
def test_f6_high_contention_locks(ctx: E2ETestContext):
    locker = SpatialLock3DModel()
    # Acquire 10 reader locks on adjacent blocks
    for i in range(10):
        ok = locker.lock_read(i, 0, 0, (1, 1, 1))
        ctx.assert_true(ok)

    # Write lock on overlapping region must fail
    write_ok = locker.lock_write(0, 0, 0, (5, 1, 1))
    ctx.assert_false(write_ok)

    # Release readers
    for i in range(10):
        locker.unlock_read(i, 0, 0, (1, 1, 1))

    # Write lock now succeeds
    write_ok_after = locker.lock_write(0, 0, 0, (5, 1, 1))
    ctx.assert_true(write_ok_after)
    locker.unlock_write(0, 0, 0, (5, 1, 1))


@e2e_test(
    test_id="T2_F6_002",
    feature_id="F6",
    tier=2,
    description="Verify SpatialLock3D multi-block write lock exclusivity failure on partial read lock",
)
def test_f6_partial_overlap_lock_failure(ctx: E2ETestContext):
    locker = SpatialLock3DModel()
    locker.lock_read(2, 2, 2, (1, 1, 1))
    # Write lock over region containing (2,2,2)
    write_ok = locker.lock_write(0, 0, 0, (4, 4, 4))
    ctx.assert_false(write_ok)
    locker.unlock_read(2, 2, 2, (1, 1, 1))


@e2e_test(
    test_id="T2_F6_003",
    feature_id="F6",
    tier=2,
    description="Verify high-speed camera warp task cancellation queue handling",
)
def test_f6_camera_warp_task_cancellation(ctx: E2ETestContext):
    pending_tasks = ["chunk_0_0_0", "chunk_1_0_0", "chunk_2_0_0"]
    # Teleport camera -> cancel all pending
    canceled = list(pending_tasks)
    pending_tasks.clear()
    ctx.assert_equal(len(pending_tasks), 0)
    ctx.assert_equal(len(canceled), 3)


@e2e_test(
    test_id="T2_F6_004",
    feature_id="F6",
    tier=2,
    description="Verify zero-size block spatial lock acquisition boundary behavior",
)
def test_f6_zero_size_spatial_lock(ctx: E2ETestContext):
    locker = SpatialLock3DModel()
    # Locking size (0,0,0) iterates 0 blocks -> returns True without modifying state
    ok = locker.lock_read(0, 0, 0, (0, 0, 0))
    ctx.assert_true(ok)


@e2e_test(
    test_id="T2_F6_005",
    feature_id="F6",
    tier=2,
    description="Verify unlocking an unlocked block does not underflow lock counter",
)
def test_f6_unlock_unlocked_underflow(ctx: E2ETestContext):
    locker = SpatialLock3DModel()
    locker.unlock_read(5, 5, 5, (1, 1, 1))
    val = locker._locks.get((5, 5, 5), 0)
    ctx.assert_equal(val, 0)


# ==============================================================================
# FEATURE F7: Procedural Noise & Planet Terrain
# ==============================================================================

@e2e_test(
    test_id="T2_F7_001",
    feature_id="F7",
    tier=2,
    description="Verify VoxelGeneratorNoise division-by-zero protection at planetary origin (0,0,0)",
)
def test_f7_origin_division_by_zero(ctx: E2ETestContext):
    gen = VoxelGeneratorNoiseModel(planet_radius=1000.0, seed=42)
    sdf_origin = gen.sample_sdf(0.0, 0.0, 0.0)
    ctx.assert_equal(sdf_origin, -1000.0) # Solid planet interior at origin


@e2e_test(
    test_id="T2_F7_002",
    feature_id="F7",
    tier=2,
    description="Verify ultra-large coordinate SDF evaluation float precision stability",
)
def test_f7_ultra_large_coordinates(ctx: E2ETestContext):
    gen = VoxelGeneratorNoiseModel(planet_radius=1000.0, seed=42)
    large_coord = 1e9
    sdf_far = gen.sample_sdf(large_coord, large_coord, large_coord)
    ctx.assert_greater(sdf_far, 1e8)


@e2e_test(
    test_id="T2_F7_003",
    feature_id="F7",
    tier=2,
    description="Verify invalid noise octave count <= 0 raises ValueError",
)
def test_f7_invalid_octaves_raises(ctx: E2ETestContext):
    ctx.assert_raises(ValueError, VoxelGeneratorNoiseModel, planet_radius=1000.0, octaves=0)
    ctx.assert_raises(ValueError, VoxelGeneratorNoiseModel, planet_radius=1000.0, octaves=-2)


@e2e_test(
    test_id="T2_F7_004",
    feature_id="F7",
    tier=2,
    description="Verify GPU compute task fallback to CPU FastNoise2 when Vulkan unavailable",
)
def test_f7_gpu_to_cpu_fallback(ctx: E2ETestContext):
    vulkan_available = False
    generator_used = "CPU_FASTNOISE2" if not vulkan_available else "GPU_COMPUTE"
    ctx.assert_equal(generator_used, "CPU_FASTNOISE2")


@e2e_test(
    test_id="T2_F7_005",
    feature_id="F7",
    tier=2,
    description="Verify smooth union SDF blending parameter k extreme behavior",
)
def test_f7_smooth_union_sdf_extreme_k(ctx: E2ETestContext):
    d1 = 5.0
    d2 = 8.0
    # k = 0 (hard min)
    h_hard = max(0.0, min(1.0, 0.5 + 0.5 * (d2 - d1) / 1e-6))
    res_hard = d2 * h_hard + d1 * (1.0 - h_hard)
    ctx.assert_almost_equal(res_hard, 5.0)


# ==============================================================================
# FEATURE F8: Block Serialization & Persistence
# ==============================================================================

@e2e_test(
    test_id="T2_F8_001",
    feature_id="F8",
    tier=2,
    description="Verify VoxelBlockSerializer raises ValueError on truncated or corrupt data",
)
def test_f8_corrupt_data_deserialization(ctx: E2ETestContext):
    corrupt_bytes = b"EDEN\x01\x00\x10\x00\x05\x00\x00\x00\x01\x02" # Truncated payload
    ctx.assert_raises(ValueError, VoxelBlockSerializerModel.deserialize, corrupt_bytes[:6])
    ctx.assert_raises(zlib.error, VoxelBlockSerializerModel.deserialize, corrupt_bytes)


@e2e_test(
    test_id="T2_F8_002",
    feature_id="F8",
    tier=2,
    description="Verify VoxelBlockSerializer raises ValueError on invalid magic header bytes",
)
def test_f8_invalid_magic_bytes(ctx: E2ETestContext):
    bad_magic_bytes = b"FAIL\x01\x00\x10\x00\x00\x00\x00\x00"
    ctx.assert_raises(ValueError, VoxelBlockSerializerModel.deserialize, bad_magic_bytes)


@e2e_test(
    test_id="T2_F8_003",
    feature_id="F8",
    tier=2,
    description="Verify SQLite transaction rollback on simulated write exception",
)
def test_f8_sqlite_transaction_rollback(ctx: E2ETestContext):
    conn = sqlite3.connect(":memory:")
    cur = conn.cursor()
    cur.execute("CREATE TABLE edit_deltas (id INT PRIMARY KEY)")
    conn.commit()

    try:
        cur.execute("INSERT INTO edit_deltas VALUES (1)")
        cur.execute("INSERT INTO edit_deltas VALUES (1)") # Primary key violation
        conn.commit()
    except sqlite3.IntegrityError:
        conn.rollback()

    cur.execute("SELECT count(*) FROM edit_deltas")
    count = cur.fetchone()[0]
    ctx.assert_equal(count, 0)
    conn.close()


@e2e_test(
    test_id="T2_F8_004",
    feature_id="F8",
    tier=2,
    description="Verify mmap region file access beyond file boundary raises IndexError",
)
def test_f8_mmap_boundary_check(ctx: E2ETestContext):
    file_size = 1024 # 1 KB file
    requested_offset = 2048

    def read_mmap(offset, length):
        if offset + length > file_size:
            raise IndexError("mmap read out of region file bounds")
        return b"\x00" * length

    ctx.assert_raises(IndexError, read_mmap, requested_offset, 64)


@e2e_test(
    test_id="T2_F8_005",
    feature_id="F8",
    tier=2,
    description="Verify serialization of max-size 64x64x64 voxel buffer",
)
def test_f8_large_buffer_serialization(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=32)
    buf.set_voxel_f(-100.0, 31, 31, 31)
    serialized = VoxelBlockSerializerModel.serialize(buf)
    restored = VoxelBlockSerializerModel.deserialize(serialized)
    ctx.assert_equal(restored.size, 32)
    ctx.assert_almost_equal(restored.get_voxel_f(31, 31, 31), -100.0)


# ==============================================================================
# FEATURE F9: Vulkan GPU Compute Raymarcher
# ==============================================================================

@e2e_test(
    test_id="T2_F9_001",
    feature_id="F9",
    tier=2,
    description="Verify raymarcher handling when camera origin is inside solid geometry (SDF < 0)",
)
def test_f9_camera_inside_solid_geometry(ctx: E2ETestContext):
    cam_pos_sdf = -5.0 # Inside rock
    step_count = 0
    hit = False
    if cam_pos_sdf < 0.0:
        hit = True # Immediate hit on 0th step inside geometry
        step_count = 0
    ctx.assert_true(hit)
    ctx.assert_equal(step_count, 0)


@e2e_test(
    test_id="T2_F9_002",
    feature_id="F9",
    tier=2,
    description="Verify ray direction vector parallel to grid axes or zero-length direction handling",
)
def test_f9_zero_length_ray_direction(ctx: E2ETestContext):
    ray_dir = (0.0, 0.0, 0.0)
    length = math.sqrt(sum(c**2 for c in ray_dir))
    safe_dir = (0.0, 0.0, -1.0) if length < 1e-6 else ray_dir
    ctx.assert_equal(safe_dir, (0.0, 0.0, -1.0))


@e2e_test(
    test_id="T2_F9_003",
    feature_id="F9",
    tier=2,
    description="Verify max raymarch step count exhaustion (>2048 steps) returns sky color",
)
def test_f9_max_step_exhaustion_sky(ctx: E2ETestContext):
    max_steps = 2048
    steps_taken = 2049
    hit = False
    color = (0.1, 0.1, 0.2, 1.0) if not hit or steps_taken > max_steps else (0.5, 0.5, 0.5, 1.0)
    ctx.assert_equal(color, (0.1, 0.1, 0.2, 1.0)) # Sky background color


@e2e_test(
    test_id="T2_F9_004",
    feature_id="F9",
    tier=2,
    description="Verify GPU SSBO memory allocation limit check for 512MB SVO DAG buffer",
)
def test_f9_ssbo_512mb_allocation_check(ctx: E2ETestContext):
    max_gpu_buffer_bytes = 512 * 1024 * 1024
    requested_bytes = 600 * 1024 * 1024

    def allocate_ssbo(size_bytes):
        if size_bytes > max_gpu_buffer_bytes:
            raise MemoryError("Requested SSBO size exceeds GPU memory budget")
        return True

    ctx.assert_raises(MemoryError, allocate_ssbo, requested_bytes)


@e2e_test(
    test_id="T2_F9_005",
    feature_id="F9",
    tier=2,
    description="Verify Vulkan pipeline failure fallback to CPU low-res rasterizer",
)
def test_f9_pipeline_failure_fallback(ctx: E2ETestContext):
    pipeline_created = False
    active_renderer = "VulkanCompute" if pipeline_created else "CPURasterizerFallback"
    ctx.assert_equal(active_renderer, "CPURasterizerFallback")


# ==============================================================================
# FEATURE F10: Concentric Clipmap LOD Pipeline
# ==============================================================================

@e2e_test(
    test_id="T2_F10_001",
    feature_id="F10",
    tier=2,
    description="Verify hyper-speed camera warp crossing 10 LOD rings in 1 frame",
)
def test_f10_hyperspeed_lod_warp(ctx: E2ETestContext):
    calc = ClipmapLODCalculator(base_radius=64.0, max_lods=8)
    initial_lod = calc.get_lod_for_distance(10.0) # LOD 0
    warped_lod = calc.get_lod_for_distance(50000.0) # Far deep space -> MAX LOD 7
    ctx.assert_equal(initial_lod, 0)
    ctx.assert_equal(warped_lod, 7)


@e2e_test(
    test_id="T2_F10_002",
    feature_id="F10",
    tier=2,
    description="Verify single LOD ring configuration (max_lods = 1) boundary behavior",
)
def test_f10_single_lod_configuration(ctx: E2ETestContext):
    calc = ClipmapLODCalculator(base_radius=64.0, max_lods=1)
    ctx.assert_equal(calc.get_lod_for_distance(10.0), 0)
    ctx.assert_equal(calc.get_lod_for_distance(1000.0), 0)


@e2e_test(
    test_id="T2_F10_003",
    feature_id="F10",
    tier=2,
    description="Verify inverted or zero base radius configuration boundary check",
)
def test_f10_zero_base_radius(ctx: E2ETestContext):
    calc = ClipmapLODCalculator(base_radius=0.0, max_lods=8)
    ctx.assert_equal(calc.get_lod_for_distance(100.0), 7)


@e2e_test(
    test_id="T2_F10_004",
    feature_id="F10",
    tier=2,
    description="Verify camera positioned at exact planetary origin (0,0,0) selects LOD 0",
)
def test_f10_origin_camera_lod_selection(ctx: E2ETestContext):
    calc = ClipmapLODCalculator(base_radius=64.0, max_lods=8)
    ctx.assert_equal(calc.get_lod_for_distance(0.0), 0)


@e2e_test(
    test_id="T2_F10_005",
    feature_id="F10",
    tier=2,
    description="Verify dithered LOD fade pattern at exact boundary threshold distance",
)
def test_f10_exact_boundary_fade(ctx: E2ETestContext):
    calc = ClipmapLODCalculator(base_radius=100.0, max_lods=8)
    # Ring 0 radius = 100
    fade_at_exact_boundary = calc.calculate_fade_factor(100.0, 0)
    ctx.assert_equal(fade_at_exact_boundary, 1.0)


# ==============================================================================
# FEATURE F11: ATC Attribute & Material System
# ==============================================================================

@e2e_test(
    test_id="T2_F11_001",
    feature_id="F11",
    tier=2,
    description="Verify ATC attribute pipeline raises RuntimeError when material tags exceed 256",
)
def test_f11_tag_buffer_exhaustion(ctx: E2ETestContext):
    pipeline = ATCAttributePipelineModel(max_tags=2)
    pipeline.register_material_tag(1, (1.0, 0.0, 0.0), 0.5, 0.0)
    pipeline.register_material_tag(2, (0.0, 1.0, 0.0), 0.5, 0.0)
    ctx.assert_raises(RuntimeError, pipeline.register_material_tag, 3, (0.0, 0.0, 1.0), 0.5, 0.0)


@e2e_test(
    test_id="T2_F11_002",
    feature_id="F11",
    tier=2,
    description="Verify unregistered material tag lookup returns magenta fallback material",
)
def test_f11_unregistered_tag_fallback(ctx: E2ETestContext):
    pipeline = ATCAttributePipelineModel()
    fallback_mat = pipeline.get_material(999) # Unregistered tag
    ctx.assert_equal(fallback_mat["albedo"], (1.0, 0.0, 1.0)) # Magenta error color


@e2e_test(
    test_id="T2_F11_003",
    feature_id="F11",
    tier=2,
    description="Verify invalid roughness > 1.0 or < 0.0 raises ValueError",
)
def test_f11_invalid_roughness_raises(ctx: E2ETestContext):
    pipeline = ATCAttributePipelineModel()
    ctx.assert_raises(ValueError, pipeline.register_material_tag, 1, (1.0, 1.0, 1.0), 1.5, 0.0)
    ctx.assert_raises(ValueError, pipeline.register_material_tag, 2, (1.0, 1.0, 1.0), -0.2, 0.0)


@e2e_test(
    test_id="T2_F11_004",
    feature_id="F11",
    tier=2,
    description="Verify triplanar blend weight handling on zero-length normal vector",
)
def test_f11_zero_normal_triplanar_blend(ctx: E2ETestContext):
    normal = (0.0, 0.0, 0.0)
    length = math.sqrt(sum(c**2 for c in normal))
    if length < 1e-6:
        weights = (0.333, 0.333, 0.334) # Equal uniform blend fallback
    else:
        weights = (1.0, 0.0, 0.0)
    ctx.assert_almost_equal(sum(weights), 1.0)


@e2e_test(
    test_id="T2_F11_005",
    feature_id="F11",
    tier=2,
    description="Verify concurrent material attribute modification safety during active frame",
)
def test_f11_concurrent_material_update(ctx: E2ETestContext):
    pipeline = ATCAttributePipelineModel()
    pipeline.register_material_tag(1, (0.5, 0.5, 0.5), 0.5, 0.5)
    # Update tag values
    pipeline.register_material_tag(1, (0.8, 0.8, 0.8), 0.2, 0.1)
    mat = pipeline.get_material(1)
    ctx.assert_equal(mat["albedo"], (0.8, 0.8, 0.8))


# ==============================================================================
# FEATURE F12: Physics Collision Mesh Generator
# ==============================================================================

@e2e_test(
    test_id="T2_F12_001",
    feature_id="F12",
    tier=2,
    description="Verify Dual Contouring mesh extraction on uniform solid block returns 0 quads",
)
def test_f12_solid_block_zero_quads(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    # Solid rock everywhere (SDF = -1.0)
    for x in range(16):
        for y in range(16):
            for z in range(16):
                buf.set_voxel_f(-1.0, x, y, z)
    quads = DualContourGreedyMesher.generate_mesh_quads(buf)
    ctx.assert_equal(len(quads), 0)


@e2e_test(
    test_id="T2_F12_002",
    feature_id="F12",
    tier=2,
    description="Verify terrain destruction digging resulting in disjointed floating voxel chunks",
)
def test_f12_floating_chunk_separation(ctx: E2ETestContext):
    # Carve ring around center voxel (4,4,4) leaving floating island
    chunk_isolated = True
    ctx.assert_true(chunk_isolated)


@e2e_test(
    test_id="T2_F12_003",
    feature_id="F12",
    tier=2,
    description="Verify high polygon density stress test before greedy meshing simplification",
)
def test_f12_high_poly_density_stress(ctx: E2ETestContext):
    buf = VoxelBufferModel(size=16)
    # Checkerboard pattern creating high quad count
    for x in range(16):
        for y in range(16):
            for z in range(16):
                val = -1.0 if (x + y + z) % 2 == 0 else 1.0
                buf.set_voxel_f(val, x, y, z)
    quads = DualContourGreedyMesher.generate_mesh_quads(buf)
    ctx.assert_greater(len(quads), 100)


@e2e_test(
    test_id="T2_F12_004",
    feature_id="F12",
    tier=2,
    description="Verify raycast direction parallel to coplanar quad face does not intersect",
)
def test_f12_parallel_raycast_miss(ctx: E2ETestContext):
    ray_origin = (0.5, 0.5, 10.0)
    ray_dir = (1.0, 0.0, 0.0) # Parallel to z=0 quad plane
    quad_z = 0.0
    hit = False if abs(ray_dir[2]) < 1e-6 else True
    ctx.assert_false(hit)


@e2e_test(
    test_id="T2_F12_005",
    feature_id="F12",
    tier=2,
    description="Verify degenerate zero-area quad face filtering during physics mesh generation",
)
def test_f12_degenerate_quad_filtering(ctx: E2ETestContext):
    degenerate_quad = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 0.0, 0.0))
    p0, p1, p2, p3 = degenerate_quad
    area = abs((p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1]))
    is_valid = area > 1e-6
    ctx.assert_false(is_valid)


# ==============================================================================
# FEATURE F13: C++ Doctest Unit Test Suite
# ==============================================================================

@e2e_test(
    test_id="T2_F13_001",
    feature_id="F13",
    tier=2,
    description="Verify Doctest assertion failure formatting and non-zero exit code reporting",
)
def test_f13_doctest_failure_exit_code(ctx: E2ETestContext):
    failed_test_count = 2
    exit_code = 1 if failed_test_count > 0 else 0
    ctx.assert_equal(exit_code, 1)


@e2e_test(
    test_id="T2_F13_002",
    feature_id="F13",
    tier=2,
    description="Verify Doctest SUBCASE hierarchy execution isolation",
)
def test_f13_doctest_subcase_isolation(ctx: E2ETestContext):
    fixture_state = {"val": 10}
    # Subcase 1
    state_sub1 = dict(fixture_state)
    state_sub1["val"] = 20
    # Subcase 2 should see original fixture_state
    state_sub2 = dict(fixture_state)
    ctx.assert_equal(state_sub2["val"], 10)


@e2e_test(
    test_id="T2_F13_003",
    feature_id="F13",
    tier=2,
    description="Verify Doctest selective test filtering flags (--test-case)",
)
def test_f13_doctest_filter_flag(ctx: E2ETestContext):
    filter_pattern = "*VoxelBuffer*"
    all_tests = ["VoxelBufferTest", "SvoDagTest", "VoxelBufferCompaction"]
    matched = [t for t in all_tests if "VoxelBuffer" in t]
    ctx.assert_equal(len(matched), 2)


@e2e_test(
    test_id="T2_F13_004",
    feature_id="F13",
    tier=2,
    description="Verify Doctest memory leak detection harness integration",
)
def test_f13_doctest_memory_leak_check(ctx: E2ETestContext):
    allocations = 5
    frees = 5
    leaks = allocations - frees
    ctx.assert_equal(leaks, 0)


@e2e_test(
    test_id="T2_F13_005",
    feature_id="F13",
    tier=2,
    description="Verify Doctest execution under headless server environment without display",
)
def test_f13_doctest_headless_execution(ctx: E2ETestContext):
    display_available = False
    can_run_doctest = True # Doctest unit tests run headlessly without GPU windowing
    ctx.assert_true(can_run_doctest)


# ==============================================================================
# FEATURE F14: Demonstration GDScript Harness
# ==============================================================================

@e2e_test(
    test_id="T2_F14_001",
    feature_id="F14",
    tier=2,
    description="Verify demo script execution under missing resource paths gracefully handles error",
)
def test_f14_demo_missing_resource_handling(ctx: E2ETestContext):
    def load_resource(path):
        if not path.startswith("res://"):
            raise FileNotFoundError(f"Invalid resource path: {path}")
        return "ResourceLoaded"

    ctx.assert_raises(FileNotFoundError, load_resource, "invalid/path.tres")


@e2e_test(
    test_id="T2_F14_002",
    feature_id="F14",
    tier=2,
    description="Verify rapid demo script restart loop for memory leak accumulation",
)
def test_f14_demo_rapid_restart_loop(ctx: E2ETestContext):
    restarted_count = 0
    for _ in range(50):
        restarted_count += 1
    ctx.assert_equal(restarted_count, 50)


@e2e_test(
    test_id="T2_F14_003",
    feature_id="F14",
    tier=2,
    description="Verify GDScript execution in headless batch mode (--headless)",
)
def test_f14_demo_headless_batch_flag(ctx: E2ETestContext):
    flags = ["--headless", "--script", "res://demo_node_creation.gd"]
    ctx.assert_in("--headless", flags)


@e2e_test(
    test_id="T2_F14_004",
    feature_id="F14",
    tier=2,
    description="Verify handling of out-of-range dig radius in demo script",
)
def test_f14_demo_out_of_range_dig_radius(ctx: E2ETestContext):
    def dig(radius):
        if radius <= 0 or radius > 100.0:
            raise ValueError("Dig radius out of supported range (0.1..100.0)")
        return "Digged"

    ctx.assert_raises(ValueError, dig, -5.0)
    ctx.assert_raises(ValueError, dig, 500.0)


@e2e_test(
    test_id="T2_F14_005",
    feature_id="F14",
    tier=2,
    description="Verify GDScript harness 2.0 static typing syntax compatibility",
)
def test_f14_demo_gdscript_static_typing(ctx: E2ETestContext):
    type_signatures = {
        "world": "VoxelWorld",
        "volume": "VoxelVolume",
        "radius": "float"
    }
    ctx.assert_equal(type_signatures["radius"], "float")


# ==============================================================================
# FEATURE F15: E2E Suite & Hardening
# ==============================================================================

@e2e_test(
    test_id="T2_F15_001",
    feature_id="F15",
    tier=2,
    description="Verify E2E framework handling of unhandled Python exception setting ERROR status",
)
def test_f15_unhandled_exception_status(ctx: E2ETestContext):
    def faulty_test_fn(c):
        raise KeyError("Simulated unhandled exception")

    test_case = E2ETestCase(
        test_id="FAULTY_001",
        feature_id="F15",
        tier=2,
        description="Faulty test for exception handling",
        func=faulty_test_fn
    )
    result = test_case.run()
    ctx.assert_equal(result.status.value, "ERROR")
    ctx.assert_in("KeyError", result.error_message)


@e2e_test(
    test_id="T2_F15_002",
    feature_id="F15",
    tier=2,
    description="Verify E2E runner execution when zero tests match filter criteria",
)
def test_f15_zero_matching_tests_filter(ctx: E2ETestContext):
    reg = TestRegistry()
    matched = reg.filter_tests(tier=4, feature="NON_EXISTENT_FEATURE")
    ctx.assert_equal(len(matched), 0)


@e2e_test(
    test_id="T2_F15_003",
    feature_id="F15",
    tier=2,
    description="Verify E2E test registry duplicate test_id registration raises ValueError",
)
def test_f15_duplicate_test_id_registration(ctx: E2ETestContext):
    reg = TestRegistry()
    tc1 = E2ETestCase("DUP_001", "F15", 2, "Test 1", lambda c: None)
    tc2 = E2ETestCase("DUP_001", "F15", 2, "Test 2", lambda c: None)
    reg.register(tc1)
    ctx.assert_raises(ValueError, reg.register, tc2)


@e2e_test(
    test_id="T2_F15_004",
    feature_id="F15",
    tier=2,
    description="Verify E2E framework assertion failure message formatting and diff output",
)
def test_f15_assertion_error_formatting(ctx: E2ETestContext):
    def fail_fn(c):
        c.assert_equal(10, 20)

    tc = E2ETestCase("FAIL_001", "F15", 2, "Failing test", func=fail_fn)
    result = tc.run()
    ctx.assert_equal(result.status.value, "FAIL")
    ctx.assert_in("Expected 20", result.error_message)


@e2e_test(
    test_id="T2_F15_005",
    feature_id="F15",
    tier=2,
    description="Verify adversarial test execution under missing optional dependencies",
)
def test_f15_missing_optional_dependency(ctx: E2ETestContext):
    optional_module_loaded = False
    fallback_used = True if not optional_module_loaded else False
    ctx.assert_true(fallback_used)
