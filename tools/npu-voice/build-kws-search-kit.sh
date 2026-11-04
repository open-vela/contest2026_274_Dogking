#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Development host only; never runs target models or modifies old reports/kits.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
[[ $# == 3 ]] || { echo 'Usage: bash build-kws-search-kit.sh ORIGINAL_ALL_KIT RETURNED_REPORT NEW_ABSOLUTE_KIT' >&2; exit 2; }
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
original=$(cd -- "$1" && pwd -P)
report=$(cd -- "$2" && pwd -P)
target=$3
[[ "$target" == /* && "$target" != / && ! -e "$target" && ! -L "$target" &&
   ! -e "$target.tar.gz" && ! -e "$target-host" ]] || { echo 'Use new output paths' >&2; exit 2; }
mkdir -- "$target" "$target-host"
target=$(cd -- "$target" && pwd -P)
for item in lib models licenses fixtures; do cp -a -- "$original/$item" "$target/"; done
cp -- "$original/voice-golden-linux-aarch64" "$original/tokens.txt" "$original/keywords.txt" "$target/"
for file in test-linux-voice.sh test-linux-kws-search.sh stream-kws.py joiner-fixtures.py \
            kws-contract.json run-voice-batch.py keyword-reference.py run-kws-search.py; do
  cp -- "$repo/tools/npu-voice/$file" "$target/"
done
cp -- "$repo/tools/npu-voice/LINUX_KWS_SEARCH.md" "$target/README.md"
python3 "$repo/tools/npu-voice/prepare-kws-search.py" "$report" "$target/search-fixtures"
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
mkdir -- "$target/source"
cp -- "$pet/voice_kws.c" "$pet/voice_kws.h" "$target/source/"
sysroot="$repo/../a733-viplite-golden/bullseye-sysroot"
aarch64-linux-gnu-gcc --sysroot="$sysroot" -isystem "$sysroot/usr/aarch64-linux-gnu/include" \
  -B"$sysroot/usr/aarch64-linux-gnu/lib" -L"$sysroot/usr/aarch64-linux-gnu/lib" \
  -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared "$pet/voice_kws.c" -lm -o "$target/libaipet-kws-aarch64.so"
gcc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared "$pet/voice_kws.c" -lm -o "$target-host/libaipet-kws-host.so"
AIPET_KWS_LIBRARY="$target-host/libaipet-kws-host.so" python3 "$repo/tools/test-kws-search.py" > "$target-host/tests.log" 2>&1
readelf --version-info "$target/libaipet-kws-aarch64.so" > "$target-host/aarch64-symbol-versions.txt"
python3 "$repo/tools/npu-voice/make-stream-manifest.py" "$target"
python3 "$target/run-kws-search.py" check "$target"
mkdir -- "$target-host/replay"
python3 -u "$target/run-kws-search.py" replay "$target" "$target-host/replay" \
  --library "$target-host/libaipet-kws-host.so" > "$target-host/replay.log"
tar -czf "$target.tar.gz" -C "$(dirname -- "$target")" "$(basename -- "$target")"
sha256sum "$target.tar.gz"
echo "PRIVATE search regression kit: $target.tar.gz"
