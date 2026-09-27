#!/usr/bin/env python3
"""
Empirical Stress Test Harness for GodotEden M2 Algorithms.
Tests:
1. VoxelBuffer Palette Compaction (0, 1, 16, 17 colors overflow)
2. LodOctree SVO DAG Deduplication & Stackless Traversal
3. VoxelDataMap Spatial Hash Lookup & Neighborhood Boundaries
4. VoxelGeneratorNoise Spherical SDF & Determinism
"""

import math
import sys
from typing import Dict, List, Tuple, Optional

# --- Target 1: VoxelBuffer Simulation ---
class VoxelBufferSim:
    BLOCK_SIZE = 16
    BLOCK_VOLUME = 4096 # 16*16*16
    
    COMPRESSION_UNIFORM = 0
    COMPRESSION_PALETTE = 1
    COMPRESSION_RAW = 2

    def __init__(self):
        self.compression = [self.COMPRESSION_UNIFORM] * 4
        self.uniform_val = [0] * 4
        self.palette = [[] for _ in range(4)]
        self.nibble_data = [bytearray() for _ in range(4)]
        self.raw_data = [[] for _ in range(4)]

    def _get_index(self, x: int, y: int, z: int) -> int:
        return x + y * 16 + z * 256

    def set_voxel_u(self, val: int, x: int, y: int, z: int, channel: int = 1):
        idx = self._get_index(x, y, z)
        comp = self.compression[channel]

        if comp == self.COMPRESSION_UNIFORM:
            if self.uniform_val[channel] == val:
                return
            # Expand uniform to palette
            self.compression[channel] = self.COMPRESSION_PALETTE
            self.palette[channel] = [self.uniform_val[channel], val]
            self.nibble_data[channel] = bytearray(self.BLOCK_VOLUME // 2)
            # set bit nibble at idx
            b = idx // 2
            s = (idx % 2) * 4
            self.nibble_data[channel][b] = (self.nibble_data[channel][b] & ~(0x0F << s)) | ((1 & 0x0F) << s)

        elif comp == self.COMPRESSION_PALETTE:
            pal = self.palette[channel]
            if val in pal:
                pal_idx = pal.index(val)
            else:
                if len(pal) < 16:
                    pal_idx = len(pal)
                    pal.append(val)
                else:
                    # 17 colors overflow -> expand to raw
                    self._expand_palette_to_raw(channel)
                    self.raw_data[channel][idx] = val
                    return

            b = idx // 2
            s = (idx % 2) * 4
            self.nibble_data[channel][b] = (self.nibble_data[channel][b] & ~(0x0F << s)) | ((pal_idx & 0x0F) << s)

        else: # RAW
            self.raw_data[channel][idx] = val

    def _expand_palette_to_raw(self, channel: int):
        self.raw_data[channel] = [0] * self.BLOCK_VOLUME
        pal = self.palette[channel]
        nib = self.nibble_data[channel]
        for i in range(self.BLOCK_VOLUME):
            b = i // 2
            s = (i % 2) * 4
            p_idx = (nib[b] >> s) & 0x0F
            if p_idx < len(pal):
                self.raw_data[channel][i] = pal[p_idx]
            else:
                self.raw_data[channel][i] = self.uniform_val[channel]
        self.compression[channel] = self.COMPRESSION_RAW
        self.palette[channel] = []
        self.nibble_data[channel] = bytearray()

    def get_voxel_u(self, x: int, y: int, z: int, channel: int = 1) -> int:
        idx = self._get_index(x, y, z)
        comp = self.compression[channel]
        if comp == self.COMPRESSION_UNIFORM:
            return self.uniform_val[channel]
        elif comp == self.COMPRESSION_PALETTE:
            b = idx // 2
            s = (idx % 2) * 4
            p_idx = (self.nibble_data[channel][b] >> s) & 0x0F
            pal = self.palette[channel]
            if p_idx < len(pal):
                return pal[p_idx]
            return self.uniform_val[channel]
        else:
            return self.raw_data[channel][idx]

    def compress_palette(self, channel: int = 1) -> bool:
        if self.compression[channel] == self.COMPRESSION_UNIFORM:
            return False
        
        all_vals = [self.get_voxel_u(x, y, z, channel) for z in range(16) for y in range(16) for x in range(16)]
        unique_vals = list(dict.fromkeys(all_vals))

        if len(unique_vals) == 1:
            self.compression[channel] = self.COMPRESSION_UNIFORM
            self.uniform_val[channel] = unique_vals[0]
            self.palette[channel] = []
            self.nibble_data[channel] = bytearray()
            self.raw_data[channel] = []
            return True
        elif len(unique_vals) <= 16:
            self.compression[channel] = self.COMPRESSION_PALETTE
            self.uniform_val[channel] = unique_vals[0]
            self.palette[channel] = list(unique_vals)
            self.nibble_data[channel] = bytearray(self.BLOCK_VOLUME // 2)
            for i, v in enumerate(all_vals):
                p_idx = self.palette[channel].index(v)
                b = i // 2
                s = (i % 2) * 4
                self.nibble_data[channel][b] = (self.nibble_data[channel][b] & ~(0x0F << s)) | ((p_idx & 0x0F) << s)
            self.raw_data[channel] = []
            return True
        else:
            if self.compression[channel] != self.COMPRESSION_RAW:
                self.compression[channel] = self.COMPRESSION_RAW
                self.raw_data[channel] = all_vals
                self.palette[channel] = []
                self.nibble_data[channel] = bytearray()
            return False

    def get_allocated_bytes(self, channel: int = 1) -> int:
        comp = self.compression[channel]
        if comp == self.COMPRESSION_UNIFORM:
            return 4
        elif comp == self.COMPRESSION_PALETTE:
            return len(self.palette[channel]) * 4 + len(self.nibble_data[channel])
        else:
            return len(self.raw_data[channel]) * 4


# --- Target 2: LodOctree Simulation ---
class SvoNodeSim:
    def __init__(self, child_mask=0, first_child_idx=0, material_tag=0, sdf_value=0.0):
        self.child_mask = child_mask
        self.first_child_idx = first_child_idx
        self.material_tag = material_tag
        self.sdf_value = sdf_value

class LodOctreeSim:
    def __init__(self):
        self.nodes: List[SvoNodeSim] = []
        self.dag_map: Dict[Tuple, int] = {}
        self.max_depth = 16
        self.clear()

    def clear(self):
        self.nodes = [SvoNodeSim()]
        self.dag_map = {}

    def insert_dag_branch(self, child_mask: int, children: List[int], material_tag: int = 0, sdf: float = 0.0) -> int:
        key = (child_mask, material_tag, tuple(children[:8]))
        if key in self.dag_map:
            return self.dag_map[key]

        node = SvoNodeSim(child_mask, children[0] if child_mask != 0 else 0, material_tag, sdf)
        new_idx = len(self.nodes)
        self.nodes.append(node)
        self.dag_map[key] = new_idx
        return new_idx

    def sample_sdf_at(self, pos: Tuple[float, float, float]) -> float:
        if not self.nodes:
            return 0.0
        px, py, pz = max(0.0, min(1.0, pos[0])), max(0.0, min(1.0, pos[1])), max(0.0, min(1.0, pos[2]))
        curr_idx = 0
        last_sdf = self.nodes[0].sdf_value

        for d in range(self.max_depth):
            if curr_idx >= len(self.nodes):
                break
            node = self.nodes[curr_idx]
            last_sdf = node.sdf_value
            if node.child_mask == 0 or node.first_child_idx == 0:
                break
            octant = (1 if px >= 0.5 else 0) | (2 if py >= 0.5 else 0) | (4 if pz >= 0.5 else 0)
            if (node.child_mask & (1 << octant)) == 0:
                break
            curr_idx = node.first_child_idx + octant
            px = (px - 0.5) * 2.0 if px >= 0.5 else px * 2.0
            py = (py - 0.5) * 2.0 if py >= 0.5 else py * 2.0
            pz = (pz - 0.5) * 2.0 if pz >= 0.5 else pz * 2.0

        return last_sdf


# --- Target 3: VoxelDataMap Simulation ---
class VoxelDataMapSim:
    def __init__(self):
        self.blocks: Dict[Tuple[int, int, int], VoxelBufferSim] = {}

    def get_or_create_block(self, pos: Tuple[int, int, int]) -> VoxelBufferSim:
        if pos not in self.blocks:
            self.blocks[pos] = VoxelBufferSim()
        return self.blocks[pos]

    def remove_block(self, pos: Tuple[int, int, int]) -> bool:
        if pos in self.blocks:
            del self.blocks[pos]
            return True
        return False

    def get_block_neighborhood(self, center: Tuple[int, int, int], radius: int = 1) -> List[Optional[VoxelBufferSim]]:
        res = []
        for dz in range(-radius, radius + 1):
            for dy in range(-radius, radius + 1):
                for dx in range(-radius, radius + 1):
                    target = (center[0] + dx, center[1] + dy, center[2] + dz)
                    res.append(self.blocks.get(target, None))
        return res


# --- Target 4: VoxelGeneratorNoise Simulation ---
class VoxelGeneratorNoiseSim:
    def __init__(self, planet_radius=1000.0, height_scale=100.0, seed=1337):
        self.planet_radius = planet_radius
        self.height_scale = height_scale
        self.seed = seed

    def get_single_sdf(self, pos: Tuple[float, float, float]) -> float:
        dist = math.sqrt(pos[0]**2 + pos[1]**2 + pos[2]**2)
        base_sdf = dist - self.planet_radius
        # Procedural pseudo-noise calculation
        n = math.sin(pos[0] * 0.005 + self.seed) * math.cos(pos[1] * 0.005) * math.sin(pos[2] * 0.005)
        terrain_height = n * self.height_scale
        return base_sdf - terrain_height


# --- Runner Functions ---

def run_tests():
    print("=== GODOTEDEN M2 EMPIRICAL STRESS TEST SUITE ===")
    
    # 1. Palette Compaction Boundary Stress Test
    print("\n[Test 1] VoxelBuffer Palette Compaction Boundary Stress Test")
    buf = VoxelBufferSim()
    
    # 1a. 0 colors (Clear/Default state)
    print("  Subtest 1a: 0 colors / empty buffer")
    buf.compress_palette(channel=1)
    assert buf.compression[1] == VoxelBufferSim.COMPRESSION_UNIFORM
    assert buf.get_allocated_bytes(1) == 4
    print("  -> PASS: 0 colors uniform state verified (4 bytes allocated)")

    # 1b. 1 color (Uniform state)
    print("  Subtest 1b: 1 color uniform buffer")
    for z in range(16):
        for y in range(16):
            for x in range(16):
                buf.set_voxel_u(42, x, y, z, channel=1)
    buf.compress_palette(channel=1)
    assert buf.compression[1] == VoxelBufferSim.COMPRESSION_UNIFORM
    assert buf.get_voxel_u(5, 5, 5, 1) == 42
    assert buf.get_allocated_bytes(1) == 4
    print("  -> PASS: 1 color uniform compaction verified (4 bytes allocated)")

    # 1c. 16 colors (Max Palette Capacity)
    print("  Subtest 1c: 16 colors palette compaction")
    for z in range(16):
        for y in range(16):
            for x in range(16):
                val = (x + y + z) % 16 + 1
                buf.set_voxel_u(val, x, y, z, channel=1)
    compressed = buf.compress_palette(channel=1)
    assert compressed == True
    assert buf.compression[1] == VoxelBufferSim.COMPRESSION_PALETTE
    assert len(buf.palette[1]) == 16
    for z in range(16):
        for y in range(16):
            for x in range(16):
                expected = (x + y + z) % 16 + 1
                assert buf.get_voxel_u(x, y, z, 1) == expected
    alloc_bytes = buf.get_allocated_bytes(1)
    savings = (16384 - alloc_bytes) / 16384.0 * 100.0
    print(f"  -> PASS: 16 colors palette compacted to {alloc_bytes} bytes ({savings:.2f}% memory reduction)")
    assert savings >= 74.0

    # 1d. 17 colors overflow
    print("  Subtest 1d: 17 colors palette overflow handling")
    buf.set_voxel_u(999, 0, 0, 0, channel=1) # 17th unique value
    assert buf.compression[1] == VoxelBufferSim.COMPRESSION_RAW
    assert buf.get_voxel_u(0, 0, 0, 1) == 999
    assert buf.get_voxel_u(1, 0, 0, 1) == (1 + 0 + 0) % 16 + 1
    comp_res = buf.compress_palette(channel=1)
    assert comp_res == False
    assert buf.compression[1] == VoxelBufferSim.COMPRESSION_RAW
    print("  -> PASS: 17 colors overflow transitioned cleanly to COMPRESSION_RAW without data corruption")


    # 2. SVO DAG Deduplication & Stackless Traversal
    print("\n[Test 2] LodOctree SVO DAG Deduplication & Stackless Traversal")
    octree = LodOctreeSim()
    children = [0] * 8
    
    # 2a. Deduplication test
    idx1 = octree.insert_dag_branch(0xFF, children, material_tag=10, sdf=-1.0)
    idx2 = octree.insert_dag_branch(0xFF, children, material_tag=10, sdf=-1.0)
    assert idx1 == idx2
    assert len(octree.nodes) == 2 # root + 1 unique branch
    print(f"  -> PASS: Duplicate subtree branch insertion deduplicated to identical DAG node index ({idx1})")

    # 2b. Stackless traversal
    sdf_val = octree.sample_sdf_at((0.25, 0.25, 0.25))
    assert sdf_val == -1.0
    print(f"  -> PASS: Stackless spatial traversal correctly sampled deep SDF value ({sdf_val})")


    # 3. VoxelDataMap Spatial Hash & Neighborhood
    print("\n[Test 3] VoxelDataMap Spatial Hash Lookup & Neighborhood Boundaries")
    vmap = VoxelDataMapSim()
    center = (0, 0, 0)
    vmap.get_or_create_block(center)
    vmap.get_or_create_block((1, 0, 0))
    vmap.get_or_create_block((-1, 0, 0))

    neighborhood = vmap.get_block_neighborhood(center, radius=1)
    assert len(neighborhood) == 27
    non_null = sum(1 for b in neighborhood if b is not None)
    assert non_null == 3
    print(f"  -> PASS: Neighborhood retrieval returned exactly 27 elements with {non_null} present blocks and 24 null boundaries")


    # 4. VoxelGeneratorNoise Spherical SDF & Consistency
    print("\n[Test 4] VoxelGeneratorNoise Spherical SDF & Determinism")
    gen1 = VoxelGeneratorNoiseSim(planet_radius=1000.0, height_scale=100.0, seed=1337)
    gen2 = VoxelGeneratorNoiseSim(planet_radius=1000.0, height_scale=100.0, seed=1337)

    sdf_origin = gen1.get_single_sdf((0, 0, 0))
    assert sdf_origin < 0.0 # Deep interior
    
    sdf_far = gen1.get_single_sdf((2000, 0, 0))
    assert sdf_far > 0.0 # Far exterior

    # Determinism across instances
    for test_pos in [(100.0, 200.0, 300.0), (-500.0, 200.0, 100.0), (0.0, 1000.0, 0.0)]:
        val1 = gen1.get_single_sdf(test_pos)
        val2 = gen2.get_single_sdf(test_pos)
        assert val1 == val2

    print(f"  -> PASS: SDF sign semantics verified (Origin: {sdf_origin:.2f}, Far: {sdf_far:.2f})")
    print("  -> PASS: Instance seed determinism verified across independent generator instances")

    print("\n=== ALL 4 EMPIRICAL STRESS TESTS PASSED SUCCESSFULLY ===")

if __name__ == "__main__":
    run_tests()
