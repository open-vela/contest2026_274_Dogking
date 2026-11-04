#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
team=$(cd "$(dirname "$0")/.." && pwd)
official=$(cd "${OPENVELA_WORKSPACE:-$team/../../quickly-openvela}" && pwd)
build="$official/cmake_out/cubie-a7z_nsh_v145_gatt_client"
evidence="$team/docs/evidence"
snapshot=$(mktemp -d /tmp/a733-v145-restore.XXXXXX)
trap 'rm -rf -- "$snapshot"' EXIT
cd "$team"
mkdir -p "$evidence"
if [[ -e "$build/nuttx" && "${1:-}" != --incremental ]]; then
  echo 'v145 ELF exists; use --incremental or select a new version' >&2; exit 1
fi
mapfile -t paths < <(awk '/^files=\(/{copy=1;next} /^patched_files=\(/{copy=1;next} copy && /^\)/{copy=0;next} copy{gsub(/^ +/,"");print}' tools/build-a733-wapi.sh)
for app in aipet aipetllm a733home; do
  source="$team/board/a733-cubie-a7z/openvela-overlay/apps/system/$app"
  [[ "$app" != a733home ]] || source="$team/app/a733home"
  while IFS= read -r path; do paths+=("${path#"$official/"}"); done < <(find "$official/apps/system/$app" -type f 2>/dev/null || true)
  while IFS= read -r path; do paths+=("apps/system/$app/${path#"$source/"}"); done < <(find "$source" -type f)
done
mapfile -t paths < <(printf '%s\n' "${paths[@]}" | sort -u)
snapshot_source()
{
  local path
  for path in "${paths[@]}"; do
    if [[ -f "$official/$path" ]]; then sha256sum "$official/$path"; else printf 'ABSENT  %s\n' "$official/$path"; fi
  done
  if [[ -L "$official/apps/system/a733home" ]]; then
    printf 'HOME_SYMLINK '; readlink "$official/apps/system/a733home"
  elif [[ -d "$official/apps/system/a733home" ]]; then echo 'HOME_DIRECTORY';
  elif [[ ! -e "$official/apps/system/a733home" ]]; then echo 'HOME_ABSENT';
  else echo 'HOME_NOT_DIRECTORY'; return 1; fi
}
snapshot_source > "$snapshot/before"
for test in home-client bttool-console bttool-lifecycle zblue-reopen fat-filepath; do
  bash "tools/test-$test.sh" 2>&1 | tee "$evidence/20261015-v145-$test-tests.log"
done
set +e
A733_BUILD_DIR="$build" OPENVELA_WORKSPACE="$official" JOBS="${JOBS:-12}" \
  bash tools/build-a733-wapi.sh "${1:-}" 2>&1 | tee "$evidence/20261015-v145-build.log"
result=${PIPESTATUS[0]}
set -e
snapshot_source > "$snapshot/after"
cmp "$snapshot/before" "$snapshot/after"
{ printf 'Official source restoration: PASS (%s paths + home directory identity)\n' "${#paths[@]}"; cat "$snapshot/after"; } \
  | tee "$evidence/20261015-v145-official-restore.log"
exit "$result"
