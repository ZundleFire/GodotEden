# Dead Ends — Milestone 2

| Iteration | Approach Tried | Why It Failed | Files Touched |
|-----------|---------------|---------------|---------------|
| 1 | In-memory HashMap stubs for `VoxelStreamSQLite` and `VoxelStreamRegionFiles` with empty `flush()` methods | Failed reviewer audit as dummy facade persistence implementations (Integrity Violation); disk I/O was not executed | `streaming/voxel_stream_sqlite.cpp`, `streaming/voxel_stream_region_files.cpp` |
| 1 | `Ref<VoxelBuffer>(const_cast<VoxelBuffer*>(this))` temporary Ref instantiation inside `duplicate_buffer()` | Temporary `Ref` destructor decrements reference count of `this`, risking premature `memdelete` if called on raw/stack instance | `storage/voxel_buffer.cpp` |
