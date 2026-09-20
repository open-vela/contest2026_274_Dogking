#!/bin/bash
#
# Pack nuttx.bin as the amp.img consumed by Rockchip U-Boot.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="$(cd "$BOARD_DIR/../../../../.." && pwd)"
WORKSPACE_ROOT="$(cd "$OPENVELA_ROOT/.." && pwd)"
SDK_ROOT_DEFAULT="$WORKSPACE_ROOT/RK3506-ALL-Files/luckfox-lyra-sdk/lyra-zero-w"

NUTTX_BIN="${1:-$OPENVELA_ROOT/cmake_out/luckfox-lyra-zero-w_nsh/nuttx.bin}"
OUTPUT_INPUT="${2:-$BOARD_DIR/amp.img}"
SDK_ROOT="${RK3506_SDK_ROOT:-$SDK_ROOT_DEFAULT}"
MKIMAGE="${MKIMAGE:-$SDK_ROOT/rtos/bsp/rockchip/tools/mkimage}"

if [ ! -f "$NUTTX_BIN" ]; then
	echo "Missing openvela binary: $NUTTX_BIN" >&2
	exit 1
fi

if [ ! -x "$MKIMAGE" ]; then
	echo "Missing executable Rockchip mkimage: $MKIMAGE" >&2
	exit 1
fi

OUTPUT_DIR="$(cd "$(dirname "$OUTPUT_INPUT")" && pwd)"
OUTPUT="$OUTPUT_DIR/$(basename "$OUTPUT_INPUT")"

PACK_DIR="$(mktemp -d)"
trap 'rm -rf "$PACK_DIR"' EXIT

if command -v dtc >/dev/null 2>&1; then
	cp "$SCRIPT_DIR/openvela.its" "$PACK_DIR/openvela.its"
	cp "$NUTTX_BIN" "$PACK_DIR/openvela.bin"

	(
		cd "$PACK_DIR"
		"$MKIMAGE" -f openvela.its -E -p 0xe00 "$OUTPUT"
	)
else
	echo "Host dtc not found; using the dependency-free FIT packer"
	python3 "$SCRIPT_DIR/pack_fit.py" "$NUTTX_BIN" "$OUTPUT"
fi

"$MKIMAGE" -l "$OUTPUT"

SDK_STAGE="${RK3506_AMP_STAGE:-$SDK_ROOT/output/openvela/amp.img}"
if [ -n "$SDK_STAGE" ]; then
	mkdir -p "$(dirname "$SDK_STAGE")"
	cp -f "$OUTPUT" "$SDK_STAGE"
	echo "Staged $SDK_STAGE"
fi

echo "Created $OUTPUT"
