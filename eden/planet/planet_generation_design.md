# Eden Planet Generation — Tectonic & Biome Design
# Based on: redblobgames/1843-planet-generation, kenny.wtf/world-synth, mapgen4 rivers
================================================================================

## OVERVIEW

Planet generation runs in two distinct phases:

  PHASE A — "Offline" / Pre-generation (runs once at world creation, ~seconds)
    Builds the large-scale data structure: plates, elevation, moisture, biomes,
    rivers. Stored as lightweight lookup data (not voxels). This is the "map".

  PHASE B — "Online" / Real-time (runs per voxel chunk, <100ms each)
    VoxelGeneratorGraph queries the pre-generated map data for the current chunk
    position and converts it to SDF voxel values + material indices.
    This is the bridge from "map" to playable terrain.

The separation means: the world shape is rich and geologically plausible, while
voxel generation stays fast enough for streaming LOD.

================================================================================
## PHASE A — PRE-GENERATION PIPELINE

### Step 1: Sphere Discretisation (Voronoi Regions)

Subdivide the planet sphere into ~10,000–50,000 Voronoi "regions" (cells).
- Place seed points on sphere using Fibonacci sphere distribution for initial
  even coverage, then apply random jitter to break regularity.
- Build Delaunay triangulation → dual gives Voronoi polygons.
- Each region knows: center point (Vector3 on unit sphere), neighbor region IDs.
- This becomes the graph for BFS/flood-fill operations.

Data per region:
  plate_id        : int
  crust_type      : enum { CONTINENTAL, OCEANIC }
  elevation       : float   [-1.0 .. 1.0], normalized
  moisture        : float   [0.0 .. 1.0]
  temperature     : float   [0.0 .. 1.0]
  biome           : enum    (see biome table below)
  river_flow      : float   [0.0 .. 1.0]  (0 = no river)
  at_boundary     : bool    (touches another plate)
  boundary_type   : enum { NONE, CONVERGENT, DIVERGENT, TRANSFORM }

### Step 2: Tectonic Plate Generation

Based on redblobgames + kenny.wtf approaches combined:

  a) Plate seed selection
     - Choose N_plates random regions as plate origins (10–30 for a 40km planet)
     - Split into CONTINENTAL (30%) and OCEANIC (70%) seeds

  b) Continental crust flood-fill (BFS with cost function)
     - Priority queue BFS from continental seeds
     - Cost per candidate region = (distance_score * distance_weight
                                  + bearing_score * bearing_weight)
                                  * (1 - noise3d(pos))
                                  * plate.growth_bias
     - distance_score = normalized distance to plate origin (favors compact shapes)
     - bearing_score  = bearing penalty from plate's growth direction (elongates plates)
     - noise modulation makes boundaries irregular and natural
     - Grow until land_fraction target reached (~0.30 of sphere for a mostly-ocean world)

  c) Oceanic crust flood-fill
     - Continue same BFS from the edge of continental regions
     - Lower cost penalties → expands further, covers remaining surface
     - Track which regions become plate boundary zones

  d) Assign plate movement vectors
     - Each plate gets a random 3D rotation axis + angular velocity (normalized)
     - Used only for boundary classification — no full simulation

### Step 3: Plate Boundary Classification & Elevation

For each pair of adjacent regions on different plates:
  - Compute dot product of their plate's movement vectors projected onto the
    boundary normal (= direction toward the boundary from each plate interior)
  - dot > +threshold  → CONVERGENT  (plates push together)
  - dot < -threshold  → DIVERGENT   (plates pull apart)
  - |dot| <= threshold → TRANSFORM  (plates slide past)

Boundary elevation rules (from redblobgames, refined):
  | Boundary type | Crust A | Crust B | Result                              |
  |---------------|---------|---------|-------------------------------------|
  | Convergent    | Cont    | Cont    | HIGH mountain chain (both sides)    |
  | Convergent    | Cont    | Ocean   | HIGH mountains (cont) + trench      |
  | Convergent    | Ocean   | Ocean   | Island arc (medium elevation)       |
  | Divergent     | Cont    | Cont    | Rift valley (low, depression)       |
  | Divergent     | Ocean   | Ocean   | Mid-ocean ridge (slight rise)       |
  | Transform     | any     | any     | No height change, slight roughness  |

