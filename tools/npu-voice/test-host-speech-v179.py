#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Real CPU ONNX oracle vs portable ASR decoder / Matcha text frontend.

Uses existing local packages/models only. No pip, downloads, sockets, board,
VIPLite, reset or cloud. Output is a NEW directory. Logical ONNX tensors are
not native NBG golden layouts or an openvela prepared package.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import queue
import struct
import subprocess
import sys
import threading
import time
import wave
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REFS = REPO.parent / 'references'
BUILD = REPO / 'out/voice-core-v179'
sys.path.insert(0, str(REPO.parent.parent / '.hostdeps-voice-batch'))
import numpy as np
import onnxruntime as ort


def sha(p):
    h = hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()


def load_module(name, p):
    spec = importlib.util.spec_from_file_location(name, p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def unix_path(p):
    s = str(p.resolve()).replace('\\', '/')
    return '/mnt/' + s[0].lower() + s[2:] if os.name == 'nt' else s


def command(p, *args):
    argv = [unix_path(p), *map(str, args)]
    return ['wsl', '--exec', *argv] if os.name == 'nt' else argv


class Bridge:
    def __init__(self, executable, *args):
        self.p = subprocess.Popen(command(executable, *args), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.q = queue.Queue()
        self.buf = bytearray()

        def reader():
            try:
                while True:
                    data = self.p.stdout.read1(65536)
                    if not data:
                        break
                    self.q.put(data)
            finally:
                self.q.put(None)
        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()

    def read(self, n):
        deadline = time.monotonic() + 60
        while len(self.buf) < n:
            try:
                data = self.q.get(timeout=max(.001, deadline - time.monotonic()))
            except queue.Empty:
                self.p.kill()
                raise TimeoutError('host oracle bridge; no retry')
            if data is None:
                raise RuntimeError('host bridge EOF: ' + self.p.stderr.read().decode(errors='replace'))
            self.buf.extend(data)
        data = bytes(self.buf[:n]); del self.buf[:n]
        return data

    def write(self, data):
        self.p.stdin.write(data); self.p.stdin.flush()

    def close(self):
        self.p.stdin.close()
        rc = self.p.wait(timeout=30)
        self.reader.join(timeout=2)
        error = self.p.stderr.read().decode(errors='replace')
        if rc:
            raise RuntimeError('host bridge exit: ' + str(rc) + ' ' + error)


def session(p):
    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    options.inter_op_num_threads = 1
    return ort.InferenceSession(str(p), options, providers=['CPUExecutionProvider'])


def tts_test(out):
    model = REFS / 'matcha-icefall-zh-baker'
    locked = {'model-steps-3.onnx': 'ff1cc7f7abdedc4eeec668a4a0dc2e72a0c314c5637d5fce885345b490bd08de',
              'tokens.txt': '56209b2bf609d5ac1d66ede6dae7bf5254bd3f8aa24c4a6823713d5b884d87ba',
              'lexicon.txt': '38b886d46aefa50da6322a64d72fd595d5f4fae1051adb160d647541b1e0a4a2'}
    for name, h in locked.items():
        assert sha(model / name) == h, name
    refpath = REFS / 'cubie-a7a-voice-assistant-main/tools/matcha_npu.py'
    ref = load_module('matcha_reference', refpath)
    tokens = ref.load_tokens(model / 'tokens.txt')
    lexicon = ref.load_lexicon(model / 'lexicon.txt', tokens)
    bridge = Bridge(BUILD / 'tts-host', unix_path(model / 'tokens.txt'), unix_path(model / 'lexicon.txt'))
    initial = bridge.read(4)
    assert struct.unpack('<i', initial)[0] == 0, ('frontend init', repr(initial))
    texts = ('你好', '测试成功', '你好，测试成功。', '今天心情很好！', '前进', '左转，右转。',
             '重庆银行', '中华人民共和国', '晚安，好梦。', '谢谢你', '123', '音量50')
    records = []
    acoustic = session(model / 'model-steps-3.onnx')
    for index, text in enumerate(texts):
        data = text.encode('utf8'); bridge.write(struct.pack('<I', len(data)) + data)
        rc = struct.unpack('<i', bridge.read(4))[0]
        assert rc == 0, (text, rc)
        actual = []
        count = struct.unpack('<I', bridge.read(4))[0]
        assert 0 < count <= 64
        for _ in range(count):
            n = struct.unpack('<I', bridge.read(4))[0]
            assert 0 < n <= 1025
            actual.append(np.frombuffer(bridge.read(n * 8), dtype='<i8'))
        normalized = ''.join('零一二三四五六七八九'[int(c)] if c.isascii() and c.isdigit() else c for c in text)
        expected = ref.text_to_sentences(normalized, tokens, lexicon)
        assert len(actual) == len(expected), text
        assert all(np.array_equal(a, b) for a, b in zip(actual, expected)), text
        record = {'text': text, 'ids': [a.tolist() for a in actual], 'exact_reference': True}
        if index < 2:
            feeds = {}
            for v in acoustic.get_inputs():
                if v.name == 'x': feeds[v.name] = actual[0].reshape(1, -1)
                elif v.name in ('x_length', 'x_lengths'): feeds[v.name] = np.array([len(actual[0])], dtype=np.int64)
                elif v.name in ('noise_scale', 'length_scale'): feeds[v.name] = np.array([1], dtype=np.float32)
                else: raise ValueError('Unknown acoustic input: ' + v.name)
            start = time.monotonic()
            mel = acoustic.run(None, feeds)[0]
            assert mel.ndim == 3 and mel.shape[:2] == (1, 80) and 0 < mel.shape[2] <= 4000
            assert np.isfinite(mel).all()
            path = out / ('matcha-' + str(index) + '.f32')
            path.write_bytes(mel.astype('<f4').tobytes())
            record.update(mel_shape=list(mel.shape), mel_sha256=sha(path),
                          cpu_seconds=time.monotonic() - start, npu=False)
        records.append(record)
        print('TTS frontend exact:', text, flush=True)
    for raw in (b'\xf0\x80\x80\x80', '你好🦊'.encode(), b'hello\0world'):
        bridge.write(struct.pack('<I', len(raw)) + raw)
        assert struct.unpack('<i', bridge.read(4))[0] < 0
    bridge.write(bytes(4)); bridge.close()
    return {'model_sha256': locked, 'reference_source_sha256': sha(refpath), 'cases': records,
            'target_acoustic_runtime': False, 'full_tts': False}


def asr_test(out):
    lock = load_module('source_lock', Path(__file__).with_name('fetch-asr-source.py'))
    source = REFS / 'zipformer-asr-upstream-e238275'
    lock.verify(source)
    models = {role: session(source / next(n for n in lock.FILES if n.startswith(role)))
              for role in ('encoder', 'decoder', 'joiner')}
    enc, dec, join = (models[r] for r in ('encoder', 'decoder', 'joiner'))
    metadata = enc.get_modelmeta().custom_metadata_map
    assert metadata['T'] == '103' and metadata['decode_chunk_len'] == '96'
    assert len(enc.get_inputs()) == len(enc.get_outputs()) == 36
    tokpath = REFS / 'awnpu_model_zoo-v1.1.0-20260910-675a8dcb/examples/zipformer/model/tokens.txt'
    assert sha(tokpath) == 'a8e0e4ec53810e433789b54a5c0134a7eaa2ffca595a6334d54c00da858841d3'
    tokens = {}
    for line in tokpath.read_text(encoding='utf8').splitlines():
        text, value = line.rsplit(' ', 1); value = int(value)
        assert value not in tokens
        tokens[value] = text
    assert set(tokens) == set(range(6257))
    records = []
    for case in ('cmd_open_light_16k', 'cmd_time_16k', 'neg_silence_3s_16k'):
        wav = REFS / 'cubie-a7a-voice-assistant-main/samples' / (case + '.wav')
        with wave.open(str(wav), 'rb') as f:
            assert f.getnchannels() == 1 and f.getsampwidth() == 2 and f.getframerate() == 16000
            samples = f.getnframes(); raw = f.readframes(samples)
        assert 0 < samples < 140000 and len(raw) == samples * 2
        folder = out / case; folder.mkdir()
        pcm = folder / 'tail-padded.s16'; features = folder / 'features.f32'
        # Real zero waveform through the same frontend, not arbitrary zero mel.
        pcm.write_bytes(raw + bytes(103 * 160 * 2))
        subprocess.run(command(BUILD / 'export-fbank', unix_path(pcm), unix_path(features)),
                       check=True, timeout=60, capture_output=True)
        feats = np.fromfile(features, dtype='<f4').reshape(-1, 80)
        state = {}
        for value in enc.get_inputs()[1:]:
            shape = [1 if d == 'N' else d for d in value.shape]
            assert all(isinstance(d, int) and d > 0 for d in shape), value.name
            dtype = np.int64 if value.type == 'tensor(int64)' else np.float32
            assert value.type in ('tensor(int64)', 'tensor(float)')
            state[value.name] = np.zeros(shape, dtype=dtype)
        rows, chunks = [], []
        real_frames = (samples + 80) // 160
        for start in range(0, real_frames, 96):
            feeds = {'x': feats[start:start + 103].reshape(1, 103, 80), **state}
            outputs = enc.run(None, feeds)
            assert outputs[0].shape == (1, 24, 512)
            assert all(np.isfinite(a).all() for a in outputs)
            chunks.append({'start': start, 'inputs': {k: hashlib.sha256(v.tobytes()).hexdigest() for k, v in feeds.items()},
                           'outputs': {v.name: hashlib.sha256(a.tobytes()).hexdigest() for v, a in zip(enc.get_outputs(), outputs)}})
            if start < 192:
                d = folder / ('chunk-' + str(start)); d.mkdir()
                for i, a in enumerate(feeds.values()): (d / ('input-' + str(i) + '.bin')).write_bytes(a.tobytes())
                for i, a in enumerate(outputs): (d / ('output-' + str(i) + '.bin')).write_bytes(a.tobytes())
            for value, a in zip(enc.get_inputs()[1:], outputs[1:]):
                assert a.shape == state[value.name].shape and a.dtype == state[value.name].dtype
                state[value.name] = a
            rows.append(outputs[0][0])
        all_rows = np.concatenate(rows)
        # Independent Python greedy reference, not a recording of C decisions.
        context = [0, 0]
        d = dec.run(None, {'y': np.array([context], dtype=np.int64)})[0].reshape(512)
        expected, positions = [], []
        for i, e in enumerate(all_rows):
            logits = join.run(None, {'encoder_out': e.reshape(1, 512), 'decoder_out': d.reshape(1, 512)})[0]
            assert logits.shape == (1, 6254) and np.isfinite(logits).all()
            best = int(logits.argmax())
            if best not in (0, 2):
                assert best != 1
                expected.append(best); positions.append(i); context = [context[1], best]
                d = dec.run(None, {'y': np.array([context], dtype=np.int64)})[0].reshape(512)
        bridge = Bridge(BUILD / 'asr-host')

        def service():
            while True:
                op = bridge.read(1)
                if op == b'D':
                    context = np.frombuffer(bridge.read(16), dtype='<i8').reshape(1, 2)
                    result = dec.run(None, {'y': context})[0].astype('<f4')
                    assert result.shape == (1, 512)
                    bridge.write(bytes(4) + result.tobytes())
                elif op == b'J':
                    pair = np.frombuffer(bridge.read(4096), dtype='<f4').reshape(2, 512)
                    result = join.run(None, {'encoder_out': pair[0:1], 'decoder_out': pair[1:2]})[0].astype('<f4')
                    assert result.shape == (1, 6254)
                    bridge.write(bytes(4) + result.tobytes())
                elif op in (b'K', b'R'):
                    return op
                elif op == b'E':
                    raise ValueError('C decoder failed: ' + str(struct.unpack('<i', bridge.read(4))[0]))
                else:
                    raise ValueError('Unknown bridge opcode ' + repr(op))
        # Open asks for the initial decoder first; service it before any frames.
        assert bridge.read(1) == b'D'
        context = np.frombuffer(bridge.read(16), dtype='<i8').reshape(1, 2)
        assert (context == 0).all()
        bridge.write(bytes(4) + dec.run(None, {'y': context})[0].astype('<f4').tobytes())
        for e in rows:
            bridge.write(struct.pack('<I', 24) + e.astype('<f4').tobytes())
            assert service() == b'K'
        bridge.write(bytes(4)); assert service() == b'R'
        n = struct.unpack('<I', bridge.read(4))[0]; assert n <= 1024
        result = np.frombuffer(bridge.read(n * 8), dtype='<u4').reshape(n, 2)
        bridge.close()
        assert result[:, 0].tolist() == expected and result[:, 1].tolist() == positions
        text = ''.join(tokens[t] for t in expected).replace('▁', ' ').strip()
        if case.startswith('neg_silence'): assert text == '', text
        else: assert text, 'unexpected empty real-speech recognition'
        record = {'case': case, 'source_sha256': sha(wav), 'text': text, 'token_ids': expected,
                  'frame_positions': positions, 'logical_onnx_outputs': chunks,
                  'c_python_exact': True, 'npu_executed': False,
                  'transcript_human_accepted': False}
        (folder / 'result.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf8')
        records.append(record)
        print('CPU ASR / C decoder exact:', case, repr(text), flush=True)
    return {'revision': lock.REVISION, 'token_sha256': sha(tokpath), 'cases': records,
            'native_nbg_golden': False, 'board_accepted': False}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    p.add_argument('--only', choices=('asr', 'tts', 'both'), default='both')
    p.add_argument('--build', type=Path, default=BUILD,
                   help='Existing host helper directory; never overwrites the v179 evidence')
    args = p.parse_args()
    BUILD = args.build.resolve()
    args.output.mkdir(parents=False, exist_ok=False)
    result = {'onnxruntime': ort.__version__, 'host_only': True,
              'board_executed': False, 'full_offline_chain': False}
    if args.only in ('tts', 'both'): result['tts'] = tts_test(args.output)
    if args.only in ('asr', 'both'): result['asr'] = asr_test(args.output)
    (args.output / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print('PASS: host semantic oracle only; no RTOS/NPU/full voice acceptance', flush=True)
