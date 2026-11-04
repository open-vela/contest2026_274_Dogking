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
# Keep originals on the project filesystem: WSL /tmp may disappear when
# the distro exits after an interrupted build. Failed cleanup keeps this.
mkdir -p "$team_dir/out/sdk-staging"
backup="$(mktemp -d "$team_dir/out/sdk-staging/a733-build-staging.XXXXXX")"
printf 'official=%s\nbuild=%s\n' "$official" "$build" > "$backup/locations.txt"
echo "SDK staging backup: $backup"

bash "$script_dir/check-openvela-first.sh"

files=(
  nuttx/sched/init/nx_start.c
  nuttx/sched/init/nx_bringup.c
  vendor/allwinnertech/boards/a733/cubie-a7z/scripts/dramboot.ld
  nuttx/arch/arm64/src/common/arm64_fatal.c
  vendor/allwinnertech/chips/a733/a733_boot.c
  vendor/allwinnertech/chips/a733/a733_image_header.S
  vendor/allwinnertech/chips/a733/a733_hwdiag.c
  vendor/allwinnertech/chips/a733/a733_ufs.c
  vendor/allwinnertech/chips/a733/a733_ufs_wire.h
  vendor/allwinnertech/chips/a733/a733_ufs_link.c
  vendor/allwinnertech/chips/a733/a733_ufs_link.h
  vendor/allwinnertech/chips/a733/a733_ufs_board.c
  vendor/allwinnertech/chips/a733/a733_ufs_board.h
  vendor/allwinnertech/chips/a733/a733_ufs_handoff.c
  vendor/allwinnertech/chips/a733/a733_ufs_handoff.h
  vendor/allwinnertech/chips/a733/a733_ufs_quiesce.c
  vendor/allwinnertech/chips/a733/a733_ufs_quiesce.h
  vendor/allwinnertech/chips/a733/a733_ufs_nop.c
  vendor/allwinnertech/chips/a733/a733_ufs_nop.h
  vendor/allwinnertech/chips/a733/a733_ufs_nop_map.c
  vendor/allwinnertech/chips/a733/a733_ufs_readcheck.c
  vendor/allwinnertech/chips/a733/a733_ufs_readcheck.h
  vendor/allwinnertech/chips/a733/a733_ufs_scratch.c
  vendor/allwinnertech/chips/a733/a733_ufs_scratch.h
  vendor/allwinnertech/chips/a733/a733_ufs_write_wire.h
  vendor/allwinnertech/chips/a733/a733_ufs_data.c
  vendor/allwinnertech/chips/a733/a733_ufs_data.h
  vendor/allwinnertech/chips/a733/a733_ufs_volume.h
  vendor/allwinnertech/chips/a733/a733_wifi_usb.c
  vendor/allwinnertech/chips/a733/a733_bt_checkpoint.inc
  vendor/allwinnertech/chips/a733/a733_bt_transport.inc
  vendor/allwinnertech/chips/a733/a733_hci_framer.h
  vendor/allwinnertech/chips/a733/a733_usb_camera.c
  vendor/allwinnertech/chips/a733/a733_camera_board.c
  vendor/allwinnertech/chips/a733/a733_camera_board.h
  vendor/allwinnertech/chips/a733/a733_camera_diag.c
  vendor/allwinnertech/chips/a733/a733_imx214_probe.c
  vendor/allwinnertech/chips/a733/a733_imx214_probe.h
  vendor/allwinnertech/chips/a733/a733_imx214_twi3.c
  vendor/allwinnertech/chips/a733/a733_imx214_twi3.h
  vendor/allwinnertech/chips/a733/a733_imx214_cold.c
  vendor/allwinnertech/chips/a733/a733_imx214_cold.h
  vendor/allwinnertech/chips/a733/a733_camera_control.c
  vendor/allwinnertech/chips/a733/a733_camera_stream.inc
  vendor/allwinnertech/chips/a733/a733_mipi_rx.c
  vendor/allwinnertech/chips/a733/a733_mipi_rx.h
  vendor/allwinnertech/chips/a733/a733_csi_capture.c
  vendor/allwinnertech/chips/a733/a733_csi_capture.h
  vendor/allwinnertech/chips/a733/a733_camera_power_handoff.h
  vendor/allwinnertech/chips/a733/a733_imx214_mode.c
  vendor/allwinnertech/chips/a733/a733_imx214_mode.h
  vendor/allwinnertech/chips/a733/a733_csi_frame_plan.c
  vendor/allwinnertech/chips/a733/a733_csi_frame_plan.h
  vendor/allwinnertech/chips/a733/a733_combo0_usb_tables.inc
  vendor/allwinnertech/chips/a733/a733_uart4.c
  vendor/allwinnertech/chips/a733/a733_i2s0_audio.c
  vendor/allwinnertech/chips/a733/a733_i2s0_playback.inc
  vendor/allwinnertech/chips/a733/a733_audio_client.c
  vendor/allwinnertech/chips/a733/a733_npu.c
  vendor/allwinnertech/chips/a733/a733_npu_memory.c
  vendor/allwinnertech/chips/a733/a733_npu_runtime.c
  vendor/allwinnertech/chips/a733/a733_npu_live.inc
  vendor/allwinnertech/chips/a733/a733_crc32_fast.h
  vendor/allwinnertech/chips/a733/a733_tensor_plan.c
  vendor/allwinnertech/chips/a733/a733_tensor_runtime.inc
  vendor/allwinnertech/chips/a733/a733_tensor_crc.inc
  vendor/allwinnertech/chips/a733/a733_tensor_ufs_path.h
  vendor/allwinnertech/chips/a733/a733_tensor_client.c
  vendor/allwinnertech/chips/a733/include/a733_npu_internal.h
  vendor/allwinnertech/chips/a733/include/a733_npu_memory.h
  vendor/allwinnertech/chips/a733/include/a733_tensor_plan.h
  nuttx/include/nuttx/npu/a733_vip2.h
  nuttx/include/nuttx/npu/a733_tensor.h
  apps/include/system/a733_tensor_client.h
  apps/include/system/a733_crc32_fast.h
  nuttx/include/sys/a733_audio.h
  apps/include/system/a733_pcm.h
  apps/include/system/a733_storage.h
  packages/ai_agent/src/voice/aipet_tts_validate.h
  vendor/allwinnertech/chips/a733/a733_header_peripherals.c
  vendor/allwinnertech/chips/a733/CMakeLists.txt
  vendor/allwinnertech/chips/a733/Kconfig
  vendor/allwinnertech/chips/a733/include/chip.h
  vendor/allwinnertech/chips/a733/include/irq.h
  vendor/allwinnertech/chips/a733/include/a733_peripherals.h
  vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig
  vendor/allwinnertech/boards/a733/cubie-a7z/include/board.h
  vendor/allwinnertech/boards/a733/cubie-a7z/Kconfig
  vendor/allwinnertech/boards/a733/cubie-a7z/src/CMakeLists.txt
  vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_boardinit.c
  vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_st7735.c
  apps/system/a733wifi/a733wifi_main.c
  apps/system/a733wifi/wifi_secret_input.h
  apps/system/a733wifi/Kconfig
  apps/system/a733services/a733services_main.c
  apps/system/a733services/services_config.c
  apps/system/a733npu/CMakeLists.txt
  apps/system/a733npu/Kconfig
  apps/system/a733npu/a733npu_main.c
  apps/system/a733display/CMakeLists.txt
  apps/system/a733display/Kconfig
  apps/system/a733display/a733display_main.c
  apps/system/a733display/aipet_ui_ipc.h
  apps/include/system/a733_vision.h
  apps/include/system/a733_camera_image.h
  apps/include/system/a733_camera_live.h
  apps/system/a733camera/CMakeLists.txt
  apps/system/a733camera/Kconfig
  apps/system/a733camera/Make.defs
  apps/system/a733camera/Makefile
  apps/system/a733camera/a733camera_main.c
  apps/system/a733camera/a733_camera_image.c
  apps/system/a733camera/a733_camera_live.c
  apps/system/a733camera/a733_camera_pipeline.inc
    apps/include/system/a733_cpu_qualify.h
)
patched_files=(
  nuttx/libs/libc/netdb/lib_dnsquery.c
  nuttx/sched/init/nx_smpstart.c
  nuttx/libs/libc/misc/lib_cxx_initialize.c
  nuttx/arch/arm64/src/common/arm64_head.S
  nuttx/arch/arm64/src/common/arm64_cpustart.c
  apps/nshlib/nsh_ddcmd.c
  external/zblue/zblue/subsys/bluetooth/host/hci_core.c
  external/zblue/zblue/subsys/bluetooth/host/scan.c
  frameworks/connectivity/bluetooth/service/stacks/zephyr/hci_h4.c
  frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_le_scan_interface.c
  nuttx/fs/fat/fs_fat32.c
  nuttx/fs/fat/fs_fat32.h
  frameworks/connectivity/bluetooth/tools/bt_tools.c
  frameworks/connectivity/bluetooth/tools/scan.c
  frameworks/connectivity/bluetooth/service/src/adapter_state.c
  frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_adapter_le_interface.c
  frameworks/connectivity/bluetooth/service/src/connection_manager.c
  external/zblue/zblue/port/kernel/poll.c
  external/zblue/CMakeLists.default.txt
  external/zblue/zblue/subsys/bluetooth/host/hci_core.h
  external/zblue/zblue/subsys/bluetooth/host/id.c
  external/zblue/zblue/port/kernel/thread.c
  frameworks/connectivity/bluetooth/CMakeLists.txt
  frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_adapter_interface.c
  frameworks/connectivity/bluetooth/service/stacks/zephyr/sal_connection_manager.c
  frameworks/connectivity/bluetooth/service/utils/btsnoop_writer.c
  nuttx/drivers/serial/uart_bth4.c
  nuttx/arch/arm64/include/arch.h
  nuttx/drivers/lcd/st7735.c
  packages/ai_agent/src/agent_main.c
  packages/ai_agent/CMakeLists.txt
  packages/ai_agent/src/infra/vela_tls.c
  packages/ai_agent/src/tools/tool_registry.c
  packages/ai_agent/src/core/agent_loop.c
  packages/ai_agent/src/llm/llm_proxy.c
  packages/ai_agent/src/core/session_mgr.c
  packages/ai_agent/src/tools/skill_loader.c
  packages/ai_agent/src/infra/config_store.c
  packages/ai_agent/src/voice/volc_tts.c
  packages/ai_agent/src/voice/volc_asr.c
  apps/system/readline/readline_common.c
)
staged_files=()
aipet_saved=false
pet_saved=false
home_saved=false
home_official="$official/apps/system/a733home"
home_source="$team_dir/app/a733home"
storage_saved=false
storage_official="$official/apps/system/a733storage"
storage_source="$team_dir/app/a733storage"
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
  local failed=0
  # A second signal must not terminate child copies during EXIT cleanup.
  trap '' INT TERM
  for path in "${staged_files[@]}"; do
    if [[ -f "$backup/$path" ]]; then
      cp -f "$backup/$path" "$official/$path" || failed=1
    else
      rm -f "$official/$path" || failed=1
    fi
  done

  for path in "${patched_files[@]}"; do
    if [[ -f "$backup/$path" ]]; then
      cp -f "$backup/$path" "$official/$path" || failed=1
    fi
  done

  if [[ "$aipet_saved" == true ]]; then
    restore_staged_app "$aipet_relative" || failed=1
  fi

  if [[ "$pet_saved" == true ]]; then
    restore_staged_app apps/system/aipet || failed=1
  fi

  if [[ "$home_saved" == true ]]; then
    # Fixed, workspace-scoped target, never a user-supplied directory.
    [[ "$home_official" == "$official/apps/system/a733home" ]] || failed=1
    restore_staged_app apps/system/a733home || failed=1
  fi
  if [[ "$storage_saved" == true ]]; then
    [[ "$storage_official" == "$official/apps/system/a733storage" ]] || failed=1
    restore_staged_app apps/system/a733storage || failed=1
  fi

  if [[ "$failed" != 0 ]]; then
    echo "SDK RESTORATION FAILED; originals retained at: $backup" >&2
    return 1
  fi
  rm -rf -- "$backup"
}

