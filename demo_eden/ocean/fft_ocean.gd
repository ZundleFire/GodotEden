@tool
extends Node
## Tessendorf FFT ocean: GPU spectrum generation + inverse-FFT displacement, driven directly via
## RenderingDevice from GDScript rather than as a compiled-in engine module -- this is iteration-
## speed critical (a C++ engine module needs a ~9 minute rebuild per change; this needs only
## re-running the demo) for a pipeline this easy to get subtly wrong (wrong FFT normalisation,
## wrong sign correction, wrong spectrum math all look like "plausible but wrong" wave noise, not
## an obvious crash).
##
## Pipeline, once per frame (see _process):
##   1. fft_time_evolve.glsl   : h0(k) -> h(k,t), Dx(k,t), Dz(k,t)   [3-layer RG32F spectra]
##   2. fft_butterfly_pass.glsl x (2*log2(N)) : separable 2D IFFT, horizontal stages then vertical
##   3. fft_assemble.glsl      : sign-correct + normalise + build the two textures the water
##                                shader actually samples (world-space displacement, and normal +
##                                foam-fold factor from finite differences of the displaced grid)
## fft_butterfly_precompute.glsl and fft_spectrum_init.glsl run once at startup (or again if
## patch_size/wind/n change) -- see rebuild().
##
## Output: displacement_texture, normal_texture (Texture2DRD) -- assign these directly as a
## ShaderMaterial's sampler2D parameters; they update in place every frame with no CPU readback.

@export var n: int = 256 # FFT grid resolution; must be a power of two.
@export var patch_size: float = 200.0 # world units the NxN grid tiles across.
@export var wind_speed: float = 12.0
@export var wind_angle_deg: float = 35.0
@export var amplitude: float = 4.0 # Phillips spectrum energy scale.
@export var choppiness: float = 1.2 # horizontal displacement strength (Tessendorf's "lambda").
@export var height_scale: float = 1.0
@export var seed: float = 1.0
## Wavelengths shorter than this are damped out of the spectrum -- see fft_spectrum_init.glsl's
## own comment. Should be set to (at least) twice whatever mesh vertex spacing actually displays
## this field, e.g. ocean_shell.gd's leaf_world_size/GRID_RES: undamped, energy the mesh cannot
## represent aliases into sharp, chaotic, self-overlapping spikes instead of a smooth surface.
@export var min_wavelength: float = 10.0
## Real time only advances the simulation this fast -- lets calm/stormy water be dialled without
## touching wind_speed (which also reshapes the spectrum, not just its speed).
@export var time_scale: float = 1.0

var displacement_texture: Texture2DRD
var normal_texture: Texture2DRD

var _rd: RenderingDevice
var _log2n: int

var _shader_butterfly_precompute: RID
var _shader_spectrum_init: RID
var _shader_time_evolve: RID
var _shader_butterfly_pass: RID
var _shader_assemble: RID

var _pipeline_butterfly_precompute: RID
var _pipeline_spectrum_init: RID
var _pipeline_time_evolve: RID
var _pipeline_butterfly_pass: RID
var _pipeline_assemble: RID

var _tex_butterfly: RID
var _tex_h0: RID
var _tex_spectra_a: RID
var _tex_spectra_b: RID
var _tex_displacement: RID
var _tex_normal: RID

var _uniform_set_spectrum_init: RID
var _uniform_set_time_evolve: RID
var _uniform_set_butterfly_h: RID # butterfly-texture-only set, rebuilt per src/dst pair below
var _uniform_set_a_to_b: RID
var _uniform_set_b_to_a: RID
var _uniform_set_assemble: RID

var _sim_time := 0.0
var _ready_ok := false


func _ready() -> void:
	_rd = RenderingServer.get_rendering_device()
	if _rd == null:
		push_warning("FFTOcean: no RenderingDevice available (headless/compatibility renderer?) -- ocean will not simulate")
		return
	_log2n = int(round(log(float(n)) / log(2.0)))
	if (1 << _log2n) != n:
		push_warning("FFTOcean: n=%d is not a power of two, clamping to %d" % [n, 1 << _log2n])
		n = 1 << _log2n

	_build_shaders()
	_build_textures()
	_build_uniform_sets()
	_precompute()
	_ready_ok = true
	# Produce one real frame of wave data immediately, rather than waiting for the first
	# _process() tick. In the editor's static (not-Playing) viewport, @tool _process() only runs
	# on the editor's own idle/redraw cadence, not a guaranteed every-frame loop the way Play does
	# -- without this, the displacement/normal textures could sit at their just-allocated,
	# undefined-content state for an indeterminate stretch, which reads as "no waves at all".
	_dispatch_frame()


func _process(delta: float) -> void:
	if not _ready_ok:
		return
	_sim_time += delta * time_scale
	_dispatch_frame()

