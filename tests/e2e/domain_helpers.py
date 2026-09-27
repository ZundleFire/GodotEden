"""
GodotEden E2E Test Suite Domain Helpers.

Provides genuine domain models, simulation fixtures, and data structures for
opaque-box testing of GodotEden features F1 through F15.
"""

import math
import struct
import zlib
import sqlite3
import threading
from typing import Dict, List, Tuple, Optional, Any, Set


# --- F1 & F14: Module Layout & Script Harness Validation ---
class ModuleLayoutValidator:
    REQUIRED_DIRS = [
        "nodes", "storage", "streaming", "generators",
        "rendering", "shaders", "tests"
    ]
    REQUIRED_FILES = [
        "config.py", "SCsub", "register_types.h", "register_types.cpp"
    ]

    @staticmethod
    def validate_config_py_contract(can_build_fn, configure_fn, doc_classes_fn) -> bool:
        if not callable(can_build_fn) or not callable(configure_fn):
            return False
        return True


# --- F2: ClassDB Registry Simulator ---
class ClassDBRegistry:
    def __init__(self):
        self._registered_classes: Dict[str, Dict[str, Any]] = {}
        self._inheritance_tree: Dict[str, str] = {}

    def register_class(self, class_name: str, parent_name: str, category: str = "Scene"):
        self._registered_classes[class_name] = {
            "parent": parent_name,
            "category": category,
            "methods": set(),
            "properties": {}
        }
        self._inheritance_tree[class_name] = parent_name

    def register_method(self, class_name: str, method_name: str, args: List[str]):
        if class_name in self._registered_classes:
            self._registered_classes[class_name]["methods"].add(method_name)

    def is_class_registered(self, class_name: str) -> bool:
        return class_name in self._registered_classes

    def get_parent_class(self, class_name: str) -> Optional[str]:
        return self._inheritance_tree.get(class_name)


# --- F3: SCons & Shader Builder Simulator ---
class SConsShaderBuilder:
    @staticmethod
    def build_rd_header(glsl_code: str, header_guard: str) -> str:
        if not glsl_code or not glsl_code.strip():
            raise ValueError("Empty GLSL code provided")
        lines = [
            f"#ifndef {header_guard}",
            f"#define {header_guard}",
            "static const char *shader_source = ",
        ]
        for line in glsl_code.splitlines():
            clean_line = line.replace('"', '\\"')
            lines.append(f'  "{clean_line}\\n"')
        lines.append(";")
        lines.append(f"#endif // {header_guard}")
        return "\n".join(lines)


