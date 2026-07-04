# Eden Project – Gameplay Systems Reference Guide
> **Engine:** `F:\Dev\GodotEden` (Godot 4 fork)
> **Project:** `F:\Dev\Projects\eden_project`
> **Date:** 2026-03-12

This guide covers all non-voxel gameplay systems: menus, player, ship, camera, gravity/physics,
atmospheres, spacemaps, VFX, mesh instances/PCG, character interactions, and animations.
See `VOXEL_REFERENCE.md` for voxel-specific documentation.

---

## Table of Contents
1. [Scene Graph Overview](#1-scene-graph-overview)
2. [Main Menu System](#2-main-menu-system)
3. [Settings System](#3-settings-system)
4. [Solar System & Orbital Mechanics](#4-solar-system--orbital-mechanics)
5. [Player / Character Controller](#5-player--character-controller)
6. [Ship Controller & Physics](#6-ship-controller--physics)
7. [Gravity & Radial Physics (Planets)](#7-gravity--radial-physics-planets)
8. [Camera System](#8-camera-system)
9. [Floating World Origin (Reference Body System)](#9-floating-world-origin-reference-body-system)
10. [Atmosphere System](#10-atmosphere-system)
11. [Space Background / Skybox](#11-space-background--skybox)
12. [VFX: Jet Thruster Effects](#12-vfx-jet-thruster-effects)
13. [VFX: Speed Particles](#13-vfx-speed-particles)
14. [Mesh Instances & PCG Props (Non-Voxel)](#14-mesh-instances--pcg-props-non-voxel)
15. [Waypoint System](#15-waypoint-system)
16. [Audio System](#16-audio-system)
17. [HUD & UI](#17-hud--ui)
18. [Lens Flare](#18-lens-flare)
19. [Character Interactions (Dig, Build, Interact)](#19-character-interactions-dig-build-interact)
20. [Animations](#20-animations)
21. [Debug Draw (DDD)](#21-debug-draw-ddd)
22. [Double Clip (Large World Camera)](#22-double-clip-large-world-camera)
23. [Collision Layers Reference](#23-collision-layers-reference)
24. [Common Patterns & Pitfalls](#24-common-patterns--pitfalls)

---

## 1. Scene Graph Overview

```
Main (main.tscn)                        # main.gd: top-level coordinator
├── MainMenu (Control)                  # gui/main_menu/
├── SettingsUI (Control)                # gui/settings/
└── [GameWorld instantiated at runtime] # game.tscn -> solar_system.gd

game.tscn (SolarSystem node)           # solar_system.gd
├── WorldEnvironment
├── SpawnPoint (Node3D)
├── MouseCapture                        # gui/mouse_capture.gd
├── HUD                                 # gui/hud.gd
├── PauseMenu                           # gui/pause_menu/
├── LensFlare                           # addons/SIsilicon.vfx.lens flare/
├── Sun/ (Node3D)
│   └── DirectionalLight3D
│   └── [MeshInstance3D sphere]
├── Earth/ (Node3D)
│   ├── VoxelLodTerrain (volume)
│   │   └── VoxelInstancer
│   ├── PlanetAtmosphere (tscn)
│   └── [Sea MeshInstance3D]
├── Camera3D (camera.gd)
└── Ship (ship.tscn -> RigidBody3D)     # ship.gd
    ├── Controller (ship_controller.gd)
    ├── Visual/VisualRoot/ship/
    │   └── JetVFX* nodes
    ├── ShipAudio
    ├── AnimationPlayer
    └── [Character (spawned at runtime)]
        ├── Head
        ├── Visual/
        ├── Controller (character_controller.gd)
        └── Audio (character_audio.gd)
```

---

## 2. Main Menu System

### Scene Structure

```
Main (main.gd)
├── MainMenu (Control) — gui/main_menu/
└── SettingsUI (Control) — gui/settings/
```

### main.gd Flow

```gdscript
extends Node

@onready var _main_menu : Control = $MainMenu
@onready var _settings_ui : Control = $SettingsUI
var _settings := Settings.new()
var _game : SolarSystem

func _ready():
    _settings_ui.set_settings(_settings)

# Signals from MainMenu buttons:
func _on_MainMenu_start_requested():
    _main_menu.hide()
    var game_scene : PackedScene = load("res://game.tscn")
    _game = game_scene.instantiate()
    _game.set_settings(_settings)
    _game.set_settings_ui(_settings_ui)
    _game.exit_to_menu_requested.connect(_on_game_exit_to_menu_requested)
    add_child(_game)

func _on_MainMenu_exit_requested():
    get_tree().quit()

func _on_game_exit_to_menu_requested():
    _game.queue_free()
    _game = null
    _main_menu.show()
```

### main_menu.gd (simple signal emitter)

```gdscript
extends Control
signal start_requested
signal settings_requested
signal exit_requested

func _on_Start_pressed(): start_requested.emit()
func _on_Settings_pressed(): settings_requested.emit()
func _on_Exit_pressed(): exit_requested.emit()
```

### Pause Menu (in-game)

Triggered by ESC key in `SolarSystem._unhandled_input()`:

```gdscript
func _unhandled_input(event: InputEvent):
    if event is InputEventKey and event.pressed and not event.is_echo():
        if event.keycode == KEY_ESCAPE:
            if _settings_ui.visible:
                _settings_ui.hide()
            elif _pause_menu.visible:
                _pause_menu.hide()
                _mouse_capture.capture()
            else:
                _pause_menu.show()
```

Pause menu signals: `resume_requested`, `settings_requested`, `exit_to_menu_requested`, `exit_to_os_requested`.

### Settings UI

Settings UI is created in `Main` and shared into the game instance via `set_settings_ui()`.
This allows settings to persist across game restarts without recreating the UI.
`_settings_ui.move_to_front()` is called when showing from the pause menu.

---

## 3. Settings System

`settings.gd` is a plain GDScript class (not a node) that holds all game settings as variables.
A single instance is created in `main.gd` and passed to all systems.

### Key Settings

```gdscript
class_name Settings

# World scale
var world_scale_x10 := false    # If true, all distances * 10

# Graphics
var shadows_enabled := true
var glow_enabled := false
var lens_flares_enabled := true
var wireframe := false
var antialias := ANTIALIAS_DISABLED
var clouds_quality := CLOUDS_HIGH

# Debug
var debug_text := false
var show_octree_nodes := false
var show_mesh_updates := false
var show_edited_data_blocks := false

# Audio
var main_volume_linear := 1.0

# Enum values:
# ANTIALIAS_DISABLED, ANTIALIAS_FXAA
# CLOUDS_DISABLED, CLOUDS_LOW, CLOUDS_HIGH
```

### Applying Settings at Runtime

Graphics settings are applied in `solar_system.gd _process()` or `_process_setting_changes()`:

```gdscript
func _process_setting_changes():
    if _settings.shadows_enabled != _directional_light.shadow_enabled:
        _directional_light.shadow_enabled = _settings.shadows_enabled
    if _settings.glow_enabled != _environment.glow_enabled:
        _environment.glow_enabled = _settings.glow_enabled
    if _settings.clouds_quality != _last_clouds_quality:
        _last_clouds_quality = _settings.clouds_quality
        for body in _bodies:
            if body.atmosphere != null:
                SolarSystemSetup.update_atmosphere_settings(body, _settings)
```

Volume/antialiasing in `main.gd _process()`:

```gdscript
AudioServer.set_bus_volume_db(0, linear_to_db(_settings.main_volume_linear))
DDD.visible = _settings.debug_text
if _settings.antialias == Settings.ANTIALIAS_FXAA:
    viewport.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA
```

---

## 4. Solar System & Orbital Mechanics

### StellarBody (stellar_body.gd)

A plain GDScript class (no Node) representing one celestial body:

```gdscript
# Static definition
var name: String
var type: int          # TYPE_SUN, TYPE_ROCKY, TYPE_GAS
var radius: float
var parent_id: int     # Index in _bodies array (-1 = no parent = Sun)
var distance_to_parent: float
var orbit_revolution_time: float
var self_revolution_time: float
var orbit_tilt: float
var self_tilt: float
var atmosphere_mode: int   # ATMOSPHERE_DISABLED, ATMOSPHERE_MONOCHROME, ATMOSPHERE_WITH_SCATTERING

# Runtime state
var orbit_revolution_progress: float   # 0..1
var self_revolution_progress: float    # 0..1

# Godot nodes (assigned during setup)
var node: Node3D           # Root node for this body
var volume: VoxelLodTerrain
var instancer: VoxelInstancer
var atmosphere: PlanetAtmosphere
var static_bodies: Array[StaticBody3D]
```

### Orbit Simulation (_physics_process in solar_system.gd)

```gdscript
# Each frame: advance revolution progress
body.self_revolution_progress += delta / body.self_revolution_time
body.orbit_revolution_progress += delta / body.orbit_revolution_time

# Compute transform from scratch each frame
func _compute_absolute_body_transform(body: StellarBody) -> Transform3D:
    var parent_transform := Transform3D()
    if body.parent_id != -1:
        parent_transform = _compute_absolute_body_transform(_bodies[body.parent_id])
    
    var orbit_angle := body.orbit_revolution_progress * TAU
    var pos := Vector3(cos(orbit_angle), 0, sin(orbit_angle)) * body.distance_to_parent
    pos = pos.rotated(Vector3(0, 0, 1), body.orbit_tilt)
    
    var self_angle := body.self_revolution_progress * TAU
    var basis := Basis.from_euler(Vector3(0, self_angle, body.self_tilt))
    
    return parent_transform * Transform3D(basis, pos)
```

### Solar System Scale (Eden Project)

| Body | Radius | Distance to Parent |
|---|---|---|
| Sun | 1500 | — |
| Mercury | 900 | 14,400 |
| Earth | 1800 | 25,600 |
| Moon | 600 | 7,500 (from Earth) |
| Mars | 1280 | 48,000 |
| Jupiter | 3000 | 70,400 |

Large world scale (`world_scale_x10 = true`) multiplies all distances and radii by 10.
Orbit speed is preserved by recalculating revolution time from angular velocity.

### Setup Pipeline

```gdscript
# Called once per body during loading screen:
SolarSystemSetup.setup_stellar_body(body, solar_system_node, settings)
# This calls: _setup_sun() OR _setup_rocky_planet()
# Then: _setup_sea(), _setup_atmosphere() as needed
```

---

## 5. Player / Character Controller

### Scene Structure (character.tscn)

```
[CharacterBody3D or root Node3D with CharacterBody component]
├── Head (Node3D)            # Camera mount point
├── Visual/ (Node3D)
│   ├── Head (visual)
│   └── FlashLight (SpotLight3D)
├── Controller (character_controller.gd)
└── Audio (character_audio.gd)
```

### character_controller.gd

Extends `Node`, uses a `CharacterBody` from `addons/zylann.3d_basics/`:

```gdscript
# Constants
const MOVE_ACCELERATION := 40.0
const MOVE_DAMP_FACTOR := 0.1
const JUMP_COOLDOWN_TIME := 0.3
const JUMP_SPEED := 8.0

func _physics_process(delta):
    # Read WASD input
    var motor := Vector3()
    if Input.is_key_pressed(KEY_W): motor += Vector3(0, 0, -1)
    if Input.is_key_pressed(KEY_S): motor += Vector3(0, 0, 1)
    if Input.is_key_pressed(KEY_A): motor += Vector3(-1, 0, 0)
    if Input.is_key_pressed(KEY_D): motor += Vector3(1, 0, 0)

    var character_body := _get_body()
    character_body.set_motor(motor)

    # Radial gravity: up = away from planet center
    var planet_up := (character_body.global_transform.origin - planet_center).normalized()
    character_body.set_planet_up(planet_up)
    
    _process_actions()
    _process_undig()  # Escape if buried in terrain
```

### CharacterBody (from 3d_basics addon)

The character uses a 3rd-party CharacterBody helper that supports:
- `set_motor(Vector3)` — input direction in local space
- `set_planet_up(Vector3)` — radial gravity direction
- Automatic vertical correction toward planet up
- Jump cooldown management

### Character Spawning

```gdscript
# In ship_controller.gd after landing:
var character: Node3D = CharacterScene.instantiate()
character.position = spawn_pos
ship.get_parent().add_child(character)     # Add to solar system node
camera.set_target(character)               # Camera switches to follow character
ship.disable_controller()                  # Freeze ship
```

### Vertical Correction (Radial Gravity)

```gdscript
const VERTICAL_CORRECTION_SPEED = PI   # Radians/second to align up vector

# The CharacterBody addon aligns the character's Y axis to planet_up
# at VERTICAL_CORRECTION_SPEED radians per second
```

---

## 6. Ship Controller & Physics

### ship.gd (extends RigidBody3D)

```gdscript
class_name Ship

# Export parameters
@export var linear_acceleration := 10.0
@export var angular_acceleration := 1000.0
@export var speed_cap_on_planet := 40.0
@export var speed_cap_in_space := 400.0

var _speed_cap_in_space_superspeed_multiplier := 10.0
var _linear_acceleration_superspeed_multiplier := 15.0
```

### States

```gdscript
const STATE_LANDED = 0
const STATE_FLYING = 1

func enable_controller():
    _controller.set_enabled(true)
    # Remove landed-only nodes from tree (interior, hatches, etc.)
    # Enable flight collision shapes
    freeze = false
    _close_hatch()

func disable_controller():
    _controller.set_enabled(false)
    # Restore landed-only nodes
    # Disable flight collision shapes
    freeze = true        # RigidBody3D freeze = static
    _open_hatch()
```

### ship_controller.gd Input

```gdscript
# Movement input -> set_move_cmd()
var motor := Vector3()
if Input.is_key_pressed(KEY_W): motor.z += 1   # Forward thrust
if Input.is_key_pressed(KEY_S): motor.z -= 1
if Input.is_key_pressed(KEY_SPACE): motor.y += 1  # Up
if Input.is_key_pressed(KEY_SHIFT): motor.y -= 1  # Down

# Turn input -> set_turn_cmd()
# Mouse: relative motion drives turn
var motion := -event.relative
_turn_cmd.x += cmd.x   # Yaw
_turn_cmd.y += cmd.y   # Pitch

# Keyboard roll (A/D):
_turn_cmd.z -= keyboard_turn_sensitivity  # Roll left/right

# Superspeed (SPACE while set_superspeed_cmd(true))
_ship.set_superspeed_cmd(Input.is_key_pressed(KEY_SPACE))
```

### _integrate_forces (RigidBody3D physics)

Forces and torques are applied during `_integrate_forces` for physics accuracy.
Reference body changes are also applied here to prevent teleport-induced flickering:

```gdscript
func _integrate_forces(state: PhysicsDirectBodyState3D):
    # Apply accumulated reference change transform
    if _ref_change_info != null:
        state.transform = _ref_change_info.inverse_transform * state.transform
        state.linear_velocity = _ref_change_info.inverse_transform.basis * state.linear_velocity
        _ref_change_info = null
    # ... apply motor forces ...
```

### Exiting Ship (Landing Detection)

```gdscript
func _try_exit_ship():
    # Checks:
    if ship.linear_velocity.length() > 1.0: return  # Still moving
    if stellar_body.type != TYPE_ROCKY: return        # Can't walk here

    # Raycast down to find ground
    var down := (planet_center - ship_pos).normalized()
    var hit := space_state.intersect_ray(ray_query)
    if hit.is_empty(): return   # No ground

    # Spawn character
    var character = CharacterScene.instantiate()
    character.position = spawn_pos
    solar_system.add_child(character)
    camera.set_target(character)
    ship.disable_controller()
```

---

## 7. Gravity & Radial Physics (Planets)

This project implements **radial gravity** (gravity pulls toward planet center) rather than
using Godot's built-in gravity direction.

### Character Radial Gravity

```gdscript
# In character_controller.gd _physics_process():
var planet_center := Vector3()   # planet node is at origin when reference body
var char_pos := character_body.global_transform.origin
var planet_up := (char_pos - planet_center).normalized()
character_body.set_planet_up(planet_up)
# The CharacterBody addon applies gravity in -planet_up direction
```

### Ship Physics (Space / Planet)

The ship uses `RigidBody3D` with force application in `_integrate_forces`.
No custom gravity is applied to the ship directly — it floats freely.
Atmospheric drag is simulated by increasing linear damp when close to a planet surface:

```gdscript
# Damping increases with proximity to planet (simulated atmosphere drag)
var _planet_damping_amount := 0.0
# Applied via state.linear_damp or similar in _integrate_forces
```

### Static Bodies for Collision

When a body becomes the reference (player is near it), static collision bodies are added:

```gdscript
# In set_reference_body():
for sb in body.static_bodies:
    body.node.add_child(sb)   # Add StaticBody3D children for collision
body.static_bodies_are_in_tree = true

# When leaving a body:
for sb in previous_body.static_bodies:
    sb.get_parent().remove_child(sb)
previous_body.static_bodies_are_in_tree = false
```

### split_chunk_rigidbody_component.gd

When terrain chunks are split by digging, they become `RigidBody3D` nodes.
`SplitChunkRigidBodyComponent` is added to manage their lifecycle:

```gdscript
# In character_controller.gd after dig:
var bodies := vt.separate_floating_chunks(splitter_aabb, camera.get_parent())
for body in bodies:
    var cmp := SplitChunkRigidBodyComponent.new()
    body.add_child(cmp)
```

---

## 8. Camera System

### camera.gd (extends Camera3D)

Third-person camera with collision avoidance, reference body change support, and CameraHints.

### Key Properties

```gdscript
@export var distance_to_target := 5.0       # Distance behind target
@export var height_modifier := 0.33         # How high above target
@export var target_height_modifier := 1.5   # Look-at offset above target
@export var side_offset := 0.0              # Lateral offset
@export var auto_find_camera_anchor := false # Scan target for CameraHints child
```

### Setting a Target

```gdscript
camera.set_target(ship)     # Works with RigidBody3D or Node3D
camera.set_target(character)
```

If `auto_find_camera_anchor = true`, the camera scans the target hierarchy for a node
with a `CameraHints` child and uses that node's position + the hints' settings.

### CameraHints (camera_hints.gd)

```gdscript
# Place as child of the node you want the camera to look at/from:
# Properties (set in inspector):
# distance_to_target, height_modifier, target_height_modifier, side_offset
```

### Physics Process Flow

```gdscript
func _physics_process(delta: float):
    # 1. Get ideal transform behind target
    var tt := _get_target_transform()
    var ct := _get_ideal_transform(tt)
    transform = ct
    look_at(tt.origin + target_height_modifier * tt.basis.y, ct.basis.y)
    var ideal_trans := transform

    # 2. Collision avoidance: raycast from target to ideal camera pos
    var hit := dss.intersect_ray(ray_from_target_to_camera)
    if not hit.is_empty():
        trans.origin = hit.position + 0.3 * hit.normal

    # 3. Smooth interpolation (latency)
    transform = prev_trans.interpolate_with(trans, 25.0 * delta)
```

### Reference Body Change Handling

```gdscript
func _on_solar_system_reference_body_changed(info: ReferenceChangeInfo):
    _last_ref_change_info = info
    _wait_for_fucking_physics = 1   # Wait 1 frame for RigidBody to apply transform
    transform = info.inverse_transform * transform
    _prev_target_pos = info.inverse_transform * _prev_target_pos
```

### Near Plane Scaling

```gdscript
# Near plane is set proportionally to distance for best depth precision:
near = distance_to_target * 0.1
```

---

## 9. Floating World Origin (Reference Body System)

When the player approaches a planet within `BODY_REFERENCE_ENTRY_RADIUS_FACTOR * radius`,
the solar system switches its "reference body." The reference body is moved to the origin
(0,0,0) and all other bodies are repositioned relative to it.

This prevents floating-point precision loss at large distances.

### Key Constants

```gdscript
const BODY_REFERENCE_ENTRY_RADIUS_FACTOR = 3.0  # Enter reference when < 3x radius away
const BODY_REFERENCE_EXIT_RADIUS_FACTOR = 3.1   # Hysteresis: exit at 3.1x
```

### set_reference_body(ref_id)

```gdscript
func set_reference_body(ref_id: int):
    var previous_body := _bodies[_reference_body_id]
    # Remove static bodies from previous reference
    for sb in previous_body.static_bodies:
        sb.get_parent().remove_child(sb)

    _reference_body_id = ref_id
    var body := _bodies[ref_id]
    
    # Move reference body to origin, calculate transform delta
    var trans := body.node.transform  # Current transform before reset
    body.node.transform = Transform3D()  # Reset to origin

    # Broadcast the delta transform to all systems that need it
    var info := ReferenceChangeInfo.new()
    info.inverse_transform = trans.affine_inverse() * body.node.transform

    # Add static collision bodies for new reference planet
    for sb in body.static_bodies:
        body.node.add_child(sb)

    reference_body_changed.emit(info)  # Ship, Camera, VoxelLodTerrain process this
```

### ReferenceChangeInfo

```gdscript
class_name ReferenceChangeInfo
var inverse_transform: Transform3D   # Apply to any world-space transform to re-center
```

### Who Listens to `reference_body_changed`

| System | Response |
|---|---|
| Ship (`ship.gd`) | Stores info; applies in `_integrate_forces()` (1 frame later) |
| Camera (`camera.gd`) | Applies immediately + waits 1 frame for physics |
| VoxelLodTerrain (via process_callback) | Can be set to PHYSICS mode to sync |

### Physics Frame Alignment

```gdscript
# Ship applies the transform in _integrate_forces for seamless transition:
func _on_solar_system_reference_body_changed(info: ReferenceChangeInfo):
    _ref_change_info = info  # Deferred to _integrate_forces

func _integrate_forces(state):
    if _ref_change_info != null:
        state.transform = _ref_change_info.inverse_transform * state.transform
        state.linear_velocity = _ref_change_info.inverse_transform.basis * state.linear_velocity
        _ref_change_info = null
```

---

## 10. Atmosphere System

Uses the `zylann.atmosphere` addon: `res://addons/zylann.atmosphere/`.

### PlanetAtmosphere Node

```gdscript
# Instantiate from scene:
const VolumetricAtmosphereScene = preload("res://addons/zylann.atmosphere/planet_atmosphere.tscn")
var atmo: PlanetAtmosphere = VolumetricAtmosphereScene.instantiate()
root.add_child(atmo)
body.atmosphere = atmo
```

### Key Properties

```gdscript
atmo.planet_radius = body.radius * 1.03   # Slightly larger than terrain radius
atmo.atmosphere_height = 0.15 * body.radius  # Thickness of atmosphere shell
atmo.sun_path = "/root/Main/GameWorld/Sun/DirectionalLight"  # Path to light node
atmo.clouds_rotation_speed = 0.5
```

### Shader Selection (update_atmosphere_settings)

The shader is chosen based on atmosphere mode and cloud quality:

```gdscript
# Scattered atmosphere (realistic, more expensive):
# ATMOSPHERE_WITH_SCATTERING + CLOUDS_HIGH -> planet_atmosphere_clouds_high.gdshader
# ATMOSPHERE_WITH_SCATTERING + CLOUDS_LOW  -> planet_atmosphere_clouds.gdshader
# ATMOSPHERE_WITH_SCATTERING + no clouds   -> planet_atmosphere_no_clouds.gdshader

# Monochrome atmosphere (fake color gradient, cheaper):
# ATMOSPHERE_MONOCHROME + CLOUDS_HIGH -> planet_atmosphere_v1_clouds_high.gdshader
# ATMOSPHERE_MONOCHROME + CLOUDS_LOW  -> planet_atmosphere_v1_clouds.gdshader

atmo.custom_shader = AtmosphereScatteredCloudsHighShader  # Ref<Shader>
```

### Scattered Atmosphere Parameters

```gdscript
atmo.set_shader_parameter(&"u_atmosphere_modulate", body.atmosphere_color)
atmo.set_shader_parameter(&"u_scattering_strength", 6.0)    # Higher = more scattering
atmo.set_shader_parameter(&"u_atmosphere_ambient_color", body.atmosphere_ambient_color)
atmo.set_shader_parameter(&"u_density", 0.05)               # Atmospheric density
```

### Cloud Parameters

```gdscript
# Requires: clouds_coverage_cubemap (Cubemap resource) and cloud_shape_texture (Texture3D)
atmo.set_shader_parameter(&"u_cloud_density_scale", 0.02)
atmo.set_shader_parameter(&"u_cloud_shape_texture", CloudShapeTexture3D)
atmo.set_shader_parameter(&"u_cloud_coverage_cubemap", body.clouds_coverage_cubemap)
atmo.set_shader_parameter(&"u_cloud_shape_factor", 0.4)
atmo.set_shader_parameter(&"u_cloud_shape_scale", 0.005)
atmo.set_shader_parameter(&"u_cloud_coverage_bias", 0.0)    # Bias cloud amount (-1 to +1)
atmo.set_shader_parameter(&"u_cloud_shape_invert", 1.0)
```

### Large Distance Sphere Hack

When the camera is far from a planet, the atmosphere needs to switch to sphere rendering
to avoid depth buffer issues:

```gdscript
func _process_atmosphere_large_distance_hack():
    for body in _bodies:
        if body.atmosphere != null:
            var distance := cam_pos.distance_to(body.node.global_transform.origin)
            var sphere_factor := clampf(
                (distance - body.radius * 3.0) / 2000.0, 0.0, 1.0)
            body.atmosphere.set_shader_parameter(&"u_sphere_depth_factor", sphere_factor)
```

### Monochrome Atmosphere Parameters

```gdscript
atmo.set_shader_parameter(&"u_day_color0", body.atmosphere_color)
atmo.set_shader_parameter(&"u_day_color1", body.atmosphere_color.lerp(Color(1,1,1), 0.5))
atmo.set_shader_parameter(&"u_night_color0", body.atmosphere_color.darkened(0.8))
atmo.set_shader_parameter(&"u_night_color1", body.atmosphere_color.darkened(0.8).lerp(Color.WHITE, 0.0))
atmo.set_shader_parameter(&"u_density", 0.001)
```

### Body-specific Cloud Textures

```gdscript
# Preloaded cloud coverage cubemaps:
const CloudCoverageTextureEarth = preload("./atmosphere/cloud_coverage_earth.tres")
const CloudCoverageTextureMars  = preload("./atmosphere/cloud_coverage_mars.tres")
const CloudCoverageTextureGas   = preload("./atmosphere/cloud_coverage_gas.tres")
const CloudShapeTexture3D       = preload("./atmosphere/noise_texture_3d.res")
```

---

## 11. Space Background / Skybox

The space background is a `WorldEnvironment` with a `Sky` resource.
In `solar_system.gd`, the sky rotation is updated to simulate planetary rotation:

```gdscript
@onready var _environment: Environment = $WorldEnvironment.environment

# When on a planet, sky appears to rotate with planet:
if _reference_body_id != 0:
    _environment.sky_rotation = ref_trans_inverse.basis.get_euler()
else:
    _environment.sky_rotation = Vector3()  # Sky is stationary in space
```

The `space_background.png` texture in `solar_system/` is the starfield/nebula image.

### Directional Shadow Scaling

Shadow distance scales based on camera distance to planet surface:

```gdscript
func _process_directional_shadow_distance():
    var distance_to_surface := maxf(distance_to_core - ref_body.radius, 0.0)
    var t := clampf((distance_to_surface - 10.0) / (1000.0 - 10.0), 0.0, 1.0)
    var shadow_distance := lerpf(500.0, 20000.0, t)
    _directional_light.directional_shadow_max_distance = shadow_distance
```

### Directional Light Setup (Sun)

```gdscript
var light := DirectionalLight3D.new()
light.shadow_enabled = true
light.shadow_opacity = 0.99           # Shadows not 100% opaque (compensate for no sky light)
light.shadow_normal_bias = 0.2
light.directional_shadow_split_1 = 0.1
light.directional_shadow_split_2 = 0.2
light.directional_shadow_split_3 = 0.5
light.directional_shadow_blend_splits = true
light.directional_shadow_max_distance = 20000.0
```

---

## 12. VFX: Jet Thruster Effects

### jet_vfx.gd (extends Node3D)

Each thruster is a scene with a `MeshInstance3D` and an `OmniLight3D`.

```gdscript
@onready var _mesh_instance: MeshInstance3D = $MeshInstance
@onready var _light: OmniLight3D = $OmniLight

var _power := 0.0        # Current smoothed power (0-1)
var _target_power := 0.0 # Target power

func _ready():
    # Duplicate the material so each jet has independent parameters
    _mesh_instance.material_override = _mesh_instance.material_override.duplicate()

func set_power(p: float):
    _target_power = clampf(p, 0.0, 1.0)

func _process(delta: float):
    _power = lerpf(_power, _target_power, delta * 2.0)  # Smooth ramp
    _mesh_instance.material_override.set_shader_parameter(&"u_power", _power)
    _mesh_instance.scale = Vector3(1, 1, 0.1 + _power * 15.0 * _mesh_instance.scale.x)
    _light.light_energy = _power * 2.0
```

### Shader (vfx_jet.gdshader)

Receives `u_power` uniform to scale emission/alpha.

### Ship Jet VFX Control (ship.gd)

```gdscript
@onready var _main_jets: Array[JetVFX] = [
    $Visual/VisualRoot/JetVFXMainLeft,
    $Visual/VisualRoot/JetVFXMainRight,
]
@onready var _left_roll_jets: Array[JetVFX] = [...]
@onready var _right_roll_jets: Array[JetVFX] = [...]

# In _process() or _integrate_forces():
for jet in _main_jets:
    jet.set_power(forward_thrust_amount)  # 0..1
```

### jet_vfx.tscn Structure

```
JetVFX (Node3D) — jet_vfx.gd
├── MeshInstance3D
│   └── vfx_jet.obj (custom mesh)
│   └── material_override: ShaderMaterial (vfx_jet.gdshader)
└── OmniLight3D
```

---

## 13. VFX: Speed Particles

Speed particles are a `GPUParticles3D` or similar system controlled by `motion_particles.gd`.

```
ship/
├── motion_particles.gd  # Controls speed particle emission rate/direction
└── speed_particles.gdshader  # Particle shader
```

`motion_particles.gd` adjusts particle emission based on ship velocity,
creating a motion-blur/speed-trail effect.

---

## 14. Mesh Instances & PCG Props (Non-Voxel)

### Sun Mesh

```gdscript
# Created procedurally in _setup_sun():
var mi := MeshInstance3D.new()
var mesh := SphereMesh.new()
mesh.radius = body.radius
mesh.height = 2.0 * mesh.radius
mi.mesh = mesh
mi.material_override = SunMaterial
mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
root.add_child(mi)
```

### Sea Sphere

```gdscript
# Transparent sphere offset slightly inside planet radius:
static func _setup_sea(body: StellarBody, root: Node3D):
    var sea_mesh := SphereMesh.new()
    sea_mesh.radius = body.radius * 0.985   # Slightly inside terrain surface
    sea_mesh.height = 2.0 * sea_mesh.radius
    var sea_mi := MeshInstance3D.new()
    sea_mi.mesh = sea_mesh
    sea_mi.material_override = WaterSeaMaterial
    root.add_child(sea_mi)
```

### Props

Props are pre-made scene files loaded and positioned procedurally:

```
props/
├── big_rocks/big_rock1.tscn + big_rock1.obj
├── rocks/rock1.tscn + rock1.obj + rock_material.tres
├── pebbles/pebble1.obj
└── grass/grass.tscn
```

### Material Sharing on Instanced Props

```gdscript
# Apply shared material to all rock meshes before creating instancer:
for mesh in [Pebble1Mesh, Rock1Mesh, BigRock1Mesh]:
    mesh.surface_set_material(0, RockMaterial)
```

> After calling `surface_set_material()`, the material is embedded in the mesh resource.
> All MultiMesh instances using that mesh will share the material automatically.

### VoxelInstanceLibraryMultiMeshItem from Scene Template

```gdscript
var item := VoxelInstanceLibraryMultiMeshItem.new()
var rock_template: Node = Rock1Scene.instantiate()
item.setup_from_template(rock_template)  # Extracts mesh, materials, collision shapes
rock_template.free()                     # Free the template node (item has a copy)
```

### Procedural Stalactite Mesh (no import needed)

```gdscript
# Create mesh at runtime:
var cone := CylinderMesh.new()
cone.radial_segments = 8
cone.rings = 0
cone.top_radius = 0.5
cone.bottom_radius = 0.1
cone.height = 2.5
cone.material = RockMaterial
item.set_mesh(cone, 0)
```

---

## 15. Waypoint System

### waypoint.gd

A Node3D marker placed in the game world. Stored per-planet:

```gdscript
# In stellar_body.gd:
var waypoints: Array[Waypoint] = []
```

### Placing a Waypoint (character_controller.gd)

```gdscript
if _waypoint_cmd:
    _waypoint_cmd = false
    var planet := _get_solar_system().get_reference_stellar_body()
    var waypoint: Waypoint = WaypointScene.instantiate()
    # Position at terrain hit point
    # Add to planet node
    planet.waypoints.append(waypoint)
```

### waypoint.tscn

Simple scene with a visual marker (sprite/mesh) and possibly a label.
The `waypoint.png` texture is used as the icon.

---

## 16. Audio System

### Audio Buses

Bus 0 = Master. Volume controlled in `main.gd _process()`:
```gdscript
AudioServer.set_bus_volume_db(0, linear_to_db(_settings.main_volume_linear))
```

### character_audio.gd

```gdscript
# Methods:
func play_dig(pos: Vector3):   # Play dig sound at position
    # Uses AudioStreamPlayer3D positioned at dig site
```

### ship_audio.gd

```gdscript
# Methods:
func play_enabled():     # Thruster startup sound
func play_disabled():    # Thruster shutdown sound
```

### Ambient Sound System (Stellar Body)

Each planet has day/night ambient sounds:
```gdscript
var day_ambient_sound: AudioStream
var night_ambient_sound: AudioStream

# Example bodies:
# Earth: EarthDaySound, EarthNightSound
# Others: WindSound
```

The HUD/audio system switches between day/night sounds based on sun angle.

---

## 17. HUD & UI

### hud.gd

```gdscript
# Located at: gui/hud.gd
# Referenced from solar_system.gd:
@onready var _hud: HUD = $HUD

_hud.hide()   # During loading
_hud.show()   # After spawn
```

Typical HUD elements: crosshair, speed readout, planet name, debug text overlay.

### Crosshair

```gdscript
# gui/crosshair.png — simple crosshair image
# Displayed as a centered TextureRect in the HUD Control
```

### loading_screen.gd

Used during solar system initialization to show progress:

```gdscript
# Signals from SolarSystem:
signal loading_progressed(info: LoadingProgress)

# LoadingProgress properties:
var message: String
var progress: float   # 0..1
var finished: bool
```

### Mouse Capture (mouse_capture.gd)

```gdscript
@onready var _mouse_capture: MouseCapture = $MouseCapture

_mouse_capture.capture()    # Lock mouse for gameplay
_mouse_capture.release()    # Release for UI

# mouse_capture.gd wraps:
Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
```

### Background Blur

`gui/background_blur.gdshader` — applied to a `ColorRect` behind menus for blurred background.

### Select Rect

`gui/select_rect.png` — stylebox/frame texture for selection highlight.

---

## 18. Lens Flare

Uses `addons/SIsilicon.vfx.lens flare/` addon.

```gdscript
@onready var _lens_flare: LensFlare = $LensFlare

# Settings applied from _process_setting_changes():
if _settings.lens_flares_enabled != _lens_flare.enabled:
    _lens_flare.enabled = _settings.lens_flares_enabled
```

The lens flare should be positioned at or near the sun's screen position.
The addon typically auto-detects the brightest light source or uses a configured `DirectionalLight3D`.

---

## 19. Character Interactions (Dig, Build, Interact)

### Raycasting for Interaction

```gdscript
# In character_controller.gd _process_actions():
var camera := get_viewport().get_camera_3d()
var front := -camera.global_transform.basis.z
var cam_pos := camera.global_transform.origin

var ray_query := PhysicsRayQueryParameters3D.new()
ray_query.from = cam_pos
ray_query.to = cam_pos + front * 50.0   # 50-unit reach
ray_query.exclude = [character_body.get_rid()]
var hit := space_state.intersect_ray(ray_query)
```

### Digging

```gdscript
if _dig_cmd and hit.collider is VoxelLodTerrain:
    _dig_cmd = false
    var volume: VoxelLodTerrain = hit.collider
    var vt: VoxelToolLodTerrain = volume.get_voxel_tool()
    var local_pos := volume.get_global_transform().affine_inverse() * hit.position
    var sphere_size := 3.5

    vt.channel = VoxelBuffer.CHANNEL_SDF
    vt.mode = VoxelTool.MODE_REMOVE
    vt.do_sphere(local_pos, sphere_size)
    _audio.play_dig(local_pos)

    # Split off floating geometry
    var splitter_aabb := AABB(local_pos, Vector3()).grow(16.0)
    var bodies := vt.separate_floating_chunks(splitter_aabb, camera.get_parent())
    for body in bodies:
        var cmp := SplitChunkRigidBodyComponent.new()
        body.add_child(cmp)
```

### Building

```gdscript
if _build_cmd and hit.collider is VoxelLodTerrain:
    _build_cmd = false
    var volume: VoxelLodTerrain = hit.collider
    var vt: VoxelTool = volume.get_voxel_tool()
    var local_pos := volume.get_global_transform().affine_inverse() * hit.position
    vt.channel = VoxelBuffer.CHANNEL_SDF
    vt.mode = VoxelTool.MODE_ADD
    vt.do_sphere(local_pos, 3.5)
```

### Debug Visualization During Interaction

```gdscript
# Draw debug shapes at hit point:
DDD.draw_box(hit.position, Vector3(0.5, 0.5, 0.5), Color(1, 1, 0))
DDD.draw_ray_3d(hit.position, hit.normal, 1.0, Color(1, 1, 0))
DDD.draw_box_aabb(splitter_aabb, Color(0, 1, 0), 60)  # 60 frames duration
```

### Input Commands (boolean flags, set in _unhandled_input)

```gdscript
var _dig_cmd := false
var _interact_cmd := false
var _build_cmd := false
var _waypoint_cmd := false

func _unhandled_input(event: InputEvent):
    if event is InputEventKey and event.pressed:
        match event.keycode:
            KEY_F:  _dig_cmd = true
            KEY_G:  _build_cmd = true
            KEY_E:  _interact_cmd = true
            KEY_H:  _waypoint_cmd = true
```

---

## 20. Animations

### Ship AnimationPlayer

The ship uses Godot's `AnimationPlayer` for hatch open/close:

```gdscript
@onready var _animation_player: AnimationPlayer = $AnimationPlayer

func _open_hatch():
    _animation_player.play("hatch_open")

func _close_hatch():
    _animation_player.play_backwards("hatch_open")
```

Animations are defined in the ship's `.tscn` file and reference the hatch bone/node transforms.

### Character Visual Alignment

The character's visual root is rotated to align with `planet_up`:

```gdscript
# Handled by CharacterBody addon:
# Smooth rotation of _visual_root toward planet_up at VERTICAL_CORRECTION_SPEED rad/s
```

### Visual Root Separation Pattern (Ship)

The ship's visual root (`_visual_root`) is updated to match the physics body transform
**at the end of `_integrate_forces()`** rather than being a direct child of the RigidBody3D.

```gdscript
# In _integrate_forces():
_visual_root.global_transform = state.transform
```

This allows the camera to follow the visual root without a 1-frame lag, enabling
seamless reference body changes.

---

## 21. Debug Draw (DDD)

`DDD` is the `zylann.debug_draw` addon (`addons/zylann.debug_draw/`). It provides
immediate-mode debug drawing with optional auto-expiry.

```gdscript
# Text overlay (refreshed each frame)
DDD.set_text("Key", value)       # Shows key: value on screen overlay
DDD.set_text("SDF at feet", sdf)
DDD.set_text("Reference body", body_name)

# 3D shapes (drawn for one frame unless duration given)
DDD.draw_box(position, size, color)
DDD.draw_box_aabb(aabb, color, duration_frames)
DDD.draw_ray_3d(origin, direction, length, color)

# Visibility toggle:
DDD.visible = _settings.debug_text
```

### Common Debug Outputs in Eden Project

```gdscript
DDD.set_text("SDF at feet", sdf)                   # Character buried detection
DDD.set_text("Reference body", body_name)           # Which planet is reference
DDD.set_text("Blocks in Earth", "D: 14, M: 23")    # Data/Mesh block counts
DDD.set_text("Shadow distance", shadow_distance)    # Dynamic shadow cascade distance
```

---

## 22. Double Clip (Large World Camera)

`double_clip/double_clip_host.gd` and `double_clip_host.tscn` implement a technique
for rendering both extremely close and extremely far objects without z-fighting.

This is needed for a space game where the camera must see both a planet 10,000 units away
and the ship interior at 0.1 units simultaneously.

The technique typically involves:
1. Rendering the far scene (planets, stars) first with a wide far plane
2. Clearing the depth buffer
3. Rendering the near scene (ship, character) with a tight near/far range

---

## 23. Collision Layers Reference

`collision_layers.gd` defines named constants for collision layer bits:

```gdscript
# Typical pattern:
const LAYER_TERRAIN = 1     # VoxelLodTerrain collision
const LAYER_SHIP = 2
const LAYER_CHARACTER = 3
const LAYER_PROPS = 4
# etc.

# Used when setting up physics bodies:
volume.collision_layer = CollisionLayers.LAYER_TERRAIN
volume.collision_mask = CollisionLayers.LAYER_CHARACTER | CollisionLayers.LAYER_SHIP
```

---

## 24. Common Patterns & Pitfalls

### PITFALL: Hard-coded Node Paths Are Fragile

```gdscript
# FRAGILE: Godot 4 import can rename nodes with @ if there are name collisions
# $Visual/VisualRoot/ship/Interior2  →  might become @Interior2@1234

# The ship.gd workaround:
var visual_model_root := _visual_root.get_node("ship")
for i in visual_model_root.get_child_count():
    var node := visual_model_root.get_child(i)
    if node is StaticBody3D:
        _landed_nodes.append(node)
```

### PITFALL: Settings Shared by Reference

```gdscript
# Settings is a plain class (not Resource), passed by reference.
# Changes in one system are immediately visible to all systems.
# This is intentional but be aware: do not store settings as a local copy.
var _settings := Settings.new()  # Created once in Main
```

### PITFALL: SolarSystem Signals Before Game Is Ready

```gdscript
# SolarSystem.reference_body_changed is emitted during _ready() → set_reference_body(0).
# Ship and Camera connect to this signal. Order of add_child() matters.
# Camera is added BEFORE Ship so it can connect first:
add_child(camera)
add_child(_ship)
```

### PATTERN: Deferred Initialization in Loading Screen

```gdscript
# Use await get_tree().process_frame between heavy operations for responsive loading:
for i in len(_bodies):
    var body = _bodies[i]
    progress_info.message = "Setting up " + body.name + "..."
    loading_progressed.emit(progress_info)
    await get_tree().process_frame   # Let loading screen update
    SolarSystemSetup.setup_stellar_body(body, self, _settings)
```

### PATTERN: Safely Connecting to Optional Parent Signal

```gdscript
# Camera connects to reference_body_changed only if the parent has it:
func _ready():
    if get_parent().has_signal("reference_body_changed"):
        get_parent().reference_body_changed.connect(_on_solar_system_reference_body_changed)
```

### PATTERN: One Settings UI Instance, Shared Across Scenes

```gdscript
# In Main: create settings UI once, share pointer into game
_game.set_settings_ui(_settings_ui)
# The game can show/hide it without owning the lifecycle
# Use move_to_front() when showing over game content
_settings_ui.move_to_front()
```

### PATTERN: World Scale Multiplier

```gdscript
const LARGE_SCALE = 10.0

func apply_game_settings(s: Settings):
    if s.world_scale_x10:
        speed_cap_in_space *= LARGE_SCALE
        speed_cap_on_planet *= 0.25 * LARGE_SCALE  # Slower on planet surface
        _speed_cap_in_space_superspeed_multiplier *= LARGE_SCALE
        _linear_acceleration_superspeed_multiplier *= LARGE_SCALE
```

### PATTERN: Mouse Capture Guard

```gdscript
# Always check mouse mode before processing mouse-based input:
func _process(delta: float):
    if Input.get_mouse_mode() != Input.MOUSE_MODE_CAPTURED:
        return  # UI has focus
```

### PATTERN: Pause Menu Priority

```gdscript
# Use _input instead of _unhandled_input for pause to avoid
# bug where control is stuck during pause menu animations:
# https://github.com/godotengine/godot/issues/20234
func _input(event: InputEvent):
    # ship_controller.gd
```

### PATTERN: RigidBody3D.freeze for Landing

```gdscript
# When landed, freeze the ship to prevent drift:
freeze = true   # RigidBody3D.freeze = kinematic freeze (acts as static)
# Disable collision shapes to prevent terrain pushing:
for cs in _flight_collision_shapes:
    cs.disabled = true
```

### PATTERN: Cleanup on NOTIFICATION_PREDELETE

```gdscript
# Nodes that own non-tree children must free them manually:
func _notification(what: int):
    if what == NOTIFICATION_PREDELETE:
        if not static_bodies_are_in_tree:
            for sb in static_bodies:
                sb.free()   # Won't be freed automatically if not in tree
```

---

*Generated: 2026-03-12 | Engine: F:\Dev\GodotEden | Project: F:\Dev\Projects\eden_project*
