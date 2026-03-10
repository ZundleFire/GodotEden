import pathlib

tres_lines = [
    '[gd_resource type="Resource" script_class="PlanetGenerator" format=3]',
    '',
    '[ext_resource type="Script" path="res://eden/planet/planet_generator.gd" id="1_gen"]',
    '',
    '[resource]',
    'script = ExtResource("1_gen")',
    'planet_radius = 40000.0',
    'height_amplitude = 800.0',
    'sea_level_bias = 0.0',
    '',
]

out = pathlib.Path('F:/Dev/Projects/new-game-project/eden/planet/planet_generator.tres')
out.write_text('\n'.join(tres_lines), encoding='utf-8')
print(f'Written {out} ({out.stat().st_size} bytes)')
print(out.read_text(encoding='utf-8'))
