import os
import sys
import math
import struct
import threading
import tempfile
import random

class SpatialLock3DSimulator:
    def __init__(self):
        self.lock = threading.Lock()
        self.lock_counts = {}
        self.active_read_regions = {} # origin -> {size: count}
        self.active_write_regions = {} # origin -> size

    def lock_read(self, block_pos, size=(1,1,1)):
        with self.lock:
            sz = (max(1, size[0]), max(1, size[1]), max(1, size[2]))
            for z in range(sz[2]):
                for y in range(sz[1]):
                    for x in range(sz[0]):
                        pos = (block_pos[0]+x, block_pos[1]+y, block_pos[2]+z)
                        if pos in self.lock_counts and self.lock_counts[pos] < 0:
                            return False
            for z in range(sz[2]):
                for y in range(sz[1]):
                    for x in range(sz[0]):
                        pos = (block_pos[0]+x, block_pos[1]+y, block_pos[2]+z)
                        self.lock_counts[pos] = self.lock_counts.get(pos, 0) + 1

            if block_pos not in self.active_read_regions:
                self.active_read_regions[block_pos] = {}
            self.active_read_regions[block_pos][sz] = self.active_read_regions[block_pos].get(sz, 0) + 1
            return True

    def unlock_read(self, block_pos, size=(1,1,1)):
        with self.lock:
            req_sz = (max(1, size[0]), max(1, size[1]), max(1, size[2]))
            sz = req_sz

            if block_pos in self.active_read_regions:
                sizes = self.active_read_regions[block_pos]
                if req_sz in sizes:
                    sz = req_sz
                    sizes[req_sz] -= 1
                    if sizes[req_sz] <= 0:
                        del sizes[req_sz]
                elif len(sizes) > 0:
                    first_key = next(iter(sizes))
                    sz = first_key
                    sizes[first_key] -= 1
                    if sizes[first_key] <= 0:
                        del sizes[first_key]
                if len(sizes) == 0:
                    del self.active_read_regions[block_pos]

            for z in range(sz[2]):
                for y in range(sz[1]):
                    for x in range(sz[0]):
                        pos = (block_pos[0]+x, block_pos[1]+y, block_pos[2]+z)
                        if pos in self.lock_counts:
                            if self.lock_counts[pos] > 0:
                                self.lock_counts[pos] -= 1
                                if self.lock_counts[pos] == 0:
                                    del self.lock_counts[pos]

    def lock_write(self, block_pos, size=(1,1,1)):
        with self.lock:
            sz = (max(1, size[0]), max(1, size[1]), max(1, size[2]))
            for z in range(sz[2]):
                for y in range(sz[1]):
                    for x in range(sz[0]):
                        pos = (block_pos[0]+x, block_pos[1]+y, block_pos[2]+z)
                        if pos in self.lock_counts and self.lock_counts[pos] != 0:
                            return False
            for z in range(sz[2]):
                for y in range(sz[1]):
                    for x in range(sz[0]):
                        pos = (block_pos[0]+x, block_pos[1]+y, block_pos[2]+z)
                        self.lock_counts[pos] = -1

            self.active_write_regions[block_pos] = sz
            return True

    def unlock_write(self, block_pos, size=(1,1,1)):
        with self.lock:
            req_sz = (max(1, size[0]), max(1, size[1]), max(1, size[2]))
            sz = req_sz

            if block_pos in self.active_write_regions:
                sz = self.active_write_regions[block_pos]
                del self.active_write_regions[block_pos]

            for z in range(sz[2]):
                for y in range(sz[1]):
                    for x in range(sz[0]):
                        pos = (block_pos[0]+x, block_pos[1]+y, block_pos[2]+z)
                        if pos in self.lock_counts and self.lock_counts[pos] == -1:
                            del self.lock_counts[pos]

    def is_locked(self, block_pos):
        with self.lock:
            return block_pos in self.lock_counts and self.lock_counts[block_pos] != 0

    def get_lock_state(self, block_pos):
        with self.lock:
            return self.lock_counts.get(block_pos, 0)


