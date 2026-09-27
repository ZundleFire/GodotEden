#!/usr/bin/env python3
"""
Empirical test generator & verification script for M3 Gate 2 Iteration 2.
Focuses on PhysicsMeshGenerator::generate_greedy_mesh_faces directional logic.
"""

import sys

def simulate_greedy_mesh_faces_cpp_current(voxels_16x16x16, iso=0.0):
    """
    Python simulation of C++ PhysicsMeshGenerator::generate_greedy_mesh_faces
    using the exact code from modules/godot_eden/rendering/physics_mesh_generator.cpp (lines 141-288).
    """
    size_x, size_y, size_z = 16, 16, 16
    faces = []

    for d in range(3):
        u_axis = (d + 1) % 3
        v_axis = (d + 2) % 3

        dim_d = 16
        dim_u = 16
        dim_v = 16

        for dir_idx in range(2):
            direction = -1 if dir_idx == 0 else 1
            normal_vec = [0, 0, 0]
            normal_vec[d] = direction

            for slice_d in range(dim_d):
                mask = [[0 for _ in range(dim_v)] for _ in range(dim_u)]

                for u in range(dim_u):
                    for v in range(dim_v):
                        pos = [0, 0, 0]
                        pos[d] = slice_d
                        pos[u_axis] = u
                        pos[v_axis] = v

                        neighbor_pos = [pos[0] + normal_vec[0], pos[1] + normal_vec[1], pos[2] + normal_vec[2]]

                        cur_solid = (voxels_16x16x16.get(tuple(pos), 1.0) <= iso)

                        neighbor_solid = False
                        if (0 <= neighbor_pos[0] < size_x and
                            0 <= neighbor_pos[1] < size_y and
                            0 <= neighbor_pos[2] < size_z):
                            neighbor_solid = (voxels_16x16x16.get(tuple(neighbor_pos), 1.0) <= iso)

                        if direction == 1:
                            if cur_solid and not neighbor_solid:
                                mask[u][v] = 1
                        else:
                            # LINE 199 IN C++: if (!cur_solid && neighbor_solid)
                            if (not cur_solid) and neighbor_solid:
                                mask[u][v] = 1

                for u in range(dim_u):
                    for v in range(dim_v):
                        if mask[u][v] == 0:
                            continue
                        
                        quad_origin = [0, 0, 0]
                        quad_origin[d] = (slice_d + 1) if direction == 1 else slice_d
                        quad_origin[u_axis] = u
                        quad_origin[v_axis] = v

                        faces.append({
                            'axis': d,
                            'dir': direction,
                            'slice': slice_d,
                            'origin': quad_origin,
                            'u': u,
                            'v': v
                        })
                        mask[u][v] = 0

    return faces

def run_empirical_tests():
    print("=== Empirical Test 1: Single Solid Voxel at (0, 0, 0) ===")
    voxels_single_0 = {(0, 0, 0): -1.0}
    faces_cpp = simulate_greedy_mesh_faces_cpp_current(voxels_single_0)
    
    print(f"Generated face count: {len(faces_cpp)}")
    for i, f in enumerate(faces_cpp):
        print(f"  Face {i+1}: Axis {f['axis']}, Dir {f['dir']}, Slice {f['slice']}, Quad Origin {f['origin']}")

    # Check negative faces for (0,0,0)
    neg_faces = [f for f in faces_cpp if f['dir'] == -1]
    print(f"Negative faces count: {len(neg_faces)}")
    
    # Expected: A solid voxel at (0,0,0) must generate 3 negative faces at slice 0 (origin [0,0,0])
    # facing -X, -Y, -Z.
    correct_neg_faces = [f for f in neg_faces if f['slice'] == 0]
    
    if len(correct_neg_faces) == 0:
        print(" [FAIL] BUG REPRODUCED: Zero negative faces generated at x=0, y=0, z=0 for solid voxel at (0,0,0)!")
    else:
        print(" [PASS] Negative faces found at slice 0.")

    print("\n=== Empirical Test 2: Solid Voxel at (15, 0, 0) (Boundary) ===")
    voxels_single_15 = {(15, 0, 0): -1.0}
    faces_15 = simulate_greedy_mesh_faces_cpp_current(voxels_single_15)
    print(f"Generated face count: {len(faces_15)}")
    for i, f in enumerate(faces_15):
        print(f"  Face {i+1}: Axis {f['axis']}, Dir {f['dir']}, Slice {f['slice']}, Quad Origin {f['origin']}")

    neg_faces_15 = [f for f in faces_15 if f['dir'] == -1]
    print(f"Negative faces count for voxel (15,0,0): {len(neg_faces_15)}")
    if len(neg_faces_15) == 0:
        print(" [FAIL] BUG REPRODUCED: Negative face (-X) completely missing for voxel at positive boundary (15,0,0)!")

if __name__ == '__main__':
    run_empirical_tests()
