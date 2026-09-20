#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
team_dir="$(cd "$script_dir/.." && pwd)"
overlay_apps="$team_dir/board/a733-cubie-a7z/openvela-overlay/apps"
product_roots=(
  "$overlay_apps/system/aipetllm"
  "$overlay_apps/system/aipet"
  "$overlay_apps/system/aipetasr"
  "$overlay_apps/ai"
)

# Product applications may use the generated configuration header, but must
# not reach into NuttX-private driver/arch headers or board diagnostic nodes.
# Hardware-specific code belongs under vendor/allwinnertech, behind a public
# openvela/NuttX device or framework ABI.
violations=0

check_source()
{
  local source="$1"

  if grep -nE '#[[:space:]]*include[[:space:]]*[<"]nuttx/' "$source" |
     grep -vE 'nuttx/config\.h' >/dev/null; then
    echo "openvela-boundary: private NuttX include in $source" >&2
    grep -nE '#[[:space:]]*include[[:space:]]*[<"]nuttx/' "$source" |
      grep -vE 'nuttx/config\.h' >&2 || true
    violations=1
  fi

  if grep -nE '(/dev/a733-|/dev/a7z-)' "$source" >/dev/null; then
    echo "openvela-boundary: board diagnostic node in product app $source" >&2
    grep -nE '(/dev/a733-|/dev/a7z-)' "$source" >&2 || true
    violations=1
  fi
}

for product_root in "${product_roots[@]}"; do
  [[ -d "$product_root" ]] || continue
  while IFS= read -r source; do
    check_source "$source"
  done < <(find "$product_root" -type f \
    \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
       -o -name '*.h' -o -name '*.hpp' \) \
    ! -path '*/third_party/*' -print)
done

if ((violations != 0)); then
  echo "openvela-boundary: FAILED" >&2
  exit 1
fi

echo "openvela-boundary: passed (official public APIs above vendor BSP)"
