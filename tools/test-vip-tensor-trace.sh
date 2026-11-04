#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
tmp=$(mktemp -d /tmp/vip-tensor-trace.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
mkdir -- "$tmp/trace"
gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo/tools/test-vip-tensor-trace.c" -Wl,--wrap=process_vm_readv -o "$tmp/test"
"$tmp/test" "$tmp/trace"