class VoxelStreamerSimulator:
    def __init__(self, max_pending=16):
        self.lock = threading.Lock()
        self.max_pending_requests = max(1, max_pending)
        self.view_center = (0, 0, 0)
        self.view_radius = 8
        self.active = False
        self.pending_requests = set()

    def set_max_pending_requests(self, p_max):
        with self.lock:
            self.max_pending_requests = max(1, p_max)

    def set_view_center(self, center):
        with self.lock:
            self.view_center = center

    def request_block(self, block_pos):
        with self.lock:
            if len(self.pending_requests) < self.max_pending_requests:
                self.pending_requests.add(block_pos)

    def cancel_request(self, block_pos):
        with self.lock:
            self.pending_requests.discard(block_pos)

    def get_sorted_pending_requests(self):
        with self.lock:
            reqs = list(self.pending_requests)
            reqs.sort(key=lambda p: (p[0]-self.view_center[0])**2 + (p[1]-self.view_center[1])**2 + (p[2]-self.view_center[2])**2)
            return reqs

    def pop_closest_request(self):
        with self.lock:
            if not self.pending_requests:
                return (0, 0, 0)
            reqs = list(self.pending_requests)
            reqs.sort(key=lambda p: (p[0]-self.view_center[0])**2 + (p[1]-self.view_center[1])**2 + (p[2]-self.view_center[2])**2)
            closest = reqs[0]
            self.pending_requests.remove(closest)
            return closest


class VoxelStreamSQLiteSimulator:
    def __init__(self):
        self.lock = threading.Lock()
        self.db_path = ""
        self.open_flag = False
        self.dirty_flag = False
        self.edit_deltas = {}

    def open(self, path):
        with self.lock:
            if self.open_flag and self.dirty_flag:
                self._flush_internal()
            self.db_path = path
            self.edit_deltas.clear()
            self.dirty_flag = False

            if os.path.exists(self.db_path):
                with open(self.db_path, "rb") as f:
                    magic_bytes = f.read(4)
                    if len(magic_bytes) == 4:
                        magic = struct.unpack("<I", magic_bytes)[0]
                        if magic in (0x4553514C, 0x45444442):
                            count_bytes = f.read(4)
                            if len(count_bytes) == 4:
                                count = struct.unpack("<I", count_bytes)[0]
                                for _ in range(count):
                                    hdr = f.read(20)
                                    if len(hdr) < 20:
                                        break
                                    x, y, z, lod, length = struct.unpack("<iiiiI", hdr)
                                    if length > 1024 * 1024:
                                        break
                                    data = f.read(length)
                                    self.edit_deltas[((x, y, z), lod)] = data
            self.open_flag = True

    def save_block(self, pos, lod, data):
        with self.lock:
            if not self.open_flag:
                return False
            self.edit_deltas[(pos, lod)] = data
            self.dirty_flag = True
            return True

    def load_block(self, pos, lod):
        with self.lock:
            if not self.open_flag:
                return b""
            return self.edit_deltas.get((pos, lod), b"")

    def flush(self):
        with self.lock:
            self._flush_internal()

    def _flush_internal(self):
        if not self.open_flag or not self.db_path or not self.dirty_flag:
            return
        temp_path = self.db_path + ".tmp"
        with open(temp_path, "wb") as f:
            f.write(struct.pack("<II", 0x4553514C, len(self.edit_deltas)))
            for (pos, lod), data in self.edit_deltas.items():
                f.write(struct.pack("<iiiiI", pos[0], pos[1], pos[2], lod, len(data)))
                if len(data) > 0:
                    f.write(data)
            f.flush()
        # Atomic swap
        if os.path.exists(self.db_path):
            os.remove(self.db_path)
        os.rename(temp_path, self.db_path)
        self.dirty_flag = False


