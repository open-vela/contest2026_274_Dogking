#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
team_dir="$(cd "$script_dir/.." && pwd)"
overlay="$team_dir/board/a733-cubie-a7z/openvela-overlay"

if [[ -n "${OPENVELA_WORKSPACE:-}" ]]; then
  official="$(cd "$OPENVELA_WORKSPACE" && pwd)"
elif [[ -d "$team_dir/../nuttx" && -d "$team_dir/../apps" ]]; then
  # Standard repo-init layout: workspace/contest2026_274_Dogking.
  official="$(cd "$team_dir/.." && pwd)"
elif [[ -d "$team_dir/../../quickly-openvela/nuttx" ]]; then
  # Maintainer layout used while developing this port.
  official="$(cd "$team_dir/../../quickly-openvela" && pwd)"
else
  echo "Cannot locate the openvela workspace." >&2
  echo "Set OPENVELA_WORKSPACE to the directory containing nuttx/apps/packages." >&2
  exit 1
fi

for required in nuttx apps packages vendor; do
  [[ -d "$official/$required" ]] || {
    echo "Invalid openvela workspace: missing $official/$required" >&2
    exit 1
  }
done

build="${A733_BUILD_DIR:-$official/cmake_out/cubie-a7z_nsh}"
backup="$(mktemp -d "${TMPDIR:-/tmp}/a733-build-staging.XXXXXX")"

bash "$script_dir/check-openvela-first.sh"

files=(
  vendor/allwinnertech/boards/a733/cubie-a7z/scripts/dramboot.ld
  nuttx/arch/arm64/src/common/arm64_fatal.c
  vendor/allwinnertech/chips/a733/a733_boot.c
  vendor/allwinnertech/chips/a733/a733_hwdiag.c
  vendor/allwinnertech/chips/a733/a733_wifi_usb.c
  vendor/allwinnertech/chips/a733/a733_usb_camera.c
  vendor/allwinnertech/chips/a733/a733_combo0_usb_tables.inc
  vendor/allwinnertech/chips/a733/a733_uart4.c
  vendor/allwinnertech/chips/a733/a733_i2s0_audio.c
  vendor/allwinnertech/chips/a733/a733_header_peripherals.c
  vendor/allwinnertech/chips/a733/a733_sdmmc.c
  vendor/allwinnertech/chips/a733/CMakeLists.txt
  vendor/allwinnertech/chips/a733/Kconfig
  vendor/allwinnertech/chips/a733/include/chip.h
  vendor/allwinnertech/chips/a733/include/irq.h
  vendor/allwinnertech/chips/a733/include/a733_peripherals.h
  vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig
  vendor/allwinnertech/boards/a733/cubie-a7z/Kconfig
  vendor/allwinnertech/boards/a733/cubie-a7z/src/CMakeLists.txt
  vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_boardinit.c
  vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_st7735.c
  apps/system/a733wifi/a733wifi_main.c
  apps/system/a733wifi/Kconfig
  apps/system/a733services/a733services_main.c
  apps/system/a733services/services_config.c
  apps/netutils/ftpd/Kconfig
  apps/netutils/ftpd/ftpd.c
  apps/netutils/ftpd/ftpd.h
  apps/netutils/ntpclient/Kconfig
  apps/netutils/ntpclient/ntpclient.c
  apps/system/a733display/CMakeLists.txt
  apps/system/a733display/Kconfig
  apps/system/a733display/a733display_main.c
)
patched_files=(
  nuttx/arch/arm64/include/arch.h
  nuttx/drivers/lcd/st7735.c
  packages/ai_agent/src/agent_main.c
  packages/ai_agent/CMakeLists.txt
  packages/ai_agent/src/infra/vela_tls.c
  packages/ai_agent/src/tools/tool_registry.c
  packages/ai_agent/src/core/agent_loop.c
  packages/ai_agent/src/core/session_mgr.c
  packages/ai_agent/src/tools/skill_loader.c
  packages/ai_agent/src/infra/config_store.c
  apps/system/readline/readline_common.c
)
staged_files=()
aipet_saved=false
pet_saved=false
aipet_relative=apps/system/aipetllm
aipet_official="$official/$aipet_relative"
aipet_overlay="$overlay/$aipet_relative"

case "$aipet_official" in
  "$official"/apps/system/aipetllm) ;;
  *) echo "Refusing unsafe AI Pet staging path: $aipet_official" >&2; exit 1 ;;
esac

