# Micro-Voxel Renderer Architecture & LOD System Technical Survey

**Explorer 2 Architectural Survey & Technical Analysis Report**  
**Project:** GodotEden Engine (`GodotEden`)  
**Working Directory:** `C:\DEV_DRIVE\Dev\GodotEden\.agents\teamwork_preview_explorer_survey_2`  
**Date:** 2026-08-06  

---

## Executive Summary

This survey provides a comprehensive technical investigation into **Micro-Voxel Renderer Architecture**, **Data Structures**, **Level-of-Detail (LOD) Systems**, and **Godot 4 RenderingDevice Compute Integration** for planetary-scale voxel environments. Inspired by industry-leading micro-voxel engines—specifically **Voxelis** (primary architecture reference), **Gvox** (decoupled streaming & palette compression), and **Voxely** (continuous LOD & dual rendering paths)—this report establishes the architectural blueprint for the built-in `godot_eden` C++ module.

---

## 1. Reference Architecture Analysis: Voxelis, Gvox, and Voxely

### 1.1 Voxelis Architecture (Primary Architectural Reference)
Voxelis (`https://github.com/WildPixelGames/voxelis`) represents the state-of-the-art in real-time Vulkan micro-voxel rendering for planetary terrain.
* **Hierarchical GPU Data Pipelines**: Voxelis avoids traditional polygon rasterization for micro-voxels. Instead, it streams sparse hierarchical voxel fields (Sparse Voxel Octrees / SVDAGs) directly into Vulkan Shader Storage Buffer Objects (SSBOs).
* **Compute Raymarching Engine**: Rendering is driven by Vulkan Compute Shaders executing Digital Differential Analyzer (DDA) raymarching through octree structures per pixel. Rays traverse bounding boxes and step through octree leaf nodes without geometry pipelines.
* **Toroidal Clipmap Rings**: To render planetary scales without infinite memory usage, Voxelis relies on concentric 3D clipmap grids ($LOD_0, LOD_1, \dots, LOD_k$) centered on the player position. As the player translates, clipmap buffers wrap toroidally in $O(1)$ time, updating only dirty slices.
* **Floating World Origin**: Combines camera-relative rendering with double-precision coordinate offsets, avoiding 32-bit floating-point precision jitter when travelling across millions of meters on planet surfaces.

### 1.2 Gvox Engine Architecture
Gvox (`https://github.com/GabeRundlett/gvox_engine`) focuses on modular voxel storage formats and hardware-friendly compression.
* **Decoupled Voxel Pipeline**: Separates procedural generation, serialization formats, and GPU renderer structures. Raw voxel streams are converted to intermediate brick arrays or palette-compressed buffers.
* **GPU Palette Compression**: Packs voxel material IDs and 8-bit channels into local sub-brick palettes ($8^3$ or $16^3$ voxel blocks). This compresses memory footprint by $4\times - 8\times$, drastically reducing GPU VRAM bandwidth during ray traversal.
* **Direct Vulkan Interop**: Uploads packed palette buffers directly to 3D texture arrays or SSBOs, bypassing CPU-side unpacking.

### 1.3 Voxely Architectural Principles
Voxely (`https://voxely.net/blog/the-perfect-voxel-engine/`) emphasizes continuous LOD transitions and multi-resolution representation.
* **Continuous LOD & Blend Margins**: Prevents popping artifacts between adjacent LOD rings by using dithered cross-fading or Signed Distance Function (SDF) morphing along border margins.
* **Decoupled Occupancy & Attribute Storage**: Stores geometric occupancy (1-bit / 8-bit SDF) separately from high-detail surface material attributes (albedo, normal, roughness), allowing millimeter geometric fidelity without inflating material memory.
* **Dual Rendering Paths**: Employs GPU compute raymarching for distant macro/micro terrain visual presentation, while generating localized dual meshes (Surface Nets / Dual Contouring) around the player for physics collision (`ConcavePolygonShape3D`).

---

## 2. Micro-Voxel Data Structures & Memory Layouts

Micro-voxels (sub-centimeter to millimeter resolution voxels) require specialized spatial acceleration structures to achieve real-time GPU performance and low memory footprints.

