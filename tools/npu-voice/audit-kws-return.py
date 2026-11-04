#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only replay audit of a stage-3 returned tensor archive. No NPU calls."""
import argparse
import importlib.util
import json
import math
import statistics
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("stream_kws", Path(__file__).with_name("stream-kws.py"))
SK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SK)


def audit(root, base):
    contract, fixture, tokens = SK.load_assets(base)
    records = [json.loads(line) for line in SK.read_file(root / "tensor-trace.jsonl").decode().splitlines()]
    seen, timing = {}, {}
    for record in records:
        tag, model = record["tag"], record["model"]
        SK.require(tag not in seen and model in contract and tag.replace("-", "").isalnum(), "Bad/duplicate trace tag")
        log = SK.read_file(root / (tag + ".log")).decode()
        SK.require(SK.metadata(log) == contract[model], "Metadata mismatch")
        elapsed = int(SK._jf.one(r"^run-status=0 wall-us=([0-9]+)$", log))
        SK.require(elapsed == record["native_run_us"], "Timing trace mismatch")
        timing.setdefault(model, []).append(elapsed)
        tensors = {}
        for kind in ("input", "output"):
            tensors[kind] = []
            folder = root / (tag + "-input" if kind == "input" else tag)
            SK.require(len(record[kind + "s"]) == len(contract[model][kind]), "Trace tensor count mismatch")
            for index, desc in enumerate(contract[model][kind]):
                data = SK.read_file(folder / ("%s%d.bin" % (kind, index)), desc["bytes"])
                SK.floats(data)
                pattern = r"^%s\[%d\] bytes=%d raw-crc32=([0-9a-f]{8})" % (kind, index, len(data))
                pattern += r" changed=([0-9]+) status=0$" if kind == "output" else "$"
                matched = SK._jf.one(pattern, log)
                checksum = matched[0] if kind == "output" else matched
                SK.require(int(checksum, 16) == SK.crc(data), "Raw tensor CRC mismatch")
                SK.require(SK.sha(data) == record[kind + "s"][index], "Raw tensor hash mismatch")
                if kind == "output":
                    SK.require(int(matched[1]) == sum(v != 0xa5 for v in data), "Prefill count mismatch")
                tensors[kind].append(data)
        seen[tag] = tensors
    expected = set()
    acoustic = {}
    for name, case in fixture["cases"].items():
        features = SK.read_file(base / "fixtures" / name / "features.f32")
        contexts = [0, 0]
        top_ids, nonblank_probs = [], []
        for chunk in range(case["chunks"]):
            tag = "%s-c%03d-encoder" % (name, chunk)
            expected.add(tag)
            current = seen[tag]
            SK.require(current["input"][0] == features[chunk * 16 * 320:(chunk * 16 + 29) * 320], "Feature slice mismatch")
            if chunk == 0:
                repeat = name + "-first-encoder-repeat"
                expected.add(repeat)
                SK.require(current == seen[repeat], "First chunk repeat mismatch")
                SK.require(all(not any(data) for data in current["input"][1:]), "Nonzero initial states")
            else:
                previous = seen["%s-c%03d-encoder" % (name, chunk - 1)]
                SK.require(current["input"][1:38] == previous["output"][1:38], "State feedback bytes differ")
                SK.require(SK.floats(current["input"][38])[0] == chunk * 16, "Unexpected scalar policy")
            for frame in range(4):
                prefix = "%s-c%03d-f%d" % (name, chunk, frame)
                expected.update([prefix + "-decoder", prefix + "-joiner"])
                decoder, joiner = seen[prefix + "-decoder"], seen[prefix + "-joiner"]
                SK.require(list(SK.floats(decoder["input"][0])) == contexts, "Decoder context mismatch")
                SK.require(joiner["input"] == [current["output"][0][frame * 1280:(frame + 1) * 1280],
                                                 decoder["output"][0]], "Encoder/decoder -> joiner mismatch")
                logits = SK.floats(joiner["output"][0])
                best = max(range(263), key=logits.__getitem__)
                denominator = sum(math.exp(v - max(logits)) for v in logits)
                nonblank = max(range(1, 263), key=logits.__getitem__)
                nonblank_probs.append(math.exp(logits[nonblank] - max(logits)) / denominator)
                top_ids.append(best)
                if best not in (0, 2):
                    contexts = [contexts[1], best]
        acoustic[name] = {"frames": len(top_ids), "blank_frames": top_ids.count(0),
                          "max_nonblank_probability": max(nonblank_probs),
                          "greedy_phonemes": " ".join(tokens[i] for i in top_ids if i not in (0, 2))}
    SK.require(set(seen) == expected, "Missing/extra run records")
    return {"result": "RAW_TRACE_AND_STATE_LINKS_VERIFIED", "native_trace_runs": len(seen), "acoustic": acoustic,
            "native_run_us": {name: {"min": min(values), "median": statistics.median(values), "max": max(values)}
                              for name, values in timing.items()},
            "recognition_passed": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("kit", type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.report, args.kit), indent=2, ensure_ascii=False))
