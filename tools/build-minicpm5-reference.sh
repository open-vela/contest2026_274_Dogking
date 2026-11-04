#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=$(cd ../../quickly-openvela && pwd)
export PATH="$sdk/prebuilts/tools/cmake/bin:$sdk/prebuilts/tools/ninja/bin:/usr/bin:/bin:$PATH"
cmake -S ../llama.cpp-0.1.2 -B out/minicpm5-host-reference -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DGGML_NATIVE=OFF -DGGML_AVX=OFF -DGGML_AVX2=OFF \
  -DGGML_FMA=OFF -DGGML_F16C=OFF -DGGML_SSE42=OFF \
  -DGGML_CPU_REPACK=OFF -DGGML_LLAMAFILE=OFF \
  -DGGML_AVX512=OFF -DGGML_CUDA=OFF -DGGML_VULKAN=OFF \
  -DLLAMA_CURL=OFF -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=ON \
  -DLLAMA_BUILD_SERVER=OFF
cmake --build out/minicpm5-host-reference --target llama-completion llama-tokenize -j 8