restore_staged_app()
{
  local relative="$1"
  case "$relative" in
    apps/system/aipet|apps/system/aipetllm|apps/system/a733home|apps/system/a733storage) ;;
    *) return 1 ;;
  esac
  [[ -d "$official/apps/system" && -d "$backup" ]] || return 1
  rm -rf -- "$official/$relative" || return 1
  if [[ -e "$backup/$relative" || -L "$backup/$relative" ]]; then
    cp -a "$backup/$relative" "$official/$relative" || return 1
  fi
}

trap 'a733_exit_status=$?; restore_official || exit 1; exit "$a733_exit_status"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
if ! same_path "$storage_source" "$storage_official"; then
  [[ "$storage_official" == "$official/apps/system/a733storage" ]] || exit 1
  [[ ! -e "$storage_official" || -d "$storage_official" || -L "$storage_official" ]] || exit 1
  mkdir -p "$backup/apps/system"
  if [[ -e "$storage_official" || -L "$storage_official" ]]; then
    cp -a "$storage_official" "$backup/apps/system/a733storage"
  fi
  storage_saved=true
  rm -rf -- "$storage_official"
  cp -a "$storage_source" "$storage_official"
fi
if ! same_path "$home_source" "$home_official"; then
  [[ "$home_official" == "$official/apps/system/a733home" ]] || exit 1
  [[ ! -e "$home_official" || -d "$home_official" || -L "$home_official" ]] || {
    echo "Refusing to replace a non-directory application target: $home_official" >&2
    exit 1
  }
  mkdir -p "$backup/apps/system"
  if [[ -e "$home_official" || -L "$home_official" ]]; then
    cp -a "$home_official" "$backup/apps/system/a733home"
  fi
  home_saved=true
  rm -rf -- "$home_official"
  cp -a "$home_source" "$home_official"
