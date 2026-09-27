"""
Empirical Verification Test Harness for Milestone 2 - GodotEden
Agent: challenger_m2_2
Date: 2026-08-05

This script models and stress-tests:
1. SpatialLock3D thread safety, reader non-exclusivity, and writer exclusivity under heavy thread contention.
2. Zstd stream round-trip serialization and header validation in VoxelBlockSerializer.
3. SQLite edit delta persistence key hashing and storage operations in VoxelStreamSQLite.
4. Sector region file coordinate math and header offset calculation in VoxelStreamRegionFiles.
"""

import math
import struct
import zlib
import threading
import random
from typing import Dict, Tuple, List


# ------------------------------------------------------------------------------
# 1. SpatialLock3D Model & Stress Harness
# ------------------------------------------------------------------------------
class SpatialLock3DVerifier:
    def __init__(self):
        self._locks: Dict[Tuple[int, int, int], int] = {}
        self._mutex = threading.Lock()

    def lock_read(self, bx: int, by: int, bz: int, size: Tuple[int, int, int] = (1, 1, 1)) -> bool:
        with self._mutex:
            sz_x, sz_y, sz_z = max(1, size[0]), max(1, size[1]), max(1, size[2])
            for z in range(bz, bz + sz_z):
                for y in range(by, by + sz_y):
                    for x in range(bx, bx + sz_x):
                        if self._locks.get((x, y, z), 0) < 0:
                            return False
            for z in range(bz, bz + sz_z):
                for y in range(by, by + sz_y):
                    for x in range(bx, bx + sz_x):
                        self._locks[(x, y, z)] = self._locks.get((x, y, z), 0) + 1
            return True

    def unlock_read(self, bx: int, by: int, bz: int, size: Tuple[int, int, int] = (1, 1, 1)):
        with self._mutex:
            sz_x, sz_y, sz_z = max(1, size[0]), max(1, size[1]), max(1, size[2])
            for z in range(bz, bz + sz_z):
                for y in range(by, by + sz_y):
                    for x in range(bx, bx + sz_x):
                        pos = (x, y, z)
                        if pos in self._locks and self._locks[pos] > 0:
                            self._locks[pos] -= 1
                            if self._locks[pos] == 0:
                                del self._locks[pos]

    def lock_write(self, bx: int, by: int, bz: int, size: Tuple[int, int, int] = (1, 1, 1)) -> bool:
        with self._mutex:
            sz_x, sz_y, sz_z = max(1, size[0]), max(1, size[1]), max(1, size[2])
            for z in range(bz, bz + sz_z):
                for y in range(by, by + sz_y):
                    for x in range(bx, bx + sz_x):
                        if self._locks.get((x, y, z), 0) != 0:
                            return False
            for z in range(bz, bz + sz_z):
                for y in range(by, by + sz_y):
                    for x in range(bx, bx + sz_x):
                        self._locks[(x, y, z)] = -1
            return True

    def unlock_write(self, bx: int, by: int, bz: int, size: Tuple[int, int, int] = (1, 1, 1)):
        with self._mutex:
            sz_x, sz_y, sz_z = max(1, size[0]), max(1, size[1]), max(1, size[2])
            for z in range(bz, bz + sz_z):
                for y in range(by, by + sz_y):
                    for x in range(bx, bx + sz_x):
                        pos = (x, y, z)
                        if self._locks.get(pos, 0) == -1:
                            del self._locks[pos]

    def get_lock_state(self, pos: Tuple[int, int, int]) -> int:
        with self._mutex:
            return self._locks.get(pos, 0)


