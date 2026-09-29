class_name EdenPlayScreen
extends PanelContainer
## The main menu's Play screen: SpacetimeDB servers on the left (this computer + added ones), the worlds the
## selected server lists on the right. Join a world, host a new one on this computer (name, seed, open to the
## LAN), delete one of yours, add or remove a server, or play the offline sandbox (not saved).
## Worlds are saved by SpacetimeDB as they are played: rejoining one puts you back where you left, with your
## inventory, the buildings and the dug terrain.

signal back
## A world was picked: EdenSession is set, the menu loads the game
signal play

var worlds: EdenWorlds
var _servers: ItemList
var _worlds: ItemList
var _status: Label
var _join: Button
var _delete: Button
var _host_form: Control
var _server_form: Control
var _host_name: LineEdit
var _host_seed: LineEdit
var _host_lan: CheckButton
var _server_name: LineEdit
var _server_address: LineEdit
var _server_list: Array[Dictionary] = []
var _world_list: Array[Dictionary] = []
var _busy := false


func _init() -> void:
	custom_minimum_size = Vector2(1100, 680)


func _ready() -> void:
	worlds = EdenWorlds.new()
	add_child(worlds)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 16)
	add_child(box)
	var title := EdenUITheme.title("PLAY", 32)
	title.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	box.add_child(title)

	var cols := HBoxContainer.new()
	cols.size_flags_vertical = Control.SIZE_EXPAND_FILL
	cols.add_theme_constant_override("separation", 20)
	box.add_child(cols)

	var left := VBoxContainer.new()
	left.custom_minimum_size.x = 340
	left.add_theme_constant_override("separation", 10)
	cols.add_child(left)
	left.add_child(_label("SERVERS", EdenUITheme.GOLD))
	_servers = ItemList.new()
	_servers.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_servers.item_selected.connect(func(_i): refresh_worlds())
	left.add_child(_servers)
	var srow := HBoxContainer.new()
	srow.add_theme_constant_override("separation", 10)
	left.add_child(srow)
	_small(srow, "ADD", func(): _show_form(_server_form))
	_small(srow, "REMOVE", _remove_server)
	_server_form = _make_server_form()
	left.add_child(_server_form)

	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 10)
	cols.add_child(right)
	right.add_child(_label("WORLDS", EdenUITheme.GOLD))
	_worlds = ItemList.new()
	_worlds.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_worlds.item_selected.connect(func(_i): _update_buttons())
	_worlds.item_activated.connect(func(_i): _join_selected())
	right.add_child(_worlds)
	var wrow := HBoxContainer.new()
	wrow.add_theme_constant_override("separation", 10)
	right.add_child(wrow)
	_join = _small(wrow, "JOIN", _join_selected)
	_small(wrow, "HOST NEW", func(): _show_form(_host_form))
	_delete = _small(wrow, "DELETE", _delete_selected)
	_small(wrow, "REFRESH", refresh_worlds)
	_host_form = _make_host_form()
	right.add_child(_host_form)

	_status = _label("", EdenUITheme.CREAM)
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(_status)
	var bottom := HBoxContainer.new()
	bottom.alignment = BoxContainer.ALIGNMENT_CENTER
	bottom.add_theme_constant_override("separation", 24)
	box.add_child(bottom)
	var offline := Button.new()
	offline.text = "OFFLINE SANDBOX"
	offline.tooltip_text = "Play without a server. Nothing is saved."
	offline.pressed.connect(func():
		EdenSession.play_offline()
		play.emit())
	bottom.add_child(offline)
	var back_b := Button.new()
	back_b.text = "BACK"
	back_b.custom_minimum_size.x = 240
	back_b.pressed.connect(func(): back.emit())
	bottom.add_child(back_b)


## Fills the server list and the selected server's worlds
func refresh() -> void:
	var keep := _servers.get_selected_items()
	_server_list = EdenWorlds.servers()
	_servers.clear()
	for s in _server_list:
		_servers.add_item(s.name)
		_servers.set_item_tooltip(_servers.item_count - 1, s.url)
	_servers.select(keep[0] if not keep.is_empty() and keep[0] < _server_list.size() else 0)
	await refresh_worlds()


func refresh_worlds() -> void:
	var s := _selected_server()
	_worlds.clear()
	_world_list.clear()
	_update_buttons()
	if s.is_empty():
		return
	_set_status("Looking for worlds on %s..." % s.name)
	var up := await worlds.ping(s.url)
	if not up:
		if s.local:
			_set_status("No worlds hosted on this computer yet. HOST NEW starts a local SpacetimeDB server." if EdenWorlds.cli() != "" \
					else "SpacetimeDB isn't installed, so this computer can't host (see spacetimedb.com/install). You can still join other servers or play offline.")
		else:
			_set_status("%s doesn't answer." % s.url)
		return
	_world_list = await worlds.list_worlds(s.url)
	for w in _world_list:
		_worlds.add_item("%s    %d online    host %s" % [w.name, w.online, w.host])
		_worlds.set_item_tooltip(_worlds.item_count - 1, "%s\n%d players have joined\nseed %d" % [w.database, w.players, w.seed])
	_set_status("%d world%s on %s." % [_world_list.size(), "" if _world_list.size() == 1 else "s", s.name] if not _world_list.is_empty() \
			else "No worlds on %s yet." % s.name)
	_update_buttons()


