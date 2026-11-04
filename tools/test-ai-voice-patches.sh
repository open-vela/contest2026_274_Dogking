#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
official=${OPENVELA_WORKSPACE:-$repo/../../quickly-openvela}
tmp=$(mktemp -d /tmp/aipet-voice-patches.XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/packages/ai_agent/src/voice"
cp "$official/packages/ai_agent/src/voice/"{volc_asr,volc_tts}.c "$tmp/packages/ai_agent/src/voice/"
patch --batch --forward --fuzz=0 -p1 -d "$tmp" < "$repo/patches/ai-agent-asr-guard.patch"
patch --batch --forward --fuzz=0 -p1 -d "$tmp" < "$repo/patches/ai-agent-tts-complete.patch"
python3 - "$tmp" <<'PY'
import sys
from pathlib import Path
p=Path(sys.argv[1])/'packages/ai_agent/src/voice'
asr=(p/'volc_asr.c').read_text()
assert 'MBEDTLS_SSL_VERIFY_OPTIONAL' not in asr
assert 'MBEDTLS_SSL_VERIFY_REQUIRED' in asr and 'mbedtls_x509_crt_parse_file' in asr
assert 'malloc(WS_BUF_SIZE + 1)' in asr
assert asr.count('if (ret != 1) { text_out[0] = 0; return -ETIMEDOUT; }')==2
# Final-result checks must follow the batch final receive and streaming free,
# not accidentally match an earlier error-cleanup block in a minimal patch.
assert asr.index('if (ret != 1)')>asr.index('int max_attempts = 100')
assert asr.rindex('if (ret != 1)')>asr.index('/* Receive responses until final */')
assert asr.index('if (time(NULL)')>asr.index('mbedtls_ctr_drbg_init')
tts=(p/'volc_tts.c').read_text()
assert 'resp, response_cap, &body_len' in tts and 'aipet_tts_validate(resp' in tts
assert 'aipet_tts_validate(resp, pcm_out, pcm_cap, &total_pcm)' in tts
batch=tts[tts.index('static int volc_tts_synthesize('):tts.index('/* ── Streaming TTS')]
assert 'using %zu bytes decoded so far' not in batch and 'PCM buffer full' not in batch
print('PASS: exact official voice patches, CA required, full-result gates, RX terminator allocation, complete TTS validation; not cloud runtime proof')
PY