same_path()
{
  [[ -e "$1" && -e "$2" ]] || return 1
  [[ "$(readlink -f "$1")" == "$(readlink -f "$2")" ]]
}

restore_official()
{
  local path
  for path in "${staged_files[@]}"; do
    if [[ -f "$backup/$path" ]]; then
      cp -f "$backup/$path" "$official/$path"
    else
      rm -f "$official/$path"
    fi
  done

  for path in "${patched_files[@]}"; do
    if [[ -f "$backup/$path" ]]; then
      cp -f "$backup/$path" "$official/$path"
    fi
  done

  if [[ "$aipet_saved" == true ]]; then
    rm -rf "$aipet_official"
    if [[ -d "$backup/$aipet_relative" ]]; then
      cp -a "$backup/$aipet_relative" "$aipet_official"
    fi
  fi

  if [[ "$pet_saved" == true ]]; then
    rm -rf "$official/apps/system/aipet"
    if [[ -d "$backup/apps/system/aipet" ]]; then
      cp -a "$backup/apps/system/aipet" "$official/apps/system/aipet"
    fi
  fi

  rm -rf "$backup"
}

trap restore_official EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for path in "${files[@]}"; do
  if same_path "$overlay/$path" "$official/$path"; then
    continue
  fi

  mkdir -p "$backup/$(dirname "$path")"
  if [[ -f "$official/$path" ]]; then
    cp "$official/$path" "$backup/$path"
  fi

  staged_files+=("$path")
  mkdir -p "$official/$(dirname "$path")"
  cp -f "$overlay/$path" "$official/$path"
done


for path in "${patched_files[@]}"; do
  mkdir -p "$backup/$(dirname "$path")"
  cp "$official/$path" "$backup/$path"
done

# Apply a patch idempotently.
#
# `patch --forward` exits 1 (not 0) when the tree already contains the change:
# it reports "Reversed (or previously applied) patch detected!  Skipping patch."
# Under `set -e` that aborted the whole script right after staging, so a repeat
# build never reached the compiler.
#
# The undo step is wrapped in a --dry-run as well, and only performed when the
# dry run reports zero fuzz and zero failures.  Reverse-applying these patches
# blindly is not safe: several hunks carry inaccurate counts (for example
# `@@ -114,1 +115,5 @@` in ai-agent-provisioning.patch actually adds six lines),
# so a partial reverse silently duplicates lines (a second `read_done:` label
# and an undeclared `value`) instead of removing them.
apply_patch()
{
  local name="$1"
  local rc=0
  local probe

  # `patch` is unusable for state detection by exit code: the reverse dry run
  # returns 0 whether or not the change is present.  Only the message text
  # distinguishes the two states:
  #   already applied -> "checking file ..."                (clean, no warning)
  #   not applied     -> "Unreversed patch detected!  Ignoring -R."
  # A probe that reports FAILED/offset/fuzz cannot be trusted either way, so
  # only a provably clean reverse is allowed to run.  Getting this wrong is
  # destructive: reversing an applied patch and then failing to re-apply it
  # silently built the kernel WITHOUT the panel init sequence (v122).
  probe="$(patch -R --dry-run --batch --no-backup-if-mismatch -p1 \
    -d "$official" < "$team_dir/patches/$name" 2>&1)" || true

  if [[ "$probe" == *"Unreversed patch detected"* ]]; then
    : # not applied - nothing to undo
  elif [[ "$probe" == *"FAILED"* || "$probe" == *"offset"* ||
          "$probe" == *"fuzz"* || "$probe" == *"hunk"* ]]; then
    echo "Build trap: cannot determine state of $name; refusing to undo it." >&2
    echo "$probe" >&2
    exit 1
  else
    patch -R --batch --no-backup-if-mismatch -p1 -d "$official" \
      < "$team_dir/patches/$name" >/dev/null 2>&1 || true
  fi

  patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
    < "$team_dir/patches/$name" || rc=$?

  if ((rc >= 2)); then
    echo "Build trap: $name failed to apply (patch exit $rc)" >&2
    exit 1
  fi

  # The --dry-run probes above leave .rej files behind, so clear them.
  find "$official" -name '*.rej' -not -path '*/.repo/*' -delete 2>/dev/null || true
}

