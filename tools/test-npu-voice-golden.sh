#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
sdk=${A733_VIP_SDK:-$repo/../a733-ai-sdk-reference/viplite-tina/v2.0}
tmp=$(mktemp -d /tmp/voice-golden-test.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
# Generated fixture, intentionally one byte instead of padded tensor size.
printf x > "$tmp/input0.bin"
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$sdk/inc" \
  "$repo/tools/npu-voice/test-voice-golden.c" -o "$tmp/test"
"$tmp/test" "$tmp"
