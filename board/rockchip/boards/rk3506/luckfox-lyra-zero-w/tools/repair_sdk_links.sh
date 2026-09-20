#!/bin/bash
#
# Repair symlinks lost when the Luckfox SDK was extracted on Windows.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="$(cd "$BOARD_DIR/../../../../.." && pwd)"
WORKSPACE_ROOT="$(cd "$OPENVELA_ROOT/.." && pwd)"
SDK_ROOT="${RK3506_SDK_ROOT:-$WORKSPACE_ROOT/RK3506-ALL-Files/luckfox-lyra-sdk/lyra-zero-w}"

restore_link()
{
	local destination="$1"
	local target="$2"

	if [ -L "$destination" ]; then
		return
	fi

	if [ -e "$destination" ]; then
		if [ -d "$destination" ] || [ -s "$destination" ]; then
			echo "Refusing to replace non-empty path: $destination" >&2
			exit 1
		fi

		rm -f -- "$destination"
	fi

	ln -s "$target" "$destination"
	echo "Restored ${destination#"$SDK_ROOT/"} -> $target"
}

restore_repo_links()
{
	local worktree="$1"
	local object_store="$2"
	local record
	local metadata
	local mode
	local object
	local stage
	local relative
	local destination
	local target

	while IFS= read -r -d '' record; do
		metadata="${record%%$'\t'*}"
		relative="${record#*$'\t'}"
		read -r mode object stage <<< "$metadata"

		[ "$mode" = 120000 ] || continue

		case "/$relative/" in
			*"/../"* | *"/./"*)
				echo "Unsafe Git path: $relative" >&2
				exit 1
				;;
		esac

		destination="$worktree/$relative"
		target="$(git --git-dir="$object_store" cat-file -p "$object")"
		restore_link "$destination" "$target"
	done < <(
		git --git-dir="$worktree/.git" --work-tree="$worktree" \
			ls-files --stage -z
	)
}

restore_repo_links \
	"$SDK_ROOT/device/rockchip" \
	"$SDK_ROOT/.repo/project-objects/linux/device/rockchip.git"

restore_repo_links \
	"$SDK_ROOT/u-boot" \
	"$SDK_ROOT/.repo/project-objects/android/rk/u-boot.git"

restore_repo_links \
	"$SDK_ROOT/.repo/manifests" \
	"$SDK_ROOT/.repo/manifests.git"

# The repo checkout also represents Git object directories as symlinks.  They
# are not part of a worktree index, so restore them explicitly.  U-Boot can
# compile without these links, but its generated version string is then
# incomplete and every build emits "bad object HEAD" diagnostics.

restore_link "$SDK_ROOT/u-boot/.git/objects" \
	"../../.repo/project-objects/android/rk/u-boot.git/objects"
restore_link "$SDK_ROOT/u-boot/.git/shallow" \
	"../../.repo/projects/u-boot.git/shallow"
restore_link "$SDK_ROOT/.repo/projects/u-boot.git/objects" \
	"../../project-objects/android/rk/u-boot.git/objects"

restore_link "$SDK_ROOT/.repo/manifest.xml" \
	"manifests/rk3506_linux6.1_release.xml"
restore_link "$SDK_ROOT/build.sh" \
	"device/rockchip/common/scripts/build.sh"
restore_link "$SDK_ROOT/Makefile" \
	"device/rockchip/common/Makefile"
restore_link "$SDK_ROOT/rkflash.sh" \
	"device/rockchip/common/scripts/rkflash.sh"
restore_link "$SDK_ROOT/kernel" "kernel-6.1"