class VoxelStreamRegionFilesSimulator:
    HEADER_TABLE_SIZE_BYTES = 524288 # 512 KiB

    @staticmethod
    def get_region_coords(chunk_pos):
        rx = math.floor(chunk_pos[0] / 32.0)
        ry = math.floor(chunk_pos[1] / 32.0)
        rz = math.floor(chunk_pos[2] / 32.0)
        return (rx, ry, rz)

    @staticmethod
    def get_chunk_header_offset(chunk_pos):
        lx = ((chunk_pos[0] % 32) + 32) % 32
        ly = ((chunk_pos[1] % 32) + 32) % 32
        lz = ((chunk_pos[2] % 32) + 32) % 32
        chunk_index = lx + (ly * 32) + (lz * 1024)
        return chunk_index * 16

    def __init__(self, region_dir):
        self.region_dir = region_dir
        self.lock = threading.Lock()
        self.region_cache = {}
        self.dirty_chunks = set()
        os.makedirs(self.region_dir, exist_ok=True)

    def _get_region_file_path(self, region_pos):
        filename = f"reg_{region_pos[0]}_{region_pos[1]}_{region_pos[2]}.edr"
        return os.path.join(self.region_dir, filename)

    def write_chunk_bytes(self, chunk_pos, data):
        with self.lock:
            self.region_cache[chunk_pos] = data
            self.dirty_chunks.add(chunk_pos)

    def read_chunk_bytes(self, chunk_pos):
        with self.lock:
            if chunk_pos in self.region_cache:
                return self.region_cache[chunk_pos]
            reg_coords = self.get_region_coords(chunk_pos)
            file_path = self._get_region_file_path(reg_coords)
            if not os.path.exists(file_path):
                return b""
            with open(file_path, "rb") as f:
                header_offset = self.get_chunk_header_offset(chunk_pos)
                f.seek(header_offset)
                hdr = f.read(16)
                if len(hdr) < 16:
                    return b""
                sector_offset, sector_length, timestamp, reserved = struct.unpack("<IIII", hdr)
                if sector_offset < self.HEADER_TABLE_SIZE_BYTES or sector_length == 0:
                    return b""
                f.seek(sector_offset)
                data = f.read(sector_length)
                self.region_cache[chunk_pos] = data
                return data

    def flush_region_files(self):
        with self.lock:
            if not self.dirty_chunks:
                return
            region_groups = {}
            for chunk_pos in self.dirty_chunks:
                reg_coords = self.get_region_coords(chunk_pos)
                if reg_coords not in region_groups:
                    region_groups[reg_coords] = []
                region_groups[reg_coords].append(chunk_pos)

            for reg_coords, chunks in region_groups.items():
                file_path = self._get_region_file_path(reg_coords)
                if not os.path.exists(file_path):
                    with open(file_path, "wb") as f_init:
                        f_init.write(b"\x00" * self.HEADER_TABLE_SIZE_BYTES)
                        f_init.flush()

                with open(file_path, "r+b") as f:
                    # SAT: Read active sectors
                    active_sectors = []
                    for c_idx in range(32 * 32 * 32):
                        f.seek(c_idx * 16)
                        hdr = f.read(8)
                        if len(hdr) == 8:
                            s_off, s_len = struct.unpack("<II", hdr)
                            if s_off >= self.HEADER_TABLE_SIZE_BYTES and s_len > 0:
                                active_sectors.append({'offset': s_off, 'length': s_len})

                    active_sectors.sort(key=lambda s: s['offset'])

                    # SAT: Build free gaps
                    free_gaps = []
                    current_pos = self.HEADER_TABLE_SIZE_BYTES
                    for s in active_sectors:
                        if s['offset'] > current_pos:
                            free_gaps.append({'offset': current_pos, 'length': s['offset'] - current_pos})
                        current_pos = max(current_pos, s['offset'] + s['length'])

                    for chunk_pos in chunks:
                        if chunk_pos not in self.region_cache:
                            continue
                        data = self.region_cache[chunk_pos]
                        header_offset = self.get_chunk_header_offset(chunk_pos)

                        f.seek(header_offset)
                        old_offset, old_length = struct.unpack("<II", f.read(8))

                        data_len = len(data)
                        target_offset = 0

                        if old_offset >= self.HEADER_TABLE_SIZE_BYTES and data_len <= old_length:
                            target_offset = old_offset
                        else:
                            if old_offset >= self.HEADER_TABLE_SIZE_BYTES and old_length > 0:
                                free_gaps.append({'offset': old_offset, 'length': old_length})

                            best_gap_idx = -1
                            for g in range(len(free_gaps)):
                                if free_gaps[g]['length'] >= data_len:
                                    best_gap_idx = g
                                    break

                            if best_gap_idx != -1:
                                target_offset = free_gaps[best_gap_idx]['offset']
                                if free_gaps[best_gap_idx]['length'] == data_len:
                                    free_gaps.pop(best_gap_idx)
                                else:
                                    free_gaps[best_gap_idx]['offset'] += data_len
                                    free_gaps[best_gap_idx]['length'] -= data_len
                            else:
                                f.seek(0, os.SEEK_END)
                                file_len = f.tell()
                                target_offset = max(file_len, self.HEADER_TABLE_SIZE_BYTES)

                        f.seek(target_offset)
                        if data_len > 0:
                            f.write(data)

                        f.seek(header_offset)
                        f.write(struct.pack("<IIII", target_offset, data_len, 12345678, 0))
                    f.flush()

            self.dirty_chunks.clear()