Mark boundary regions with their elevation type. Then use distance-field
interpolation (3 distance fields: to mountains, to ocean, to coast) to propagate
elevation across the rest of each plate — regions far from any boundary get their
plate's base elevation.

Add fractal noise on top of the interpolated field to break up uniform areas.

### Step 4: Ocean / Land Threshold

  - Regions with elevation < 0.0 → ocean
  - Regions with elevation >= 0.0 → land
  - Shallow (0.0–0.1) → coast/beach
  - Deep ocean (< -0.5) → abyssal plain

### Step 5: Moisture Assignment

Simple approach (sufficient for a game world):
  - Base moisture: random per plate (continental = 0.4–0.8, ocean = 1.0)
  - Modulate by latitude proxy: equator = higher moisture potential
  - Reduce moisture on the lee side of mountain chains (rain shadow):
      for mountain boundary regions, moisture of regions "downwind" is * 0.4
  - Add noise variation within plates

More advanced (optional later): simulate prevailing wind belts
  (Hadley cells: 0°–30° = Trade winds in, 30°–60° = Westerlies, 60°–90° = Polar)

### Step 6: Temperature

  - Base temperature from latitude (angle from equatorial plane on sphere):
      temp = 1.0 - abs(dot(pos_normalized, north_pole)) 
  - Altitude lapse rate: -0.006°C per metre → in normalized terms, subtract
      (elevation * altitude_temp_drop) from temperature
  - Result: poles cold, equator warm; mountains cold regardless of latitude

### Step 7: Biome Assignment (Whittaker diagram)

Using temperature + moisture lookup:

  | temp \ moisture | 0.0–0.2       | 0.2–0.4        | 0.4–0.6         | 0.6–0.8        | 0.8–1.0          |
  |-----------------|---------------|----------------|-----------------|----------------|------------------|
  | 0.0–0.2 (cold)  | ICE/TUNDRA    | TUNDRA         | TUNDRA          | BOREAL_FOREST  | BOREAL_FOREST    |
  | 0.2–0.4 (cool)  | DESERT_COLD   | SHRUBLAND      | TEMPERATE_GRASS | TEMP_FOREST    | TEMP_RAINFOREST  |
  | 0.4–0.6 (warm)  | DESERT        | SAVANNA        | SAVANNA         | TROPICAL_GRASS | TROPICAL_FOREST  |
  | 0.6–0.8 (hot)   | HOT_DESERT    | HOT_DESERT     | SAVANNA         | TROPICAL_GRASS | TROPICAL_FOREST  |
  | 0.8–1.0 (hot)   | HOT_DESERT    | HOT_DESERT     | SAVANNA         | TROPICAL_GRASS | JUNGLE           |

  Special overrides:
  - Ocean regions → OCEAN / SHALLOW_OCEAN
  - elevation > 0.85 anywhere → ALPINE / SNOW_CAP
  - Convergent mountain boundary + cold → MOUNTAIN

  Biome → voxel texture slot mapping:
    OCEAN / SHALLOW_OCEAN  → slot 3 (sand at waterline), no surface voxels above
    TUNDRA / ICE           → slot 2 (snow)
    BOREAL_FOREST          → slot 0 (grass/moss)
    TEMP_FOREST            → slot 0 (grass)
    TROPICAL_FOREST        → slot 5 (moss/lush)
    JUNGLE                 → slot 5 (moss/lush)
    DESERT / HOT_DESERT    → slot 3 (sand)
    SAVANNA                → slot 4 (dirt/dry grass)
    SHRUBLAND              → slot 0 (grass)
    GRASSLAND              → slot 0 (grass)
    ALPINE / SNOW_CAP      → slot 2 (snow)
    MOUNTAIN               → slot 1 (rock)

