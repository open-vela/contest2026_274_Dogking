#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}
test_dir=$(mktemp -d /tmp/a733-fat-test.XXXXXX)
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/nuttx/fs/fat"
cp "$official/nuttx/fs/fat/fs_fat32.c" "$official/nuttx/fs/fat/fs_fat32.h" \
  "$test_dir/nuttx/fs/fat/"
patch --batch --reject-file=- -p1 -d "$test_dir" \
  < "$team/patches/nuttx-fat-filepath.patch"
awk '/^static int fat_ioctl/{copy=1} copy{print} copy && /^}/{exit}' \
  "$test_dir/nuttx/fs/fat/fs_fat32.c" > "$test_dir/fat-filepath-extracted.h"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$test_dir" "$team/tools/test-fat-filepath.c" -o "$test_dir/fat-test"
"$test_dir/fat-test"