fi
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

patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/nuttx-arm64-a733-aff1-cpuid.patch"
if [[ "${A733_BOOT_DIAGNOSTICS:-0}" == 1 ]]; then
  patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
    < "$team_dir/patches/nuttx-a733-boot-progress-v188.patch"
fi
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/nuttx-st7735-werror.patch"
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/nsh-dd-large-seek.patch"

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
# Restored/checkpointed overlay files can be older than cached objects while
# containing different bytes. v178 forces ONLY these disposable app copies
# dirty; never touch the source checkout or a shared SDK application in place.
if [[ "${A733_FORCE_LOCAL_LLM_REBUILD:-0}" == 1 ]]; then
  [[ "$aipet_saved" == true && "$pet_saved" == true ]] || {
    echo 'Forced LLM rebuild requires separate, restorable staging copies' >&2
    exit 1
  }
  find "$aipet_official" "$official/apps/system/aipet" -maxdepth 1 -type f \
    \( -name '*.c' -o -name '*.cxx' -o -name '*.h' -o -name '*.hxx' -o -name '*.inc' \) \
    -exec touch -- {} +
  echo 'v178: forced local LLM/app staged timestamps; source bytes unchanged'
fi
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/ai-agent-a733-pet-channel.patch"
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/ai-agent-http-completion.patch"
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/ai-agent-single-instance.patch"
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/ai-agent-provisioning.patch"
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/ai-agent-tts-complete.patch"
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/ai-agent-asr-guard.patch"
if [[ "${A733_CLOUD_LATENCY_V227:-0}" == 1 ]]; then
  python3 "$team_dir/tools/apply-cloud-latency-v227.py" "$official/packages/ai_agent/src/llm/llm_proxy.c"