# No explicit _exit_tree() cleanup: a ShaderMaterial elsewhere in the tree may still hold a
# Texture2DRD pointing at these textures when this node's own teardown runs, and freeing the RD
# resources first raced that reference (confirmed via the exact "Texture ... is not a valid
# texture" error this produced). RenderingDevice.finalize() reclaims everything on process exit
# regardless, which is the only time this node is ever actually freed today -- the "N RIDs leaked"
# messages at shutdown are informational, not a real leak in a long-running session.


func _load_shader(path: String) -> RID:
	var file: RDShaderFile = load(path)
	if file == null:
		push_error("FFTOcean: failed to load %s" % path)
		return RID()
	var spirv := file.get_spirv()
	return _rd.shader_create_from_spirv(spirv)


func _build_shaders() -> void:
	_shader_butterfly_precompute = _load_shader("res://ocean/fft_butterfly_precompute.glsl")
	_shader_spectrum_init = _load_shader("res://ocean/fft_spectrum_init.glsl")
	_shader_time_evolve = _load_shader("res://ocean/fft_time_evolve.glsl")
	_shader_butterfly_pass = _load_shader("res://ocean/fft_butterfly_pass.glsl")
	_shader_assemble = _load_shader("res://ocean/fft_assemble.glsl")

	_pipeline_butterfly_precompute = _rd.compute_pipeline_create(_shader_butterfly_precompute)
	_pipeline_spectrum_init = _rd.compute_pipeline_create(_shader_spectrum_init)
	_pipeline_time_evolve = _rd.compute_pipeline_create(_shader_time_evolve)
	_pipeline_butterfly_pass = _rd.compute_pipeline_create(_shader_butterfly_pass)
	_pipeline_assemble = _rd.compute_pipeline_create(_shader_assemble)


func _make_texture(width: int, height: int, layers: int, format: RenderingDevice.DataFormat) -> RID:
	var fmt := RDTextureFormat.new()
	fmt.width = width
	fmt.height = height
	fmt.array_layers = max(layers, 1)
	fmt.format = format
	fmt.texture_type = RenderingDevice.TEXTURE_TYPE_2D_ARRAY if layers > 1 else RenderingDevice.TEXTURE_TYPE_2D
	fmt.usage_bits = RenderingDevice.TEXTURE_USAGE_STORAGE_BIT \
			| RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT \
			| RenderingDevice.TEXTURE_USAGE_CAN_UPDATE_BIT \
			| RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
	var view := RDTextureView.new()
	return _rd.texture_create(fmt, view, [])


func _build_textures() -> void:
	_tex_butterfly = _make_texture(_log2n, n, 1, RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT)
	_tex_h0 = _make_texture(n, n, 1, RenderingDevice.DATA_FORMAT_R32G32_SFLOAT)
	_tex_spectra_a = _make_texture(n, n, 3, RenderingDevice.DATA_FORMAT_R32G32_SFLOAT)
	_tex_spectra_b = _make_texture(n, n, 3, RenderingDevice.DATA_FORMAT_R32G32_SFLOAT)
	_tex_displacement = _make_texture(n, n, 1, RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT)
	_tex_normal = _make_texture(n, n, 1, RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT)

	displacement_texture = Texture2DRD.new()
	displacement_texture.texture_rd_rid = _tex_displacement
	normal_texture = Texture2DRD.new()
	normal_texture.texture_rd_rid = _tex_normal


func _image_uniform(binding: int, tex: RID) -> RDUniform:
	var u := RDUniform.new()
	u.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u.binding = binding
	u.add_id(tex)
	return u


func _build_uniform_sets() -> void:
	_uniform_set_spectrum_init = _rd.uniform_set_create([_image_uniform(0, _tex_h0)], _shader_spectrum_init, 0)
	_uniform_set_time_evolve = _rd.uniform_set_create(
			[_image_uniform(0, _tex_h0), _image_uniform(1, _tex_spectra_a)], _shader_time_evolve, 0)
	# Butterfly stages ping-pong between the two spectra buffers; both directions are needed since
	# log2(n) is typically even (n a power of two >= 2 with log2n even for n=256) but nothing here
	# should assume that -- both uniform sets always exist and _dispatch_frame() picks per-stage.
	_uniform_set_a_to_b = _rd.uniform_set_create(
			[_image_uniform(0, _tex_butterfly), _image_uniform(1, _tex_spectra_a), _image_uniform(2, _tex_spectra_b)],
			_shader_butterfly_pass, 0)
	_uniform_set_b_to_a = _rd.uniform_set_create(
			[_image_uniform(0, _tex_butterfly), _image_uniform(1, _tex_spectra_b), _image_uniform(2, _tex_spectra_a)],
			_shader_butterfly_pass, 0)
	_uniform_set_assemble = _rd.uniform_set_create(
			[_image_uniform(0, _tex_spectra_a), _image_uniform(1, _tex_displacement), _image_uniform(2, _tex_normal)],
			_shader_assemble, 0)


