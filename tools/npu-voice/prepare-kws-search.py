#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Extract only hash-verified real encoder/logit fixtures; no NPU/model changes."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def prepare(report, output):
    output.mkdir()
    traces = {}
    with (report / "tensor-trace.jsonl").open(encoding="utf-8") as stream:
        for line in stream:
            entry = json.loads(line)
            require(entry["tag"] not in traces, "Duplicate trace tag")
            traces[entry["tag"]] = entry
    cases, cached = [], {}

    def save(data):
        digest = sha(data)
        relative = digest + ".bin"
        path = output / relative
        if path.exists():
            require(path.read_bytes() == data, "Content-address collision")
        else:
            with path.open("xb") as stream:
                stream.write(data)
        return dict(path="search-fixtures/" + relative, sha256=digest)

    for tag in ("wake-full", "wake-short", "wake-double", "negative-command", "negative-noise", "negative-silence"):
        label = tag + "-normalized-feature-offset"
        result = json.loads((report / (label + ".json")).read_text(encoding="utf-8"))
        enc = []
        for chunk, expected in enumerate(result["encoder_hashes"]):
            origin = label + "-c%03d-encoder" % chunk
            data = (report / origin / "output0.bin").read_bytes()
            require(len(data) == 5120 and sha(data) == expected == traces[origin]["outputs"][0], "Encoder changed")
            enc.extend(save(data[frame*1280:(frame+1)*1280]) for frame in range(4))
        for frame in result["frames"]:
            origin = frame["joiner_origin"]
            row = frame["chunk"]*4 + frame["frame"]
            context = frame["context"]
            require(all(isinstance(i, int) and 0 <= i < 263 for i in context) and len(context) == 2, "Bad context")
            input0 = (report / (origin + "-input") / "input0.bin").read_bytes()
            input1 = (report / (origin + "-input") / "input1.bin").read_bytes()
            decoded = (report / frame["decoder_origin"] / "output0.bin").read_bytes()
            token_input = (report / (frame["decoder_origin"] + "-input") / "input0.bin").read_bytes()
            require(token_input == struct.pack("<2f", *context), "Decoder context mismatch")
            require(input1 == decoded and sha(input1) == traces[frame["decoder_origin"]]["outputs"][0], "Bad decoder link")
            require(sha(input0) == enc[row]["sha256"], "Bad acoustic frame link")
            require([sha(input0), sha(input1)] == traces[origin]["inputs"], "Joiner inputs changed")
            data = (report / origin / "output0.bin").read_bytes()
            require(len(data) == 1052 and sha(data) == traces[origin]["outputs"][0], "Joiner output changed")
            key = (sha(input0), tuple(context))
            entry = dict(**save(data), encoder_sha256=key[0], context=context, origin=origin)
            require(key not in cached or cached[key]["sha256"] == entry["sha256"], "Conflicting repeated output")
            cached[key] = entry
        cases.append(dict(tag=tag, expected=result["expected"], encoder_frames=enc))
    summary_path = report / "batch-summary.json"
    provenance = dict(report=report.name, batch_summary_sha256=sha(summary_path.read_bytes()),
                      trace_sha256=sha((report / "tensor-trace.jsonl").read_bytes()),
                      source="normalized / feature-offset; FROZEN Linux NPU output, not new live capture",
                      end_to_end_accepted=False)
    index = dict(schema=1, provenance=provenance, cases=cases, logits=list(cached.values()))
    with (output / "index.json").open("x", encoding="utf-8") as stream:
        json.dump(index, stream, ensure_ascii=False, indent=2)
    print(json.dumps(dict(cases=len(cases), encoder_frames=sum(len(c["encoder_frames"]) for c in cases),
                          cached_logits=len(cached), provenance=provenance), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("report", type=Path)
    p.add_argument("new_output", type=Path)
    args = p.parse_args()
    prepare(args.report.resolve(strict=True), args.new_output)
