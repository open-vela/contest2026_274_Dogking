#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/../.."
repo=$PWD
input="$repo/out/asr-import-v178-20261025-r2"
output="$repo/out/asr-export-v178-20261025"
[[ -f "$input/import-report.json" && ! -e "$output" ]]
mkdir "$output"
docker run --rm --network none --read-only --cap-drop ALL \
  --security-opt no-new-privileges --pids-limit 256 --memory 8g --cpus 4 \
  --tmpfs /tmp:rw,size=2g -e HOME=/tmp -e PYTHONDONTWRITEBYTECODE=1 \
  -v "$input:/import:ro" -v "$repo/tools/npu-voice:/scripts:ro" \
  -v "$output:/work:rw" -w /work ubuntu-npu:v2.0.10.2 \
  python3 /scripts/export-asr-fp16.py
