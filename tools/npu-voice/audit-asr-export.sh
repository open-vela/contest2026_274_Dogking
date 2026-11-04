#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/../.."
root="$PWD/out/asr-export-v178-20261025"
for role in encoder decoder joiner; do
  [[ -s "$root/${role}_nbg_unify/network_binary.nb" ]]
  [[ ! -e "$root/${role}-nbinfo.txt" ]]
  for flag in -n -in -out -m; do
    docker run --rm --network none --read-only --cap-drop ALL \
      --security-opt no-new-privileges -v "$root:/models:ro" \
      ubuntu-npu:v2.0.10.2 /root/nbinfo "$flag" \
      "/models/${role}_nbg_unify/network_binary.nb" >>"$root/${role}-nbinfo.txt"
  done
done
python3 tools/npu-voice/audit-asr-export.py "$root"
