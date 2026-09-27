## 2026-08-05T18:58:34Z
<USER_REQUEST>
You are Explorer 2 for Milestone 3 (Iteration 2 Remediation) of GodotEden.
Working Directory: C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3\explorer_iter2_2

MANDATORY DOCUMENTS TO READ:
- C:\DEV_DRIVE\Dev\GodotEden\ORIGINAL_REQUEST.md
- C:\DEV_DRIVE\Dev\GodotEden\PROJECT.md
- C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3\SCOPE.md
- C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3\GATE_STATUS.md
- C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3\reviewer_1\handoff.md

YOUR TASKS:
1. Analyze GLSL shader bugs in modules/godot_eden/shaders/micro_voxel_raymarch.glsl and clipmap_lod.glsl.
2. Fix undeclared identifier AtcPackedGpuMaterial by replacing with GpuMaterialData.
3. Replace hardcoded root_min / root_max (vec3(-512.0) / vec3(512.0)) in micro_voxel_raymarch.glsl by adding vec3 root_min and vec3 root_max to RaymarchPushConstants struct in both C++ and GLSL.
4. Fix ray direction inverse division safeguards and SVO octant coordinate space updates.
5. Fix toroidal offset calculation in clipmap_lod.glsl to prevent negative indices: ((grid_cell % grid_extent) + grid_extent) % grid_extent.
6. Write a detailed remediation plan in C:\DEV_DRIVE\Dev\GodotEden\.agents\sub_orch_m3\explorer_iter2_2\handoff.md with exact GLSL shader code blocks.
7. Send a message to parent (sub_orch_m3) notifying when complete.
</USER_REQUEST>
