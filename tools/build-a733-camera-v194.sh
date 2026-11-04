#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
cd "$(dirname "$0")/.."
# Same configuration/cache, new BSP source and independent release artifacts.
[[ $# == 2 ]]
bash tools/build-a733-camera-v193.sh "$1" "$2"
