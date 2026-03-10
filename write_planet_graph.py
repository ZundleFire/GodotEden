import pathlib

# Planet sphere graph:
# SdfSphere(X, Y, Z, radius=40000) gives a sphere SDF.
# We add 3 noise layers to displace the surface:
#   noise_macro  * 800   (continent scale)
#   noise_meso   * 200   (hills)
#   noise_micro  * 40    (surface detail)
# Final: SdfSphere + (macro + meso + micro) -> OutputSDF
#
# Connections list format: [from_node_id, from_port, to_node_id, to_port]
#
# Node layout:
#  1 = InputX
#  2 = InputY
#  3 = InputZ
#  4 = OutputSDF
#  5 = Constant (radius = 40000.0)
#  6 = SdfSphere  (ports: 0=x, 1=y, 2=z, 3=radius)
#  7 = Noise3D macro  (auto-connects X/Y/Z)
#  8 = Multiply macro * 800
#  9 = Constant 800.0
# 10 = Noise3D meso
# 11 = Multiply meso * 200
# 12 = Constant 200.0
# 13 = Noise3D micro
# 14 = Multiply micro * 40
# 15 = Constant 40.0
# 16 = Add (sphere + macro*800)
# 17 = Add (+ meso*200)
# 18 = Add (+ micro*40)  -> OutputSDF

lines = [
    '[gd_resource type="VoxelGeneratorGraph" format=3 uid="uid://planet_sphere_graph"]',
    '',
    '[sub_resource type="FastNoiseLite" id="FastNoiseLite_macro"]',
    'noise_type = 1',             # TYPE_SIMPLEX_SMOOTH
    'frequency = 0.00003',        # very large continent features on 40km sphere
    'fractal_type = 1',           # FBM
    'fractal_octaves = 5',
    'fractal_gain = 0.5',
    'seed = 111',
    '',
    '[sub_resource type="FastNoiseLite" id="FastNoiseLite_meso"]',
    'noise_type = 1',
    'frequency = 0.0003',
    'fractal_type = 1',
    'fractal_octaves = 5',
    'fractal_gain = 0.45',
    'seed = 222',
    '',
    '[sub_resource type="FastNoiseLite" id="FastNoiseLite_micro"]',
    'noise_type = 1',
    'frequency = 0.003',
    'fractal_type = 1',
    'fractal_octaves = 3',
    'fractal_gain = 0.4',
    'seed = 333',
    '',
    '[resource]',
    'graph_data = {',
    '"connections": [',
    # X/Y/Z -> SdfSphere ports 0,1,2
    '[1, 0, 6, 0],',
    '[2, 0, 6, 1],',
    '[3, 0, 6, 2],',
    # radius constant -> SdfSphere port 3
    '[5, 0, 6, 3],',
    # SdfSphere -> Add16 port 0
    '[6, 0, 16, 0],',
    # macro noise (auto-connects X/Y/Z) -> multiply port 0
    '[7, 0, 8, 0],',
    # macro amplitude constant -> multiply port 1
    '[9, 0, 8, 1],',
    # macro*800 -> Add16 port 1
    '[8, 0, 16, 1],',
    # sphere+macro -> Add17 port 0
    '[16, 0, 17, 0],',
    # meso noise -> multiply port 0
    '[10, 0, 11, 0],',
    # meso amplitude -> multiply port 1
    '[12, 0, 11, 1],',
    # meso*200 -> Add17 port 1
    '[11, 0, 17, 1],',
    # sphere+macro+meso -> Add18 port 0
    '[17, 0, 18, 0],',
    # micro noise -> multiply port 0
    '[13, 0, 14, 0],',
    # micro amplitude -> multiply port 1
    '[15, 0, 14, 1],',
    # micro*40 -> Add18 port 1
    '[14, 0, 18, 1],',
    # final -> OutputSDF
    '[18, 0, 4, 0]',
    '],',
    '"nodes": {',
    '"1": {"auto_connect": false, "gui_position": Vector2(40, 80), "type": "InputX"},',
    '"2": {"auto_connect": false, "gui_position": Vector2(40, 160), "type": "InputY"},',
    '"3": {"auto_connect": false, "gui_position": Vector2(40, 240), "type": "InputZ"},',
    '"4": {"auto_connect": true, "gui_position": Vector2(1400, 400), "type": "OutputSDF"},',
    '"5": {"auto_connect": false, "gui_position": Vector2(40, 340), "type": "Constant", "value": 40000.0},',
    '"6": {"auto_connect": false, "gui_position": Vector2(280, 200), "type": "SdfSphere"},',
    '"7": {"auto_connect": true, "gui_position": Vector2(280, 520), "noise": SubResource("FastNoiseLite_macro"), "type": "Noise3D", "x": 0.0, "y": 0.0, "z": 0.0},',
    '"8": {"auto_connect": false, "gui_position": Vector2(520, 520), "type": "Multiply"},',
    '"9": {"auto_connect": false, "gui_position": Vector2(280, 640), "type": "Constant", "value": 800.0},',
    '"10": {"auto_connect": true, "gui_position": Vector2(280, 760), "noise": SubResource("FastNoiseLite_meso"), "type": "Noise3D", "x": 0.0, "y": 0.0, "z": 0.0},',
    '"11": {"auto_connect": false, "gui_position": Vector2(520, 760), "type": "Multiply"},',
    '"12": {"auto_connect": false, "gui_position": Vector2(280, 880), "type": "Constant", "value": 200.0},',
    '"13": {"auto_connect": true, "gui_position": Vector2(280, 1000), "noise": SubResource("FastNoiseLite_micro"), "type": "Noise3D", "x": 0.0, "y": 0.0, "z": 0.0},',
    '"14": {"auto_connect": false, "gui_position": Vector2(520, 1000), "type": "Multiply"},',
    '"15": {"auto_connect": false, "gui_position": Vector2(280, 1120), "type": "Constant", "value": 40.0},',
    '"16": {"auto_connect": false, "gui_position": Vector2(760, 340), "type": "Add"},',
    '"17": {"auto_connect": false, "gui_position": Vector2(1000, 400), "type": "Add"},',
    '"18": {"auto_connect": false, "gui_position": Vector2(1200, 400), "type": "Add"}',
    '},',
    '"version": 2',
    '}',
    '',
]

out = pathlib.Path('F:/Dev/Projects/new-game-project/eden/planet/planet_sphere_graph.tres')
out.write_text('\n'.join(lines), encoding='utf-8')
print(f'Written: {out}')
print(f'Size: {out.stat().st_size} bytes')
