import re

path = r'F:\Dev\Projects\new-game-project\scenes\terrain_test.tscn'
content = open(path, 'r', encoding='utf-8').read()

# Replace the VoxelFormat sub-resource to set indices_depth to 8-bit
# _data array: [version=0, TYPE=0(8bit), SDF=1(16bit), COLOR=0(8bit), INDICES=0(8bit), WEIGHTS=1(16bit), D5=0, D6=0, D7=0]
old = '[sub_resource type="VoxelFormat" id="VoxelFormat_o3i6r"]\n'
new = '[sub_resource type="VoxelFormat" id="VoxelFormat_o3i6r"]\n_data = [0, 0, 1, 0, 0, 1, 0, 0, 0]\n'
content = content.replace(old, new)

open(path, 'w', encoding='utf-8', newline='\n').write(content)
print('done')

# Verify
if '_data = [0, 0, 1, 0, 0, 1, 0, 0, 0]' in content:
    print('VoxelFormat indices_depth = 8-bit confirmed')
else:
    print('ERROR: replacement failed')
    print(content[content.find('VoxelFormat'):content.find('VoxelFormat')+200])
