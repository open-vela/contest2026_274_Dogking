#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

# Stable public entry point.  The historical build-a733-wapi.sh name is kept
# because recovery notes refer to it; it now performs the complete board,
# Agent, local-LLM and compatibility-patch build.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$script_dir/build-a733-wapi.sh" "$@"
