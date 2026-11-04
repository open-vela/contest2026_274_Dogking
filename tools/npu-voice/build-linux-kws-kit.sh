#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Run on the development Linux/WSL host; no network, no board/NPU calls.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
[[ $# == 1 ]] || { printf 'Usage: bash build-linux-kws-kit.sh NEW_ABSOLUTE_OUTPUT_DIRECTORY\n' >&2; exit 2; }
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
parent=$(cd -- "$repo/.." && pwd -P)
target=$1
[[ "$target" == /* && "$target" != / && ! -e "$target" && ! -L "$target" &&
   ! -e "$target.tar.gz" && ! -e "$target-host-build" ]] || { printf 'Output must be new and absolute.\n' >&2; exit 2; }
mkdir -- "$target" "$target-host-build"
target=$(cd -- "$target" && pwd -P)
build="$target-host-build"
mkdir -p "$target/lib" "$target/models/kws" "$target/models/vocoder" "$target/licenses"
sdk="$parent/references/awnpu_model_zoo-v1.1.0-20260910-675a8dcb/common/npuruntime/lib_linux_aarch64/A733"
reference="$parent/references/cubie-a7a-voice-assistant-main"
pet="$repo/board/a733-cubie-a7z/openvela-overlay/apps/system/aipet"
knf="$pet/third_party/kaldi-native-fbank"
cp -- "$repo/out/npu-voice-bullseye/voice-golden-linux-aarch64" "$target/"
cp -- "$sdk/libNBGlinker.so" "$sdk/libVIPhal.so" "$target/lib/"
cp -- "$reference/prebuilt/kws/encoder_float_a733.nb" "$reference/prebuilt/kws/decoder_float_a733.nb" \
  "$reference/prebuilt/kws/joiner_float_a733.nb" "$target/models/kws/"
cp -- "$reference/prebuilt/vocoder/vocoder_int16_a733.nb" "$target/models/vocoder/"
cp -- "$reference/prebuilt/kws/tokens.txt" "$target/"
for file in test-linux-voice.sh test-linux-kws.sh stream-kws.py joiner-fixtures.py kws-contract.json LINUX_BOARD_CHECK.md; do
  cp -- "$repo/tools/npu-voice/$file" "$target/"
done
cp -- "$repo/tools/npu-voice/LINUX_KWS_CHECK.md" "$target/README.md"
cp -- "$knf/LICENSE" "$target/licenses/kaldi-native-fbank-LICENSE"
cp -- "$knf/PROVENANCE.json" "$target/licenses/kaldi-native-fbank-PROVENANCE.json"
find "$knf" -type f -iname '*license*' -print > "$build/license-source-list.txt"
# Include every license source (including Ooura FFT notices) by source-relative path.
while IFS= read -r license; do
  relative=${license#"$knf"/}
  mkdir -p -- "$target/licenses/kaldi-native-fbank/$(dirname -- "$relative")"
  cp -- "$license" "$target/licenses/kaldi-native-fbank/$relative"
done < "$build/license-source-list.txt"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter \
  -I"$pet" -I"$knf/csrc/include" "$repo/tools/npu-voice/export-fbank.cxx" \
  "$pet/voice_fbank.cxx" "$knf"/csrc/src/*.cc -lm -o "$build/export-fbank"
python3 "$repo/tools/npu-voice/prepare-kws.py" "$reference/samples/wake_nihao_xiaorui_16k.wav" \
  "$build/export-fbank" "$target/fixtures"
# Hash-verify all seven unchanged native assets before finalizing the private kit.
source "$repo/tools/npu-voice/test-linux-voice.sh"
base="$target" work="$build"
verify_assets
python3 "$repo/tools/npu-voice/make-stream-manifest.py" "$target"
python3 "$target/stream-kws.py" check "$target"
tar -czf "$target.tar.gz" -C "$(dirname -- "$target")" "$(basename -- "$target")"
sha256sum "$target.tar.gz"
printf 'Private local kit (not for redistribution): %s.tar.gz\nHost build retained: %s\n' "$target" "$build"
