class_name EdenNet
extends Node
## Multiplayer through SpacetimeDB (the `eden` module in multiplayer/server). Speaks SpacetimeDB's JSON WebSocket
## protocol (v1.json.spacetimedb) straight from GDScript, no SDK:
##   - subscribes to the `player` and `voxel_edit` tables; other players appear as EdenRemotePlayer avatars that
##     follow their updates (smoothed), voxel edits by anyone are applied to the local terrain (all of them on
##     joining, so the world keeps its changes);
##   - sends this player's position/state ~10 times a second (update_player) and every dig/place (add_voxel_edit).
## The identity token is kept in user://eden_net_token so a restart is the same player.
## Made by EdenPlayer when Scene Setup > online is on (or the game runs with `-- --online`).

signal connected(identity: String)
signal players_changed

const PROTOCOL := "v1.json.spacetimedb"
const SEND_INTERVAL := 0.1
const TOKEN_PATH := "user://eden_net_token"

@export var server_url := "ws://127.0.0.1:3180"
@export var database := "eden"
@export var player_name := "Explorer"

## Connection state for the HUD and tests: "offline", "connecting", "online", "error"
var status := "offline"
var identity := ""
## identity -> row Dictionary (all players the server knows, online or not)
var players := {}
## identity -> EdenRemotePlayer
var avatars := {}
var edits_applied := 0
## World clock rows received (the calendar follows the server's)
var clock_updates := 0
## Where the last edit from someone else was applied (world space), for tests
var last_edit_position := Vector3.ZERO

var _player: EdenPlayer
var _ws := WebSocketPeer.new()
var _send_t := 0.0
var _request := 0
var _last_sent := {}
var _token := ""
var _idle := 0.0


func setup(player: EdenPlayer) -> void:
	_player = player
	if FileAccess.file_exists(TOKEN_PATH):
		_token = FileAccess.get_file_as_string(TOKEN_PATH).strip_edges()
	_ws.supported_protocols = PackedStringArray([PROTOCOL])
	_ws.inbound_buffer_size = 1 << 22
	if _token != "":
		_ws.handshake_headers = PackedStringArray(["Authorization: Bearer " + _token])
	var url := "%s/v1/database/%s/subscribe" % [server_url, database]
	var err := _ws.connect_to_url(url)
	status = "connecting" if err == OK else "error"
	if player.miner:
		player.miner.edited.connect(_on_local_edit)
	if player.builder:
		player.builder.place_requested.connect(func(k: String, xf: Transform3D):
			var o := xf.origin - _player._center()
			var q := xf.basis.get_rotation_quaternion()
			_call("place_piece", [k, o.x, o.y, o.z, q.x, q.y, q.z, q.w]))
		player.builder.remove_requested.connect(func(id: int): _call("remove_piece", [id]))
	if player.calendar:
		player.calendar.clock_set.connect(func(d: float, rate: float): _call("set_clock", [d, rate]))


func _process(delta: float) -> void:
	_ws.poll()
	match _ws.get_ready_state():
		WebSocketPeer.STATE_OPEN:
			while _ws.get_available_packet_count() > 0:
				_on_message(_ws.get_packet().get_string_from_utf8())
			_send_t -= delta
			if _send_t <= 0.0 and status == "online":
				_send_t = SEND_INTERVAL
				_send_state()
		WebSocketPeer.STATE_CLOSED:
			if status != "error":
				status = "error"
				push_warning("EdenNet: connection closed (%d %s)" % [_ws.get_close_code(), _ws.get_close_reason()])


func online_count() -> int:
	var n := 0
	for id in players:
		if players[id].get("online", false):
			n += 1
	return n


# ------------------------------------------------------------------------------------------------------------
# Outgoing

func _call(reducer: String, args: Array) -> void:
	_request += 1
	_ws.send_text(JSON.stringify({"CallReducer": {
		"reducer": reducer, "args": JSON.stringify(args, "", true, true), "request_id": _request, "flags": 0}}))


func _send_state() -> void:
	if not _player.ready_to_move:
		return
	var p := _player.global_position - _player._center()
	var state := {"x": p.x, "y": p.y, "z": p.z, "yaw": _facing_angle(), "state": _player.animator.state,
			"speed": snappedf(_player.animator.ground_speed, 0.05)}
	# Standing still sends nothing new: only changes go out (plus a keep-alive every 2 s)
	_idle += SEND_INTERVAL
	if state == _last_sent and _idle < 2.0:
		return
	_idle = 0.0
	_last_sent = state
	_call("update_player", [p.x, p.y, p.z, state.yaw, state.state, state.speed])


# The body's facing as an angle from planet north (what the avatar is turned by on the other side)
func _facing_angle() -> float:
	var up := _player.up_direction
	return EdenPlayer._north(up).signed_angle_to(_player._facing, up)


func _on_local_edit(world_pos: Vector3, radius: float, mode: int, material: int) -> void:
	if status != "online":
		return
	var p := world_pos - _player._center()
	_call("add_voxel_edit", [p.x, p.y, p.z, radius, mode, material])


# ------------------------------------------------------------------------------------------------------------
# Incoming

