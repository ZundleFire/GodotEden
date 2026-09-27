def can_build(env, platform):
    # Task #2 linkage-proof scope: Windows only (prebuilt ONNX Runtime binaries
    # vendored under thirdparty/onnxruntime are win-x64 only for now).
    return platform == "windows"


def configure(env):
    pass


def get_doc_classes():
    return []


def get_doc_path():
    return "doc_classes"
