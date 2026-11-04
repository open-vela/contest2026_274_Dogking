#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Package locked real-audio Linux native ASR tests, no hardware execution.

Private/local kit includes vendor libraries and models; do not publish it.
Requires actual native --inspect logs and the Bullseye-sysroot runner build.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tarfile
import wave

REPO = Path(__file__).resolve().parents[2]


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--inspection', type=Path, required=True)
    parser.add_argument('--runner', type=Path, required=True)
    a = parser.parse_args()
    assert not a.output.exists() and not a.output.with_suffix('.tar.gz').exists()
    spec = importlib.util.spec_from_file_location('native_asr', Path(__file__).with_name('asr-native-v183.py'))
    native = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(native)
    contract = {r: native.metadata((a.inspection / (r + '-inspect.log')).read_text())
                for r in ('encoder', 'decoder', 'joiner')}
    native.qualify(contract)
    a.output.mkdir()
    out = a.output
    (out / 'models').mkdir()
    (out / 'lib').mkdir()
    (out / 'fixtures').mkdir()
    (out / 'inspection').mkdir()
    export = REPO / 'out/asr-export-v178-20261025'
    report = json.loads((export / 'artifacts-verified.json').read_text())
    for m in report['models']:
        entry = m['nbg'][0]
        src = export / entry['path']
        assert src.stat().st_size == entry['bytes'] and digest(src) == entry['sha256']
        shutil.copyfile(src, out / 'models' / (m['role'] + '.nb'))
        shutil.copyfile(a.inspection / (m['role'] + '-inspect.log'),
                        out / 'inspection' / (m['role'] + '.log'))
    sdk = REPO.parent / 'references/awnpu_model_zoo-v1.1.0-20260910-675a8dcb'
    for name in ('libNBGlinker.so', 'libVIPhal.so'):
        shutil.copyfile(sdk / 'common/npuruntime/lib_linux_aarch64/A733' / name, out / 'lib' / name)
    tokens = sdk / 'examples/zipformer/model/tokens.txt'
    assert digest(tokens) == 'a8e0e4ec53810e433789b54a5c0134a7eaa2ffca595a6334d54c00da858841d3'
    shutil.copyfile(tokens, out / 'tokens.txt')
    shutil.copyfile(a.runner, out / 'voice-native')
    (out / 'voice-native').chmod(0o755)
    shutil.copyfile(Path(__file__).with_name('asr-native-v183.py'), out / 'asr-native-v183.py')
    cases = []
    # These fixtures were independently checked against all production online
    # frontend/state outputs in v181. Input is real normalized PCM, not zero mel.
    gold = json.loads((REPO / 'docs/evidence/local-voice-v179/host-speech-summary.json').read_text(encoding='utf8'))
    for expected in gold['asr']['cases']:
        name = expected['case']
        source = REPO / 'out/host-speech-v179-r5' / name
        wav = REPO.parent / 'references/cubie-a7a-voice-assistant-main/samples' / (name + '.wav')
        assert digest(wav) == expected['source_sha256']
        features = source / 'features.f32'
        raw = features.read_bytes()
        native.floats(raw)
        with wave.open(str(wav), 'rb') as f:
            assert (f.getframerate(), f.getnchannels(), f.getsampwidth()) == (16000, 1, 2)
            samples = f.getnframes()
        folder = out / 'fixtures' / name
        folder.mkdir()
        shutil.copyfile(features, folder / 'features.f32')
        shutil.copyfile(wav, folder / 'source.wav')
        assert hashlib.sha256(raw[:103 * 320]).hexdigest() == expected['logical_onnx_outputs'][0]['inputs']['x']
        cases.append(dict(case=name, feature_bytes=len(raw), feature_sha256=digest(features),
                          source_sha256=digest(wav), real_frames=(samples + 80) // 160,
                          text=expected['text'], token_ids=expected['token_ids'],
                          frame_positions=expected['frame_positions']))
    native.record(out / 'contract.json', contract)
    native.record(out / 'fixtures.json', cases)
    (out / 'README.txt').write_text('''PRIVATE LOCAL TEST KIT: not for redistribution.
Linux real-fixture NPU ASR evidence only. NOT an openvela voice image/provider.
Stop all other /dev/vipcore users. No reset, retry, flashing or audio playback.

python3 asr-native-v183.py .
sudo python3 asr-native-v183.py . --run --npu-idle-confirmed --output results-NEW

Every native input/output is retained. Encoder state comes from its own 35
native outputs; speech, time question and silence are distinct fixtures.
The CPU oracle supplies expected text for comparison only, never NPU outputs.
Do not infer resident inference latency from the deliberately slow file/process
diagnostic. A successful Linux result is not an RTOS/native-layout acceptance.
Preserve the entire results folder, including errors. Do not rerun after an
accelerator failure until the device state has been checked manually.
''', encoding='utf8')
    files = {str(p.relative_to(out)).replace('\\', '/'): dict(bytes=p.stat().st_size, sha256=digest(p))
             for p in out.rglob('*') if p.is_file()}
    native.record(out / 'manifest.json', dict(schema=183, files=files, full_voice_chain=False,
                                             npu_executed=False, target_cid='1000003b'))
    native.check(out)
    archive = out.with_suffix('.tar.gz')
    def portable_modes(info):
        # Windows chmod does not encode POSIX executable permission in tar.
        info.mode = 0o755 if info.isdir() or info.name.endswith('/voice-native') else 0o644
        return info
    with tarfile.open(archive, 'x:gz') as t:
        t.add(out, arcname=out.name, filter=portable_modes)
    with tarfile.open(archive, 'r:gz') as t:
        assert t.getmember(out.name + '/voice-native').mode == 0o755
    print(json.dumps(dict(archive=str(archive), bytes=archive.stat().st_size, sha256=digest(archive)), indent=2))


if __name__ == '__main__':
    main()
