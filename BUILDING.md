# Building GodotEden on Windows (Vulkan)

## Prerequisites

### 1. Visual Studio 2022
Download from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/).
During install, select the following workload:
- ✅ **Desktop development with C++**
  - Windows 10/11 SDK is included automatically

### 2. Python 3.6+
Download from [python.org](https://www.python.org/downloads/).
Verify with:
```powershell
python --version
```

### 3. SCons
```powershell
pip install scons
```

### 4. Vulkan SDK
Download from [lunarg.com/vulkan-sdk](https://vulkan.lunarg.com/sdk/home#windows) and run the installer.
It sets the `VULKAN_SDK` environment variable automatically. Verify after install:
```powershell
echo $env:VULKAN_SDK
# Expected output: C:\VulkanSDK\1.3.xxx.x
```

---

## Build Steps

> ⚠️ **Important:** Use **"Developer PowerShell for VS 2022"** (found in the Start menu), not regular PowerShell. MSVC must be on PATH.

### Navigate to the repo
```powershell
cd "f:\Dev\GodotEden"
```

### First-time full build (recommended)
```powershell
scons platform=windows target=editor vulkan=yes use_mingw=no -j8
```

---

## Build Flags Reference

| Flag | Value | Description |
|---|---|---|
| `platform` | `windows` | Target platform |
| `target` | `editor` | Builds the Godot editor |
| `vulkan` | `yes` | Enable Vulkan rendering |
| `use_mingw` | `no` | Use MSVC instead of MinGW |
| `-j` | `8` | Parallel jobs — set to your CPU core count |
| `dev_build` | `yes` | Include debug symbols (optional) |
| `optimize` | `none` | Skip optimisation — faster compile, slower runtime (optional) |
| `progress` | `no` | Cleaner terminal output (optional) |

---

## Output

The built editor binary will be at:
```
f:\Dev\GodotEden\bin\godot.windows.editor.x86_64.exe
```

---

## Subsequent Builds

SCons only recompiles changed files, so after the first build:
```powershell
scons platform=windows target=editor vulkan=yes use_mingw=no -j8
```

> First build: ~30–60 minutes depending on CPU.  
> Incremental builds: seconds to a few minutes.

---

## Verify Vulkan is Working

Launch the editor and check:
**Editor → About → Rendering**

It should display **Vulkan (Forward+)** or **Vulkan (Mobile)**.

---

## Notes

- `modules/voxel` is included as a git submodule — SCons picks it up automatically as a custom module, no extra flags needed.
- To pull upstream Godot engine updates: `git fetch origin`
- To push your changes: `git push godoteden HEAD:main`
