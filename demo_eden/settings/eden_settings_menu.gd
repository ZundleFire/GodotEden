class_name EdenSettingsMenu
extends CanvasLayer
## In-game settings (Esc): graphics quality level and its individual settings, display options. Changes apply as
## they are made (sliders when released) through EdenGraphics and are remembered. Touching a single setting switches
## the level to Custom, starting from the level it was on.

signal closed

const ACCENT := Color(0.33, 0.78, 0.67)

var graphics: EdenGraphics
var _quality: OptionButton
var _rows := {} # preset property -> control
var _vsync: CheckButton
var _fps_cap: OptionButton
var _show_fps: CheckButton
var _refreshing := false

const FPS_CAPS := [0, 30, 60, 120, 144]
## [preset property, label, kind, min, max, step, unit]
const SETTINGS := [
	["render_scale", "Render resolution", "slider", 0.5, 1.0, 0.01, "%"],
	["antialiasing", "Anti-aliasing", "option", ["Off", "FXAA", "MSAA 2x", "MSAA 4x"]],
	["grass_density", "Grass density", "slider", 0.0, 2.0, 0.05, "%"],
	["grass_distance", "Grass distance", "slider", 15.0, 150.0, 5.0, " m"],
	["grass_blades", "Grass detail (blades)", "slider", 2, 12, 1, ""],
	["foliage_density", "Tree & rock density", "slider", 0.1, 2.0, 0.05, "%"],
	["foliage_detail_distance", "Tree detail distance", "slider", 0.0, 400.0, 10.0, " m"],
	["foliage_far_detail", "Distant tree detail", "slider", 0.3, 2.0, 0.05, "%"],
	["foliage_far_visibility", "Tree view distance", "slider", 50.0, 1500.0, 25.0, ""],
	["ssao", "Ambient occlusion", "check"],
	["glow", "Glow", "check"],
	["light_ray_samples", "Light shafts (samples)", "slider", 0, 256, 16, ""],
	["ocean_reflection_steps", "Ocean reflections (steps)", "slider", 0, 64, 4, ""],
]


func _init() -> void:
	layer = 60
	visible = false


func setup(p_graphics: EdenGraphics) -> void:
	graphics = p_graphics
	_build()
	_refresh()


func open() -> void:
	_refresh()
	visible = true
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func close() -> void:
	visible = false
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	closed.emit()


func _unhandled_input(event: InputEvent) -> void:
	if visible and event is InputEventKey and event.pressed and event.physical_keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()


# ------------------------------------------------------------------------------------------------------------

func _style(bg: Color, radius := 8, border := Color.TRANSPARENT) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = bg
	s.set_corner_radius_all(radius)
	s.set_content_margin_all(14)
	if border.a > 0.0:
		s.border_color = border
		s.set_border_width_all(1)
	return s