def test_spatial_lock_3d():
    print("--- Testing SpatialLock3D ---")
    lock_sys = SpatialLock3DSimulator()

    # 1. Basic Read/Write
    assert lock_sys.lock_read((0, 0, 0), (2, 2, 2)) == True, "Read lock acquisition failed"
    assert lock_sys.lock_write((1, 1, 1), (1, 1, 1)) == False, "Write lock should fail when reader active"
    assert lock_sys.is_locked((0, 0, 0)) == True
    assert lock_sys.get_lock_state((0, 0, 0)) == 1

    # 2. Additional Reader
    assert lock_sys.lock_read((0, 0, 0), (1, 1, 1)) == True
    assert lock_sys.get_lock_state((0, 0, 0)) == 2

    # 3. Size Mismatch Unlock Safety
    # Unlock read with WRONG size (1,1,1) instead of (2,2,2)
    lock_sys.unlock_read((0, 0, 0), (1, 1, 1)) # Decrements size (1,1,1)
    lock_sys.unlock_read((0, 0, 0), (10, 10, 10)) # Mismatch fallback! Should use size (2,2,2)
    assert lock_sys.is_locked((0, 0, 0)) == False, "Lock count should be 0 after size-mismatch unlock"
    assert lock_sys.get_lock_state((0, 0, 0)) == 0

    # 4. Write Lock Exclusion
    assert lock_sys.lock_write((0, 0, 0), (2, 2, 2)) == True
    assert lock_sys.get_lock_state((0, 0, 0)) == -1
    assert lock_sys.lock_read((0, 0, 0), (1, 1, 1)) == False, "Reader should fail when writer active"
    assert lock_sys.lock_write((1, 1, 1), (1, 1, 1)) == False, "Second writer should fail when writer active"

    # Unlock write with wrong size (1,1,1)
    lock_sys.unlock_write((0, 0, 0), (1, 1, 1)) # Should retrieve tracked (2,2,2)
    assert lock_sys.is_locked((1, 1, 1)) == False, "Write unlock size-mismatch fallback failed"
    assert lock_sys.get_lock_state((1, 1, 1)) == 0

    # 5. Multithreaded stress
    def thread_worker(id):
        for _ in range(100):
            pos = (random.randint(-5, 5), random.randint(-5, 5), random.randint(-5, 5))
            if random.random() < 0.8:
                if lock_sys.lock_read(pos, (2, 2, 2)):
                    lock_sys.unlock_read(pos, (2, 2, 2))
            else:
                if lock_sys.lock_write(pos, (2, 2, 2)):
                    lock_sys.unlock_write(pos, (2, 2, 2))

    threads = [threading.Thread(target=thread_worker, args=(i,)) for i in range(10)]
    for t in threads: t.start()
    for t in threads: t.join()

    print("SpatialLock3D test PASSED.")


def test_voxel_streamer():
    print("--- Testing VoxelStreamer ---")
    streamer = VoxelStreamerSimulator(max_pending=16)
    streamer.set_view_center((10, 10, 10))

    # Fill queue past limit
    for i in range(25):
        streamer.request_block((i, 10, 10))

    sorted_reqs = streamer.get_sorted_pending_requests()
    assert len(sorted_reqs) == 16, f"Queue cap failed, expected 16, got {len(sorted_reqs)}"

    # Pop closest request to view_center (10, 10, 10)
    closest = streamer.pop_closest_request()
    assert closest == (10, 10, 10), f"Closest request expected (10,10,10), got {closest}"

    print("VoxelStreamer test PASSED.")


