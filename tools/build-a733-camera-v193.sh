#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
# Reuse the mutable v192 SDK build cache, NOT its sealed release or image.
# The v192 config/ABI is retained; source and final artifacts are versioned
# separately. Avoid a full multi-thousand-object rebuild across WSL 9p.
[[ $# == 2 ]]
export OPENVELA_WORKSPACE=${OPENVELA_WORKSPACE:-$(cd ../../quickly-openvela && pwd)}
cache="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v192_camera"
if [[ -f "$cache/CMakeCache.txt" ]]; then
  if [[ ! -f "$cache/build.ninja" ]]; then
    export A733_REGENERATE_CACHE=1
  fi
  bash tools/build-a733-camera-v192.sh "$1" "$2" --incremental
else
  # Clean clones can use the existing fresh-build path. It refuses a stray
  # pre-existing directory; no deletion or implicit reuse here.
  bash tools/build-a733-camera-v192.sh "$1" "$2"
fi
