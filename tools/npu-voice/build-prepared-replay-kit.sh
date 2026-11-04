#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
[[ $# == 2 ]] || exit 2
repo=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
source=$(cd -- "$1" && pwd -P)
target=$2
[[ "$target" == /* && ! -e "$target" && ! -e "$target.tar.gz" ]] || exit 2
mkdir -- "$target"
cp -- "$source"/*.a7tv "$source/export-index.json" "$target/"
cp -- "$repo/tools/npu-voice/run-prepared-replay.py" "$target/"
sysroot="$repo/../a733-viplite-golden/bullseye-sysroot"
inc="$repo/board/a733-cubie-a7z/openvela-overlay"
aarch64-linux-gnu-gcc --sysroot="$sysroot" -isystem "$sysroot/usr/aarch64-linux-gnu/include" \
  -B"$sysroot/usr/aarch64-linux-gnu/lib" -L"$sysroot/usr/aarch64-linux-gnu/lib" \
  -std=c11 -O2 -Wall -Wextra -Werror -DA733_TENSOR_LINUX_REPLAY \
  -I"$inc/nuttx/include" -I"$inc/vendor/allwinnertech/chips/a733/include" \
  "$repo/tools/a733-tensor-check.c" "$inc/vendor/allwinnertech/chips/a733/a733_tensor_plan.c" \
  -o "$target/tensor-check-linux-aarch64"
python3 - "$target" <<'PY'
import sys,json,hashlib
from pathlib import Path
p=Path(sys.argv[1]);files={f.name:dict(bytes=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in p.iterdir()}
with (p/'kit-assets.json').open('x') as s:json.dump(files,s,indent=2,sort_keys=True)
PY
python3 "$target/run-prepared-replay.py" check
tar -czf "$target.tar.gz" -C "$(dirname -- "$target")" "$(basename -- "$target")"
sha256sum "$target.tar.gz"