```
                              Voxel Data Structures
                                        │
             ┌──────────────────────────┼──────────────────────────┐
             ▼                          ▼                          ▼
   Sparse Voxel Octree         Sparse Voxel DAG             Voxel Clipmaps
         (SVO)                      (SVDAG)                   (Clipboxes)
   Hierarchical 8-way pool     Subtree Deduplication      Concentric Toroidal Grids
   Fast ray skipping           10x-100x Compression       Constant O(1) Memory
```

### 2.1 Sparse Voxel Octree (SVO)
* **Structure**: An 8-way tree structure where each internal node points to up to 8 child nodes. Leaf nodes store material IDs or SDF values.
* **Memory Layout**: Represented as a flat node array (pool) in a single Vulkan SSBO.
* **Node Packing (16-Byte `std430` Aligned)**:
  ```cpp
  struct SvoNode {
      uint32_t child_mask = 0;       // Bitmask (bits 0-7) for active child nodes
      uint32_t first_child_idx = 0;  // Index in node array of the first contiguous child (0 if leaf)
      uint32_t material_tag = 0;     // Material attribute ID / palette index
      float sdf_value = 0.0f;        // Representative Signed Distance Function value
  };
  ```
* **Subtree Bricking**: To maximize GPU cache hit ratios, SVO leaves point to dense $16^3$ or $32^3$ voxel "bricks", combining hierarchical skipping with dense SIMD array access.

### 2.2 Sparse Voxel Directed Acyclic Graph (SVDAG)
* **Structure**: Extends the SVO by merging identical subtrees. If two octree nodes have identical child configurations and material tags, they share a single node entry in the pool.
* **Deduplication Algorithm**: Built bottom-up using a hash table (`SvoDagKey` mapped via Murmur3 or xxHash):
  ```cpp
  struct SvoDagKey {
      uint32_t children[8];
      uint32_t material_tag;
      uint8_t child_mask;

      bool operator==(const SvoDagKey &p_other) const {
          if (child_mask != p_other.child_mask || material_tag != p_other.material_tag) return false;
          for (int i = 0; i < 8; i++) {
              if (children[i] != p_other.children[i]) return false;
          }
          return true;
      }
  };
  ```
* **Compression Efficiency**: Reduces SVO memory footprint by $10\times - 100\times$ for repetitive terrain (rock layers, dirt, planetary crust).
* **Dynamic Edit Challenge**: SVDAGs are read-optimized. Dynamic voxel modifications (digging/building) require marking subtree branches dirty, copying nodes on write (COW), and updating the hash table.

### 2.3 Nested Toroidal Voxel Clipmaps
* **Structure**: $k$ concentric 3D grids ($N \times N \times N$, e.g. $64 \times 64 \times 64$) centered on the active camera. Level $i$ has voxel scale $s_i = s_0 \cdot 2^i$.
* **Toroidal Indexing**: When the camera moves by $\Delta \mathbf{x}$, grid indices wrap modulo $N$:
  $$\text{GridPos}(x, y, z) = (\lfloor x / s_i \rfloor \bmod N, \lfloor y / s_i \rfloor \bmod N, \lfloor z / s_i \rfloor \bmod N)$$
* **Memory & Updates**: Fixed VRAM consumption regardless of world size; updating position requires streaming only the newly exposed 2D slices along the movement vector ($O(N^2)$ per ring update).

### 2.4 Data Structure Trade-Off Matrix

| Data Structure | VRAM Footprint | Raymarching Speed | Dynamic Edit Complexity | GPU Cache Locality | Best Use Case |
|---|---|---|---|---|---|
| **Sparse Voxel Octree (SVO)** | Moderate ($50-200$ MB/km³) | High (Hierarchical Ray Skip) | Low (Direct Index Mod) | Medium (Pointer Chasing) | Dynamic terrain, moderate distance |
| **Sparse Voxel DAG (SVDAG)** | Ultra-Low ($5-20$ MB/km³) | Very High | High (Subtree Re-hashing) | High (Shared Subtrees) | Static planetary terrain, background |
| **Voxel Clipmap** | Constant (Fixed per Ring) | Extremely High (Direct 3D Texture/Array Lookup) | Very Low (Slice Overwrite) | Maximum (Dense 3D Grid) | Camera-centric real-time rendering |
| **Brickmap / Chunk Grid** | High ($100-500$ MB/km³) | Medium (Dense Brick Traverser) | Very Low (Local Block Swap) | Maximum (Dense Memory) | Local player interaction, editing |