fi
if [[ "${A733_CLOUD_TRANSPORT_V229:-0}" == 1 ]]; then
  python3 "$team_dir/tools/apply-dns-close-v229.py" "$official/nuttx/libs/libc/netdb/lib_dnsquery.c"
  printf '\nCONFIG_NETDB_MAX_IPv4ADDR=4\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
patch --forward --batch --no-backup-if-mismatch -p1 -d "$official" \
  < "$team_dir/patches/readline-utf8-backspace.patch"

if grep -qx 'CONFIG_BLUETOOTH=y' \
  "$overlay/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"; then
  patch --forward --batch --no-backup-if-mismatch --reject-file=- -p1 -d "$official" \
    < "$team_dir/patches/bluetooth-a733-build.patch"
  patch --forward --batch --no-backup-if-mismatch --reject-file=- -p1 -d "$official" \
    < "$team_dir/patches/bluetooth-bth4-reopen.patch"
  patch --forward --batch --no-backup-if-mismatch --reject-file=- -p1 -d "$official" \
    < "$team_dir/patches/bluetooth-ble-only-build.patch"
  patch --forward --batch --no-backup-if-mismatch --reject-file=- -p1 -d "$official" \
    < "$team_dir/patches/zblue-a733-compat.patch"
  patch --forward --batch --no-backup-if-mismatch --reject-file=- -p1 -d "$official" \
    < "$team_dir/patches/bluetooth-cli-init-recovery.patch"
  patch --forward --batch --no-backup-if-mismatch --reject-file=- -p1 -d "$official" \
    < "$team_dir/patches/bluetooth-ble-lifecycle.patch"
  patch --forward --batch --no-backup-if-mismatch --reject-file=- -p1 -d "$official" \
    < "$team_dir/patches/bluetooth-zblue-reopen.patch"
  patch --forward --batch --no-backup-if-mismatch --reject-file=- -p1 -d "$official" \
    < "$team_dir/patches/nuttx-fat-filepath.patch"
