#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
[[ $# == 2 ]]
bash tools/build-a733-camera-v199.sh "$1" "$2"