---

## 3. Rendering Pipelines: GPU Compute Raymarching vs Fast Meshing

```
                             Rendering Architecture
                                       │
            ┌──────────────────────────┴──────────────────────────┐
            ▼                                                     ▼
   GPU Compute Raymarching Path                          Fast Mesh Generation Path
 (VoxelRendererRD / Vulkan Compute)                   (PhysicsMeshGenerator / Engine)
            │                                                     │
   Screen-Space DDA Raymarch                              ┌───────┴───────┐
   Multi-scale Clipmap Traversal                          ▼               ▼
   16-bit Octahedral Normals                        Dual Contouring   Greedy Meshing
   Zero Geometry Pass                            (Physics Terrain) (Collision Shapes)
```

### 3.1 GPU Compute Raymarching Pipeline
The compute raymarching engine dispatches GLSL compute shaders (`micro_voxel_raymarch.glsl`) via Godot 4 `RenderingDevice`.

* **Ray Generation & Workgroup Layout**:
  * Workgroups are dispatched in 2D tiles of size $(8, 8, 1)$ across screen pixels.
  * Ray origin $\mathbf{O} = \mathbf{P}_{\text{cam}}$, direction $\mathbf{D} = \text{normalize}(\text{unproject}(\text{pixel\_uv}))$.
* **Digital Differential Analyzer (DDA) Algorithm**:
  1. Intersect primary ray with bounding volume AABB using slab method ($t_{\text{near}}, t_{\text{far}}$).
  2. Compute step directions $\mathbf{step} = \text{sign}(\mathbf{D})$ and ray delta terms $\mathbf{t}_{\Delta} = |1.0 / \mathbf{D}|$.
  3. Traverse SVO/Clipmap levels: step along the axis with minimum current ray distance $t_{\text{max}}$.
  4. Upon reaching an occupied leaf node or SDF crossing ($SDF \le 0$), calculate surface hit position $\mathbf{P}_{\text{hit}} = \mathbf{O} + t_{\text{hit}} \mathbf{D}$.
* **Material & Normal Unpacking**:
  To minimize SSBO bandwidth, material properties are packed into std430 32-byte structures:
  ```glsl
  // Octahedral 16-bit Normal Unpacking
  vec3 unpack_oct16(uint p_packed) {
      vec2 e = vec2(float(p_packed & 0xFFu), float((p_packed >> 8u) & 0xFFu)) / 255.0 * 2.0 - 1.0;
      vec3 v = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
      if (v.z < 0.0) {
          v.xy = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
      }
      return normalize(v);
  }
  ```

### 3.2 Fast Mesh Generation Algorithms

When geometry is required for physics collision (`ConcavePolygonShape3D`), shadow mapping, or legacy rasterization backends, fast meshing algorithms generate polygonal surfaces from voxel volumes.

#### 1. Smooth Surface Nets (Dual Grid)
* **Algorithm**: For every $2 \times 2 \times 2$ dual cell with isosurface edge sign changes, place a single vertex at the average centroid of edge intersection points.
* **Characteristics**: Fast, produces smooth manifold meshes with low quad counts, ideal for organic terrain.

#### 2. Dual Contouring (DC) (Dual Grid)
* **Algorithm**: Evaluates Hermite data (exact isosurface intersection points $\mathbf{p}_i$ and surface normals $\mathbf{n}_i$). Positions the dual cell vertex by solving the Quadratic Error Function (QEF):
  $$E(\mathbf{x}) = \sum_i (\mathbf{n}_i \cdot (\mathbf{x} - \mathbf{p}_i))^2$$
* **QEF Solution**: Solved via Singular Value Decomposition (SVD) or pseudoinverse.
* **Advantage**: Preserves sharp geometric features (corners, edges, artificial structures) while remaining smooth on curved surfaces.

#### 3. Marching Cubes & Transvoxel (Primal Grid)
* **Marching Cubes**: Evaluates 256 corner occupancy configurations per voxel cube to emit pre-calculated triangulations.
* **Transvoxel**: Solves LOD boundary seams between adjacent octree nodes of differing resolution by introducing transition cells (512 lookup cases), eliminating visual gaps without requiring geometry skirts.

