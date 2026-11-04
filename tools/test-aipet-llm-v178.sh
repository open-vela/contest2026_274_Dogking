#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
pet=board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm
sdk=$(cd ../../quickly-openvela && pwd)
mkdir -p out/v178-llm-tests
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$pet/../aipet" tools/test-aipet-model-path.cxx -o out/v178-llm-tests/model-path
out/v178-llm-tests/model-path
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tools/test-aipet-llm-api.cxx "$pet/aipetllm_api.cxx" -o out/v178-llm-tests/api
out/v178-llm-tests/api
tmp=$(mktemp -d /tmp/local-llm-v178.XXXXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
  -DAIPET_LOCAL_DIR="\"$tmp/state\"" -Itools/llm-host-config \
  -I"$pet" -I"$pet/../aipet" tools/test-local-llm-config.cxx \
  "$pet/../aipet/local_llm.cxx" "$pet/../aipet/pet_core.cxx" -o out/v178-llm-tests/config
out/v178-llm-tests/config "$tmp"
"$sdk/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-gcc" \
  -O2 -std=c11 -Wall -Wextra -Werror -march=armv8-a+simd \
  -c "$pet/aipetllm_activation.c" -o out/v178-llm-tests/activation-arm64.o
bash tools/test-aipet-routes.sh
python3 -m py_compile tools/inspect-minicpm5.py tools/npu-voice/import-asr-locked.py \
  tools/npu-voice/export-asr-fp16.py tools/npu-voice/audit-asr-export.py
echo 'PASS: v178 model routing, bounded API, ARM64 NEON compile and provisional streaming'
