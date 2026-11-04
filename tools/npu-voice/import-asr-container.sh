#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/../.."
repo=$PWD
source=$(cd ../references/zipformer-asr-upstream-e238275 && pwd)
name=${1:-asr-import-v178-20261025}
[[ "$name" =~ ^asr-import-v178-[a-zA-Z0-9-]+$ ]]
output="$repo/out/$name"
[[ ! -e "$output" ]]
mkdir "$output"
# No network, hardware/device/socket access or source writes. Root is needed
# only on the host to access the pre-existing Docker socket.
docker run --rm --network none --read-only --cap-drop ALL \
  --security-opt no-new-privileges --pids-limit 256 --memory 8g --cpus 4 \
  --tmpfs /tmp:rw,size=1g -e HOME=/tmp -e PYTHONDONTWRITEBYTECODE=1 \
  -v "$source:/source:ro" -v "$repo/tools/npu-voice:/scripts:ro" \
  -v "$output:/work:rw" -w /work ubuntu-npu:v2.0.10.2 \
  python3 /scripts/import-asr-locked.py