#### 4. Greedy Meshing (Primal Grid)
* **Algorithm**: Sweeps 2D slices along X, Y, Z axes. Merges adjacent coplanar quad faces sharing identical material IDs into large single quads.
* **Performance**: Reduces quad and vertex counts by $80\% - 90\%$, making it the optimal generator for blocky terrain and physics collision meshes.

### 3.3 Comparative Technical Matrix

| Metric / Requirement | GPU Compute Raymarching | Dual Contouring | Smooth Surface Nets | Transvoxel | Greedy Meshing |
|---|---|---|---|---|---|
| **Grid Type** | SVO / SVDAG / Clipmap | Dual Grid | Dual Grid | Primal Grid | Primal Grid |
| **Sharp Edge Preservation** | Excellent (Procedural SDF) | Excellent (QEF Solved) | Poor (Smoothed) | Poor (Clamped) | Excellent (Blocky) |
| **GPU Draw Call Overhead** | Zero (Compute Dispatch) | High (Multi-Mesh) | High (Multi-Mesh) | High (Multi-Mesh) | High (Multi-Mesh) |
| **Physics Collision Support** | Requires Mesh Pass | Direct Output | Direct Output | Direct Output | Direct Output (Best) |
| **Memory Footprint** | Extremely Low (SSBO) | Moderate (VB/IB) | Moderate (VB/IB) | High (VB/IB) | Low (Merged Quads) |
| **LOD Seam Resolution** | Seamless (Cone/SDF) | Hermite Transition | Skirts / Transition | Transvoxel Cells | Skirts |

---

## 4. Planetary Scale Level-of-Detail (LOD) & World Origin Systems

Planetary rendering spans scales from $10^{-3}$ meters (micro-voxels at player feet) to $10^7$ meters (planetary orbit). Managing LOD and precision across 10 orders of magnitude requires strict mathematical error metrics and reference frame shifting.

### 4.1 Screen-Space Error Metrics & LOD Criteria
The LOD level $k$ for a voxel node at distance $D$ from the camera is derived from the allowable screen-space geometric error $\tau$ (in pixels):

$$\text{Error}_{\text{screen}} = \frac{\delta \cdot y_{\text{res}}}{2 \cdot D \cdot \tan(\text{FOV}/2)} \le \tau$$

Where:
* $\delta = \text{voxel\_size}(k) = s_0 \cdot 2^k$ is the geometric voxel size at level $k$.
* $y_{\text{res}}$ is the vertical viewport resolution in pixels.
* $\text{FOV}$ is the camera field of view.
* $D$ is the Euclidean distance from camera to the node center.

Solving for required LOD level $k$:
$$k = \left\lfloor \log_2 \left( \frac{2 \cdot D \cdot \tau \cdot \tan(\text{FOV}/2)}{s_0 \cdot y_{\text{res}}} \right) \right\rfloor$$

#### Hysteresis Band & LOD Ping-Ponging
To prevent constant LOD switching (ping-ponging) when the camera oscillates near a boundary distance $D_{\text{split}}$, a hysteresis margin $\epsilon_{\text{hyst}} \approx 0.15$ is applied:
* Split node to finer LOD if $D < D_{\text{split}} \cdot (1.0 - \epsilon_{\text{hyst}})$.
* Merge nodes to coarser LOD if $D > D_{\text{split}} \cdot (1.0 + \epsilon_{\text{hyst}})$.

### 4.2 Clipmap Toroidal Updates & Ring Shifting
Concentric clipmap levels ($k = 0 \dots L-1$) are updated in world space relative to the camera:

1. **Snapped Ring Center**:
   $$\mathbf{P}_{\text{snapped}, k} = \lfloor \mathbf{P}_{\text{cam}} / s_k \rfloor \cdot s_k$$
2. **Toroidal Grid Offset**:
   $$\mathbf{O}_k = \left( \lfloor \mathbf{P}_{\text{snapped}, k} / s_k \rfloor \right) \bmod N_{\text{grid}}$$
3. **Dirty Slice Detection**: When $\mathbf{P}_{\text{snapped}, k}$ shifts by $m$ voxel steps along axis $A$, only the $m$ wrapped slices in the 3D texture/buffer are marked dirty and repopulated via compute background threads.