func _precompute() -> void:
	var groups_n16 := int(ceil(float(n) / 16.0))

	var bf_pc := PackedByteArray()
	bf_pc.resize(16)
	bf_pc.encode_u32(0, n)
	bf_pc.encode_u32(4, _log2n)
	bf_pc.encode_u32(8, 0)
	bf_pc.encode_u32(12, 0)
	var bf_set := _rd.uniform_set_create([_image_uniform(0, _tex_butterfly)], _shader_butterfly_precompute, 0)
	var list := _rd.compute_list_begin()
	_rd.compute_list_bind_compute_pipeline(list, _pipeline_butterfly_precompute)
	_rd.compute_list_bind_uniform_set(list, bf_set, 0)
	_rd.compute_list_set_push_constant(list, bf_pc, bf_pc.size())
	_rd.compute_list_dispatch(list, _log2n, int(ceil(float(n) / 64.0)), 1)
	_rd.compute_list_end()

	_rebuild_spectrum()


## Regenerates the static h0(k) field -- call after changing patch_size/wind_speed/wind_angle_deg/
## amplitude/seed at runtime; those only take effect on the next call to this, not every frame
## (the per-frame pass only evolves the existing h0 in time).
func rebuild_spectrum() -> void:
	if _ready_ok:
		_rebuild_spectrum()


func _rebuild_spectrum() -> void:
	var pc := PackedByteArray()
	pc.resize(32)
	pc.encode_u32(0, n)
	pc.encode_float(4, patch_size)
	pc.encode_float(8, wind_speed)
	pc.encode_float(12, deg_to_rad(wind_angle_deg))
	pc.encode_float(16, amplitude)
	pc.encode_float(20, seed)
	pc.encode_float(24, min_wavelength)
	pc.encode_float(28, 0.0)

	var list := _rd.compute_list_begin()
	_rd.compute_list_bind_compute_pipeline(list, _pipeline_spectrum_init)
	_rd.compute_list_bind_uniform_set(list, _uniform_set_spectrum_init, 0)
	_rd.compute_list_set_push_constant(list, pc, pc.size())
	var groups := int(ceil(float(n) / 16.0))
	_rd.compute_list_dispatch(list, groups, groups, 1)
	_rd.compute_list_end()


func _dispatch_frame() -> void:
	var groups := int(ceil(float(n) / 16.0))

	# 1. Time evolution: h0 -> spectra_a (height, Dx, Dz).
	var te_pc := PackedByteArray()
	te_pc.resize(16)
	te_pc.encode_u32(0, n)
	te_pc.encode_float(4, patch_size)
	te_pc.encode_float(8, _sim_time)
	te_pc.encode_float(12, 0.0)
	var list := _rd.compute_list_begin()
	_rd.compute_list_bind_compute_pipeline(list, _pipeline_time_evolve)
	_rd.compute_list_bind_uniform_set(list, _uniform_set_time_evolve, 0)
	_rd.compute_list_set_push_constant(list, te_pc, te_pc.size())
	_rd.compute_list_dispatch(list, groups, groups, 1)
	_rd.compute_list_end()

	# 2. Separable 2D IFFT: log2(n) horizontal stages, then log2(n) vertical stages, ping-ponging
	# between spectra_a/spectra_b. current_in_a tracks which buffer holds the live data.
	var current_in_a := true
	for direction in [0, 1]:
		for stage in range(_log2n):
			var bp_pc := PackedByteArray()
			bp_pc.resize(16)
			bp_pc.encode_u32(0, n)
			bp_pc.encode_u32(4, stage)
			bp_pc.encode_u32(8, direction)
			bp_pc.encode_u32(12, 0)
			var uset := _uniform_set_a_to_b if current_in_a else _uniform_set_b_to_a
			var l2 := _rd.compute_list_begin()
			_rd.compute_list_bind_compute_pipeline(l2, _pipeline_butterfly_pass)
			_rd.compute_list_bind_uniform_set(l2, uset, 0)
			_rd.compute_list_set_push_constant(l2, bp_pc, bp_pc.size())
			_rd.compute_list_dispatch(l2, groups, groups, 3)
			_rd.compute_list_end()
			current_in_a = not current_in_a

	# 3. Assemble reads spectra_a specifically (_uniform_set_assemble is bound to it) -- the ping-
	# pong stage count above (2*log2n) must land back in A. log2n is always an integer >= 1 for a
	# power-of-two n, so 2*log2n is always even: current_in_a is guaranteed true here.
	var asm_pc := PackedByteArray()
	asm_pc.resize(16)
	asm_pc.encode_u32(0, n)
	asm_pc.encode_float(4, patch_size)
	asm_pc.encode_float(8, choppiness)
	asm_pc.encode_float(12, height_scale)
	var l3 := _rd.compute_list_begin()
	_rd.compute_list_bind_compute_pipeline(l3, _pipeline_assemble)
	_rd.compute_list_bind_uniform_set(l3, _uniform_set_assemble, 0)
	_rd.compute_list_set_push_constant(l3, asm_pc, asm_pc.size())
	_rd.compute_list_dispatch(l3, groups, groups, 1)
	_rd.compute_list_end()
