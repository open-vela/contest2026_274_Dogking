#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Bounded Linux real-fixture KWS diagnostic, NOT a production ASR recognizer.

Uses the unchanged validated native runner. Every process dumps all padded
tensors; no opaque caches, silent state truncation, cloud or microphone access.
"""
import argparse
import hashlib
import json
import math
import os
import platform
import re
import signal
import stat
import struct
import subprocess
import sys
import time
from pathlib import Path

from importlib.util import module_from_spec, spec_from_file_location

_spec = spec_from_file_location("joiner_validation", Path(__file__).with_name("joiner-fixtures.py"))
_jf = module_from_spec(_spec)
_spec.loader.exec_module(_jf)
require, read_file, sha, crc = _jf.require, _jf.read_file, _jf.sha, _jf.crc


def metadata(text):
    ni, no = _jf.one(r"^inputs=([0-9]+) outputs=([0-9]+)$", text)
    tensors = {"input": [], "output": []}
    for kind, count in [("input", int(ni)), ("output", int(no))]:
        require(0 < count <= 64, "Bad tensor count")
        for index in range(count):
            size, fmt, quant, dims = _jf.one(
                r"^%s\[%d\] name=.+ padded-bytes=([0-9]+) format=([0-9]+) quant=([0-9]+) dims=([0-9x]+)$" %
                (kind, index), text)
            tensors[kind].append({"bytes": int(size), "format": int(fmt), "quant": int(quant),
                                  "dims": [int(dim) for dim in dims.split("x")]})
    _jf.one(r"^reference-status=0;.*$", text)
    _jf.one(r"^process-exit=0 tee-exit=0$", text)
    return tensors


def floats(data):
    require(len(data) % 4 == 0, "Not FP32 bytes")
    values = struct.unpack("<%df" % (len(data) // 4), data)
    require(all(math.isfinite(value) for value in values), "Nonfinite native tensor")
    return values


def load_assets(base):
    manifest = json.loads(read_file(base / "stream-assets.json").decode())
    require(manifest.get("schema") == 1 and len(manifest["files"]) >= 8, "Bad stream asset manifest")
    required = {"kws-contract.json", "tokens.txt", "fixtures/manifest.json",
                "fixtures/speech/features.f32", "fixtures/silence/features.f32",
                "models/kws/encoder_float_a733.nb", "models/kws/decoder_float_a733.nb",
                "models/kws/joiner_float_a733.nb", "voice-golden-linux-aarch64",
                "lib/libNBGlinker.so", "lib/libVIPhal.so"}
    require(required <= set(manifest["files"]), "Missing locked stream assets")
    for relative, entry in manifest["files"].items():
        path = Path(relative)
        require(not path.is_absolute() and ".." not in path.parts, "Unsafe manifest path")
        asset = base / path
        require(asset.is_file() and not asset.is_symlink() and asset.stat().st_size == entry["bytes"] and
                0 < entry["bytes"] <= 128 * 1024 * 1024, "Invalid asset: " + relative)
        digest = hashlib.sha256()
        with asset.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        require(digest.hexdigest() == entry["sha256"], "Asset changed: " + relative)
    contract = json.loads(read_file(base / "kws-contract.json").decode())
    require(set(contract) == {"encoder", "decoder", "joiner"}, "Missing model contract")
    for name, count in [("encoder", 39), ("decoder", 1), ("joiner", 2)]:
        require(len(contract[name]["input"]) == count, "Bad native inputs")
        require(len(contract[name]["output"]) == (39 if name == "encoder" else 1), "Bad native outputs")
        for tensor in contract[name]["input"] + contract[name]["output"]:
            require(tensor["format"] == 0 and tensor["quant"] == 0 and
                    math.prod(tensor["dims"]) * 4 == tensor["bytes"], "Unsupported FP32 contract/padding")
    encoder = contract["encoder"]
    require(encoder["input"][0]["dims"] == [80, 29, 1] and
            encoder["output"][0]["dims"] == [320, 4, 1], "Wrong encoder frame layout")
    for index in range(1, 39):
        require(encoder["input"][index] == encoder["output"][index], "State contract mismatch")
    require(encoder["input"][38]["dims"] == [1], "Wrong scalar state")
    require([x["dims"] for x in contract["decoder"]["input"]] == [[2, 1]] and
            [x["dims"] for x in contract["decoder"]["output"]] == [[320, 1]], "Wrong decoder layout")
    require([x["dims"] for x in contract["joiner"]["input"]] == [[320, 1], [320, 1]] and
            contract["joiner"]["output"][0]["dims"] == [263, 1], "Wrong joiner layout")
    fixture = json.loads(read_file(base / "fixtures/manifest.json").decode())
    require(fixture["segment_frames"] == 29 and fixture["offset_frames"] == 16 and
            fixture["encoder_frames"] == 4 and set(fixture["cases"]) == {"speech", "silence"},
            "Wrong fixture settings")
    for name, record in fixture["cases"].items():
        data = read_file(base / "fixtures" / name / "features.f32", record["feature_bytes"])
        require(sha(data) == record["feature_sha256"] and len(data) == record["frames"] * 320 and
                record["chunks"] == (record["frames"] - 29) // 16 + 1 and
                2 <= record["chunks"] <= 50, "Wrong fixture bytes/chunks")
        floats(data)
    tokens = {}
    for line in read_file(base / "tokens.txt").decode("utf-8").splitlines():
        token, number = line.rsplit(maxsplit=1)
        index = int(number)
        require(index not in tokens, "Duplicate token")
        tokens[index] = token
    require(set(tokens) == set(range(263)) and tokens[0] == "<blk>" and tokens[2] == "<unk>", "Wrong KWS vocabulary")
    return contract, fixture, tokens


def stop_process(process):
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=5)


class Runner:
    def __init__(self, base, work, contract, budget_seconds=1200, max_calls=1000):
        self.base, self.work, self.contract = base, work, contract
        self.deadline = time.monotonic() + budget_seconds
        self.max_calls = max_calls
        self.calls = 0
        self.trace = (work / "tensor-trace.jsonl").open("x")

    def close(self):
        self.trace.close()

    def invoke(self, tag, name, inputs=None):
        require(re.fullmatch(r"[a-z0-9-]+", tag), "Unsafe run tag")
        require(time.monotonic() < self.deadline and self.calls < self.max_calls, "Global deadline/call budget exceeded")
        self.calls += 1
        require(name in self.contract, "Unknown model")
        model = (self.base / "models/vocoder/vocoder_int16_a733.nb" if name == "vocoder" else
                 self.base / "models/kws" / (name + "_float_a733.nb"))
        output = self.work / tag
        command = [str(model), "--inspect"]
        if inputs is not None:
            require(len(inputs) == len(self.contract[name]["input"]), "Input count mismatch")
            folder = self.work / (tag + "-input")
            folder.mkdir()
            for index, (data, desc) in enumerate(zip(inputs, self.contract[name]["input"])):
                require(len(data) == desc["bytes"], "Input size mismatch")
                floats(data)
                with (folder / ("input%d.bin" % index)).open("xb") as stream:
                    stream.write(data)
            command = [str(model), str(folder), str(output)]
        logpath = self.work / (tag + ".log")
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = str(self.base / "lib")
        print("NPU call=%d %s (%s)" % (self.calls, tag, name), flush=True)
        with logpath.open("xb") as log:
            process = subprocess.Popen(["timeout", "--signal=TERM", "--kill-after=5s", "60s",
                                        str(self.base / "voice-golden-linux-aarch64"), *command],
                                       stdout=log, stderr=subprocess.STDOUT, env=env, start_new_session=True)
            try:
                status = process.wait(timeout=min(66, max(0.01, self.deadline - time.monotonic())))
            except BaseException:
                stop_process(process)
                raise
            log.write(("process-exit=%d tee-exit=0\n" % status).encode())
        require(status == 0, "Native run failed rc=%d (%s); preserve log; timeout needs cold reboot" % (status, tag))
        text = read_file(logpath).decode("utf-8")
        require(metadata(text) == self.contract[name], "Runtime contract changed: " + tag)
        if inputs is None:
            return None
        elapsed = int(_jf.one(r"^run-status=0 wall-us=([0-9]+)$", text))
        for index, data in enumerate(inputs):
            size, checksum = _jf.one(r"^input\[%d\] bytes=([0-9]+) raw-crc32=([0-9a-f]{8})$" % index, text)
            require(int(size) == len(data) and int(checksum, 16) == crc(data), "Native input bytes/CRC mismatch")
        outputs = []
        for index, desc in enumerate(self.contract[name]["output"]):
            data = read_file(output / ("output%d.bin" % index), desc["bytes"])
            floats(data)
            size, checksum, changed = _jf.one(
                r"^output\[%d\] bytes=([0-9]+) raw-crc32=([0-9a-f]{8}) changed=([0-9]+) status=0$" % index, text)
            require(int(size) == len(data) and int(checksum, 16) == crc(data) and
                    int(changed) == sum(value != 0xa5 for value in data) and int(changed) > 0,
                    "Incomplete native output/CRC/prefill mismatch")
            outputs.append(data)
        self.trace.write(json.dumps({"tag": tag, "model": name, "native_run_us": elapsed,
                                     "inputs": [sha(data) for data in inputs],
                                     "outputs": [sha(data) for data in outputs]}, sort_keys=True) + "\n")
        self.trace.flush()
        return outputs


def stream_case(base, work, runner, contract, record, name, tokens, policy):
    features = read_file(base / "fixtures" / name / "features.f32", record["feature_bytes"])
    states = [bytes(desc["bytes"]) for desc in contract["encoder"]["input"]]
    context = [0, 0]
    emitted, frames, state_scalars, encoder_hashes = [], [], [], []
    for chunk in range(record["chunks"]):
        offset = chunk * 16
        states[0] = features[offset * 320:(offset + 29) * 320]
        if policy == "feature-offset":
            states[38] = struct.pack("<f", offset)
        scalar_in = floats(states[38])[0]
        output = runner.invoke("%s-c%03d-encoder" % (name, chunk), "encoder", states)
        if chunk == 0:
            repeat = runner.invoke(name + "-first-encoder-repeat", "encoder", states)
            require(output == repeat, "First chunk repeat differs in encoder/state output")
        encoder_hashes.append(sha(output[0]))
        scalar_out = floats(output[38])[0]
        require(scalar_out >= 0 and scalar_out.is_integer(), "Invalid processed_lens output")
        state_scalars.append({"chunk": chunk, "feature_offset": offset, "input": scalar_in, "output": scalar_out})
        # Exact VIP-native bytes for all 38 states. FP32 scalar policy is explicit,
        # not reinterpret-cast INT64 and not silently zeroed on every chunk.
        states = [b""] + output[1:]
        for frame in range(4):
            tag = "%s-c%03d-f%d" % (name, chunk, frame)
            dec = runner.invoke(tag + "-decoder", "decoder", [struct.pack("<2f", *context)])[0]
            enc = output[0][frame * 1280:(frame + 1) * 1280]
            logits = floats(runner.invoke(tag + "-joiner", "joiner", [enc, dec])[0])
            best = max(range(263), key=logits.__getitem__)
            probability = 1 / sum(math.exp(value - logits[best]) for value in logits)
            frames.append({"chunk": chunk, "frame": frame, "context": context.copy(),
                           "argmax": best, "token": tokens[best], "top1_probability": probability})
            # A one-symbol-per-frame greedy diagnostic, NOT the reference's
            # keyword-biased beam/trie. Keep blank/unknown out of token history.
            if best not in (0, 2):
                emitted.append(best)
                context = [context[1], best]
        print("%s chunk=%d/%d phonemes=%s" % (name, chunk + 1, record["chunks"],
                                             " ".join(tokens[index] for index in emitted)), flush=True)
    result = {"fixture": name, "lens_policy": policy, "chunks": record["chunks"],
              "processed_lens": state_scalars, "encoder_hashes": encoder_hashes,
              "token_ids": emitted, "phonemes": " ".join(tokens[index] for index in emitted), "frames": frames,
              "not_accuracy_acceptance": True}
    with (work / (name + "-greedy.json")).open("x", encoding="utf-8") as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False, sort_keys=True)
    return result


def run(base, work, policy):
    contract, fixture, tokens = load_assets(base)
    runner = Runner(base, work, contract)
    try:
        for name in ("encoder", "decoder", "joiner"):
            runner.invoke(name + "-info", name)
        results = [stream_case(base, work, runner, contract, fixture["cases"][name], name, tokens, policy)
                   for name in ("speech", "silence")]
        require(results[0]["encoder_hashes"] != results[1]["encoder_hashes"],
                "No encoder distinction between speech/silence; stop for diagnosis")
        with (work / "stream-summary.json").open("x", encoding="utf-8") as stream:
            json.dump({"result": "COLLECTED_REAL_KWS_GREEDY_DIAGNOSTIC", "calls": runner.calls,
                       "lens_policy": policy, "source": fixture["audio_source_name"],
                       "source_sha256": fixture["source_sha256"],
                       "phonemes": {item["fixture"]: item["phonemes"] for item in results},
                       "limitations": ["Host-precomputed fbank; no live capture", "Greedy, not official keyword beam",
                                       "KWS phonemes, not Chinese ASR", "Fresh process per network call; not warm RTF",
                                       "No openvela NPU execution or TTS acceptance"]}, stream, indent=2, ensure_ascii=False)
    finally:
        runner.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["check", "run"])
    parser.add_argument("base", type=Path)
    parser.add_argument("work", nargs="?", type=Path)
    parser.add_argument("--lens-policy", choices=["feature-offset", "feedback"], default="feature-offset")
    parser.add_argument("--npu-idle-confirmed", action="store_true")
    args = parser.parse_args()
    try:
        base = args.base.resolve(strict=True)
        if args.mode == "check":
            load_assets(base)
            print("Fixture/FP32 contracts verified; no NPU calls")
        else:
            require(args.npu_idle_confirmed and platform.system() == "Linux" and platform.machine() == "aarch64" and
                    stat.S_ISCHR(os.stat("/dev/vipcore").st_mode), "Use confirmed official Linux hardware wrapper")
            require(args.work is not None and args.work.is_dir(), "Missing fresh work directory")
            def interrupted(signum, frame):
                raise KeyboardInterrupt("Signal %d; preserve logs, no retry" % signum)
            signal.signal(signal.SIGINT, interrupted)
            signal.signal(signal.SIGTERM, interrupted)
            run(base, args.work, args.lens_policy)
    except KeyboardInterrupt as error:
        print("STOP: " + str(error), file=sys.stderr)
        return 130
    except (ValueError, OSError, KeyError, TypeError, struct.error, subprocess.SubprocessError) as error:
        print("STOP: " + str(error), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