fi

cd "$official"
if [[ "${A733_BOOT_DIAGNOSTICS:-0}" == 1 ]]; then
  [[ "${A733_GENERAL_VOICE_EXPERIMENTAL:-0}" == 1 ]]
  printf '\nCONFIG_A733_BOOT_DIAGNOSTICS=y\nCONFIG_BOARD_LATE_INITIALIZE=y\nCONFIG_BOARD_INITTHREAD_STACKSIZE=262144\nCONFIG_BOARD_INITTHREAD_PRIORITY=100\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_CAMERA_EXPERIMENTAL:-0}" == 1 ]]; then
  # Passive CCU/PIO report and FILE preparation only; no sensor/CSI writes.
  printf '\nCONFIG_A733_IMX214_CHECKPOINT=y\nCONFIG_SYSTEM_A733CAMERA=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_CAMERA_CONTROL_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_CAMERA_EXPERIMENTAL:-0}" == 1 ]]
  printf '\nCONFIG_A733_CAMERA_CONTROL_TEST=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_CAMERA_LIVE_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_CAMERA_CONTROL_EXPERIMENTAL:-0}" == 1 ]]
  printf '\nCONFIG_FS_TMPFS=y\nCONFIG_A733_CAMERA_LIVE_TEST=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_CAMERA_PERF_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_CAMERA_LIVE_EXPERIMENTAL:-0}" == 1 ]]
  printf '\nCONFIG_A733_CAMERA_PERF_TEST=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_TENSOR_EXPERIMENTAL:-0}" == 1 ]]; then
  # Only the temporarily staged SDK defconfig changes. The normal board
  # default remains disabled and cleanup restores the SDK source verbatim.
  printf '\nCONFIG_A733_NPU_TENSOR_IO=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_GENERAL_ASR_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_TENSOR_EXPERIMENTAL:-0}" == 1 ]] || {
    echo "General ASR memory profile requires A733_TENSOR_EXPERIMENTAL=1" >&2
    exit 1
  }
  printf '\nCONFIG_A733_NPU_GENERAL_ASR=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_CPU_ACOUSTIC_EXPERIMENTAL:-0}" == 1 ]]; then
  # ORT uses stream formatting and wstring_view. Build the matching libc++
  # implementation too; never undefine feature guards for headers alone.
  printf '\n# CONFIG_CXX_NO_LOCALIZATION is not set\nCONFIG_CXX_MINI_LOCALIZATION=y\nCONFIG_CXX_WCHAR=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_GENERAL_VOICE_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_GENERAL_ASR_EXPERIMENTAL:-0}" == 1 && "${A733_CPU_ACOUSTIC_EXPERIMENTAL:-0}" == 1 ]]
  [[ -n "${A733_ORT_STATIC_PREFIX:-}" && -f "$A733_ORT_STATIC_PREFIX/manifest.json" ]]
  printf '\nCONFIG_A733_CPU_ACOUSTIC=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_UFS_EXPERIMENTAL:-0}" == 1 ]]; then
  if [[ "${A733_PRODUCT_V1:-0}" == 1 ]]; then
    [[ "${A733_GENERAL_VOICE_EXPERIMENTAL:-0}" == 1 && "${A733_CAMERA_LIVE_EXPERIMENTAL:-0}" == 1 ]]
    printf '\nCONFIG_A733_PRODUCT_V1=y\n' >> \
      "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
    if [[ "${A733_PRODUCT_V12:-0}" == 1 ]]; then
      printf '\nCONFIG_A733_PRODUCT_V12=y\n' >> \
        "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
    fi
  fi
  printf '\nCONFIG_A733_UFS_READONLY=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_UFS_HANDOFF_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_UFS_EXPERIMENTAL:-0}" == 1 ]] || {
    echo "UFS handoff probe requires A733_UFS_EXPERIMENTAL=1" >&2
    exit 1
  }
  printf '\nCONFIG_A733_UFS_HANDOFF_PROBE=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_UFS_QUIESCE_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_UFS_HANDOFF_EXPERIMENTAL:-0}" == 1 ]] || {
    echo "UFS quiesce requires A733_UFS_HANDOFF_EXPERIMENTAL=1" >&2
    exit 1
  }
  printf '\nCONFIG_A733_UFS_QUIESCE=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_UFS_DMA_NOP_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_UFS_QUIESCE_EXPERIMENTAL:-0}" == 1 ]] || {
    echo "UFS DMA NOP requires A733_UFS_QUIESCE_EXPERIMENTAL=1" >&2
    exit 1
  }
  printf '\nCONFIG_A733_UFS_DMA_NOP=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_UFS_READCHECK_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_UFS_DMA_NOP_EXPERIMENTAL:-0}" == 1 ]] || {
    echo "UFS readcheck requires A733_UFS_DMA_NOP_EXPERIMENTAL=1" >&2
    exit 1
  }
  printf '\nCONFIG_A733_UFS_READCHECK=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_UFS_BLOCK_RO_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_UFS_READCHECK_EXPERIMENTAL:-0}" == 1 ]] || {
    echo 'UFS block-ro requires explicit READCHECK experiment' >&2; exit 1;
  }
  printf '\nCONFIG_A733_UFS_BLOCK_RO=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_UFS_SCRATCH_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_UFS_BLOCK_RO_EXPERIMENTAL:-0}" == 1 ]] || {
    echo 'UFS scratch requires explicit BLOCK_RO experiment' >&2; exit 1;
  }
  printf '\nCONFIG_A733_UFS_SCRATCH=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
