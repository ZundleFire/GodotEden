@tool
extends Node3D
class_name CloudShell
## Carrier geometry for shaders/planet_clouds.gdshader: one sphere sitting in the middle of the
## cloud slab, around the whole planet.
##
## Intentionally NOT a quadtree like ocean_shell.gd. That layer needs real geometry because it
## displaces vertices to make waves; this one does all its work per-fragment from an analytic
## ray/slab intersection, so the mesh is nothing but a surface to run the fragment shader on. Its
## tessellation therefore affects only the silhouette seen from outside, never the cloud shapes.
##
## Register the material with PlanetAtmosphere.linked_materials so it receives the same
## scattering uniforms as the sky -- that is what makes clouds redden with the sunset.

const CLOUD_SHADER_PATH := "res://shaders/planet_clouds.gdshader"

@export var planet_radius: float = 40000.0:
	set(v):
		planet_radius = v
		_rebuild()
@export var planet_center: Vector3 = Vector3.ZERO:
	set(v):
		planet_center = v
		_rebuild()
## Slab bounds above sea level. Kept in sync with the shader's own uniforms.
@export var cloud_bottom: float = 1200.0:
	set(v):
		cloud_bottom = v
		_rebuild()
@export var cloud_top: float = 1900.0:
	set(v):
		cloud_top = v
		_rebuild()
## Only affects the silhouette seen from space; the cloud shapes are fragment-side.
@export var mesh_segments: int = 96:
	set(v):
		mesh_segments = maxi(v, 8)
		_rebuild()

@export_group("Baked noise")
## Edge length of the cubic noise texture. 64 is 262k texels (~256KB as L8) and is plenty at
## these feature sizes; 128 costs 8x the memory and generation time for little visible gain.
@export var noise_resolution: int = 64:
	set(v):
		noise_resolution = clampi(v, 16, 256)
		_rebuild_noise()
@export var noise_frequency: float = 0.035:
	set(v):
		noise_frequency = v
		_rebuild_noise()
@export var noise_octaves: int = 4:
	set(v):
		noise_octaves = clampi(v, 1, 8)
		_rebuild_noise()
## Domain warp amplitude, baked in at generation time. This is what gives the clouds their torn,
## angular silhouettes; it used to be a runtime cost and is now free.
@export var warp_amplitude: float = 28.0:
	set(v):
		warp_amplitude = v
		_rebuild_noise()
@export var noise_seed: int = 1337:
	set(v):
		noise_seed = v
		_rebuild_noise()

var _mesh_instance: MeshInstance3D
var _material: ShaderMaterial
var _noise_texture: NoiseTexture3D


func _ready() -> void:
	_rebuild()


## The ShaderMaterial, for handing to PlanetAtmosphere.linked_materials.
func get_material() -> ShaderMaterial:
	if _material == null:
		_build_material()
	return _material


func _build_material() -> void:
	var shader := load(CLOUD_SHADER_PATH) as Shader
	if shader == null:
		push_error("CloudShell: could not load %s" % CLOUD_SHADER_PATH)
		return
	_material = ShaderMaterial.new()
	_material.shader = shader
	_rebuild_noise()


## Bake the cloud field into a seamless 3D texture.
##
## This replaces what used to be ~144 procedural hash-noise evaluations per pixel. Two things
## matter here and both are easy to get wrong:
##
##  - `seamless` is required. The shader samples with repeat wrapping, so a non-seamless texture
##    would show hard discontinuities wherever the coordinate wraps.
##  - the domain warp is applied HERE, at bake time, instead of in the shader. It is the single
##    most expensive part of a procedural cloud field and it is completely static, so paying for
##    it once at startup rather than per-pixel per-frame is free quality.
##
## Generation runs on a background thread inside NoiseTexture3D; the texture simply pops in when
## ready, so the first frame or two may show no clouds.
func _rebuild_noise() -> void:
	if _material == null:
		return

	var fnl := FastNoiseLite.new()
	fnl.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	fnl.seed = noise_seed
	fnl.frequency = noise_frequency
	fnl.fractal_type = FastNoiseLite.FRACTAL_FBM
	fnl.fractal_octaves = noise_octaves
	fnl.fractal_gain = 0.5
	fnl.fractal_lacunarity = 2.0
	if warp_amplitude > 0.0:
		fnl.domain_warp_enabled = true
		fnl.domain_warp_type = FastNoiseLite.DOMAIN_WARP_SIMPLEX
		fnl.domain_warp_amplitude = warp_amplitude
		fnl.domain_warp_fractal_type = FastNoiseLite.DOMAIN_WARP_FRACTAL_INDEPENDENT
		fnl.domain_warp_fractal_octaves = 2

	_noise_texture = NoiseTexture3D.new()
	_noise_texture.width = noise_resolution
	_noise_texture.height = noise_resolution
	_noise_texture.depth = noise_resolution
	_noise_texture.seamless = true
	# Without normalising, fbm output clusters around the midpoint and the coverage threshold
	# becomes hypersensitive -- a 0.01 change in cloud_coverage would swing clear to overcast.
	_noise_texture.normalize = true
	_noise_texture.noise = fnl

	_material.set_shader_parameter("cloud_noise", _noise_texture)


func _rebuild() -> void:
	if not is_inside_tree():
		return
	if _material == null:
		_build_material()
	if _material == null:
		return

	var mid := planet_radius + (cloud_bottom + cloud_top) * 0.5

	if _mesh_instance == null:
		_mesh_instance = MeshInstance3D.new()
		_mesh_instance.name = "CloudShellMesh"
		# The shader raymarches from the camera, so Godot's own frustum/occlusion reasoning
		# about this sphere is unhelpful -- keep it always drawn.
		_mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		_mesh_instance.extra_cull_margin = 16384.0
		add_child(_mesh_instance)

	var sphere := SphereMesh.new()
	sphere.radius = mid
	sphere.height = mid * 2.0
	sphere.radial_segments = mesh_segments
	sphere.rings = maxi(mesh_segments / 2, 4)

	_mesh_instance.mesh = sphere
	_mesh_instance.material_override = _material
	_mesh_instance.position = planet_center

	_material.set_shader_parameter("planet_radius", planet_radius)
	_material.set_shader_parameter("planet_center", planet_center)
	_material.set_shader_parameter("cloud_bottom", cloud_bottom)
	_material.set_shader_parameter("cloud_top", cloud_top)