func _on_message(text: String) -> void:
	var msg = JSON.parse_string(text)
	if not msg is Dictionary:
		return
	if msg.has("IdentityToken"):
		var t: Dictionary = msg.IdentityToken
		identity = _id(t.identity)
		if t.get("token", "") != "" and t.token != _token:
			var f := FileAccess.open(TOKEN_PATH, FileAccess.WRITE)
			f.store_string(t.token)
		_request += 1
		_ws.send_text(JSON.stringify({"Subscribe": {
			"query_strings": ["SELECT * FROM player", "SELECT * FROM voxel_edit", "SELECT * FROM build_piece", "SELECT * FROM world_clock"], "request_id": _request}}))
		_call("set_name", [player_name])
	elif msg.has("InitialSubscription"):
		_apply_update(msg.InitialSubscription.database_update)
		status = "online"
		if _player.builder:
			_player.builder.online = true
		connected.emit(identity)
	elif msg.has("TransactionUpdate"):
		var tu: Dictionary = msg.TransactionUpdate
		var st = tu.get("status", {})
		if st is Dictionary and st.has("Committed"):
			_apply_update(st.Committed)
		elif st is Dictionary and st.has("Failed"):
			push_warning("EdenNet: reducer failed: %s" % str(st.Failed))
	elif msg.has("TransactionUpdateLight"):
		_apply_update(msg.TransactionUpdateLight.update)


func _apply_update(db_update: Dictionary) -> void:
	for table in db_update.get("tables", []):
		for qu in table.get("updates", []):
			# Query updates arrive plain or wrapped as {"Uncompressed": {...}}
			var u: Dictionary = qu.get("Uncompressed", qu) if qu is Dictionary else {}
			var deletes: Array = u.get("deletes", [])
			var inserts: Array = u.get("inserts", [])
			match table.table_name:
				"player":
					for r in deletes:
						_player_row(_row(r), false)
					for r in inserts:
						_player_row(_row(r), true)
					players_changed.emit()
				"voxel_edit":
					for r in inserts:
						_edit_row(_row(r))
				"world_clock":
					for r in inserts:
						_clock_row(_row(r))
				"build_piece":
					for r in deletes:
						var row := _piece_row(_row(r))
						if _player.builder:
							_player.builder.despawn_net(int(row.id))
					for r in inserts:
						var row := _piece_row(_row(r))
						if _player.builder:
							var q := Quaternion(row.qx, row.qy, row.qz, row.qw).normalized()
							_player.builder.spawn(str(row.kind), Transform3D(Basis(q), _player._center() + Vector3(row.x, row.y, row.z)), int(row.id))


# A row arrives as JSON text of the row (an object by field name, or an array in column order)
func _row(r) -> Dictionary:
	var v = JSON.parse_string(r) if r is String else r
	if v is Array:
		return {"_array": v}
	return v if v is Dictionary else {}


# Identities come as {"__identity__": "0x.."} (rows by field name), ["0x.."] (rows as arrays) or a plain string
static func _id(v) -> String:
	if v is Array and v.size() == 1:
		return str(v[0])
	if v is Dictionary:
		return str(v.values()[0]) if v.size() == 1 else JSON.stringify(v)
	return str(v)


func _player_row(r: Dictionary, inserted: bool) -> void:
	if r.has("_array"): # column order: identity name online x y z yaw state speed
		var a: Array = r._array
		r = {"identity": a[0], "name": a[1], "online": a[2], "x": a[3], "y": a[4], "z": a[5], "yaw": a[6], "state": a[7], "speed": a[8]}
	var id := _id(r.identity)
	if not inserted:
		# An update is a delete then an insert of the same key: keep the avatar, the insert refreshes it
		players.erase(id)
		return
	players[id] = r
	if id == identity:
		return
	var avatar: EdenRemotePlayer = avatars.get(id)
	if not r.online:
		if avatar:
			avatar.queue_free()
			avatars.erase(id)
		return
	if avatar == null:
		avatar = EdenRemotePlayer.create(_player._planet)
		avatars[id] = avatar
		get_tree().current_scene.add_child(avatar)
	avatar.set_target(_player._center() + Vector3(r.x, r.y, r.z), float(r.yaw), str(r.state), float(r.speed), str(r.name))


# The world's calendar clock (column order: id days set_at days_per_second); timestamps come as
# {"__timestamp_micros_since_unix_epoch__": n} or [n]
func _clock_row(r: Dictionary) -> void:
	if r.has("_array"):
		var a: Array = r._array
		r = {"days": a[1], "set_at": a[2], "days_per_second": a[3]}
	var ts = r.set_at
	var micros: float = float(ts.values()[0]) if ts is Dictionary else (float(ts[0]) if ts is Array else float(ts))
	if _player.calendar:
		_player.calendar.follow_server(float(r.days), micros / 1e6, float(r.days_per_second))
	clock_updates += 1


# Column order: id kind x y z qx qy qz qw author
static func _piece_row(r: Dictionary) -> Dictionary:
	if r.has("_array"):
		var a: Array = r._array
		return {"id": a[0], "kind": a[1], "x": a[2], "y": a[3], "z": a[4], "qx": a[5], "qy": a[6], "qz": a[7], "qw": a[8]}
	return r


func _edit_row(r: Dictionary) -> void:
	if r.has("_array"): # column order: id author x y z radius mode material
		var a: Array = r._array
		r = {"author": a[1], "x": a[2], "y": a[3], "z": a[4], "radius": a[5], "mode": a[6], "material": a[7]}
	if _id(r.author) == identity and status == "online":
		return # made here (already applied); on joining, our own earlier edits are replayed like anyone's
	if _player.miner:
		_player.miner.apply_edit(_player._center() + Vector3(r.x, r.y, r.z), float(r.radius), int(r.mode), int(r.material))
		edits_applied += 1
		last_edit_position = _player._center() + Vector3(r.x, r.y, r.z)
