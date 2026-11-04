#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
model=${1:-/mnt/c/Users/Lenovo/Downloads/MiniCPM5-1B-Q4_K_M.gguf}
pet=board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm
ggml="$pet/third_party/ggml"
ref=out/minicpm5-host-reference
dest=${AIPETLLM_TEST_OUT:-out/minicpm5-host-test}
[[ "$dest" =~ ^out/[a-zA-Z0-9_-]+$ ]]
mkdir -p "$dest"
preview_flags=()
if [[ "${AIPETLLM_PREVIEW_V12:-0}" == 1 ]]; then
  preview_flags=(-DCONFIG_A733_PRODUCT_V12=1)
fi
gcc -D_GNU_SOURCE -DAIPETLLM_LOGITS_TEST_API -O2 -std=c11 -ffunction-sections -fdata-sections \
  -I"$ggml/include" -I"$ggml/src" -I"$ggml/src/ggml-cpu" \
  -c "$pet/aipetllm_modelcheck.c" -o "$dest/modelcheck.o" "${preview_flags[@]}"
gcc -O2 -std=c11 -c "$pet/aipetllm_activation.c" -o "$dest/activation.o"
g++ -O2 -std=c++17 -Wall -Wextra -Werror \
  -I"$pet" -I../llama.cpp-0.1.2/include -I../llama.cpp-0.1.2/ggml/include \
  "${AIPETLLM_TEST_SOURCE:-tools/test-minicpm5.cxx}" "$pet/aipetllm_bpe.cxx" "$pet/aipetllm_api.cxx" \
  "$dest/modelcheck.o" \
  "$dest/activation.o" \
  -L"$ref/bin" -Wl,-rpath,"$PWD/$ref/bin" -Wl,--gc-sections \
  -Wl,--wrap=sched_setaffinity -Wl,--wrap=sched_getcpu \
  -lllama -lggml-cpu -lggml-base -lggml \
  -lm -lpthread -o "$dest/test"
"$dest/test" "$model" "${@:2}"
