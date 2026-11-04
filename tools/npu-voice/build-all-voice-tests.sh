#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Local development Linux/WSL host only, never runs an accelerator.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
[[ $# == 2 ]] || { printf 'Usage: bash build-all-voice-tests.sh NEW_ABSOLUTE_KIT HOST_PREPARED_TTS_DIRECTORY\n' >&2; exit 2; }
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
parent=$(cd -- "$repo/.." && pwd -P)
target=$1
tts=$(cd -- "$2" && pwd -P)
[[ "$target" == /* && ! -e "$target" && ! -L "$target" && ! -e "$target.tar.gz" &&
   ! -e "$target-foundation" && ! -e "$target-host-build" ]] || { printf 'Use new output paths.\n' >&2; exit 2; }
bash "$repo/tools/npu-voice/build-linux-kws-kit.sh" "$target-foundation"
mkdir -- "$target" "$target-host-build"
target=$(cd -- "$target" && pwd -P)
for item in lib models licenses fixtures; do cp -a -- "$target-foundation/$item" "$target/"; done
cp -- "$target-foundation/voice-golden-linux-aarch64" "$target/"
for file in test-linux-voice.sh test-linux-voice-all.sh stream-kws.py joiner-fixtures.py kws-contract.json \
            run-voice-batch.py keyword-reference.py vocoder-contract.json LINUX_BOARD_CHECK.md; do
  cp -- "$repo/tools/npu-voice/$file" "$target/"
done
reference="$parent/references/cubie-a7a-voice-assistant-main"
cp -- "$reference/prebuilt/kws/tokens.txt" "$target/"
cp -- "$reference/keywords_wake.txt" "$target/keywords.txt"
cp -- "$reference/NOTICE" "$target/licenses/reference-NOTICE"
cp -- "$repo/tools/npu-voice/LINUX_VOICE_ALL.md" "$target/README.md"
cp -a -- "$tts" "$target/tts-fixtures"
python3 "$repo/tools/npu-voice/prepare-voice-batch.py" features "$reference" \
  "$target-foundation-host-build/export-fbank" "$target/batch-fixtures"
make -C "$repo/tools/npu-voice" SYSROOT="$parent/a733-viplite-golden/bullseye-sysroot" \
  OUT="$target-host-build" BINARY=voice-repeat-linux-aarch64
cp -- "$target-host-build/voice-repeat-linux-aarch64" "$target/"
python3 "$repo/tools/npu-voice/make-stream-manifest.py" "$target"
python3 "$target/run-voice-batch.py" check "$target"
tar -czf "$target.tar.gz" -C "$(dirname -- "$target")" "$(basename -- "$target")"
sha256sum "$target.tar.gz"
printf 'PRIVATE local available-asset batch: %s.tar.gz\n' "$target"