### 4.3 Seamless LOD Transitions & Blend Margins
To eliminate pop-in between adjacent LOD rings, shaders apply distance-based dithered blending across a transition zone $[D_{\text{inner}}, D_{\text{outer}}]$:

$$\alpha_{\text{blend}} = \text{clamp}\left( \frac{D - D_{\text{inner}}}{D_{\text{outer}} - D_{\text{inner}}}, 0.0, 1.0 \right)$$

In the GLSL compute shader, a blue noise dither matrix compares $\alpha_{\text{blend}}$ against pixel threshold values to sample between coarse and fine voxel clipmaps seamlessly.

### 4.4 Floating World Origin & Precision Jitter Mitigation
Single-precision 32-bit IEEE 754 floats lose sub-millimeter precision at distances exceeding $10,000$ meters ($24\text{-bit}$ significand $\rightarrow \sim 1\text{ mm}$ resolution at $10\text{ km}$, $\sim 1\text{ m}$ resolution at $10,000\text{ km}$).

* **Double Precision World Coordinates**: The global universe position of planets, ships, and players is tracked using 64-bit vectors (`Vector3i` chunk offsets + `Vector3` local positions or double-precision C++ structs).
* **Camera-Relative Rendering**: Before submitting node transformations or push constants to the GPU, all coordinates are offset by the active camera's 64-bit world position:
  $$\mathbf{X}_{\text{gpu}} = \text{float32}(\mathbf{X}_{\text{world\_64}} - \mathbf{P}_{\text{cam\_64}})$$
* **GodotEden Reference Body System**: Integrated with `solar_system.gd` via `ReferenceChangeInfo` events, automatically re-centering the coordinate origin when crossing planetary thresholds.

---

## 5. Shader Architecture & Godot 4 RenderingDevice Integration

Godot 4 introduces the `RenderingDevice` (RD) API—a low-level abstraction over Vulkan and Direct3D 12. Using RD, `godot_eden` executes compute shaders and manages SSBOs directly on the GPU without going through the standard high-level scene renderer.

```
                           Godot 4 Architecture
                                     │
                        VoxelRendererRD (Node3D)
                                     │
           ┌─────────────────────────┴─────────────────────────┐
           ▼                                                   ▼
 Godot RenderingDevice (RD)                          Godot RenderingServer (RS)
 (Vulkan Compute Shader Pipeline)                    (Mesh & Scene Graph Path)
           │                                                   │
 ┌─────────┼─────────┬─────────┐                               ▼
 ▼         ▼         ▼         ▼                        MeshInstance3D
Shader   SSBOs   Pipelines  Uniforms                   ArrayMesh
(SPIR-V) (Data)  (Compute)  (Sets)               ConcavePolygonShape3D
```

### 5.1 RenderingDevice Lifecycle & Class Architecture
The C++ node `VoxelRendererRD` (extending `Node3D`) encapsulates GPU compute management:

1. **Device Access**: Obtains the global rendering device:
   ```cpp
   RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
   ```
2. **SPIR-V Shader Initialization**: Reads precompiled SPIR-V byte arrays from header wrappers (`micro_voxel_raymarch.glsl.gen.h`):
   ```cpp
   Ref<RDShaderFile> shader_file;
   // Loaded from compiled GLSL SPIR-V
   RID shader_rid = rd->shader_create_from_spirv(shader_spirv_data);
   RID pipeline_rid = rd->compute_pipeline_create(shader_rid);
   ```

### 5.2 GPU Memory Layouts & SSBO Specifications (`std430`)

To ensure exact byte alignment between C++ and Vulkan GLSL, all structs strictly adhere to `std430` alignment rules.

#### 1. SVO Node SSBO (`std430` - 16 Bytes)
```cpp
// C++ Declaration (godot_eden/storage/lod_octree.h)
struct alignas(16) SvoNodeGpu {
    uint32_t child_mask;       // Offset 0
    uint32_t first_child_idx;  // Offset 4
    uint32_t material_tag;     // Offset 8
    float sdf_value;           // Offset 12
};
```
```glsl
// GLSL Declaration (shaders/micro_voxel_raymarch.glsl)
struct SvoNodeGpu {
    uint child_mask;
    uint first_child_idx;
    uint material_tag;
    float sdf_value;
};

layout(std430, set = 0, binding = 0) restriction readonly buffer SvoNodeBuffer {
    SvoNodeGpu nodes[];
} svo_buffer;
```

