import re

path = r'F:\Dev\Projects\new-game-project\scenes\terrain_test.tscn'
content = open(path, 'r', encoding='utf-8').read()

# Add texturing_mode=1 to the mesher sub-resource
content = re.sub(
    r'(\[sub_resource type="VoxelMesherTransvoxel" id="VoxelMesherTransvoxel_w3uoq"\])',
    r'\1\ntexturing_mode = 1',
    content
)

# Fix VoxelLodTerrain view_distance = 2048 -> 85000
content = content.replace('view_distance = 2048', 'view_distance = 85000')

open(path, 'w', encoding='utf-8', newline='\n').write(content)
print('done')
