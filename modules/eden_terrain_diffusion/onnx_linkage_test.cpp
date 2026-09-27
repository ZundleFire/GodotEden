#include "onnx_linkage_test.h"

#include "core/error/error_macros.h"
#include "core/os/memory.h"
#include "core/string/print_string.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

#include <onnxruntime_cxx_api.h>

#include <array>
#include <string>
#include <vector>

namespace eden {
namespace onnx_test {

namespace {

// Linkage-proof only: hardcoded path to the smallest of the 3 exported
// terrain-diffusion cascade stages (Task #1 of this chain). This does not
// belong in the real generator (later task) -- that will need a proper
// resource path / import setting instead of a hardcoded absolute path into
// another repo.
const char *MODEL_PATH =
		"C:\\DEV_DRIVE\\Dev\\Projects\\eden-project\\prototypes\\terrain-diffusion-ml\\"
		"vendor\\terrain-diffusion\\onnx_out\\coarse_model.onnx";

#ifdef _WIN32
std::wstring to_wide(const char *p_utf8) {
	std::wstring out;
	// MODEL_PATH is plain ASCII, so a naive widen is sufficient here.
	while (*p_utf8) {
		out.push_back(static_cast<wchar_t>(*p_utf8));
		++p_utf8;
	}
	return out;
}
#endif

} // namespace

void run_onnx_linkage_test() {
	print_line("[eden_onnx_test] Starting ONNX Runtime linkage test...");
	print_line(String("[eden_onnx_test] ONNX Runtime version: ") + Ort::GetVersionString().c_str());

	Ort::Env ort_env(ORT_LOGGING_LEVEL_WARNING, "eden_onnx_linkage_test");

	Ort::SessionOptions session_options;
	session_options.SetIntraOpNumThreads(1);
	session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

	bool cuda_ep_requested = false;
	bool cuda_ep_appended = false;
	{
		cuda_ep_requested = true;
		try {
			OrtCUDAProviderOptions cuda_options{};
			cuda_options.device_id = 0;
			session_options.AppendExecutionProvider_CUDA(cuda_options);
			cuda_ep_appended = true;
			print_line("[eden_onnx_test] CUDA execution provider appended to session options.");
		} catch (const Ort::Exception &e) {
			print_line(String("[eden_onnx_test] CUDA EP append FAILED: ") + e.what() +
					" -- falling back to CPU EP.");
		}
	}

#ifdef _WIN32
	std::wstring model_path_w = to_wide(MODEL_PATH);
#endif

	Ort::Session *session = nullptr;
	bool running_on_cuda = false;

	// If CUDA EP was appended, try to actually create the session with it.
	// Session creation is where a missing/mismatched CUDA/cuDNN runtime
	// (cudart64_12.dll, cudnn64_9.dll, etc.) or GPU driver problem will
	// surface, even though appending the EP to SessionOptions succeeded.
	if (cuda_ep_appended) {
		try {
#ifdef _WIN32
			session = memnew(Ort::Session(ort_env, model_path_w.c_str(), session_options));
#else
			session = memnew(Ort::Session(ort_env, MODEL_PATH, session_options));
#endif
			running_on_cuda = true;
			print_line("[eden_onnx_test] Session created successfully WITH CUDA EP active.");
		} catch (const Ort::Exception &e) {
			print_line(String("[eden_onnx_test] Session creation with CUDA EP FAILED: ") + e.what() +
					" -- retrying with CPU EP only.");
			session = nullptr;
		}
	}

	if (session == nullptr) {
		Ort::SessionOptions cpu_only_options;
		cpu_only_options.SetIntraOpNumThreads(1);
		cpu_only_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
		try {
#ifdef _WIN32
			session = memnew(Ort::Session(ort_env, model_path_w.c_str(), cpu_only_options));
#else
			session = memnew(Ort::Session(ort_env, MODEL_PATH, cpu_only_options));
#endif
			running_on_cuda = false;
			print_line("[eden_onnx_test] Session created successfully with CPU EP.");
		} catch (const Ort::Exception &e) {
			print_line(String("[eden_onnx_test] FAILED to create session at all: ") + e.what());
			ERR_PRINT("[eden_onnx_test] ONNX linkage test FAILED (could not create session).");
			return;
		}
	}

	// coarse_model.onnx expects (verified via onnxruntime.InferenceSession
	// introspection on the exported model, batch size forced to 1 here):
	//   x            : float32 [1, 11, 64, 64]
	//   noise_labels : float32 [1]
	//   cond_0..cond_4: float32 [1]  (5 scalar conditional inputs)
	// output: float32 [1, 6, 64, 64]
	Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

	constexpr int64_t kBatch = 1;
	constexpr int64_t kInChannels = 11;
	constexpr int64_t kSize = 64;
	constexpr int kNumCond = 5;

	std::vector<float> x_data(kBatch * kInChannels * kSize * kSize, 0.0f);
	std::vector<float> noise_labels_data(kBatch, 0.0f);
	std::array<std::vector<float>, kNumCond> cond_data;
	for (int i = 0; i < kNumCond; ++i) {
		cond_data[i] = std::vector<float>(kBatch, 0.0f);
	}

	std::vector<int64_t> x_shape = { kBatch, kInChannels, kSize, kSize };
	std::vector<int64_t> scalar_shape = { kBatch };

	std::vector<Ort::Value> input_tensors;
	input_tensors.push_back(Ort::Value::CreateTensor<float>(
			mem_info, x_data.data(), x_data.size(), x_shape.data(), x_shape.size()));
	input_tensors.push_back(Ort::Value::CreateTensor<float>(
			mem_info, noise_labels_data.data(), noise_labels_data.size(), scalar_shape.data(), scalar_shape.size()));
	for (int i = 0; i < kNumCond; ++i) {
		input_tensors.push_back(Ort::Value::CreateTensor<float>(
				mem_info, cond_data[i].data(), cond_data[i].size(), scalar_shape.data(), scalar_shape.size()));
	}

	std::vector<const char *> input_names = { "x", "noise_labels", "cond_0", "cond_1", "cond_2", "cond_3", "cond_4" };
	std::vector<const char *> output_names = { "output" };

	try {
		auto output_tensors = session->Run(
				Ort::RunOptions{ nullptr },
				input_names.data(), input_tensors.data(), input_tensors.size(),
				output_names.data(), output_names.size());

		ERR_FAIL_COND_MSG(output_tensors.size() != 1, "[eden_onnx_test] Unexpected output tensor count.");

		Ort::TensorTypeAndShapeInfo out_info = output_tensors[0].GetTensorTypeAndShapeInfo();
		std::vector<int64_t> out_shape = out_info.GetShape();

		String shape_str = "[";
		for (size_t i = 0; i < out_shape.size(); ++i) {
			shape_str += String::num_int64(out_shape[i]);
			if (i + 1 < out_shape.size()) {
				shape_str += ", ";
			}
		}
		shape_str += "]";

		const float *out_data = output_tensors[0].GetTensorData<float>();
		print_line(String("[eden_onnx_test] Run() succeeded. Output shape: ") + shape_str +
				" first value: " + String::num(out_data[0]));

		print_line(String("[eden_onnx_test] Execution provider actually used: ") +
				(running_on_cuda ? "CUDA" : "CPU"));
		print_line(String("[eden_onnx_test] CUDA EP requested: ") + (cuda_ep_requested ? "yes" : "no") +
				", CUDA EP appended to SessionOptions: " + (cuda_ep_appended ? "yes" : "no") +
				", CUDA EP actually active at Run() time: " + (running_on_cuda ? "yes" : "no"));
		print_line("[eden_onnx_test] ONNX linkage test PASSED.");
	} catch (const Ort::Exception &e) {
		print_line(String("[eden_onnx_test] session->Run() FAILED: ") + e.what());
		ERR_PRINT("[eden_onnx_test] ONNX linkage test FAILED (Run() threw).");
	}

	memdelete(session);
}

} // namespace onnx_test
} // namespace eden
