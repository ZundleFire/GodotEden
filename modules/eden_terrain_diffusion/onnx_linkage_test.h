#ifndef EDEN_ONNX_LINKAGE_TEST_H
#define EDEN_ONNX_LINKAGE_TEST_H

// Task #2 (ONNX Runtime native linkage proof) for the terrain diffusion ML
// generator. See ADR-0023 and the terrain-diffusion-ml prototype.
//
// Runs a single forward pass of coarse_model.onnx through the vendored ONNX
// Runtime C++ API to prove the thirdparty/onnxruntime vendoring + SCons
// linkage works end to end, and reports whether the CUDA execution provider
// actually initialized or whether it fell back to CPU.
//
// This is deliberately NOT wired into the real generator yet (no RNG port,
// no scheduler, no cascade) -- it is a standalone linkage/runtime smoke test
// triggered by the --run_onnx_test command line flag (see register_types.cpp).
namespace eden {
namespace onnx_test {

void run_onnx_linkage_test();

} // namespace onnx_test
} // namespace eden

#endif // EDEN_ONNX_LINKAGE_TEST_H