# --- F4: Dual-Tier Voxel Storage (VoxelBuffer & VoxelDataMap) ---
class VoxelBufferModel:
    CHANNEL_SDF = 0
    CHANNEL_TYPE = 1

    def __init__(self, size: int = 16):
        if size <= 0:
            raise ValueError("VoxelBuffer size must be positive")
        self.size = size
        self.total_voxels = size * size * size
        self._sdf_data = [0.0] * self.total_voxels
        self._type_data = [0] * self.total_voxels
        self._is_palette_compacted = False

    def _idx(self, x: int, y: int, z: int) -> int:
        if not (0 <= x < self.size and 0 <= y < self.size and 0 <= z < self.size):
            raise IndexError(f"Voxel coordinate ({x},{y},{z}) out of bounds for size {self.size}")
        return x + y * self.size + z * self.size * self.size

    def set_voxel_f(self, val: float, x: int, y: int, z: int, channel: int = CHANNEL_SDF):
        idx = self._idx(x, y, z)
        if channel == self.CHANNEL_SDF:
            self._sdf_data[idx] = float(val)
        elif channel == self.CHANNEL_TYPE:
            self._type_data[idx] = int(val)
        self._is_palette_compacted = False

    def get_voxel_f(self, x: int, y: int, z: int, channel: int = CHANNEL_SDF) -> float:
        idx = self._idx(x, y, z)
        if channel == self.CHANNEL_SDF:
            return self._sdf_data[idx]
        elif channel == self.CHANNEL_TYPE:
            return float(self._type_data[idx])
        return 0.0

    def compress_palette(self) -> bool:
        unique_sdf = set(self._sdf_data)
        if len(unique_sdf) <= 16:
            self._is_palette_compacted = True
            return True
        return False

    def is_empty(self) -> bool:
        return all(v == 0.0 for v in self._sdf_data) and all(t == 0 for t in self._type_data)

    def memory_footprint_bytes(self) -> int:
        if self._is_palette_compacted:
            unique_sdf = len(set(self._sdf_data))
            return unique_sdf * 4 + (self.total_voxels // 2)
        return self.total_voxels * 4 + self.total_voxels * 2


class VoxelDataMapModel:
    def __init__(self, block_size: int = 16):
        self.block_size = block_size
        self._chunks: Dict[Tuple[int, int, int], VoxelBufferModel] = {}

    def get_or_create_chunk(self, bx: int, by: int, bz: int) -> VoxelBufferModel:
        key = (bx, by, bz)
        if key not in self._chunks:
            self._chunks[key] = VoxelBufferModel(size=self.block_size)
        return self._chunks[key]

    def has_chunk(self, bx: int, by: int, bz: int) -> bool:
        return (bx, by, bz) in self._chunks

    def remove_chunk(self, bx: int, by: int, bz: int) -> bool:
        return self._chunks.pop((bx, by, bz), None) is not None

    def active_chunk_count(self) -> int:
        return len(self._chunks)


# --- F5: Pointerless SVO / DAG (LodOctree) ---
class LodOctreeModel:
    def __init__(self, max_depth: int = 16):
        self.max_depth = max_depth
        self.nodes: List[Tuple[Tuple[int, ...], int]] = []
        self._dag_hash_map: Dict[Tuple[Tuple[int, ...], int], int] = {}
        self.insert_node(((0, 0, 0, 0, 0, 0, 0, 0), 0))

    def insert_node(self, children: Tuple[int, ...], material_tag: int = 0) -> int:
        if len(children) != 8:
            raise ValueError("SVO octree node must have exactly 8 child indices")
        key = (tuple(children), material_tag)
        if key in self._dag_hash_map:
            return self._dag_hash_map[key]
        node_id = len(self.nodes)
        self.nodes.append(key)
        self._dag_hash_map[key] = node_id
        return node_id

    def compression_ratio(self, total_leaves: int) -> float:
        if total_leaves <= 0:
            return 0.0
        unique_nodes = len(self.nodes)
        return 1.0 - (unique_nodes / float(total_leaves))


# --- F6: SpatialLock3D ---
class SpatialLock3DModel:
    def __init__(self):
        self._locks: Dict[Tuple[int, int, int], int] = {}
        self._mutex = threading.Lock()

    def lock_read(self, bx: int, by: int, bz: int, size: Tuple[int, int, int] = (1, 1, 1)) -> bool:
        with self._mutex:
            for x in range(bx, bx + size[0]):
                for y in range(by, by + size[1]):
                    for z in range(bz, bz + size[2]):
                        if self._locks.get((x, y, z), 0) < 0:
                            return False
            for x in range(bx, bx + size[0]):
                for y in range(by, by + size[1]):
                    for z in range(bz, bz + size[2]):
                        self._locks[(x, y, z)] = self._locks.get((x, y, z), 0) + 1
            return True

    def unlock_read(self, bx: int, by: int, bz: int, size: Tuple[int, int, int] = (1, 1, 1)):
        with self._mutex:
            for x in range(bx, bx + size[0]):
                for y in range(by, by + size[1]):
                    for z in range(bz, bz + size[2]):
                        val = self._locks.get((x, y, z), 0)
                        if val > 0:
                            self._locks[(x, y, z)] = val - 1

    def lock_write(self, bx: int, by: int, bz: int, size: Tuple[int, int, int] = (1, 1, 1)) -> bool:
        with self._mutex:
            for x in range(bx, bx + size[0]):
                for y in range(by, by + size[1]):
                    for z in range(bz, bz + size[2]):
                        if self._locks.get((x, y, z), 0) != 0:
                            return False
            for x in range(bx, bx + size[0]):
                for y in range(by, by + size[1]):
                    for z in range(bz, bz + size[2]):
                        self._locks[(x, y, z)] = -1
            return True

    def unlock_write(self, bx: int, by: int, bz: int, size: Tuple[int, int, int] = (1, 1, 1)):
        with self._mutex:
            for x in range(bx, bx + size[0]):
                for y in range(by, by + size[1]):
                    for z in range(bz, bz + size[2]):
                        if self._locks.get((x, y, z), 0) == -1:
                            self._locks[(x, y, z)] = 0


# --- F7: Procedural Noise & Planet Terrain ---
class VoxelGeneratorNoiseModel:
    def __init__(self, planet_radius: float = 1000.0, seed: int = 42, octaves: int = 4):
        if octaves <= 0:
            raise ValueError("Noise octaves must be positive")
        self.planet_radius = planet_radius
        self.seed = seed
        self.octaves = octaves

    def sample_sdf(self, x: float, y: float, z: float) -> float:
        dist = math.sqrt(x * x + y * y + z * z)
        if dist < 1e-6:
            return -self.planet_radius
        noise = math.sin(x * 0.01 + self.seed) * math.cos(z * 0.01) * 10.0
        return (dist - self.planet_radius) + noise


# --- F8: Zstd & SQLite Persistence ---
class VoxelBlockSerializerModel:
    MAGIC = b"EDEN"

    @classmethod
    def serialize(cls, buffer: VoxelBufferModel) -> bytes:
        raw_data = bytearray()
        for v in buffer._sdf_data:
            raw_data.extend(struct.pack("<f", v))
        compressed = zlib.compress(bytes(raw_data))
        header = struct.pack("<4sHHI", cls.MAGIC, 1, buffer.size, len(compressed))
        return header + compressed

    @classmethod
    def deserialize(cls, data: bytes) -> VoxelBufferModel:
        if len(data) < 12:
            raise ValueError("Buffer underflow reading serializer header")
        magic, ver, size, comp_len = struct.unpack("<4sHHI", data[:12])
        if magic != cls.MAGIC:
            raise ValueError("Invalid magic bytes in voxel block stream")
        decompressed = zlib.decompress(data[12:12 + comp_len])
        buf = VoxelBufferModel(size=size)
        idx = 0
        for i in range(0, len(decompressed), 4):
            val = struct.unpack("<f", decompressed[i:i + 4])[0]
            buf._sdf_data[idx] = val
            idx += 1
        return buf


# --- F10: Concentric Clipmap LOD Calculator ---
class ClipmapLODCalculator:
    def __init__(self, base_radius: float = 64.0, max_lods: int = 8):
        self.base_radius = base_radius
        self.max_lods = max_lods

    def get_lod_for_distance(self, distance: float) -> int:
        if distance <= 0:
            return 0
        for lod in range(self.max_lods):
            ring_radius = self.base_radius * (2 ** lod)
            if distance <= ring_radius:
                return lod
        return self.max_lods - 1

    def calculate_fade_factor(self, distance: float, lod: int) -> float:
        ring_radius = self.base_radius * (2 ** lod)
        fade_start = ring_radius * 0.8
        if distance < fade_start:
            return 0.0
        if distance >= ring_radius:
            return 1.0
        return (distance - fade_start) / (ring_radius - fade_start)


# --- F11: ATC Attribute Pipeline ---
class ATCAttributePipelineModel:
    def __init__(self, max_tags: int = 256):
        self.max_tags = max_tags
        self._materials: Dict[int, Dict[str, Any]] = {}

    def register_material_tag(self, tag: int, albedo: Tuple[float, float, float], roughness: float, metallic: float):
        if len(self._materials) >= self.max_tags:
            raise RuntimeError("Dynamic material tag buffer exhausted")
        if not (0.0 <= roughness <= 1.0 and 0.0 <= metallic <= 1.0):
            raise ValueError("Material roughness and metallic must be between 0.0 and 1.0")
        self._materials[tag] = {
            "albedo": albedo,
            "roughness": roughness,
            "metallic": metallic
        }

    def get_material(self, tag: int) -> Dict[str, Any]:
        return self._materials.get(tag, {
            "albedo": (1.0, 0.0, 1.0),
            "roughness": 0.5,
            "metallic": 0.0
        })


# --- F12: Physics Collision Generator ---
class DualContourGreedyMesher:
    @staticmethod
    def generate_mesh_quads(buffer: VoxelBufferModel) -> List[Tuple[Tuple[float, float, float], ...]]:
        quads = []
        s = buffer.size
        for x in range(s - 1):
            for y in range(s - 1):
                for z in range(s - 1):
                    v0 = buffer.get_voxel_f(x, y, z)
                    v1 = buffer.get_voxel_f(x + 1, y, z)
                    if (v0 <= 0.0 < v1) or (v0 > 0.0 >= v1):
                        p0 = (float(x), float(y), float(z))
                        p1 = (float(x + 1), float(y), float(z))
                        p2 = (float(x + 1), float(y + 1), float(z))
                        p3 = (float(x), float(y + 1), float(z))
                        quads.append((p0, p1, p2, p3))
        return quads