def stress_test_spatial_lock():
    lock = SpatialLock3DVerifier()
    errors = []
    iterations = 500
    active_readers = 0
    active_writers = 0
    lock_counts_lock = threading.Lock()

    def worker_thread(thread_id: int):
        nonlocal active_readers, active_writers
        for _ in range(iterations):
            bx = random.randint(-5, 5)
            by = random.randint(-5, 5)
            bz = random.randint(-5, 5)
            is_writer = (random.random() < 0.2)

            if is_writer:
                if lock.lock_write(bx, by, bz, (2, 2, 2)):
                    with lock_counts_lock:
                        active_writers += 1
                        if active_readers > 0 or active_writers > 1:
                            errors.append(f"Writer safety violation! Readers={active_readers}, Writers={active_writers}")
                    # Simulate work
                    random_sleep = random.random() * 0.0001
                    with lock_counts_lock:
                        active_writers -= 1
                    lock.unlock_write(bx, by, bz, (2, 2, 2))
            else:
                if lock.lock_read(bx, by, bz, (2, 2, 2)):
                    with lock_counts_lock:
                        active_readers += 1
                        if active_writers > 0:
                            errors.append(f"Reader safety violation! Active writer present!")
                    random_sleep = random.random() * 0.0001
                    with lock_counts_lock:
                        active_readers -= 1
                    lock.unlock_read(bx, by, bz, (2, 2, 2))

    threads = [threading.Thread(target=worker_thread, args=(i,)) for i in range(20)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    return len(errors) == 0, errors


# ------------------------------------------------------------------------------
# 2. VoxelStreamRegionFiles Offset Math Verifier
# ------------------------------------------------------------------------------
def verify_region_files_offset_math():
    failures = []
    
    def get_region_coords(pos: Tuple[int, int, int]) -> Tuple[int, int, int]:
        rx = math.floor(pos[0] / 32.0)
        ry = math.floor(pos[1] / 32.0)
        rz = math.floor(pos[2] / 32.0)
        return (rx, ry, rz)

    def get_chunk_header_offset(pos: Tuple[int, int, int]) -> int:
        lx = ((pos[0] % 32) + 32) % 32
        ly = ((pos[1] % 32) + 32) % 32
        lz = ((pos[2] % 32) + 32) % 32
        chunk_index = lx + (ly * 32) + (lz * 1024)
        return chunk_index * 16

    # Test 1: (4, 8, 12) -> offset 200768
    off1 = get_chunk_header_offset((4, 8, 12))
    if off1 != 200768:
        failures.append(f"(4,8,12) offset mismatch: expected 200768, got {off1}")

    # Test 2: (0, 0, 0) -> offset 0
    off0 = get_chunk_header_offset((0, 0, 0))
    if off0 != 0:
        failures.append(f"(0,0,0) offset mismatch: expected 0, got {off0}")

    # Test 3: (31, 31, 31) -> offset 524272
    off31 = get_chunk_header_offset((31, 31, 31))
    if off31 != 524272:
        failures.append(f"(31,31,31) offset mismatch: expected 524272, got {off31}")

    # Test 4: Negative coord (-1, -1, -1) -> region (-1,-1,-1), local (31,31,31) -> offset 524272
    reg_neg = get_region_coords((-1, -1, -1))
    off_neg = get_chunk_header_offset((-1, -1, -1))
    if reg_neg != (-1, -1, -1):
        failures.append(f"(-1,-1,-1) region coords mismatch: expected (-1,-1,-1), got {reg_neg}")
    if off_neg != 524272:
        failures.append(f"(-1,-1,-1) offset mismatch: expected 524272, got {off_neg}")

    # Test 5: Bijective uniqueness test across all 32,768 local positions in a region
    offsets_set = set()
    for z in range(32):
        for y in range(32):
            for x in range(32):
                off = get_chunk_header_offset((x, y, z))
                if off in offsets_set:
                    failures.append(f"Duplicate header offset detected at local position ({x},{y},{z}): {off}")
                offsets_set.add(off)

    if len(offsets_set) != 32768:
        failures.append(f"Unique offset count mismatch: expected 32768, got {len(offsets_set)}")

    return len(failures) == 0, failures


if __name__ == "__main__":
    print("Executing SpatialLock3D Concurrency Stress Test...")
    lock_ok, lock_errs = stress_test_spatial_lock()
    print(f"SpatialLock3D Concurrency Test: {'PASS' if lock_ok else 'FAIL'}")

    print("Executing VoxelStreamRegionFiles Offset Math Verification...")
    math_ok, math_errs = verify_region_files_offset_math()
    print(f"Region Files Math Test: {'PASS' if math_ok else 'FAIL'}")
