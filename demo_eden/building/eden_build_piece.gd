class_name EdenBuildPiece
extends StaticBody3D
## One placed building piece (see EdenBuildPieces for the kinds). EdenBuilder places, connects and removes them and
## works out their structural support.

## Collision layer of pieces (layer 3): the player and tools include it in their masks; support queries use it alone
const LAYER := 4

var kind := ""
## Server row id when multiplayer (EdenNet), 0 offline
var net_id := 0
## 0..1: 1 standing on the ground, less the further it hangs from support
var support := 0.0
var grounded := false
var neighbors: Array[EdenBuildPiece] = []
var mesh_instance: MeshInstance3D


static func create(p_kind: String) -> EdenBuildPiece:
	var p := EdenBuildPiece.new()
	p.kind = p_kind
	p.collision_layer = LAYER
	p.collision_mask = 0
	p.mesh_instance = MeshInstance3D.new()
	p.mesh_instance.mesh = EdenBuildPieces.mesh(p_kind)
	p.add_child(p.mesh_instance)
	var s: Array = EdenBuildPieces.shape(p_kind)
	var cs := CollisionShape3D.new()
	cs.shape = s[0]
	cs.transform = s[1]
	p.add_child(cs)
	return p


func material() -> int:
	return EdenBuildPieces.material_of(kind)


## Shows the support colour over the piece (build mode) or not
func show_support(on: bool) -> void:
	var c := EdenBuildPieces.support_color(support)
	mesh_instance.set_instance_shader_parameter("tint", Color(c.r, c.g, c.b, 0.45 if on else 0.0))
