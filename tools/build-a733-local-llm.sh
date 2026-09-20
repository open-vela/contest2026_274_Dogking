#!/usr/bin/env bash
set -euo pipefail

# Build the A733 local-LLM stage from WSL without changing the shared OpenVela
# environment.  The explicit Python 3 path is intentional: some WSL images
# still expose Python 2 as /usr/local/bin/python.
root=$(cd "$(dirname "$0")/.." && pwd)
board="$root/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh"
build="$root/cmake_out/cubie-a7z_nsh_v58_aipet_llm"

bash "$root/tools/check-openvela-first.sh"

if [[ "${1:-}" == "--clean" ]]; then
  rm -rf "$build"
fi

source "$root/build/envsetup.sh" >/dev/null
cmake -B "$build" -S "$root/nuttx" \
  -DBOARD_CONFIG="$board" \
  -DCUSTOM_MODULE_PATH="$root/build/cmake" \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DEXTRA_FLAGS='-Wno-cpp -Wno-deprecated-declarations' \
  -GNinja
cmake --build "$build" -j"${JOBS:-8}"

printf '\nA733 local-LLM stage built:\n'
ls -lh "$build/nuttx.bin" "$build/nuttx" "$build/System.map"
grep -E 'CONFIG_(FS_LARGEFILE|HAVE_CXX|CXX_STANDARD|LIBCXX|LIBCXXABI|TLS_NELEM|TLS_TASK_NELEM|SYSTEM_AIPETLLM|AIPETLLM_)' "$build/.config"