if [[ "${A733_UFS_DATA_EXPERIMENTAL:-0}" == 1 ]]; then
  [[ "${A733_UFS_SCRATCH_EXPERIMENTAL:-0}" == 1 ]] || {
    echo 'UFS data requires explicit SCRATCH experiment' >&2; exit 1;
  }
  printf '\nCONFIG_A733_UFS_DATA=y\nCONFIG_SYSTEM_A733STORAGE=y\n' >> \
    "$official/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"
fi
export PATH="$official/prebuilts/build-tools/linux-x86_64/bin:$official/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$official/prebuilts/tools/linux-x86_64:$official/prebuilts/tools/cmake/bin:$official/prebuilts/tools/ninja:/usr/bin:/bin:${PATH:-}"

# Fail before a full compilation if Kconfig silently disabled the bulk app.
# The final config and linked ufs_main checks remain mandatory as well.
if [[ "${A733_UFS_DATA_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -q 'depends on CRYPTO_MBEDTLS && FS_FAT && A733_UFS_DATA' "$storage_official/Kconfig"
fi

if [[ "${1:-}" == "--incremental" ]]; then
  # An interrupted CMake regeneration may leave the cache/objects intact
  # but remove build.ninja. Recover only an explicitly selected matching
  # Ninja cache, inside this same staged/restored SDK transaction. Never
  # delete the build directory or silently configure a different source.
  if [[ "${A733_REGENERATE_CACHE:-0}" == 1 ]]; then
    test -f "$build/CMakeCache.txt"
    grep -qx 'CMAKE_GENERATOR:INTERNAL=Ninja' "$build/CMakeCache.txt"
    grep -Fxq "CMAKE_HOME_DIRECTORY:INTERNAL=$official/nuttx" "$build/CMakeCache.txt"
    cmake -S "$official/nuttx" -B "$build"
  fi
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
if [[ "${A733_BOOT_DIAGNOSTICS:-0}" == 1 ]]; then
  for config in CONFIG_A733_BOOT_DIAGNOSTICS=y CONFIG_BOARD_LATE_INITIALIZE=y \
    CONFIG_BOARD_INITTHREAD_STACKSIZE=262144 CONFIG_BOARD_INITTHREAD_PRIORITY=100 \
    CONFIG_NSH_ARCHINIT=y CONFIG_LIBC_MAX_EXITFUNS=0; do
    grep -qx "$config" "$build/.config"
  done
fi
expected_ncpus=$(sed -n 's/^CONFIG_SMP_NCPUS=//p' \
  "$overlay/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig")
[[ "$expected_ncpus" =~ ^[2-8]$ ]]
grep -qx "CONFIG_SMP_NCPUS=$expected_ncpus" "$build/.config"
grep -qx 'CONFIG_ARCH_HAVE_MULTICPU=y' "$build/.config"
if [[ "${A733_CAMERA_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_IMX214_CHECKPOINT=y' "$build/.config"
  grep -qx 'CONFIG_SYSTEM_A733CAMERA=y' "$build/.config"
fi
if [[ "${A733_TENSOR_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_NPU_TENSOR_IO=y' "$build/.config"
fi
if [[ "${A733_GENERAL_ASR_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_NPU_GENERAL_ASR=y' "$build/.config"
fi
if [[ "${A733_CPU_ACOUSTIC_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_CXX_MINI_LOCALIZATION=y' "$build/.config"
  grep -qx 'CONFIG_CXX_WCHAR=y' "$build/.config"
fi
if [[ "${A733_GENERAL_VOICE_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_CPU_ACOUSTIC=y' "$build/.config"
fi
if [[ "${A733_PRODUCT_V1:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_PRODUCT_V1=y' "$build/.config" || {
    echo 'STOP: product v1 sources are NOT enabled in actual .config; no product acceptance' >&2
    exit 1
  }
fi
if [[ "${A733_UFS_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_UFS_READONLY=y' "$build/.config"
fi
if [[ "${A733_UFS_HANDOFF_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_UFS_HANDOFF_PROBE=y' "$build/.config"
fi
if [[ "${A733_UFS_QUIESCE_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_UFS_QUIESCE=y' "$build/.config"
fi
if [[ "${A733_UFS_DMA_NOP_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_UFS_DMA_NOP=y' "$build/.config"
fi
if [[ "${A733_UFS_READCHECK_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_UFS_READCHECK=y' "$build/.config"
fi
for display_config in \
  CONFIG_BOARD_A7Z_ST7735=y \
  CONFIG_SPI_CMDDATA=y \
  CONFIG_LCD_ST7735=y \
  CONFIG_LCD_ST7735_GM11=y \
  CONFIG_GRAPHICS_LVGL=y \
  CONFIG_LV_USE_NUTTX_LCD=y \
  CONFIG_SYSTEM_A733DISPLAY=y \
  CONFIG_A733_I2S0_AUDIO=y
do
  grep -qx "$display_config" "$build/.config"
done
if [[ "${A733_UFS_BLOCK_RO_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_UFS_BLOCK_RO=y' "$build/.config"
fi
sha256sum "$build/nuttx.bin"
if [[ "${A733_UFS_SCRATCH_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_UFS_SCRATCH=y' "$build/.config"
fi
bash "$script_dir/check-a733-elf-layout.sh" "$build/nuttx" "$build/nuttx.bin"
if [[ "${A733_UFS_DATA_EXPERIMENTAL:-0}" == 1 ]]; then
  grep -qx 'CONFIG_A733_UFS_DATA=y' "$build/.config"
  grep -qx 'CONFIG_SYSTEM_A733STORAGE=y' "$build/.config"
fi

if grep -qx 'CONFIG_DRIVERS_BLUETOOTH=y' \
  "$overlay/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig"; then
  for bt_config in \
    CONFIG_64BIT=y \
    CONFIG_KVDB=y \
    CONFIG_KVDB_SERVER=y \
    CONFIG_DRIVERS_BLUETOOTH=y \
    CONFIG_UART_BTH4=y \
    CONFIG_LIBUV_EXTENSION=y \
    CONFIG_UNQLITE=y \
    CONFIG_BLUETOOTH_SERVICE=y \
    CONFIG_BLUETOOTH_SERVER=y \
    CONFIG_BLUETOOTH_STACK_LE_ZBLUE=y \
    CONFIG_BLUETOOTH_GATT_CLIENT=y \
    CONFIG_BT_CENTRAL=y \
    CONFIG_BT_H4=y
  do
    grep -qx "$bt_config" "$build/.config"
  done
fi