### Step 8: Rivers (mapgen4 approach, adapted for sphere)

  - Rivers flow on the Delaunay triangle mesh (dual of Voronoi)
  - Each triangle = one river segment node
  - Build binary tree: start from coastlines, grow uphill following lowest-elevation
    neighbors, fork at tributaries until reaching "springs" at moisture-rich high areas
  - Array representation: BFS order, parent array for O(1) parent lookup
  - Simulate rainfall flow: traverse array in reverse (leaves → root)
      1. rainfall = moisture[triangle] * rainfall_rate
      2. flow[t] += rainfall
      3. flow[parent[t]] += flow[t]
  - Threshold: only rivers with flow > min_flow_threshold are kept
  - River data stored per-triangle: flow_volume (used to scale river width/depth in voxels)
  - River carving in voxel generator: at query time, check if current position is
    near a high-flow river triangle → subtract from SDF (carve a valley/channel)

================================================================================
## PHASE B — VOXEL QUERY (Real-time, per chunk)

When VoxelLodTerrain needs a chunk at world position P:

### The Query Function

Given a 3D world position P, map to planet surface:
  1. direction   = normalize(P - planet_center)
  2. surface_pos = planet_center + direction * planet_radius
  3. Find which Voronoi region contains surface_pos
     → Use spatial hash / k-d tree on sphere for O(log N) lookup
     → Returns: region data (elevation, biome, moisture, river_flow)

### SDF Calculation

  base_sdf = length(P - planet_center) - planet_radius

  // Large-scale shape from plate tectonics
  terrain_height = region.elevation * max_terrain_height  // e.g. max 3000m

  // Medium-scale: fractal noise layered on top (biome-specific params)
  noise_height = biome_noise(direction, region.biome)

  // River carving
  river_carve = river_sdf(P, region.river_flow)  // negative = carve valley

  // Final surface height
  surface_height = terrain_height + noise_height + river_carve

  // Voxel SDF
  sdf = length(P - planet_center) - (planet_radius + surface_height)

  // Caves (optional): subtract cave SDF from solid interior
  // sdf = max(sdf, -cave_sdf(P))

### Biome Noise Parameters (per biome)

  MOUNTAINS:      amplitude=400, frequency=0.002, octaves=6, ridged=true
  HILLS:          amplitude=80,  frequency=0.008, octaves=4, ridged=false
  GRASSLAND:      amplitude=20,  frequency=0.02,  octaves=3, ridged=false
  DESERT:         amplitude=15,  frequency=0.015, octaves=2, ridged=false (dunes)
  JUNGLE:         amplitude=30,  frequency=0.025, octaves=4, ridged=false
  TUNDRA:         amplitude=10,  frequency=0.01,  octaves=2, ridged=false
  OCEAN_FLOOR:    amplitude=200, frequency=0.003, octaves=3, ridged=false

  Biomes blend at region boundaries by interpolating noise params based on
  distance to boundary (smooth lerp over ~5% of region width).

### Material Index Assignment

  material_index = biome_to_texture_slot[region.biome]

  // Slope override (same as shader, but in generator for Mixel4 data)
  // Computed from SDF gradient direction approximation:
  normal_approx = direction  // sphere normal = outward direction
  slope = 1.0 - abs(dot(normal_approx, up_world))  // 0=flat, 1=vertical
  if slope > 0.65:  material_index = 1  // rock on cliffs

  // Altitude override
  if surface_height > snow_line:  material_index = 2  // snow

================================================================================
## IMPLEMENTATION PLAN

### Tools / Code involved

  Language:      GDScript (prototype) → C++ in modules/voxel later if needed
  Generator:     VoxelGeneratorScript (GDScript) extending VoxelGenerator
                 → Later: VoxelGeneratorGraph for GPU normalmap support
  Lookup data:   Stored as PackedFloat32Array / PackedInt32Array on the generator
                 → Serialisable to a .res file (one file per planet seed)
  Spatial query: Implemented as a GDScript helper class using octree or
                 bucket grid on sphere surface

