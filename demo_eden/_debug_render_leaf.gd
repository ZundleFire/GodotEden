extends Node3D
## Scratch diagnostic: actually RENDERS a single leaf blob under a known top-down light from
## known camera angles, and saves the framebuffer to PNG -- ground truth pixels, not just
## checking data self-consistency. Mirrors main.gd's proven frame-counter screenshot pattern.

var _frame := 0
var _cam: Camera3D
var _phase := 0
var _canopy_center := Vector3.ZERO

func _ready() -> void:
	var world_env := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.1, 0.1, 0.1)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	world_env.environment = env
	add_child(world_env)

	# Light points straight down (-Y): anything with an UPWARD-facing normal should render
	# bright, anything DOWNWARD-facing should render dark. Unambiguous ground truth.
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-90, 0, 0)
	light.light_energy = 2.0
	light.shadow_enabled = false
	add_child(light)

	var rng := RandomNumberGenerator.new()
	rng.seed = 0
	var shape := EdenTreeBuilder.Shape.new()
	var result := EdenTreeBuilder.build(rng, shape)
	var mesh: ArrayMesh = result["foliage_mesh"]

	var mesh_inst := MeshInstance3D.new()
	mesh_inst.mesh = mesh
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(1, 1, 1)
	mat.roughness = 1.0
	mesh_inst.material_override = mat
	add_child(mesh_inst)

	# Real tree canopy sits up around the crown (trunk_height=3 default), not at the origin.
	var canopy_center := Vector3(0, 3.5, 0)
	_cam = Camera3D.new()
	add_child(_cam)
	_cam.position = canopy_center + Vector3(0, 8, 0.001)
	_cam.look_at(canopy_center, Vector3(0, 0, -1))
	_cam.current = true
	_canopy_center = canopy_center


func _process(_delta: float) -> void:
	_frame += 1
	if _frame == 30:
		var img := get_viewport().get_texture().get_image()
		img.save_png("res://_debug_leaf_top_down.png")
		print("EDEN_DEBUG: saved top-down, size=", img.get_size())
		# Flip to look from BELOW, straight up -- light still shines down (-Y), so the underside
		# should be uniformly dark if normals/winding are correct. Bright here would be the
		# concrete, ground-truth proof of an inverted-normal/backwards-winding bug.
		_cam.position = _canopy_center + Vector3(0, -8, 0.001)
		_cam.look_at(_canopy_center, Vector3(0, 0, -1))
	elif _frame == 60:
		var img2 := get_viewport().get_texture().get_image()
		img2.save_png("res://_debug_leaf_bottom_up.png")
		print("EDEN_DEBUG: saved bottom-up, size=", img2.get_size())
		get_tree().quit(0)