#### 2. Clipmap Level Metadata SSBO (`std430` - 32 Bytes)
```cpp
struct alignas(16) ClipmapLevelGpu {
    Vector3i snapped_origin;   // Offset 0 (12 bytes)
    float voxel_size;          // Offset 12 (4 bytes)
    Vector3i toroidal_offset;  // Offset 16 (12 bytes)
    uint32_t lod_level;        // Offset 28 (4 bytes)
};
```
```glsl
struct ClipmapLevelGpu {
    ivec3 snapped_origin;
    float voxel_size;
    ivec3 toroidal_offset;
    uint lod_level;
};

layout(std430, set = 0, binding = 1) restriction readonly buffer ClipmapBuffer {
    ClipmapLevelGpu levels[];
} clipmap_buffer;
```

#### 3. Push Constants Layout (64 Bytes)
```cpp
struct RaymarchPushConstants {
    Transform3D inv_view_proj; // 48 bytes (Camera Unprojection)
    Vector3 camera_pos;        // 12 bytes
    uint32_t frame_index;      // 4 bytes (For temporal dither)
};
```

### 5.3 Compute Dispatch & Frame Execution Sequence

During every render frame notification (`NOTIFICATION_INTERNAL_PROCESS`), `VoxelRendererRD` dispatches compute raymarching:

```cpp
void VoxelRendererRD::_render_compute_pass(const Size2i &p_viewport_size) {
    RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
    if (!rd || !compute_pipeline.is_valid()) return;

    // 1. Update Push Constants
    RaymarchPushConstants push_constants;
    push_constants.inv_view_proj = get_camera_inv_view_proj();
    push_constants.camera_pos = get_camera_relative_pos();
    push_constants.frame_index = Engine::get_singleton()->get_process_frames();

    // 2. Dispatch Compute List
    int64_t compute_list = rd->compute_list_begin();
    rd->compute_list_bind_compute_pipeline(compute_list, compute_pipeline);
    rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
    rd->compute_list_set_push_constant(compute_list, &push_constants, sizeof(RaymarchPushConstants));

    uint32_t group_x = (p_viewport_size.width + 7) / 8;
    uint32_t group_y = (p_viewport_size.height + 7) / 8;
    rd->compute_list_dispatch(compute_list, group_x, group_y, 1);
    
    rd->compute_list_end();
}
```

---

## 6. Synthesis & Architectural Blueprint for `GodotEden`

Based on this technical survey, the recommended architecture for the `godot_eden` C++ engine module combines the strengths of Voxelis, Gvox, and Voxely:

```
                                  GodotEden Module
                                         │
              ┌──────────────────────────┴──────────────────────────┐
              ▼                                                     ▼
     Distant Terrain Path                                  Local Terrain Path
   (Macro / Micro Raymarch)                             (Physics & Interaction)
              │                                                     │
   SVDAG + Toroidal Clipmaps                             Dual Contouring / Transvoxel
   Packed GPU Palettes (32-byte std430)                  ConcavePolygonShape3D
   Vulkan Compute Raymarch (RD API)                      Smooth Surface MeshInstance3D
```

### Key Architectural Recommendations:

1. **Adopt a Dual-Path Pipeline**:
   * **Compute Raymarching Path (`VoxelRendererRD`)**: Use SVDAG node pools and toroidal clipmaps for rendering vast planetary landscapes at zero draw call cost.
   * **Dual Contouring / Meshing Path (`PhysicsMeshGenerator` / `VoxelLodTerrain`)**: Generate local polygonal meshes around the player for physics collision (`ConcavePolygonShape3D`) and interactive dig/build modification.
2. **Standardize Memory Buffers with Palette Compression**:
   * Implement sub-brick palette packing (Gvox-style) into 32-byte std430 GPU material structs, storing octahedral 16-bit packed normals and 8-bit RGBA albedo.
3. **Seamless Floating World Origin Integration**:
   * Connect clipmap toroidal ring updates directly to GodotEden's floating world origin system (`solar_system.gd` `ReferenceChangeInfo`), preserving sub-millimeter precision across millions of kilometers of space travel.

---