### Files to create

  eden/planet/
    planet_generator.gd          — VoxelGeneratorScript subclass, Phase B query
    planet_pre_generator.gd      — Phase A: builds the region/plate/biome data
    voronoi_sphere.gd            — Fibonacci points, BFS neighbors, spatial query
    plate_tectonics.gd           — Plate growing, boundary classification, elevation
    biome_classifier.gd          — Temperature/moisture → biome enum
    river_system.gd              — Binary tree river flow simulation
    planet_data.gd               — Data container (PackedArrays), save/load
    planet_data.tres             — Saved output of a pre-generation run

### Generation sequence (planet_pre_generator.gd)

  func generate_planet(seed: int, radius_km: float) -> PlanetData:
    var sphere = VoronoiSphere.new(NUM_REGIONS, seed)
    var tectonics = PlateTectonics.new(sphere, NUM_PLATES, seed)
    tectonics.grow_plates()
    tectonics.classify_boundaries()
    tectonics.assign_elevation()
    var climate = ClimateModel.new(sphere, tectonics)
    climate.assign_moisture()
    climate.assign_temperature()
    var biomes = BiomeClassifier.new(sphere, climate)
    biomes.classify()
    var rivers = RiverSystem.new(sphere, tectonics, climate)
    rivers.build_tree()
    rivers.simulate_flow()
    return PlanetData.new(sphere, tectonics, biomes, rivers)

### Voxel query (planet_generator.gd)

  extends VoxelGeneratorScript

  var planet_data: PlanetData

  func _generate_block(out_buffer: VoxelBuffer, origin: Vector3i, lod: int):
    for z in BLOCK_SIZE:
      for y in BLOCK_SIZE:
        for x in BLOCK_SIZE:
          var world_pos = Vector3(origin) + Vector3(x, y, z) * (1 << lod)
          var sdf = query_sdf(world_pos)
          var mat = query_material(world_pos)
          out_buffer.set_voxel_f(sdf, x, y, z, VoxelBuffer.CHANNEL_SDF)
          out_buffer.set_voxel(mat, x, y, z, VoxelBuffer.CHANNEL_INDICES)

================================================================================
## PLANET SCALE NOTES (40km radius)

  Planet radius:       40,000 m (40 km)
  Planet circumference: ~251,000 m
  Max terrain height:   3,000 m (mountains) — 7.5% of radius, feels large
  Ocean depth:         -2,000 m (ocean floor)
  Snow line:            2,000 m elevation
  Voronoi region count: 20,000 (each covers ~3.14 km² avg at surface)
  Num tectonic plates:  15–25

  VoxelLodTerrain settings:
    lod_count:           8   (LOD0=1m voxels, LOD7=128m voxels at horizon)
    lod_distance:        256 (meters of LOD0 detail around player)
    view_distance:       32,768 (32km — nearly half the planet visible)
    bounds:              sphere of radius 43,000 (terrain + some sky)

  Floating point concern:
    At 40km radius, max distance from origin = 43,000 m.
    Godot uses float32 for positions → precision = ~0.004m at 40km distance.
    This is acceptable for now. Origin rebasing (eden_largeworld) adds ~1mm
    precision anywhere on the surface once we need it for fine close-up detail.

================================================================================
## REFERENCE IMPLEMENTATIONS

  redblobgames/1843-planet-generation  (JS, Apache-2.0)
    → planet-generation.js: tectonic plates, elevation, biomes, rivers
    → sphere-mesh.js: Fibonacci sphere, Delaunay/Voronoi on sphere
    → Translatable to GDScript almost 1:1 (array-based, no complex deps)
    → URL: https://github.com/redblobgames/1843-planet-generation

  kenny.wtf world-synth  (TS/WebGL)
    → Cost function BFS for plate growing (distanceScore + biasDirectionScore)
    → Continental vs oceanic crust expansion in two passes
    → H3 hex grid (we'll use Voronoi instead)
    → URL: https://kenny.wtf/posts/world-synth-tectonic-plates/

  mapgen4 river system  (JS, redblobgames)
    → Binary tree river representation in array form
    → Two-pass: build tree (coastline → spring), simulate flow (spring → coast)
    → Direct port to GDScript: replace JS arrays with PackedInt32Array
    → URL: https://simblob.blogspot.com/2018/10/mapgen4-river-representation.html
