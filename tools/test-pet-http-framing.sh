#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
test_bin=$(mktemp /tmp/a733-http-test.XXXXXX)
trap 'rm -f -- "$test_bin"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "$repo/tools/test-pet-http-framing.c" -o "$test_bin"
"$test_bin"
