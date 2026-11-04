#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Maintainer-only reproducible candidate build; ordinary workspace builds use
# build-a733-wapi.sh. Never overwrite the exact v142 ELF/image recovery point.
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
if [[ -f "$team/patches/bluetooth-zblue-reopen.patch" ]]; then
  echo 'This checkout contains v144 changes; use build-a733-ble-v144.sh.' >&2
  echo 'Reproduce v143 from tag a733-ble-v143-lifecycle-candidate in a separate checkout.' >&2
  exit 1
fi
official=$(cd "${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}" && pwd)
build="$official/cmake_out/cubie-a7z_nsh_v143_ble_lifecycle"
evidence="$team/docs/evidence"
snapshot=$(mktemp -d /tmp/a733-v143-restore.XXXXXX)
trap 'rm -rf -- "$snapshot"' EXIT
cd "$team"
mkdir -p "$evidence"
if [[ -e "$build/nuttx" && "${1:-}" != --incremental ]]; then
  echo "v143 ELF already exists; use --incremental or select a new version" >&2
  exit 1
fi
mapfile -t paths < <(awk '/^files=\(/{copy=1;next} /^patched_files=\(/{copy=1;next} copy && /^\)/{copy=0;next} copy{gsub(/^ +/,"");print}' tools/build-a733-wapi.sh)
for app in aipet aipetllm; do
  while IFS= read -r path; do paths+=("${path#"$official/"}"); done < <(find "$official/apps/system/$app" -type f 2>/dev/null || true)
  while IFS= read -r path; do paths+=("${path#"$team/board/a733-cubie-a7z/openvela-overlay/"}"); done < <(find "$team/board/a733-cubie-a7z/openvela-overlay/apps/system/$app" -type f)
done
mapfile -t paths < <(printf '%s\n' "${paths[@]}" | sort -u)
for path in "${paths[@]}"; do
  if [[ -f "$official/$path" ]]; then
    sha256sum "$official/$path"
  else
    printf 'ABSENT  %s\n' "$official/$path"
  fi
done > "$snapshot/before"
bash tools/test-bttool-console.sh 2>&1 | tee "$evidence/20261014-v143-console-tests.log"
bash tools/test-bttool-lifecycle.sh 2>&1 | tee "$evidence/20261014-v143-lifecycle-tests.log"
bash tools/test-fat-filepath.sh 2>&1 | tee "$evidence/20261014-v143-fat-tests.log"
set +e
A733_BUILD_DIR="$build" OPENVELA_WORKSPACE="$official" JOBS="${JOBS:-12}" \
  bash tools/build-a733-wapi.sh "${1:-}" 2>&1 | tee "$evidence/20261014-v143-build.log"
result=${PIPESTATUS[0]}
set -e
for path in "${paths[@]}"; do
  if [[ -f "$official/$path" ]]; then sha256sum "$official/$path"; else printf 'ABSENT  %s\n' "$official/$path"; fi
done > "$snapshot/after"
cmp "$snapshot/before" "$snapshot/after"
{ printf 'Official source restoration: PASS (%s paths)\n' "${#paths[@]}"; cat "$snapshot/after"; } \
  | tee "$evidence/20261014-v143-official-restore.log"
exit "$result"
