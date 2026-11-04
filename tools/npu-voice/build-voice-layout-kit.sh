#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Host only; no NPU calls. Never overwrites a passed kit or raw report.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
[[ $# == 3 ]] || { echo 'Usage: bash build-voice-layout-kit.sh ORIGINAL_ALL_KIT GOLDEN_REPORT NEW_ABSOLUTE_KIT' >&2; exit 2; }
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
original=$(cd -- "$1" && pwd -P)
report=$(cd -- "$2" && pwd -P)
target=$3
[[ "$target" == /* && "$target" != / && ! -e "$target" && ! -L "$target" &&
   ! -e "$target.tar.gz" && ! -e "$target-host" ]] || { echo 'Use new output paths' >&2; exit 2; }
mkdir -- "$target" "$target-host"
target=$(cd -- "$target" && pwd -P)
for item in lib models licenses fixtures; do cp -a -- "$original/$item" "$target/"; done
cp -- "$original/voice-golden-linux-aarch64" "$original/tokens.txt" "$target/"
for file in test-linux-voice.sh test-linux-voice-layout.sh stream-kws.py joiner-fixtures.py \
            kws-contract.json vocoder-contract.json run-voice-batch.py keyword-reference.py capture-voice-layout.py; do
  cp -- "$repo/tools/npu-voice/$file" "$target/"
done
cp -- "$repo/tools/npu-voice/LINUX_VOICE_LAYOUT.md" "$target/README.md"
python3 "$repo/tools/npu-voice/prepare-layout-fixtures.py" "$report" "$target/layout-fixtures"
sysroot="$repo/../a733-viplite-golden/bullseye-sysroot"
make -C "$repo/tools/npu-voice" SYSROOT="$sysroot" OUT="$target-host" \
  BINARY=voice-layout-linux-aarch64 EXTRA_CFLAGS=-DVOICE_CAPTURE_LOGICAL=1
cp -- "$target-host/voice-layout-linux-aarch64" "$target/"
aarch64-linux-gnu-gcc --sysroot="$sysroot" -isystem "$sysroot/usr/aarch64-linux-gnu/include" \
  -B"$sysroot/usr/aarch64-linux-gnu/lib" -L"$sysroot/usr/aarch64-linux-gnu/lib" \
  -O2 -Wall -Wextra -Werror -fPIC -shared "$repo/tools/npu-voice/vip-tensor-trace.c" \
  -Wl,--version-script="$repo/tools/npu-voice/vip-tensor-trace.exports" -o "$target/libvip-tensor-trace.so"
readelf --version-info "$target/voice-layout-linux-aarch64" > "$target-host/runner-symbols.txt"
readelf --version-info "$target/libvip-tensor-trace.so" > "$target-host/tracer-symbols.txt"
python3 "$repo/tools/npu-voice/make-stream-manifest.py" "$target"
python3 "$target/capture-voice-layout.py" check "$target"
tar -czf "$target.tar.gz" -C "$(dirname -- "$target")" "$(basename -- "$target")"
sha256sum "$target.tar.gz"
echo "PRIVATE four-model layout capture kit: $target.tar.gz"
