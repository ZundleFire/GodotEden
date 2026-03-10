import os, shutil

src_dir = r'F:\Dev\GodotEden\eden\shaders'
dst_dir = r'F:\Dev\Projects\new-game-project\eden\shaders'
os.makedirs(dst_dir, exist_ok=True)

shaders = [
    'planet_lowpoly.gdshader',
    'debug_tectonic.gdshader',
    'debug_moisture.gdshader',
    'debug_biome_flat.gdshader',
    'debug_combined.gdshader',
]

for s in shaders:
    src = os.path.join(src_dir, s)
    dst = os.path.join(dst_dir, s)
    content = open(src, 'r', encoding='utf-8').read()
    open(dst, 'w', encoding='utf-8', newline='\n').write(content)
    print(f'  copied: {s}')

print('All shaders copied.')
