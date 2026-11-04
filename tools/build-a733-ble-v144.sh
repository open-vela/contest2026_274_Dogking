#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
if [[ -f "$team/app/a733home/a733home_main.c" ]]; then
  echo 'This checkout contains v145 changes; use build-a733-ble-v145.sh.' >&2
  echo 'Reproduce v144 from a733-ble-v144-lifecycle-5cycle-pass in a separate checkout.' >&2
  exit 1
fi
official=$(cd "${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}" && pwd)
build="$official/cmake_out/cubie-a7z_nsh_v144_ble_reopen"
evidence="$team/docs/evidence"
snapshot=$(mktemp -d /tmp/a733-v144-restore.XXXXXX)
trap 'rm -rf -- "$snapshot"' EXIT
cd "$team"
mkdir -p "$evidence"
if [[ -e "$build/nuttx" && "${1:-}" != --incremental ]]; then
  echo 'v144 ELF already exists; use --incremental or select a new version' >&2
  exit 1
fi
mapfile -t paths < <(awk '/^files=\(/{copy=1;next} /^patched_files=\(/{copy=1;next} copy && /^\)/{copy=0;next} copy{gsub(/^ +/,"");print}' tools/build-a733-wapi.sh)
for app in aipet aipetllm; do
  while IFS= read -r path; do paths+=("${path#"$official/"}"); done < <(find "$official/apps/system/$app" -type f 2>/dev/null || true)
  while IFS= read -r path; do paths+=("${path#"$team/board/a733-cubie-a7z/openvela-overlay/"}"); done < <(find "$team/board/a733-cubie-a7z/openvela-overlay/apps/system/$app" -type f)
done
mapfile -t paths < <(printf '%s\n' "${paths[@]}" | sort -u)
for path in "${paths[@]}"; do
  if [[ -f "$official/$path" ]]; then sha256sum "$official/$path"; else printf 'ABSENT  %s\n' "$official/$path"; fi
done > "$snapshot/before"
for test in bttool-console bttool-lifecycle zblue-reopen fat-filepath; do
  bash "tools/test-$test.sh" 2>&1 | tee "$evidence/20261014-v144-$test-tests.log"
done
set +e
A733_BUILD_DIR="$build" OPENVELA_WORKSPACE="$official" JOBS="${JOBS:-12}" \
  bash tools/build-a733-wapi.sh "${1:-}" 2>&1 | tee "$evidence/20261014-v144-build.log"
result=${PIPESTATUS[0]}
set -e
for path in "${paths[@]}"; do
  if [[ -f "$official/$path" ]]; then sha256sum "$official/$path"; else printf 'ABSENT  %s\n' "$official/$path"; fi
done > "$snapshot/after"
cmp "$snapshot/before" "$snapshot/after"
{ printf 'Official source restoration: PASS (%s paths)\n' "${#paths[@]}"; cat "$snapshot/after"; } \
  | tee "$evidence/20261014-v144-official-restore.log"
exit "$result"
