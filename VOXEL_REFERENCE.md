# Voxel Module – Complete Reference Guide
> **Engine:** `F:\Dev\GodotEden` (Godot 4 fork with custom `modules/voxel` built-in)
> **Project:** `F:\Dev\Projects\eden_project`
> **Module source:** `F:\Dev\GodotEden\modules\voxel`
> **GDScript addon (shaders/inc):** `res://addons/zylann.voxel/`

---

## Table of Contents
1. [Module Architecture](#1-module-architecture)
2. [Core Node Types](#2-core-node-types)
3. [VoxelLodTerrain – Full API Reference](#3-voxellodterrain--full-api-reference)
4. [VoxelGeneratorGraph – Node Graph System](#4-voxelgeneratorgraph--node-graph-system)
5. [VoxelGeneratorScript – GDScript Generator](#5-voxelgeneratorscript--gdscript-generator)
6. [Material Workflow (ShaderMaterial + Voxel Includes)](#6-material-workflow-shadermaterial--voxel-includes)
7. [Streaming & Persistence (VoxelStreamSQLite)](#7-streaming--persistence-voxelstreamsqlite)
8. [VoxelInstancer & Instance Library](#8-voxelinstancer--instance-library)
9. [VoxelTool – Runtime Editing](#9-voxeltool--runtime-editing)
10. [Runtime vs Editor Mode Behaviour](#10-runtime-vs-editor-mode-behaviour)
11. [GPU Generation & Normalmap System](#11-gpu-generation--normalmap-system)
12. [VoxelBuffer & Channels](#12-voxelbuffer--channels)
13. [Debug Tools & Stats](#13-debug-tools--stats)
14. [Eden Project – Live Examples](#14-eden-project--live-examples)
15. [Common Patterns & Pitfalls](#15-common-patterns--pitfalls)

---

## 1. Module Architecture

```
modules/voxel/
├── terrain/
│   ├── voxel_node.h            # Base class for all voxel terrain nodes
│   ├── variable_lod/           # VoxelLodTerrain (smooth, LOD, transvoxel)
│   │   ├── voxel_lod_terrain.h/.cpp
│   │   ├── voxel_lod_terrain_update_task.h/.cpp
│   │   ├── voxel_lod_terrain_update_clipbox_streaming.h/.cpp
│   │   ├── shader_material_pool_vlt.h/.cpp   # Per-block ShaderMaterial pool
│   │   └── lod_octree.h
│   ├── fixed_lod/              # VoxelTerrain (flat, blocky)
│   └── instancing/             # VoxelInstancer
│       ├── voxel_instancer.h/.cpp
│       └── voxel_instance_library*.h/.cpp
├── generators/
│   ├── voxel_generator.h       # Abstract base
│   ├── voxel_generator_script.h/.cpp   # GDScript/C# generator
│   ├── graph/
│   │   ├── voxel_generator_graph.h/.cpp
│   │   └── voxel_graph_function.h/.cpp # The node graph itself
│   └── simple/                 # Simple flat/heightmap generators
├── meshers/
│   ├── voxel_mesher.h
│   ├── transvoxel/             # Smooth meshing (marching cubes variant)
│   └── blocky/                 # Minecraft-style blocky meshing
├── storage/
│   ├── voxel_buffer.h/.cpp     # The in-memory voxel container
│   └── voxel_data.h/.cpp
├── streams/
│   ├── voxel_stream.h          # Abstract save/load interface
│   ├── sqlite/                 # VoxelStreamSQLite
│   └── voxel_stream_script.h   # GDScript stream
├── modifiers/                  # Runtime SDF modifiers
├── shaders/                    # GLSL includes (.gdshaderinc)
└── engine/
    ├── voxel_engine.h/.cpp     # Main singleton (VoxelEngine)
    └── gpu/                    # Compute shader infrastructure
```

**Key C++ Namespaces:**
- Everything lives in `zylann::voxel`
- Graph nodes live in `zylann::voxel::pg`
- Godot-facing wrappers use `zylann::voxel::godot`

---

## 2. Core Node Types

| GDScript Class | C++ Class | Use Case |
|---|---|---|
| `VoxelLodTerrain` | `VoxelLodTerrain` | Smooth terrain with LOD (planets, landscape) |
| `VoxelTerrain` | `VoxelTerrain` | Fixed LOD blocky terrain |
| `VoxelInstancer` | `VoxelInstancer` | Instanced props on voxel surface |
| `VoxelViewer` | `VoxelViewer` | Marks a camera/player as a streaming anchor |
| `VoxelGeneratorGraph` | `VoxelGeneratorGraph` | Visual node-graph procedural generator |
| `VoxelGeneratorScript` | `VoxelGeneratorScript` | GDScript/C# custom generator |
| `VoxelMesherTransvoxel` | `VoxelMesherTransvoxel` | Smooth mesh from SDF |
| `VoxelMesherBlocky` | `VoxelMesherBlocky` | Blocky mesh from type data |
| `VoxelStreamSQLite` | `VoxelStreamSQLite` | Persistent save/load via SQLite |
| `VoxelTool` / `VoxelToolLodTerrain` | — | Runtime dig/build |
| `VoxelBuffer` | `VoxelBuffer` | In-memory block of voxel data |
| `VoxelEngine` | `VoxelEngine` (singleton) | Stats, GPU task count, global config |

---

## 3. VoxelLodTerrain – Full API Reference

`VoxelLodTerrain` inherits `VoxelNode` → `Node3D`.

### Required Properties

```gdscript
# The procedural generator resource
volume.generator = VoxelGeneratorGraph.new()

# The mesher (always VoxelMesherTransvoxel for smooth LOD terrain)
volume.mesher = VoxelMesherTransvoxel.new()

# Optional: persistence layer
volume.stream = VoxelStreamSQLite.new()

# A single ShaderMaterial for ALL blocks (copies are made per-block automatically)
volume.material = my_shader_material
```

### LOD Configuration

```gdscript
volume.lod_count = 7                  # Number of LOD levels. Planet ~1800r -> 7, larger = 8+
volume.lod_distance = 60.0            # Distance (voxels) between LOD transitions
volume.secondary_lod_distance = 0.0  # If >0, used for finer control beyond lod_distance
volume.view_distance = 100000.0       # Maximum visible radius (voxels)
volume.lod_fade_duration = 0.3        # Seconds to fade between LODs (0 = instant snap)
volume.mesh_block_size = 32           # Block size in voxels: 16 or 32 (default 16)
```

### Bounds

```gdscript
# ALWAYS set bounds. Without bounds, streaming is unbounded (possible crash/OOM).
# Use a power-of-two that contains the full geometry.
var pot := 1024
while body.radius >= pot:
    pot *= 2
volume.voxel_bounds = AABB(
    Vector3(-pot, -pot, -pot),
    Vector3(2 * pot, 2 * pot, 2 * pot)
)
```

### Collision

```gdscript
volume.generate_collisions = true
volume.collision_lod_count = 2       # Only generate collision for LOD 0 and 1
volume.collision_layer = 1
volume.collision_mask = 1
volume.collision_margin = 0.04       # default
volume.collision_update_delay = 0   # Milliseconds; add delay if performance is tight
```

### Threading

```gdscript
volume.threaded_update_enabled = true   # Run update logic off main thread (recommended)
volume.full_load_mode_enabled = true    # Keep all edited blocks in memory (no data streaming)
```

> **`full_load_mode_enabled`:** When `true`, all blocks that have been edited are kept loaded
> regardless of distance. This is slower for memory but avoids the complexity of data streaming.
> For planets with SQLite saves, set to `true` so edits are always accessible.

### Process Callback

```gdscript
# PROCESS_CALLBACK_IDLE (default) — updates during _process()
# PROCESS_CALLBACK_PHYSICS — updates during _physics_process()
# PROCESS_CALLBACK_DISABLED — manual control
volume.set_process_callback(VoxelLodTerrain.PROCESS_CALLBACK_IDLE)
```

> **When to use PHYSICS:** If you use a floating world origin and teleport the player during
> `_integrate_forces()`, set this to `PROCESS_CALLBACK_PHYSICS` so the terrain updates
> in the same frame, preventing flicker and reload.

### Streaming System

```gdscript
# STREAMING_SYSTEM_CLIPBOX (newer, recommended)
# STREAMING_SYSTEM_LEGACY_OCTREE (older, default for compatibility)
volume.streaming_system = VoxelLodTerrain.STREAMING_SYSTEM_CLIPBOX
```

### Editor / Run Mode

```gdscript
volume.run_stream_in_editor = false  # If true, generator runs in the editor viewport
```

> **Critical:** Setting `run_stream_in_editor = true` means the generator/stream runs when
> the scene is open in the editor. This can be very slow with heavy generators. Keep it `false`
> unless you intentionally want live preview.

---

## 4. VoxelGeneratorGraph – Node Graph System

`VoxelGeneratorGraph` is a **Resource** (saved as `.tres`). It contains a `VoxelGraphFunction`
which is the actual node graph.

### Load and Duplicate (Runtime Pattern)

```gdscript
# IMPORTANT: Always .duplicate(true) before runtime modification.
# Otherwise you edit the shared .tres and all bodies share it.
const BasePlanetVoxelGraph = preload("./voxel_graph_planet_v4.tres")

var generator: VoxelGeneratorGraph = BasePlanetVoxelGraph.duplicate(true)
var graph: VoxelGraphFunction = generator.get_main_function()
```

### Finding and Modifying Nodes

```gdscript
# Find a named node in the graph
var sphere_node_id := graph.find_node_by_name("sphere")

# Set a node's default input (input port value when not connected)
# API changed in v1.4.2:
if VoxelVersion.get_v() >= Vector3i(1, 4, 2):
    graph.set_node_default_input(sphere_node_id, 3, body.radius)
else:
    graph.set_node_param(sphere_node_id, 0, body.radius)

# Get/set node params (resource references like noise)
var noise_param_id := 0
var noise = graph.get_node_param(noise_node_id, noise_param_id)
noise.seed = body.name.hash()   # Modify resource in place (it was duplicated)

# Set default input by port index
graph.set_node_default_input(cave_height_node_id, 1, body.radius - 100.0)
```

### Compilation

```gdscript
generator.compile()  # Must call after modifying the graph at runtime!
```

### Performance Tuning

```gdscript
generator.use_subdivision = true       # Subdivide blocks for better range analysis
generator.subdivision_size = 8         # Must divide evenly into block size
generator.use_optimized_execution_map = true  # Skip nodes not affecting current block
# generator.sdf_clip_threshold = 10.0  # High = disable clipping (consistent SDF, slower)
```

### Graph Node Types (VoxelGraphFunction.NodeTypeID)

Key node type IDs you will use from code:

| NodeTypeID Constant | Purpose |
|---|---|
| `NODE_CONSTANT` | Fixed float value |
| `NODE_INPUT_X/Y/Z` | World-space coordinates |
| `NODE_OUTPUT_SDF` | The SDF output (required) |
| `NODE_OUTPUT_WEIGHT` | Texture blending weight (0-1 per layer) |
| `NODE_OUTPUT_TYPE` | Blocky voxel type output |
| `NODE_SDF_SPHERE` | Sphere SDF primitive |
| `NODE_SDF_SPHERE_HEIGHTMAP` | Sphere with heightmap displacement |
| `NODE_SDF_PLANE` | Infinite plane |
| `NODE_NOISE_2D/3D` | FastNoiseLite noise |
| `NODE_FAST_NOISE_2D/3D` | FastNoise2 (faster, higher quality) |
| `NODE_MIX` | Linear interpolation |
| `NODE_SELECT` | Conditional select based on threshold |
| `NODE_CURVE` | Godot Curve remapping |
| `NODE_EXPRESSION` | Inline GDScript-like math expression |
| `NODE_FUNCTION` | Reusable sub-graph |
| `NODE_SDF_SMOOTH_UNION` | Smooth boolean union of two SDFs |
| `NODE_SDF_SMOOTH_SUBTRACT` | Smooth boolean subtract of two SDFs |
| `NODE_REMAP` | Remap a value from one range to another |
| `NODE_CLAMP_C` | Clamp with constant min/max |
| `NODE_SPOTS_3D` | Procedural 3D spots distribution |

### Creating Nodes at Runtime (Advanced)

```gdscript
# Creating a node from code (less common; prefer editor-authored graphs)
var node_id := graph.create_node(VoxelGraphFunction.NODE_CONSTANT, Vector2(0, 0))
graph.set_node_param(node_id, 0, 42.0)  # param 0 = value for CONSTANT node
graph.add_connection(node_id, 0, sphere_node_id, 3)
```

### TextureMode (for Mixel4 blending)

```gdscript
# TEXTURE_MODE_MIXEL4 (default): 4 indices + 4 weights in 16-bit INDICES/WEIGHTS channels
# TEXTURE_MODE_SINGLE: 1 index in 8-bit INDICES channel
generator.texture_mode = VoxelGeneratorGraph.TEXTURE_MODE_MIXEL4
```

---

## 5. VoxelGeneratorScript – GDScript Generator

Use this when you need full programmatic control over voxel generation that cannot be
expressed in the graph editor, or for prototyping.

### C++ Internal Behaviour

From `voxel_generator_script.cpp`:
- A **temporary** `VoxelBuffer` wrapper is created each call.
- Filled by your script, then `move_to()` transfers it into the engine buffer.
- **Do NOT keep a reference to the buffer after `_generate_block()` returns.**
- Called from **worker threads**; the wrapper is discarded immediately after return.

### GDScript Implementation

```gdscript
extends VoxelGeneratorScript

func _get_used_channels_mask() -> int:
    # Return a bitmask of channels your generator writes to.
    # VoxelBuffer.CHANNEL_SDF = 0 -> bit 0 -> mask = 1
    # VoxelBuffer.CHANNEL_TYPE = 1 -> bit 1 -> mask = 2
    # Both SDF and TYPE: mask = 3
    return 1 << VoxelBuffer.CHANNEL_SDF

func _generate_block(buffer: VoxelBuffer, origin_in_voxels: Vector3i, lod: int) -> void:
    # origin_in_voxels: world-space voxel coordinate of block's corner
    # lod: level of detail (0 = finest; each LOD level doubles the voxel world size)
    # buffer.get_size(): typically Vector3i(16,16,16) or Vector3i(32,32,32)

    var size := buffer.get_size()
    var voxel_size := 1 << lod  # At LOD 1, each voxel = 2 world units

    for z in size.z:
        for x in size.x:
            for y in size.y:
                var world_pos := Vector3(
                    origin_in_voxels.x + x * voxel_size,
                    origin_in_voxels.y + y * voxel_size,
                    origin_in_voxels.z + z * voxel_size
                )
                var sdf := _compute_sdf(world_pos)
                buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer.CHANNEL_SDF)

func _compute_sdf(world_pos: Vector3) -> float:
    # Sphere SDF example (planet)
    var radius := 1800.0
    return world_pos.length() - radius
```

### Threading Contract

> **CRITICAL:** `_generate_block` is called from **worker threads**, NOT the main thread.
> - Do NOT call any Godot scene tree functions, signals, or non-thread-safe methods.
> - DO use thread-safe resources: noise, math, read-only data arrays.
> - The `VoxelBuffer` parameter is a temporary wrapper; do NOT store it in a member variable.
> - After `_generate_block` returns, the buffer reference becomes INVALID.

### Runtime vs Editor

- **Editor:** `_generate_block` runs only if `VoxelLodTerrain.run_stream_in_editor = true`.
- **Game:** Always runs on worker threads when blocks stream in.
- `_get_used_channels_mask()` is called once from the main thread.

### Common Mistake

```gdscript
# WRONG: Storing the buffer reference
var _last_buffer: VoxelBuffer
func _generate_block(buffer: VoxelBuffer, origin: Vector3i, lod: int):
    _last_buffer = buffer  # UNDEFINED BEHAVIOUR after function returns!

# CORRECT: Use only inside the function
func _generate_block(buffer: VoxelBuffer, origin: Vector3i, lod: int):
    var size = buffer.get_size()
    # fill buffer...
    # buffer goes out of scope safely
```

---

## 6. Material Workflow (ShaderMaterial + Voxel Includes)

### The ShaderMaterial Pool Problem

`VoxelLodTerrain` needs **one `ShaderMaterial` per block** because blocks have individual
shader parameters (block local transform, LOD fade, virtual texture UVs, etc.).

**The problem:** Godot connects a signal from `Shader` to every `ShaderMaterial` referencing it.
When `ShaderMaterial.duplicate()` is called, Godot adds to a huge signal list causing severe
stutter with many blocks.

**The solution:** `ShaderMaterialPoolVLT` (in `shader_material_pool_vlt.h`) — a custom pool
that recycles materials instead of always creating new ones. This is automatic inside
`VoxelLodTerrain`. You just assign one material and the engine handles copies.

```gdscript
# Assign a single material; VoxelLodTerrain handles per-block copies internally
volume.material = my_shader_material  # Must be a ShaderMaterial
```

### Required Shader Includes

Your `.gdshader` MUST include these three files:

```glsl
shader_type spatial;

#include "res://addons/zylann.voxel/shaders/transvoxel.gdshaderinc"
#include "res://addons/zylann.voxel/shaders/lod_fade.gdshaderinc"
#include "res://addons/zylann.voxel/shaders/virtual_texturing.gdshaderinc"
```

### Required Uniform Declarations

These uniforms are **set automatically by the engine per block**. You MUST declare them
even if you do not use them directly, or the engine will silently fail to write them:

```glsl
// Set per-block by VoxelLodTerrain — declare all of these:
uniform int u_transition_mask;
uniform vec2 u_lod_fade;
uniform vec4 u_voxel_virtual_texture_offset_scale;
uniform float u_voxel_virtual_texture_tile_size;
uniform float u_voxel_virtual_texture_fade;
uniform float u_voxel_cell_size;
uniform int u_voxel_block_size;
uniform mat4 u_block_local_transform;   // Block-to-planet-local transform (translation only)

// Your own uniforms:
uniform sampler2D u_top_albedo_texture : source_color;
uniform sampler2D u_side_albedo_texture : source_color;
uniform float u_mountain_height;
```

### Transvoxel Position (CRITICAL in vertex())

```glsl
void vertex() {
    // MUST be called FIRST. Applies transition cell vertex morphing for seam stitching.
    VERTEX = get_transvoxel_position(VERTEX, CUSTOM0);

    // Now use VERTEX for your own calculations
    vec3 local_pos = (u_block_local_transform * vec4(VERTEX, 1.0)).xyz;
    v_up_planet = normalize(local_pos);
    v_planet_height = length(local_pos);
    v_triplanar_uv = local_pos * 0.05;
    // ...
}
```

> **Without `get_transvoxel_position()`**: cracks appear at LOD transition boundaries.

### LOD Fade (CRITICAL in fragment())

```glsl
void fragment() {
    // ... your shading code ...

    // MUST be last: LOD fade discard (dithered fade between LOD levels)
    if (get_lod_fade_discard(SCREEN_UV)) {
        discard;
    }
}
```

> **Without LOD fade discard:** LOD transitions will hard-cut (pop) instead of fading smoothly.

### Normal Reconstruction for Smooth Terrain

```glsl
void fragment() {
    // Reconstruct high-quality normals for smooth voxel terrain
    vec3 normal_model;
    vec3 normal_view = get_voxel_normal_view_model(
        v_vertex_pos_model,        // VERTEX value captured in vertex()
        NORMAL,                    // rasterized interpolated normal
        VIEW_MATRIX * MODEL_MATRIX,
        v_normal_model,            // NORMAL captured in vertex()
        normal_model               // out: reconstructed model-space normal
    );
    NORMAL = normal_view;
}
```

### Triplanar Mapping for Terrain

Voxel meshes have no UV coords. Use triplanar projection with planet-local coordinates:

```glsl
vec4 triplanar_texture(sampler2D p_sampler, vec3 p_weights, vec3 p_triplanar_pos) {
    vec4 samp = vec4(0.0);
    samp += texture(p_sampler, p_triplanar_pos.xy) * p_weights.z;
    samp += texture(p_sampler, p_triplanar_pos.xz) * p_weights.y;
    samp += texture(p_sampler, p_triplanar_pos.zy * vec2(-1.0, 1.0)) * p_weights.x;
    return samp;
}

// In vertex(): compute planet-local position for triplanar coords
v_triplanar_uv = local_pos * 0.05;   // 0.05 = inverse of ~20 world-units per tile

// In fragment(): compute blend weights from surface normal
float sharpness = 8.0;
vec3 blend = pow(abs(normal_model), vec3(sharpness));
blend /= dot(blend, vec3(1.0));       // normalize blend weights
vec3 albedo = triplanar_texture(u_albedo_tex, blend, v_triplanar_uv).rgb;
```

### Slope-based Blending (Top vs Side Textures)

```glsl
// In fragment():
// v_up_planet = planet-local normalize(vertex_pos) — the "up" direction
float flatness = max(dot(normal_model, v_up_planet), 0.0);
float topness = smoothstep(0.85, 1.0, flatness);

// Height-based masking (hide top texture on tall peaks):
float mountain_factor = smoothstep(
    u_mountain_height - 3.0,
    u_mountain_height + 3.0,
    v_planet_height
);
topness = mix(topness, 0.0, mountain_factor);

ALBEDO = mix(side_col, top_col, topness);
NORMAL_MAP = mix(side_norm, top_norm, topness);
```

### Distance LOD for Textures (Far Tiling)

```glsl
// Reduce tiling frequency at large distances to avoid aliasing:
float far_min_distance = 100.0;
float far_max_distance = 200.0;
float far_factor = clamp(
    (v_camera_distance - far_min_distance) / (far_max_distance - far_min_distance),
    0.0, 1.0
);
vec3 far_col = triplanar_texture(u_side_tex, blend, v_triplanar_uv * 0.25).rgb;
side_col = mix(side_col, far_col, far_factor);
```

### Material .tres Structure

```
[gd_resource type="ShaderMaterial" load_steps=6 format=3]
[ext_resource type="Shader" path="res://solar_system/materials/planet_ground.gdshader" id="5"]
[ext_resource type="Texture2D" path="res://textures/Rocks07_col.webp" id="2"]
...

[resource]
shader = ExtResource("5")
# Voxel-managed params (leave null; engine writes per-block)
shader_parameter/u_transition_mask = null
shader_parameter/u_lod_fade = null
shader_parameter/u_voxel_virtual_texture_tile_size = null
shader_parameter/u_voxel_virtual_texture_fade = null
shader_parameter/u_voxel_cell_size = null
shader_parameter/u_voxel_block_size = null
shader_parameter/u_voxel_virtual_texture_offset_scale = null
shader_parameter/u_block_local_transform = null
# Your custom params:
shader_parameter/u_mountain_height = null       # set at runtime
shader_parameter/u_top_modulate = Color(1,1,1,1)
shader_parameter/u_top_albedo_texture = ExtResource("4")
shader_parameter/u_side_albedo_texture = ExtResource("2")
```

### Runtime Material Customization

```gdscript
# Always duplicate before modifying to avoid affecting the shared .tres
var mat: ShaderMaterial = PlanetRockyMaterial.duplicate()
mat.set_shader_parameter(&"u_mountain_height", body.radius + 80.0)
mat.set_shader_parameter(&"u_top_modulate", Color(1.0, 0.6, 0.3))
volume.material = mat
```

---

## 7. Streaming & Persistence (VoxelStreamSQLite)

### Setup

```gdscript
var stream := VoxelStreamSQLite.new()
stream.database_path = "debug_data/Earth.sqlite"  # Relative to project or absolute path
volume.stream = stream
```

### How It Works

- On block load: if a block has previously been edited and saved, the stream provides
  the saved data instead of running the generator.
- On edit via `VoxelTool`: edits are queued in memory.
- On `save_modified_blocks()`: all dirty blocks are flushed to SQLite.
- Block format: compressed binary (lz4 or zstd) inside SQLite.

### Saving

```gdscript
# Synchronous save (call before quit)
volume.save_modified_blocks()

# Game exit pattern from solar_system.gd:
func _notification(what: int):
    match what:
        NOTIFICATION_WM_CLOSE_REQUEST:
            for body in _bodies:
                if body.volume != null:
                    body.volume.save_modified_blocks()
```

### full_load_mode_enabled

```gdscript
# true: all modified blocks stay in RAM regardless of view distance (recommended for planets)
# false: data streaming active (blocks load/unload based on distance — more complex)
volume.full_load_mode_enabled = true
```

### Custom GDScript Stream

```gdscript
extends VoxelStreamScript

func _load_voxel_block(query: VoxelStreamScript.VoxelQueryData) -> int:
    # query.voxel_buffer: write loaded data here
    # query.origin_in_voxels: block world position
    # query.lod: level of detail
    # Return: VoxelStream.RESULT_BLOCK_FOUND or RESULT_BLOCK_NOT_FOUND
    return VoxelStream.RESULT_BLOCK_NOT_FOUND

func _save_voxel_block(query: VoxelStreamScript.VoxelQueryData) -> void:
    pass  # Serialize query.voxel_buffer and save
```

---

## 8. VoxelInstancer & Instance Library

`VoxelInstancer` is a **child of VoxelLodTerrain**. It spawns MultiMesh instances or scenes
on the voxel surface based on density/slope/noise rules, tied to terrain LOD chunks.

### Setup

```gdscript
var instancer := VoxelInstancer.new()
instancer.set_up_mode(VoxelInstancer.UP_MODE_SPHERE)  # CRITICAL for planets
# UP_MODE_POSITIVE_Y for flat terrain

var library := VoxelInstanceLibrary.new()
# ... add items to library ...
instancer.library = library

volume.add_child(instancer)  # Must be child of VoxelLodTerrain
```

### UP_MODE Values

| Mode | Description |
|---|---|
| `UP_MODE_POSITIVE_Y` | World Y is up (flat terrain) |
| `UP_MODE_SPHERE` | Up = away from local origin (planet surface, radial gravity) |

### VoxelInstanceLibraryMultiMeshItem

For GPU-instanced static props (rocks, grass, pebbles):

```gdscript
var item := VoxelInstanceLibraryMultiMeshItem.new()

# Option A: set mesh directly
item.set_mesh(Pebble1Mesh, 0)        # (mesh, surface_index)

# Option B: from scene template (copies collision, materials, etc.)
var template: Node = RockScene.instantiate()
item.setup_from_template(template)
template.free()                      # Free the template — item keeps the data

item.name = "rock"
item.lod_index = 2                   # Spawn when this LOD is active (0=closest detail)
item.persistent = true               # Edits saved to stream
item.generator = instance_generator
library.add_item(ITEM_ID, item)      # ITEM_ID is any integer key
```

### VoxelInstanceGenerator

```gdscript
var gen := VoxelInstanceGenerator.new()
gen.density = 0.08                   # Instances per square unit on surface
gen.min_scale = 0.5
gen.max_scale = 0.8
gen.min_slope_degrees = 0            # Surface must be at least this steep
gen.max_slope_degrees = 12           # Surface must be at most this steep
gen.vertical_alignment = 0.0        # 0.0=align to surface normal, 1.0=align to world up
gen.emit_mode = VoxelInstanceGenerator.EMIT_FROM_FACES
gen.offset_along_normal = 0.0       # Push instance away from surface
gen.random_vertical_flip = true      # Randomly flip 180° (for stalactites)

# Distribution type for scale randomization:
# DISTRIBUTION_LINEAR (default), DISTRIBUTION_QUADRATIC, DISTRIBUTION_CUBIC
gen.scale_distribution = VoxelInstanceGenerator.DISTRIBUTION_CUBIC

# Noise-based clustering (instances only spawn where noise > threshold)
gen.noise = FastNoiseLite.new()
gen.noise.frequency = 1.0 / 16.0
gen.noise.fractal_octaves = 2
gen.noise_on_scale = 1              # Which noise "scale" to evaluate (affects UV)
```

### Stalactite / Upside-down Pattern

```gdscript
# Spawn on ceiling (faces pointing downward, slope 140-180 degrees)
gen.min_slope_degrees = 140
gen.max_slope_degrees = 180
gen.vertical_alignment = 1.0
gen.offset_along_normal = -0.5      # Embed slightly into surface
gen.random_vertical_flip = false
```

### Library Item IDs (Eden Project)

```gdscript
library.add_item(0, rock_item)        # rocks
library.add_item(1, big_rock_item)    # big rocks
library.add_item(2, grass_item)       # grass / pebbles
library.add_item(3, stalactite_item)  # stalactites
```

---

## 9. VoxelTool – Runtime Editing

```gdscript
# Get a tool (must be called on terrain after it is in scene tree)
var vt: VoxelToolLodTerrain = volume.get_voxel_tool()

vt.channel = VoxelBuffer.CHANNEL_SDF
vt.mode = VoxelTool.MODE_REMOVE      # or MODE_ADD, MODE_SET

# ALL positions must be in volume LOCAL space (not world space!)
var local_pos := volume.get_global_transform().affine_inverse() * world_hit_position

vt.do_sphere(local_pos, sphere_radius)   # Sphere edit
vt.do_box(local_min, local_max)          # Box edit

# Read SDF (trilinearly interpolated)
var sdf := vt.get_voxel_f_interpolated(local_pos)

# Post-edit: separate floating chunks (physics bodies from disconnected geometry)
var splitter_aabb := AABB(local_pos, Vector3()).grow(16.0)
var bodies := vt.separate_floating_chunks(splitter_aabb, parent_for_bodies)
for body in bodies:
    var cmp := SplitChunkRigidBodyComponent.new()
    body.add_child(cmp)
```

### VoxelTool.mode Values

| Mode | SDF Effect | Result |
|---|---|---|
| `MODE_ADD` | Subtracts from SDF | Adds solid voxels |
| `MODE_REMOVE` | Adds to SDF | Removes solid voxels (dig) |
| `MODE_SET` | Sets SDF directly | Raw control |

### Detect and Escape Buried Character

```gdscript
var local_pos := volume.get_global_transform().affine_inverse() * char.global_position
var sdf := vt.get_voxel_f_interpolated(local_pos)
if sdf < -0.001:
    var up := local_pos.normalized()  # planet-radial up
    for i in 10:
        local_pos += 0.2 * up
        if vt.get_voxel_f_interpolated(local_pos) > 0.0005:
            break
    char.global_position = volume.global_transform * local_pos
```

---

## 10. Runtime vs Editor Mode Behaviour

### `run_stream_in_editor`

| Setting | Behaviour |
|---|---|
| `false` (default) | Generator/stream only runs in game; editor shows empty |
| `true` | Generator runs in editor, terrain is visible in viewport |

> Heavy generators (large VoxelGraphs) can slow the editor significantly.
> Use `false` unless you need live in-editor preview.

### `_generate_block` call context

| Situation | Thread |
|---|---|
| Game, block streaming | Worker thread (VoxelEngine task scheduler) |
| Editor, `run_stream_in_editor=true` | Worker thread |
| Direct `generate_block()` call (C++ internal) | Main thread |

### `compile()` Must Be Called After Graph Edits

```gdscript
graph.set_node_default_input(node_id, port_idx, new_value)
generator.compile()  # Without this, changes have NO EFFECT

# compile() returns a CompilationResult:
var result: Dictionary = generator.compile()
if not result.get("success", false):
    push_error("Graph compile failed: " + str(result.get("message", "")))
```

### Editor Warnings (Configuration Checks)

`VoxelLodTerrain.get_configuration_warnings()` checks:
- mesher is assigned
- generator OR stream is assigned
- material is assigned (soft warning)
- voxel_bounds is not the default infinite value

---

## 11. GPU Generation & Normalmap System

### GPU Generator

Only `VoxelGeneratorGraph` supports GPU generation (compiled with `VOXEL_ENABLE_GPU`):

```gdscript
volume.generator_use_gpu = true   # Hint: falls back to CPU if GPU path unavailable
```

Graph nodes are auto-translated to GLSL compute shaders. Unsupported nodes fall back to CPU.

### Waiting for Shader Compilation (Loading Screen Pattern)

```gdscript
# VoxelEngine compiles shaders async; poll until done before showing game
var initial_gpu_tasks: int = VoxelEngine.get_stats()["tasks"].get("gpu", 0)
if initial_gpu_tasks > 0:
    var remaining := initial_gpu_tasks
    while remaining > 0:
        await get_tree().process_frame
        remaining = VoxelEngine.get_stats()["tasks"].get("gpu", 0)
        var done := initial_gpu_tasks - remaining
        progress_info.progress = float(done) / float(initial_gpu_tasks)
        loading_progressed.emit(progress_info)
```

### Normalmap System (Virtual Detail Normals)

For smooth LOD terrain, the engine bakes high-resolution normal tiles for distant LODs,
giving them surface detail without extra geometry. Uses compute shaders internally.

```gdscript
volume.normalmap_enabled = true
volume.normalmap_tile_resolution_min = 4     # Min tiles per block face (quality floor)
volume.normalmap_tile_resolution_max = 8     # Max tiles per block face (quality ceiling)
volume.normalmap_begin_lod_index = 2         # Apply starting at LOD 2 and above
volume.normalmap_max_deviation_degrees = 50  # How far normals can deviate from mesh
volume.normalmap_octahedral_encoding_enabled = false  # Octahedral vs spherical encoding
volume.normalmap_use_gpu = true              # GPU bake (much faster than CPU)

# Override generator for normalmaps (if main generator is expensive):
volume.normalmap_generator_override = lightweight_generator
volume.normalmap_generator_override_begin_lod_index = 3
```

> The normalmap generator evaluates SDF at higher resolution than the mesh, then computes
> surface normals. These normals are stored in virtual texture tiles sampled by the shader
> via `u_voxel_virtual_texture_*` uniforms and the `virtual_texturing.gdshaderinc` functions.

---

## 12. VoxelBuffer & Channels

### Channel IDs

```gdscript
VoxelBuffer.CHANNEL_TYPE     # 0 — voxel type (for blocky mesher)
VoxelBuffer.CHANNEL_SDF      # 1 — signed distance field (for transvoxel)
VoxelBuffer.CHANNEL_COLOR    # 2 — per-voxel RGBA color
VoxelBuffer.CHANNEL_INDICES  # 3 — texture layer indices (4 packed 8-bit values)
VoxelBuffer.CHANNEL_WEIGHTS  # 4 — texture blend weights (4 packed 4-bit values)
VoxelBuffer.CHANNEL_DATA5    # 5 — custom data
VoxelBuffer.CHANNEL_DATA6    # 6 — custom data
VoxelBuffer.CHANNEL_DATA7    # 7 — custom data
```

### Channel Depths

```gdscript
VoxelBuffer.DEPTH_8_BIT    # Good for TYPE, INDICES, WEIGHTS
VoxelBuffer.DEPTH_16_BIT   # Best for SDF (signed, highest precision)
VoxelBuffer.DEPTH_32_BIT
VoxelBuffer.DEPTH_64_BIT
```

### SDF Convention

- Negative = inside solid (underground)
- Positive = outside solid (air)
- Zero-crossing = surface
- Stored as signed 16-bit int; `set_voxel_f(-1.0, ...)` maps to full negative
- `get_voxel_f_interpolated()` returns float in approximately [-1.0, 1.0]

### `_get_used_channels_mask()` Pattern

```gdscript
func _get_used_channels_mask() -> int:
    # SDF only:
    return 1 << VoxelBuffer.CHANNEL_SDF
    # SDF + texture indices + weights:
    # return (1 << VoxelBuffer.CHANNEL_SDF) | (1 << VoxelBuffer.CHANNEL_INDICES) | (1 << VoxelBuffer.CHANNEL_WEIGHTS)
```

---

## 13. Debug Tools & Stats

### VoxelEngine Global Stats

```gdscript
var stats := VoxelEngine.get_stats()
# stats["tasks"]: Dictionary
#   "gpu": int        -- pending GPU tasks (shader compilation, normalmap bake)
#   "generate": int   -- pending block generation tasks
#   "mesh_setups": int
#   "stream_read": int
#   "stream_write": int
var gpu_tasks: int = stats["tasks"].get("gpu", 0)
```

### VoxelLodTerrain Debug Draw Flags

```gdscript
volume.debug_set_draw_enabled(true)

# Available flags:
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_OCTREE_NODES, true)
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_OCTREE_BOUNDS, true)
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_MESH_UPDATES, true)
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_EDIT_BOXES, true)
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_VOLUME_BOUNDS, true)
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_EDITED_BLOCKS, true)
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_MODIFIER_BOUNDS, true)
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_ACTIVE_MESH_BLOCKS, true)
volume.debug_set_draw_flag(VoxelLodTerrain.DEBUG_DRAW_VIEWER_CLIPBOXES, true)
```

### Block Count HUD

```gdscript
DDD.set_text("Data blocks", volume.debug_get_data_block_count())
DDD.set_text("Mesh blocks", volume.debug_get_mesh_block_count())
DDD.set_text("Instancer blocks", body.instancer.debug_get_block_count())
```

### Statistics Dictionary

```gdscript
var s := volume.get_statistics()
# s["blocked_lods"]            -- nodes waiting for data (should reach 0 when loaded)
# s["dropped_block_loads"]     -- high = streaming cannot keep up
# s["dropped_block_meshs"]     -- high = meshing cannot keep up
# s["time_detect_required_blocks"] -- microseconds
# s["time_io_requests"]        -- microseconds
# s["time_mesh_requests"]      -- microseconds
# s["time_update_task"]        -- total threaded update time (microseconds)
```

---

## 14. Eden Project – Live Examples

### Complete Planet Setup (solar_system_setup.gd)

```gdscript
static func _setup_rocky_planet(body: StellarBody, root: Node3D, settings: Settings):
    # 1. Material (ALWAYS duplicate to avoid sharing between bodies)
    var mat: ShaderMaterial = PlanetGrassyMaterial.duplicate()
    mat.set_shader_parameter(&"u_mountain_height", body.radius + 80.0)

    # 2. Generator: duplicate + modify + compile
    var generator: VoxelGeneratorGraph = BasePlanetVoxelGraph.duplicate(true)
    var graph: VoxelGraphFunction = generator.get_main_function()

    var sphere_node_id := graph.find_node_by_name("sphere")
    if VoxelVersion.get_v() >= Vector3i(1, 4, 2):
        graph.set_node_default_input(sphere_node_id, 3, body.radius)
    else:
        graph.set_node_param(sphere_node_id, 0, body.radius)

    var noise = graph.get_node_param(ravine_blend_noise_node_id, 0)
    noise.seed = body.name.hash()

    generator.compile()
    generator.use_subdivision = true
    generator.subdivision_size = 8
    generator.use_optimized_execution_map = true

    # 3. SQLite stream
    var stream := VoxelStreamSQLite.new()
    stream.database_path = str("debug_data/", body.name, ".sqlite")

    # 4. Bounds: next power-of-two above planet radius
    var pot := 1024
    while body.radius >= pot:
        pot *= 2

    # 5. VoxelLodTerrain
    var volume := VoxelLodTerrain.new()
    volume.lod_count = 7
    volume.lod_distance = 60.0
    volume.collision_lod_count = 2
    volume.generator = generator
    volume.stream = stream
    volume.view_distance = 100000.0
    volume.voxel_bounds = AABB(Vector3(-pot,-pot,-pot), Vector3(2*pot,2*pot,2*pot))
    volume.lod_fade_duration = 0.3
    volume.threaded_update_enabled = true
    volume.full_load_mode_enabled = true
    volume.normalmap_enabled = true
    volume.normalmap_tile_resolution_min = 4
    volume.normalmap_tile_resolution_max = 8
    volume.normalmap_begin_lod_index = 2
    volume.normalmap_max_deviation_degrees = 50
    volume.normalmap_use_gpu = true
    volume.material = mat
    volume.mesh_block_size = 32
    volume.mesher = VoxelMesherTransvoxel.new()
    root.add_child(volume)
    body.volume = volume

    # 6. Props instancer
    _configure_instancing_for_planet(body, volume)
```

### Saved Data Paths

```
debug_data/
├── Earth.sqlite   (edited chunks for Earth)
├── Mars.sqlite
├── Mercury.sqlite
└── Moon.sqlite
```

### VoxelGraph Assets

```
solar_system/
├── voxel_graph_planet.tres      (v1)
├── voxel_graph_planet_v2.tres   (v2)
├── voxel_graph_planet_v3.tres   (v3)
└── voxel_graph_planet_v4.tres   (v4, current — used by setup code)
```

Named nodes in the planet graph (referenced in setup code):
- `"sphere"` — SDF sphere node (param 3 = radius in API >= 1.4.2)
- `"ravine_blend_noise"` — noise for ravine blending (param 0 = FastNoiseLite resource)
- `"cave_height_subtract"` — input 1 = threshold height for cave removal
- `"cave_noise"` — noise for cave generation (param 0 = FastNoiseLite resource)
- `"ravine_depth_multiplier"` — input 1 = depth scale value

---

## 15. Common Patterns & Pitfalls

### PITFALL: Not Duplicating Generator/Material

```gdscript
# WRONG: All bodies share the generator resource
volume.generator = BasePlanetVoxelGraph
# Any node param change affects all planets simultaneously!

# CORRECT:
volume.generator = BasePlanetVoxelGraph.duplicate(true)
```

### PITFALL: Not Setting voxel_bounds

```gdscript
# WRONG: No bounds = infinite streaming = OOM crash eventually
var volume := VoxelLodTerrain.new()
# (no voxel_bounds)

# CORRECT:
volume.voxel_bounds = AABB(Vector3(-2048,-2048,-2048), Vector3(4096,4096,4096))
```

### PITFALL: Forgetting generator.compile() After Edit

```gdscript
# WRONG: edit has zero effect
graph.set_node_default_input(sphere_id, 3, new_radius)

# CORRECT:
graph.set_node_default_input(sphere_id, 3, new_radius)
generator.compile()
```

### PITFALL: Missing get_transvoxel_position() in Shader

```glsl
// WRONG: cracks at LOD seams
void vertex() {
    v_pos = VERTEX;  // did not call get_transvoxel_position first!
}

// CORRECT:
void vertex() {
    VERTEX = get_transvoxel_position(VERTEX, CUSTOM0);
    v_pos = VERTEX;
}
```

### PITFALL: Missing get_lod_fade_discard() in Shader

```glsl
// WRONG: hard LOD pops instead of smooth fades
void fragment() {
    ALBEDO = vec3(1.0);
    // no discard call
}

// CORRECT:
void fragment() {
    ALBEDO = vec3(1.0);
    if (get_lod_fade_discard(SCREEN_UV)) { discard; }
}
```

### PITFALL: Wrong Coordinate Space for VoxelTool

```gdscript
# WRONG: world-space position
vt.do_sphere(hit.position, 3.5)

# CORRECT: volume local-space
var local := volume.get_global_transform().affine_inverse() * hit.position
vt.do_sphere(local, 3.5)
```

### PITFALL: Holding VoxelBuffer Reference After _generate_block

```gdscript
# WRONG (VoxelGeneratorScript):
var _saved_buffer: VoxelBuffer
func _generate_block(buffer, origin, lod):
    _saved_buffer = buffer  # INVALID after function returns

# CORRECT: only use buffer inside the function
func _generate_block(buffer, origin, lod):
    var size = buffer.get_size()
    # fill buffer...
    # done
```

### PATTERN: Determine LOD Count for Planet Size

```gdscript
# Base: 7 LODs for radius up to ~2048
# Each doubling of world scale needs +1 LOD
var extra_lods := 0
if settings.world_scale_x10:
    var temp := int(LARGE_SCALE)   # LARGE_SCALE = 10
    while temp > 1:
        extra_lods += 1
        temp /= 2
volume.lod_count = 7 + extra_lods
```

### PATTERN: Per-Body Deterministic Noise Seeds

```gdscript
noise.seed = body.name.hash()  # "Earth" always gets the same seed
```

### PATTERN: Check VoxelVersion for API Changes

```gdscript
if VoxelVersion.get_v() >= Vector3i(1, 4, 2):
    graph.set_node_default_input(sphere_node_id, 3, body.radius)
else:
    graph.set_node_param(sphere_node_id, 0, body.radius)
```

### PATTERN: Floating World Origin + Terrain Sync

```gdscript
# Problem: player teleports in _integrate_forces, terrain updates in _process
# -> terrain unloads/reloads over 3 frames -> flicker

# Solution A: match process modes
volume.set_process_callback(VoxelLodTerrain.PROCESS_CALLBACK_PHYSICS)

# Solution B: disable during teleport
volume.set_process_callback(VoxelLodTerrain.PROCESS_CALLBACK_DISABLED)
# ... apply transform change ...
volume.set_process_callback(VoxelLodTerrain.PROCESS_CALLBACK_PHYSICS)
```

---

*Generated: 2026-03-12 | Engine: F:\Dev\GodotEden | Project: F:\Dev\Projects\eden_project*
