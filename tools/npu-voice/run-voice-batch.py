#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""One-shot safe Linux batch for available KWS/vocoder assets. No cloud/ASR claim."""
import argparse
import json
import math
import os
import platform
import re
import signal
import shutil
import stat
import statistics
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path
import importlib.util


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


SK = module("stream_kws", "stream-kws.py")
KR = module("keyword_reference", "keyword-reference.py")
R, read, sha = SK.require, SK.read_file, SK.sha


def write_json(path, value):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False, sort_keys=True)


def environment(work, label):
    # Conservative TEST policy, not a claim about the silicon's rated limit.
    values = {"label": label, "monotonic": time.monotonic(), "software_temp_cap_mc": 75000}
    hot = []
    for path in list(Path("/sys/class/thermal").glob("thermal_zone*/temp")) + \
            list(Path("/sys/devices/system/cpu").glob("cpu*/cpufreq/scaling_cur_freq")):
        try:
            values[str(path)] = path.read_text().strip()
            if path.name == "temp" and values[str(path)].lstrip("-").isdigit() and int(values[str(path)]) >= 75000:
                hot.append(str(path))
        except OSError:
            pass
    with (work / "batch-environment.jsonl").open("a") as stream:
        stream.write(json.dumps(values, sort_keys=True) + "\n")
    R(not hot, "Software test temperature cap (75C) reached; preserve logs and cool manually: " + ",".join(hot))


