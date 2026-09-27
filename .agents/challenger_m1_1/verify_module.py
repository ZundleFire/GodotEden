import ast
import os
import re
import sys

def test_python_files():
    print("=== Testing Python files syntax ===")
    config_path = r"C:\DEV_DRIVE\Dev\GodotEden\modules\godot_eden\config.py"
    scsub_path = r"C:\DEV_DRIVE\Dev\GodotEden\modules\godot_eden\SCsub"

    with open(config_path, "r", encoding="utf-8") as f:
        config_code = f.read()
    ast.parse(config_code, filename="config.py")
    print("✓ config.py AST parse successful.")

    with open(scsub_path, "r", encoding="utf-8") as f:
        scsub_code = f.read()
    ast.parse(scsub_code, filename="SCsub")
    print("✓ SCsub AST parse successful.")

def test_header_guards_and_includes():
    print("\n=== Testing Header Include & Pragma Once ===")
    base_dir = r"C:\DEV_DRIVE\Dev\GodotEden\modules\godot_eden"
    headers = []
    for root, dirs, files in os.walk(base_dir):
        for file in files:
            if file.endswith(".h"):
                headers.append(os.path.join(root, file))

    for h in headers:
        rel_path = os.path.relpath(h, base_dir)
        with open(h, "r", encoding="utf-8") as f:
            content = f.read()
        if "#pragma once" not in content:
            print(f"❌ Missing #pragma once in {rel_path}")
        else:
            print(f"✓ #pragma once present in {rel_path}")

def test_classdb_bindings():
    print("\n=== Testing ClassDB Registrations & Bindings ===")
    base_dir = r"C:\DEV_DRIVE\Dev\GodotEden\modules\godot_eden"
    cpp_files = []
    for root, dirs, files in os.walk(base_dir):
        for file in files:
            if file.endswith(".cpp"):
                cpp_files.append(os.path.join(root, file))

    classes = ["VoxelWorld", "VoxelVolume", "VoxelRenderer", "VoxelStreamer", "VoxelGenerator"]
    
    # Check register_types.cpp
    reg_path = os.path.join(base_dir, "register_types.cpp")
    with open(reg_path, "r", encoding="utf-8") as f:
        reg_content = f.read()

    for cls in classes:
        pattern = rf"GDREGISTER_CLASS\({cls}\)"
        if re.search(pattern, reg_content):
            print(f"✓ {cls} is registered in register_types.cpp with GDREGISTER_CLASS")
        else:
            print(f"❌ {cls} missing GDREGISTER_CLASS in register_types.cpp")

def test_gdclass_inheritance():
    print("\n=== Testing GDCLASS Macro and Inheritance ===")
    classes_map = {
        "VoxelWorld": "Node3D",
        "VoxelVolume": "Resource",
        "VoxelRenderer": "Node3D",
        "VoxelStreamer": "RefCounted",
        "VoxelGenerator": "Resource"
    }
    
    base_dir = r"C:\DEV_DRIVE\Dev\GodotEden\modules\godot_eden"
    for cls, expected_parent in classes_map.items():
        # Find header file
        found = False
        for root, dirs, files in os.walk(base_dir):
            for file in files:
                if file.endswith(".h"):
                    h_path = os.path.join(root, file)
                    with open(h_path, "r", encoding="utf-8") as f:
                        content = f.read()
                    if f"class {cls}" in content:
                        found = True
                        gdclass_pattern = rf"GDCLASS\s*\(\s*{cls}\s*,\s*{expected_parent}\s*\)"
                        if re.search(gdclass_pattern, content):
                            print(f"✓ {cls} correctly defines GDCLASS({cls}, {expected_parent})")
                        else:
                            print(f"❌ {cls} GDCLASS macro mismatch or missing (expected parent: {expected_parent})")
                        
                        inherit_pattern = rf"class\s+{cls}\s*:\s*public\s+{expected_parent}"
                        if re.search(inherit_pattern, content):
                            print(f"✓ {cls} correctly inherits public {expected_parent}")
                        else:
                            print(f"❌ {cls} C++ inheritance mismatch (expected: public {expected_parent})")
        if not found:
            print(f"❌ Header for {cls} not found!")

if __name__ == "__main__":
    test_python_files()
    test_header_guards_and_includes()
    test_classdb_bindings()
    test_gdclass_inheritance()