func _build() -> void:
	var dim := ColorRect.new()
	dim.color = Color(0.02, 0.04, 0.05, 0.55)
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(dim)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(center)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(560, 0)
	panel.add_theme_stylebox_override("panel", _style(Color(0.07, 0.1, 0.12, 0.96), 10, Color(1, 1, 1, 0.08)))
	center.add_child(panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	panel.add_child(box)

	var title := Label.new()
	title.text = "Settings"
	title.add_theme_font_size_override("font_size", 26)
	box.add_child(title)

	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 18)
	grid.add_theme_constant_override("v_separation", 8)
	box.add_child(grid)

	_quality = OptionButton.new()
	for n in EdenGraphics.QUALITY_NAMES:
		_quality.add_item(n)
	_quality.item_selected.connect(func(i):
		if not _refreshing:
			graphics.quality = i
			graphics.apply()
			_refresh())
	_row(grid, "Graphics quality", _quality, true)

	for s in SETTINGS:
		var key: String = s[0]
		var control: Control
		match s[2]:
			"slider":
				control = _slider(key, s[3], s[4], s[5], s[6])
			"option":
				var ob := OptionButton.new()
				for item in s[3]:
					ob.add_item(item)
				ob.item_selected.connect(func(i): _set_value(key, i))
				control = ob
			"check":
				var cb := CheckButton.new()
				cb.text = "On"
				cb.toggled.connect(func(on): _set_value(key, on))
				control = cb
		_rows[key] = control
		_row(grid, s[1], control)

	var sep := HSeparator.new()
	box.add_child(sep)
	var display := GridContainer.new()
	display.columns = 2
	display.add_theme_constant_override("h_separation", 18)
	display.add_theme_constant_override("v_separation", 8)
	box.add_child(display)
	_vsync = CheckButton.new()
	_vsync.text = "On"
	_vsync.toggled.connect(func(on):
		if not _refreshing:
			graphics.vsync = on)
	_row(display, "VSync", _vsync)
	_fps_cap = OptionButton.new()
	for c in FPS_CAPS:
		_fps_cap.add_item("Unlimited" if c == 0 else "%d fps" % c)
	_fps_cap.item_selected.connect(func(i):
		if not _refreshing:
			graphics.max_fps = FPS_CAPS[i])
	_row(display, "Frame rate cap", _fps_cap)
	_show_fps = CheckButton.new()
	_show_fps.text = "On"
	_show_fps.toggled.connect(func(on):
		if not _refreshing:
			graphics.show_fps = on)
	_row(display, "Show FPS", _show_fps)

	var buttons := HBoxContainer.new()
	buttons.alignment = BoxContainer.ALIGNMENT_END
	buttons.add_theme_constant_override("separation", 10)
	box.add_child(buttons)
	var quit := Button.new()
	quit.text = "Quit game"
	quit.pressed.connect(func(): get_tree().quit())
	buttons.add_child(quit)
	var resume := Button.new()
	resume.text = "Resume"
	resume.add_theme_stylebox_override("normal", _style(ACCENT.darkened(0.35), 6))
	resume.add_theme_stylebox_override("hover", _style(ACCENT.darkened(0.2), 6))
	resume.pressed.connect(close)
	buttons.add_child(resume)


func _row(grid: GridContainer, text: String, control: Control, strong := false) -> void:
	var label := Label.new()
	label.text = text
	if strong:
		label.add_theme_color_override("font_color", ACCENT)
	grid.add_child(label)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	control.custom_minimum_size.x = 260
	grid.add_child(control)


func _slider(key: String, lo: float, hi: float, step: float, unit: String) -> Control:
	var row := HBoxContainer.new()
	var slider := HSlider.new()
	slider.min_value = lo
	slider.max_value = hi
	slider.step = step
	slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	var value := Label.new()
	value.custom_minimum_size.x = 64
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	var show := func(v: float): value.text = ("%d%%" % roundi(v * 100.0)) if unit == "%" else ("%d%s" % [roundi(v), unit])
	slider.value_changed.connect(show)
	# Applied when let go: foliage settings rebuild the foliage, which shouldn't happen on every step of a drag
	slider.drag_ended.connect(func(changed): if changed: _set_value(key, slider.value))
	row.add_child(slider)
	row.add_child(value)
	row.set_meta("slider", slider)
	row.set_meta("show", show)
	return row


func _set_value(key: String, value) -> void:
	if _refreshing:
		return
	var p := graphics.edit_custom()
	p.set(key, int(value) if typeof(p.get(key)) == TYPE_INT else value)
	graphics.apply()
	_refresh()


# The controls from the current level
func _refresh() -> void:
	if graphics == null or _quality == null:
		return
	_refreshing = true
	var p := graphics.current()
	_quality.select(graphics.quality)
	for key in _rows:
		var c: Control = _rows[key]
		var v = p.get(key)
		if c.has_meta("slider"):
			(c.get_meta("slider") as HSlider).set_value_no_signal(v)
			c.get_meta("show").call(float(v))
		elif c is OptionButton:
			c.select(int(v))
		elif c is CheckButton:
			c.set_pressed_no_signal(v)
	_vsync.set_pressed_no_signal(graphics.vsync)
	_fps_cap.select(maxi(FPS_CAPS.find(graphics.max_fps), 0))
	_show_fps.set_pressed_no_signal(graphics.show_fps)
	_refreshing = false
