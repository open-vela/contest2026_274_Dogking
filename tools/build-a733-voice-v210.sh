#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# == 2 || ( $# == 3 && "$3" == --incremental ) ]]
export A733_V209_BUILD_DIR="${A733_V210_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v210_voice}"
# Complete v209 profile, camera opt-in and SWOUT1 guard retained.
# Does not automatically start camera, capture, inference or playback.
bash tools/build-a733-camera-v209.sh "$@"
