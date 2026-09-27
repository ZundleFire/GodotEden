class_name EdenCalendarPanel
extends CanvasLayer
## The calendar (K): the year's twelve months with each hemisphere's seasons under them and today marked; the
## date, local time, season, daylight and moon where you stand; and controls to speed time up or skip to a season.

const SEASON_COLORS := [Color(0.45, 0.8, 0.4), Color(0.95, 0.8, 0.3), Color(0.9, 0.5, 0.2), Color(0.5, 0.7, 0.95)]
const SPEEDS := [["Pause", 0.0], ["1x", 1.0], ["30x", 30.0], ["600x", 600.0]]

var calendar: EdenCalendar
var player: EdenPlayer
var _title: Label
var _info: Label
var _year: Control


func _init() -> void:
	layer = 55
	visible = false


func setup(p_calendar: EdenCalendar, p_player: EdenPlayer) -> void:
	calendar = p_calendar
	player = p_player
	_build()


func toggle() -> void:
	visible = not visible
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE if visible else Input.MOUSE_MODE_CAPTURED


func _process(_d: float) -> void:
	if not visible or calendar == null:
		return
	var up := player.up_direction
	var lat := calendar.latitude(up)
	var here_north := lat >= 0.0
	var st := calendar.sun_times(lat)
	var daylight := "polar day: the sun never sets" if st[2] >= 24.0 else ("polar night: the sun never rises" if st[2] <= 0.0 else
			"sunrise %s, sunset %s  (%dh %02dm of daylight)" % [EdenCalendar.clock(st[0]), EdenCalendar.clock(st[1]), int(st[2]), int(fmod(st[2], 1.0) * 60.0)])
	var moon := fposmod(calendar.days / calendar.lunar_cycle_days, 1.0)
	var moon_name: String = ["New moon", "Waxing crescent", "First quarter", "Waxing gibbous", "Full moon", "Waning gibbous",
			"Last quarter", "Waning crescent"][int(moon * 8.0 + 0.5) % 8]
	_title.text = "%s    %s local time" % [calendar.date_string(), EdenCalendar.clock(calendar.local_hour(up))]
	_info.text = "\n".join([
		"Here: %.0f° %s  -  %s" % [absf(lat), "N" if here_north else "S", calendar.season_at(up)],
		"Northern hemisphere: %s    Southern hemisphere: %s" % [calendar.season_name(true), calendar.season_name(false)],
		"Today %s" % daylight,
		"Sun %.1f° %s of the equator    %s" % [absf(calendar.declination()), "north" if calendar.declination() >= 0.0 else "south", moon_name],
		"A year is %d days (12 months of %d); a day lasts %.0f real minutes at 1x%s" % [calendar.days_per_year(), calendar.days_per_month,
				calendar.day_length_minutes, "   (shared with everyone online)" if calendar.is_synced() else ""],
	])
	_year.queue_redraw()


func _build() -> void:
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	center.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(center)
	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.07, 0.1, 0.12, 0.95)
	style.set_corner_radius_all(10)
	style.set_content_margin_all(16)
	panel.add_theme_stylebox_override("panel", style)
	center.add_child(panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	panel.add_child(box)
	_title = Label.new()
	_title.add_theme_font_size_override("font_size", 24)
	box.add_child(_title)
	_year = Control.new()
	_year.custom_minimum_size = Vector2(720, 96)
	_year.draw.connect(_draw_year)
	box.add_child(_year)
	_info = Label.new()
	box.add_child(_info)

	var speed := HBoxContainer.new()
	speed.add_theme_constant_override("separation", 6)
	var sl := Label.new()
	sl.text = "Time speed"
	sl.custom_minimum_size.x = 110
	speed.add_child(sl)
	for s in SPEEDS:
		var b := Button.new()
		b.text = s[0]
		b.pressed.connect(func():
			calendar.running = s[1] > 0.0
			calendar.time_scale = maxf(s[1], 0.0001)
			calendar.publish())
		speed.add_child(b)
	box.add_child(speed)
	var skip := HBoxContainer.new()
	skip.add_theme_constant_override("separation", 6)
	var kl := Label.new()
	kl.text = "Skip to (here)"
	kl.custom_minimum_size.x = 110
	skip.add_child(kl)
	for i in 4:
		var b := Button.new()
		b.text = EdenCalendar.SEASONS[i]
		b.add_theme_color_override("font_color", SEASON_COLORS[i])
		b.pressed.connect(func():
			calendar.skip_to_season(i, calendar.latitude(player.up_direction) >= 0.0)
			calendar.publish())
		skip.add_child(b)
	var close := Button.new()
	close.text = "Close (K)"
	close.pressed.connect(toggle)
	skip.add_child(close)
	box.add_child(skip)


# Twelve month cells; under them the northern and southern seasons as coloured bands; a line at today
func _draw_year() -> void:
	var w := _year.size.x
	var cell := w / 12.0
	var font := _year.get_theme_default_font()
	for i in 12:
		var r := Rect2(i * cell + 1, 0, cell - 2, 40)
		var current := i == calendar.month - 1
		_year.draw_rect(r, Color(1, 1, 1, 0.16 if current else 0.06))
		_year.draw_string(font, Vector2(r.position.x + 6, 25), EdenCalendar.MONTHS[i], HORIZONTAL_ALIGNMENT_LEFT, cell - 8, 14,
				Color(1, 1, 1, 1.0 if current else 0.7))
	# Seasons by year phase: month 1 starts the northern spring (the south is half a year on)
	for row in 2:
		var y := 48.0 + row * 22.0
		for s in 4:
			var x0 := w * s / 4.0
			var season := (s + (0 if row == 0 else 2)) % 4
			_year.draw_rect(Rect2(x0 + 1, y, w / 4.0 - 2, 16), SEASON_COLORS[season].darkened(0.25))
			_year.draw_string(font, Vector2(x0 + 6, y + 13), "%s  %s" % ["North" if row == 0 else "South", EdenCalendar.SEASONS[season]],
					HORIZONTAL_ALIGNMENT_LEFT, w / 4.0 - 8, 12, Color(0.05, 0.05, 0.05))
	var x := w * calendar.year_phase()
	_year.draw_line(Vector2(x, 0), Vector2(x, 88), Color(1, 1, 1), 2.0)
