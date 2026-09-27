import os


def can_build(env, platform):
    # Task #2 linkage-proof scope: Windows only (prebuilt ONNX Runtime binaries
    # vendored under thirdparty/onnxruntime are win-x64 only for now). ONNX
    # Runtime is not in git (2.5 GB); without it the module is skipped so a
    # fresh clone still builds. See SETUP.md.
    ort = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "thirdparty", "onnxruntime", "lib", "onnxruntime.lib")
    if platform == "windows" and not os.path.isfile(ort):
        print("eden_terrain_diffusion: thirdparty/onnxruntime not found, module skipped (see SETUP.md)")
        return False
    return platform == "windows"


def configure(env):
    pass


def get_doc_classes():
    return []


def get_doc_path():
    return "doc_classes"
