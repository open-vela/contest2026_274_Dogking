#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
export A733_V215_BUILD_DIR="${A733_V216_BUILD_DIR:-$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v216_voice_quality}"
bash tools/build-a733-voice-v215.sh "$@"