# Assert that a patch really did land.
#
# Every patch below used to be applied blind: `patch` exiting 1 for an ignored
# hunk is not fatal, so the build happily continued with the change MISSING.
# That is exactly how v122 shipped a kernel whose st7735 driver had no panel
# init sequence at all, while the serial log looked perfectly healthy.
require_contains()
{
  local file="$1"
  local needle="$2"
  local what="$3"

  if ! grep -q -- "$needle" "$official/$file"; then
    echo "Build trap: $file does not contain '$needle' after patching." >&2
    echo "  ($what)" >&2
    exit 1
  fi
}

apply_patch nuttx-arm64-a733-aff1-cpuid.patch
apply_patch nuttx-st7735-werror.patch
apply_patch lvgl-nuttx-lcd-release-free.patch

# Assert the two patches that are invisible at runtime when missing.
require_contains nuttx/drivers/lcd/st7735.c st7735_initseq \
  "panel init sequence (FRMCTR/PWCTR/VMCTR/gamma) must be sent at startup"
require_contains nuttx/drivers/lcd/st7735.c st7735_cmd1 \
  "single-parameter command helper used by the init sequence"

if same_path "$aipet_overlay" "$aipet_official"; then
  aipet_saved=false
elif [[ -d "$aipet_official" ]]; then
  mkdir -p "$backup/$(dirname "$aipet_relative")"
  cp -a "$aipet_official" "$backup/$aipet_relative"
  aipet_saved=true
  rm -rf "$aipet_official"
  cp -a "$aipet_overlay" "$aipet_official"
else
  aipet_saved=true
  cp -a "$aipet_overlay" "$aipet_official"
fi

if same_path "$overlay/apps/system/aipet" "$official/apps/system/aipet"; then
  pet_saved=false
elif [[ -d "$official/apps/system/aipet" ]]; then
  cp -a "$official/apps/system/aipet" "$backup/apps/system/aipet"
  pet_saved=true
  rm -rf "$official/apps/system/aipet"
  cp -a "$overlay/apps/system/aipet" "$official/apps/system/aipet"
else
  pet_saved=true
  mkdir -p "$official/apps/system"
  cp -a "$overlay/apps/system/aipet" "$official/apps/system/aipet"
fi
apply_patch ai-agent-a733-pet-and-http.patch
apply_patch ai-agent-single-instance.patch
apply_patch ai-agent-provisioning.patch
apply_patch readline-utf8-backspace.patch

cd "$official"
export PATH="$official/prebuilts/build-tools/linux-x86_64/bin:$official/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$official/prebuilts/tools/linux-x86_64:$official/prebuilts/tools/cmake/bin:$official/prebuilts/tools/ninja:/usr/bin:/bin:${PATH:-}"

if [[ "${1:-}" == "--incremental" ]]; then
  if [[ "${BUILD_KEEP_GOING:-0}" == 1 ]]; then
    cmake --build "$build" -j"${JOBS:-8}" -- -k 0
  else
    cmake --build "$build" -j"${JOBS:-8}"
  fi
else
  # build.sh preserves an existing CMake cache.  Remove only this board's
  # generated directory so defconfig changes (notably CONFIG_SMP) cannot be
  # silently ignored by a stale .config.
  case "$build" in
    "$official"/cmake_out/*)
      rm -rf "$build"
      ;;
    *)
      echo "Refusing unsafe build cleanup path: $build" >&2
      exit 1
      ;;
  esac

  ./nuttx/tools/build.sh \
    vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh \
    --cmake -b "$build" -j"${JOBS:-8}"
fi

grep -E 'CONFIG_(NETDEV_WIRELESS_IOCTL|WIRELESS_WAPI|WIRELESS_WAPI_CMDTOOL)' \
  "$build/.config"

grep -qx 'CONFIG_SMP=y' "$build/.config"
expected_ncpus=$(sed -n 's/^CONFIG_SMP_NCPUS=//p' \
  "$overlay/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig")
[[ "$expected_ncpus" =~ ^[2-8]$ ]]
grep -qx "CONFIG_SMP_NCPUS=$expected_ncpus" "$build/.config"
grep -qx 'CONFIG_ARCH_HAVE_MULTICPU=y' "$build/.config"
for display_config in \
  CONFIG_BOARD_A7Z_ST7735=y \
  CONFIG_SPI_CMDDATA=y \
  CONFIG_LCD_ST7735=y \
  CONFIG_LCD_ST7735_GM11=y \
  CONFIG_GRAPHICS_LVGL=y \
  CONFIG_LV_USE_NUTTX_LCD=y \
  CONFIG_SYSTEM_A733DISPLAY=y
do
  grep -qx "$display_config" "$build/.config"
done
sha256sum "$build/nuttx.bin"