func _selected_server() -> Dictionary:
	var sel := _servers.get_selected_items()
	return _server_list[sel[0]] if not sel.is_empty() and sel[0] < _server_list.size() else {}


func _selected_world() -> Dictionary:
	var sel := _worlds.get_selected_items()
	return _world_list[sel[0]] if not sel.is_empty() and sel[0] < _world_list.size() else {}


func _update_buttons() -> void:
	if _join == null:
		return
	_join.disabled = _selected_world().is_empty() or _busy
	_delete.disabled = _selected_world().is_empty() or not _selected_server().get("local", false) or _busy


func _join_selected() -> void:
	var s := _selected_server()
	var w := _selected_world()
	if w.is_empty() or _busy:
		return
	_set_busy(true, "Joining %s..." % w.name)
	var meta := await worlds.world_meta(s.url, w.database)
	_set_busy(false)
	if meta.is_empty():
		_set_status("%s can't be reached." % w.name)
		return
	await worlds.ensure_token(s.url)
	EdenSession.play_online(s.url, w.database, meta.name, meta.seed)
	play.emit()


func _host() -> void:
	var world_name := _host_name.text.strip_edges()
	if world_name == "":
		_set_status("Give the world a name.")
		return
	var seed_text := _host_seed.text.strip_edges()
	var world_seed := int(seed_text) if seed_text.is_valid_int() else seed_text.hash()
	_set_busy(true, "Creating %s (starting the server if needed)..." % world_name)
	var r := await worlds.host_world(world_name, world_seed, EdenOptions.player_name, _host_lan.button_pressed)
	_set_busy(false)
	if r.has("error"):
		_set_status(r.error)
		return
	_host_form.visible = false
	_servers.select(0)
	await refresh_worlds()
	for i in _world_list.size():
		if _world_list[i].database == r.database:
			_worlds.select(i)
	_update_buttons()
	_set_status("%s is ready. JOIN to enter it." % world_name)


func _delete_selected() -> void:
	var w := _selected_world()
	if w.is_empty() or not _selected_server().get("local", false):
		return
	_set_busy(true, "Deleting %s..." % w.name)
	var ok := await worlds.delete_world(w.database)
	_set_busy(false)
	_set_status("%s deleted." % w.name if ok else "Couldn't delete %s." % w.name)
	await refresh_worlds()


func _add_server() -> void:
	if _server_address.text.strip_edges() == "":
		_set_status("Type the server's address (host or host:port).")
		return
	var url := EdenWorlds.add_server(_server_name.text, _server_address.text)
	_server_form.visible = false
	_server_name.text = ""
	_server_address.text = ""
	await refresh()
	for i in _server_list.size():
		if _server_list[i].url == url:
			_servers.select(i)
	await refresh_worlds()


func _remove_server() -> void:
	var s := _selected_server()
	if s.is_empty() or s.local:
		_set_status("This computer can't be removed.")
		return
	EdenWorlds.remove_server(s.url)
	await refresh()


# ------------------------------------------------------------------------------------------------------------

func _make_host_form() -> Control:
	var form := GridContainer.new()
	form.columns = 2
	form.add_theme_constant_override("h_separation", 16)
	form.add_theme_constant_override("v_separation", 8)
	form.visible = false
	form.add_child(_label("World name", EdenUITheme.CREAM))
	_host_name = LineEdit.new()
	_host_name.text = "New Eden"
	_host_name.max_length = 40
	_host_name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	form.add_child(_host_name)
	form.add_child(_label("Seed", EdenUITheme.CREAM))
	_host_seed = LineEdit.new()
	_host_seed.placeholder_text = "number or word"
	form.add_child(_host_seed)
	form.add_child(_label("Open to LAN", EdenUITheme.CREAM))
	_host_lan = CheckButton.new()
	_host_lan.text = "Others on the network can join"
	form.add_child(_host_lan)
	form.add_child(Control.new())
	var create := Button.new()
	create.text = "CREATE WORLD"
	create.pressed.connect(_host)
	form.add_child(create)
	return form


func _make_server_form() -> Control:
	var form := VBoxContainer.new()
	form.visible = false
	form.add_theme_constant_override("separation", 8)
	_server_name = LineEdit.new()
	_server_name.placeholder_text = "Name"
	form.add_child(_server_name)
	_server_address = LineEdit.new()
	_server_address.placeholder_text = "Address (host:3180)"
	_server_address.text_submitted.connect(func(_t): _add_server())
	form.add_child(_server_address)
	var add := Button.new()
	add.text = "ADD SERVER"
	add.pressed.connect(_add_server)
	form.add_child(add)
	return form


func _show_form(form: Control) -> void:
	var show := not form.visible
	_host_form.visible = false
	_server_form.visible = false
	form.visible = show
	if show and form == _host_form:
		_host_seed.text = str(randi() % 1000000)
		_host_name.grab_focus()
	elif show:
		_server_address.grab_focus()


func _small(parent: Control, text: String, action: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.add_theme_font_size_override("font_size", 12)
	b.pressed.connect(action)
	parent.add_child(b)
	return b


func _label(text: String, color: Color) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_color_override("font_color", color)
	return l


func _set_status(text: String) -> void:
	_status.text = text


func _set_busy(on: bool, text := "") -> void:
	_busy = on
	if text != "":
		_set_status(text)
	_update_buttons()