def check(base):
    contract, _, tokens = SK.load_assets(base)
    locked = json.loads(read(base / "stream-assets.json").decode())["files"]
    R({"voice-repeat-linux-aarch64", "vocoder-contract.json", "batch-fixtures/cases.json", "tts-fixtures/manifest.json",
       "keywords.txt", "keyword-reference.py", "run-voice-batch.py"} <= set(locked), "Unlocked batch components")
    voc = json.loads(read(base / "vocoder-contract.json").decode())
    R(voc == {"input": [{"bytes": 64000, "format": 0, "quant": 0, "dims": [200, 80, 1]}],
              "output": [{"bytes": 204800, "format": 0, "quant": 0, "dims": [51200, 1]}]}, "Wrong vocoder contract")
    contract["vocoder"] = voc
    cases = json.loads(read(base / "batch-fixtures/cases.json").decode())
    R(len(cases) == 12 and len({item["tag"] for item in cases}) == 12, "Missing matrix fixtures")
    for item in cases:
        R(re.fullmatch(r"[a-z-]+", item["tag"]) and item["scale"] in ("normalized", "pcm-units"), "Bad scenario")
        data = read(base / item["feature_path"], item["feature_bytes"])
        R(sha(data) == item["feature_sha256"] and len(data) == item["frames"] * 320 and
          item["chunks"] == (item["frames"] - 29) // 16 + 1 and 2 <= item["chunks"] <= 50, "Bad matrix features")
        SK.floats(data)
    tts = json.loads(read(base / "tts-fixtures/manifest.json").decode())
    R(1 <= len(tts["items"]) <= 8 and len({item["tag"] for item in tts["items"]}) == len(tts["items"]), "Bad TTS set")
    for item in tts["items"]:
        R(re.fullmatch(r"tts-[0-9-]+", item["tag"]) and 1 <= item["mel_frames_kept"] <= 200, "Bad TTS frames")
        data = read(base / item["path"], 64000)
        R(sha(data) == item["sha256"], "Changed CPU mel")
        SK.floats(data)
    KR.Keywords(read(base / "keywords.txt").decode("utf-8"), {token: index for index, token in tokens.items()})
    return contract, tokens, cases, tts


class Batch:
    def __init__(self, base, work, contract):
        self.base, self.work, self.contract = base, work, contract
        self.runner = SK.Runner(base, work, contract, budget_seconds=5400, max_calls=20000)
        self.decoder_cache = {}

    def decoder(self, tag, context):
        key = tuple(context)
        if key not in self.decoder_cache:
            R(len(self.decoder_cache) < 4096, "Decoder cache budget exceeded")
            self.decoder_cache[key] = (self.runner.invoke(tag, "decoder", [struct.pack("<2f", *context)])[0], tag)
        return self.decoder_cache[key]

    def scenario(self, case, policy, tokens):
        label = case["tag"] + "-" + policy
        environment(self.work, label)
        features = read(self.base / case["feature_path"], case["feature_bytes"])
        states = [bytes(item["bytes"]) for item in self.contract["encoder"]["input"]]
        keyword = KR.Keywords(read(self.base / "keywords.txt").decode("utf-8"),
                              {token: index for index, token in tokens.items()})
        greedy_context, greedy, hits, frames, scalars = [0, 0], [], [], [], []
        encoder_hashes = []
        for chunk in range(case["chunks"]):
            tag = "%s-c%03d" % (label, chunk)
            environment(self.work, tag)
            states[0] = features[chunk * 16 * 320:(chunk * 16 + 29) * 320]
            if policy == "feature-offset":
                states[38] = struct.pack("<f", chunk * 16)
            scalar = SK.floats(states[38])[0]
            outputs = self.runner.invoke(tag + "-encoder", "encoder", states)
            if chunk == 0:
                again = self.runner.invoke(label + "-encoder-repeat", "encoder", states)
                R(outputs == again, "Matrix first chunk/state repeat mismatch")
            encoder_hashes.append(sha(outputs[0]))
            scalars.append({"chunk": chunk, "input": scalar, "output": SK.floats(outputs[38])[0]})
            states = [b""] + outputs[1:]
            for frame in range(4):
                prefix = tag + "-f%d" % frame
                enc = outputs[0][frame * 1280:(frame + 1) * 1280]
                frame_cache = {}
                for algorithm, context in (("greedy", greedy_context.copy()), ("reference-keyword", keyword.context.copy())):
                    key = tuple(context)
                    if key not in frame_cache:
                        dec, origin = self.decoder(prefix + "-" + algorithm + "-decoder", context)
                        native_tag = prefix + "-" + algorithm + "-joiner"
                        logits = SK.floats(self.runner.invoke(native_tag, "joiner", [enc, dec])[0])
                        frame_cache[key] = logits, origin, native_tag
                    logits, origin, native_tag = frame_cache[key]
                    best = max(range(263), key=logits.__getitem__)
                    chosen, hit = best, None
                    if algorithm == "greedy":
                        if best not in (0, 2):
                            greedy.append(best)
                            greedy_context = [greedy_context[1], best]
                    else:
                        chosen, hit = keyword.consume(logits, chunk * 4 + frame)
                        if hit:
                            hits.append(hit)
                    frames.append({"chunk": chunk, "frame": frame, "algorithm": algorithm, "context": context,
                                   "acoustic_argmax": best, "selected": chosen, "token": tokens[chosen],
                                   "decoder_origin": origin, "joiner_origin": native_tag, "hit": hit})
            print("MATRIX %s chunk=%d/%d hits=%d" % (label, chunk + 1, case["chunks"], len(hits)), flush=True)
        names = [hit["phrase"] for hit in hits]
        classified = bool(names) if case["expected"] else not names
        result = {"scenario": label, "scale": case["scale"], "lens_policy": policy, "source": case["source"],
                  "expected": case["expected"], "hits": hits, "fixture_classification_ok": classified,
                  "exact_phrase_observed": case["expected"] in names if case["expected"] else not names,
                  "greedy_phonemes": " ".join(tokens[index] for index in greedy), "processed_lens": scalars,
                  "encoder_hashes": encoder_hashes, "frames": frames,
                  "legacy_heuristics_not_production_acceptance": True}
        write_json(self.work / (label + ".json"), result)
        return result

    def warm(self, name, inputs, repeats=10):
        tag = "warm-" + name
        folder = self.work / (tag + "-input")
        folder.mkdir()
        for index, data in enumerate(inputs):
            R(len(data) == self.contract[name]["input"][index]["bytes"], "Bad warm input")
            SK.floats(data)
            with (folder / ("input%d.bin" % index)).open("xb") as stream:
                stream.write(data)
        model = (self.base / "models/vocoder/vocoder_int16_a733.nb" if name == "vocoder" else
                 self.base / "models/kws" / (name + "_float_a733.nb"))
        output = self.work / tag
        logfile = self.work / (tag + ".log")
        environment(self.work, tag)
        R(time.monotonic() < self.runner.deadline, "Batch deadline")
        self.runner.calls += 1
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = str(self.base / "lib")
        started = time.monotonic()
        with logfile.open("xb") as log:
            process = subprocess.Popen(["timeout", "--signal=TERM", "--kill-after=5s", "60s",
                                        str(self.base / "voice-repeat-linux-aarch64"), str(model), str(folder),
                                        str(output), "--repeat", str(repeats)], stdout=log, stderr=subprocess.STDOUT,
                                       env=env, start_new_session=True)
            try:
                status = process.wait(timeout=min(66, max(0.01, self.runner.deadline - time.monotonic())))
            except BaseException:
                SK.stop_process(process)
                raise
            log.write(("process-exit=%d tee-exit=0\n" % status).encode())
        R(status == 0, "Warm process failed; do not reuse NPU automatically")
        text = read(logfile).decode()
        R(SK.metadata(text) == self.contract[name], "Warm metadata changed")
        for index, data in enumerate(inputs):
            value = SK._jf.one(r"^input\[%d\] bytes=%d raw-crc32=([0-9a-f]{8})$" % (index, len(data)), text)
            R(int(value, 16) == SK.crc(data), "Warm input CRC")
        markers = list(re.finditer(r"^repeat-index=([0-9]+) total=%d$" % repeats, text, re.MULTILINE))
        R(len(markers) == repeats, "Missing repeat marker")
        reference, times = None, []
        for index, marker in enumerate(markers):
            R(int(marker.group(1)) == index, "Repeat sequence mismatch")
            part = text[marker.end():markers[index + 1].start() if index + 1 < repeats else len(text)]
            times.append(int(SK._jf.one(r"^run-status=0 wall-us=([0-9]+)$", part)))
            values = []
            for tensor, desc in enumerate(self.contract[name]["output"]):
                data = read(output / ("step%03d" % index) / ("output%d.bin" % tensor), desc["bytes"])
                SK.floats(data)
                checksum, changed = SK._jf.one(r"^output\[%d\] bytes=%d raw-crc32=([0-9a-f]{8}) changed=([0-9]+) status=0$" %
                                               (tensor, len(data)), part)
                R(int(checksum, 16) == SK.crc(data) and int(changed) == sum(v != 0xa5 for v in data) and int(changed) > 0,
                  "Warm output corruption/prefill")
                values.append(data)
            if reference is None:
                reference = values
            R(reference == values, "Warm repeat outputs differ")
        cold = self.runner.invoke(name + "-reload-a", name, inputs)
        again = self.runner.invoke(name + "-reload-b", name, inputs)
        R(cold == again == reference, "Warm vs unload/reload outputs differ")
        return {"model": name, "repeats": repeats, "raw_native_call_us": times, "first_call_us": times[0],
                "warm_median_us": statistics.median(times[1:]), "warm_max_us": max(times[1:]),
                "process_wall_seconds_includes_dumps": time.monotonic() - started,
                "all_outputs_equal_after_reload": True, "output_sha256": [sha(data) for data in reference]}

    def vocoder(self, tts):
        results = []
        items = [{"tag": "silence-floor", "path": "tts-fixtures/silence-floor.f32", "mel_frames_kept": 200,
                  "text": "(mel floor, not speech)"}] + tts["items"]
        for item in items:
            mel = read(self.base / item["path"], 64000)
            tag = "vocoder-" + item["tag"]
            output = self.runner.invoke(tag, "vocoder", [mel])[0]
            repeat = self.runner.invoke(tag + "-repeat", "vocoder", [mel])[0]
            R(output == repeat, "Real mel vocoder repeat mismatch")
            values = SK.floats(output)
            peak = max(abs(v) for v in values)
            rms = math.sqrt(sum(v * v for v in values) / len(values))
            attenuation = max(1, peak / 0.8)
            keep = item["mel_frames_kept"] * 256
            pcm = struct.pack("<%dh" % keep, *[int(max(-1, min(1, v / attenuation)) * 32767) for v in values[:keep]])
            with wave.open(str(self.work / (tag + ".wav")), "wb") as stream:
                stream.setnchannels(1); stream.setsampwidth(2); stream.setframerate(22050); stream.writeframes(pcm)
            results.append({"tag": tag, "text": item["text"], "raw_sha256": sha(output), "raw_peak": peak, "raw_rms": rms,
                            "safety_attenuation_only": attenuation, "audio_samples": keep,
                            "semantic_audio_acceptance": "LISTENING_PENDING"})
        R(len({item["raw_sha256"] for item in results}) == len(results), "Vocoder outputs unchanged for distinct mels")
        return results

    def negative_input(self):
        folder = self.work / "malformed-input"
        folder.mkdir()
        (folder / "input0.bin").write_bytes(b"x")
        (folder / "input1.bin").write_bytes(bytes(1280))
        logpath = self.work / "malformed-input.log"
        env = os.environ.copy(); env["LD_LIBRARY_PATH"] = str(self.base / "lib")
        self.runner.calls += 1
        with logpath.open("xb") as log:
            process = subprocess.Popen(["timeout", "--signal=TERM", "--kill-after=5s", "60s",
                                        str(self.base / "voice-golden-linux-aarch64"),
                                        str(self.base / "models/kws/joiner_float_a733.nb"), str(folder),
                                        str(self.work / "malformed-output")], stdout=log, stderr=subprocess.STDOUT,
                                       env=env, start_new_session=True)
            try:
                status = process.wait(timeout=66)
            except BaseException:
                SK.stop_process(process); raise
        text = read(logpath).decode()
        R(status == 1 and "input0 must be exactly 1280 padded bytes" in text and
          "run-status=" not in text and "reference-status=1;" in text,
          "Malformed input did not fail in the expected pre-run path")
        return {"result": "REJECTED_BEFORE_RUN", "hardware_timeout_injected": False,
                "note": "Subsequent positive warm/reload checks test reuse; not a reset or leak certification"}


def run(base, work):
    contract, tokens, cases, tts = check(base)
    batch = Batch(base, work, contract)
    try:
        for name in contract:
            batch.runner.invoke(name + "-info", name)
        negative = batch.negative_input()
        first = cases[0]
        enc = [bytes(item["bytes"]) for item in contract["encoder"]["input"]]
        enc[0] = read(base / first["feature_path"])[:9280]
        warm_inputs = {"encoder": enc, "decoder": [bytes(8)], "joiner": [bytes(1280), bytes(1280)],
                       "vocoder": [read(base / "tts-fixtures/silence-floor.f32", 64000)]}
        warm = [batch.warm(name, inputs) for name, inputs in warm_inputs.items()]
        R(warm[2]["output_sha256"] == [SK._jf.ZERO_HASH], "New runner joiner zero baseline changed")
        write_json(work / "warm-summary.json", warm)
        voices = batch.vocoder(tts)
        write_json(work / "vocoder-summary.json", voices)
        matrix = []
        for case in cases:
            for policy in ("feature-offset", "feedback"):
                result = batch.scenario(case, policy, tokens)
                matrix.append({key: result[key] for key in ("scenario", "scale", "lens_policy", "expected", "hits",
                                                            "fixture_classification_ok", "exact_phrase_observed", "greedy_phonemes")})
        configs = {}
        for scale in ("normalized", "pcm-units"):
            for policy in ("feature-offset", "feedback"):
                selected = [item for item in matrix if item["scale"] == scale and item["lens_policy"] == policy]
                configs[scale + "/" + policy] = {"fixture_count": len(selected),
                    "classification_checks_ok": sum(item["fixture_classification_ok"] for item in selected),
                    "exact_phrase_checks_ok": sum(item["exact_phrase_observed"] for item in selected),
                    "all_classification_checks_ok": all(item["fixture_classification_ok"] for item in selected)}
        summary = {"result": "COLLECTED_AVAILABLE_ASSET_BATCH", "native_process_calls": batch.runner.calls,
                   "decoder_contexts_cached": len(batch.decoder_cache), "configs": configs, "matrix": matrix,
                   "warm": warm, "vocoder": voices, "end_to_end_accepted": False,
                   "malformed_input": negative,
                   "blocked_or_not_claimed": ["ASR NBG absent; ONNX sources are not executable NBG",
                       "Licensed ASR conversion toolchain not validated by this kit; no fabricated ASR pass",
                       "Acoustic Matcha ran on development-host CPU, not A7Z CPU/NPU",
                       "KWS legacy continuation heuristics, not production accuracy acceptance",
                       "WAV listening acceptance pending; no automatic playback",
                       "No intentional accelerator hang/reset injection; recovery not certified",
                       "No live microphone or openvela NPU application execution"]}
        write_json(work / "batch-summary.json", summary)
        print(json.dumps({"result": summary["result"], "configs": configs, "voices": voices,
                          "native_process_calls": batch.runner.calls, "end_to_end_accepted": False}, indent=2, ensure_ascii=False))
    finally:
        batch.runner.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["check", "run"])
    parser.add_argument("base", type=Path)
    parser.add_argument("work", nargs="?", type=Path)
    parser.add_argument("--npu-idle-confirmed", action="store_true")
    args = parser.parse_args()
    try:
        if args.mode == "check":
            check(args.base.resolve(strict=True)); print("Batch assets/fixtures/contracts OK; no NPU calls")
        else:
            R(args.npu_idle_confirmed and platform.system() == "Linux" and platform.machine() == "aarch64" and
              stat.S_ISCHR(os.stat("/dev/vipcore").st_mode) and args.work and args.work.is_dir(), "Use confirmed Linux wrapper")
            R(shutil.disk_usage(args.work).free >= 2 * 1024 ** 3, "Need at least 2 GiB free for complete tensor reports")
            def interrupted(signum, frame):
                raise KeyboardInterrupt("Signal %d; preserve report" % signum)
            signal.signal(signal.SIGINT, interrupted); signal.signal(signal.SIGTERM, interrupted)
            run(args.base.resolve(strict=True), args.work)
    except KeyboardInterrupt as error:
        print("STOP:", error, file=sys.stderr); sys.exit(130)
    except (ValueError, OSError, KeyError, TypeError, struct.error, subprocess.SubprocessError) as error:
        print("STOP:", error, file=sys.stderr); sys.exit(2)