def test_voxel_stream_sqlite():
    print("--- Testing VoxelStreamSQLite ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        db_path = os.path.join(tmpdir, "test_edits.sqlite")
        db = VoxelStreamSQLiteSimulator()
        db.open(db_path)

        data1 = b"VoxelEditDataChunk1"
        data2 = b"VoxelEditDataChunk2"

        db.save_block((1, 2, 3), 0, data1)
        db.save_block((4, 5, 6), 1, data2)

        # Flush triggers atomic .tmp swap
        db.flush()

        assert os.path.exists(db_path)
        assert not os.path.exists(db_path + ".tmp")

        # Reload from disk
        db2 = VoxelStreamSQLiteSimulator()
        db2.open(db_path)
        assert db2.load_block((1, 2, 3), 0) == data1
        assert db2.load_block((4, 5, 6), 1) == data2

    print("VoxelStreamSQLite test PASSED.")


def test_voxel_stream_region_files():
    print("--- Testing VoxelStreamRegionFiles ---")
    with tempfile.TemporaryDirectory() as tmpdir:
        rf = VoxelStreamRegionFilesSimulator(tmpdir)

        # Test header offset and region coords calculation
        c_pos = (-5, 10, 35)
        reg_coords = VoxelStreamRegionFilesSimulator.get_region_coords(c_pos)
        assert reg_coords == (-1, 0, 1), f"Expected (-1, 0, 1), got {reg_coords}"

        hdr_offset = VoxelStreamRegionFilesSimulator.get_chunk_header_offset(c_pos)
        assert 0 <= hdr_offset < 524288, f"Header offset out of bounds: {hdr_offset}"

        # Write chunks
        chunk1 = (0, 0, 0)
        chunk2 = (1, 0, 0)
        payload1 = b"Payload1_InitialData" # len 20
        payload2 = b"Payload2_InitialData" # len 20

        rf.write_chunk_bytes(chunk1, payload1)
        rf.write_chunk_bytes(chunk2, payload2)
        rf.flush_region_files()

        reg_file = rf._get_region_file_path((0, 0, 0))
        assert os.path.exists(reg_file)
        file_size = os.path.getsize(reg_file)
        assert file_size >= 524288 + 40, f"File size unexpectedly small: {file_size}"

        # SAT gap sector reuse test
        # Expand chunk1 so old sector is freed into free_gaps
        payload1_expanded = b"Payload1_ExpandedData_RequiresLargerSector_1234567890" # len 53
        rf.write_chunk_bytes(chunk1, payload1_expanded)
        rf.flush_region_files()

        # Write chunk3 with size <= 20 to check if free gap at 524288 is reused
        chunk3 = (2, 0, 0)
        payload3 = b"Payload3_GapReuse" # len 17
        rf.write_chunk_bytes(chunk3, payload3)
        rf.flush_region_files()

        # Read chunk3 and verify boundary check
        read_payload3 = rf.read_chunk_bytes(chunk3)
        assert read_payload3 == payload3, f"Read failed: {read_payload3}"

        # Test Boundary check enforcement (sector_offset < 524288 must return empty)
        # Corrupt header table entry for chunk4 to point to offset 100 (< 524288)
        chunk4 = (3, 0, 0)
        hdr_off4 = rf.get_chunk_header_offset(chunk4)
        with open(reg_file, "r+b") as f:
            f.seek(hdr_off4)
            f.write(struct.pack("<IIII", 100, 50, 0, 0)) # corrupt sector offset = 100

        read_payload4 = rf.read_chunk_bytes(chunk4)
        assert read_payload4 == b"", "Boundary check failed: corrupted header allowed offset < 524288!"

    print("VoxelStreamRegionFiles test PASSED.")


if __name__ == "__main__":
    test_spatial_lock_3d()
    test_voxel_streamer()
    test_voxel_stream_sqlite()
    test_voxel_stream_region_files()
    print("\nALL EMPIRICAL VERIFICATION TESTS PASSED SUCCESSFULLY!")
