#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline deterministic FP32 fixtures/validation; standard library only.

This is input-sensitivity evidence, not a semantic KWS/ASR oracle. No NPU API.
Tensor contract and zero baseline are pinned to the user's 2026-10-19 board run.
"""
import hashlib
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path

ZERO_HASH = "b2a50b97a684a67b8957eb54674aea44f5a9d395101d7c175654f9316161fff3"
TAGS = {"zero-a": "zero", "encoder-a": "encoder", "decoder-a": "decoder",
        "both-a": "both", "encoder-b": "encoder", "zero-b": "zero"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_file(path, size=None):
    require(path.is_file() and not path.is_symlink(), "Missing/nonregular/symlink: " + str(path))
    if size is not None:
        require(path.stat().st_size == size, "Unexpected file size: " + str(path))
    require(path.stat().st_size <= 1024 * 1024, "Oversized evidence file")
    return path.read_bytes()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def crc(data):
    # Runner uses reflected CRC32 with initial=0, final xor=0, not zlib default.
    return (zlib.crc32(data, 0xffffffff) ^ 0xffffffff) & 0xffffffff


def fixture_bytes():
    zero = bytes(1280)
    # Exact binary fractions; small bounded signed gradients, not acoustic data.
    encoder = struct.pack("<320f", *[((i * 17) % 65 - 32) / 64 for i in range(320)])
    decoder = struct.pack("<320f", *[((i * 29 + 7) % 65 - 32) / 128 for i in range(320)])
    return {"zero": (zero, zero), "encoder": (encoder, zero),
            "decoder": (zero, decoder), "both": (encoder, decoder)}


def prepare(root):
    fixtures = root / "fixtures"
    fixtures.mkdir()  # Never overwrite prior fixtures/evidence.
    manifest = {}
    for name, pair in fixture_bytes().items():
        folder = fixtures / name
        folder.mkdir()
        for index, data in enumerate(pair):
            path = folder / ("input%d.bin" % index)
            with path.open("xb") as stream:
                stream.write(data)
            manifest[str(path.relative_to(fixtures))] = {"bytes": len(data), "sha256": sha(data),
                                                       "raw_crc32": "%08x" % crc(data)}
    with (root / "fixture-manifest.json").open("x") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)


def fixtures_check(root):
    # Recreate canonical values instead of trusting a user-editable manifest.
    for name, pair in fixture_bytes().items():
        for index, data in enumerate(pair):
            actual = read_file(root / "fixtures" / name / ("input%d.bin" % index), len(data))
            require(actual == data, "Fixture modified: %s/input%d" % (name, index))


def one(pattern, text):
    matches = re.findall(pattern, text, re.MULTILINE)
    require(len(matches) == 1, "Missing/duplicate log record: " + pattern)
    return matches[0]


def contract(text):
    one(r"^inputs=2 outputs=1$", text)
    for kind, index, size, dims in [("input", 0, 1280, "320x1"),
                                    ("input", 1, 1280, "320x1"),
                                    ("output", 0, 1052, "263x1")]:
        one(r"^%s\[%d\] name=.+ padded-bytes=%d format=0 quant=0 dims=%s$" %
            (kind, index, size, dims), text)
    one(r"^reference-status=0;.*$", text)
    one(r"^process-exit=0 tee-exit=0$", text)


def output_check(root, tag):
    require(tag in TAGS, "Unknown run tag")
    fixtures_check(root)
    log = read_file(root / (tag + ".log")).decode("utf-8")
    contract(log)
    elapsed = int(one(r"^run-status=0 wall-us=([0-9]+)$", log))
    for index, data in enumerate(fixture_bytes()[TAGS[tag]]):
        reported = one(r"^input\[%d\] bytes=1280 raw-crc32=([0-9a-f]{8})$" % index, log)
        require(int(reported, 16) == crc(data), "Input CRC mismatch at input%d" % index)
    data = read_file(root / tag / "output0.bin", 1052)
    reported_crc, changed = one(
        r"^output\[0\] bytes=1052 raw-crc32=([0-9a-f]{8}) changed=([0-9]+) status=0$", log)
    require(int(reported_crc, 16) == crc(data), "Output CRC mismatch")
    require(int(changed) == sum(value != 0xa5 for value in data) and int(changed) > 0,
            "Output prefill/change count mismatch")
    values = struct.unpack("<263f", data)
    require(all(math.isfinite(value) for value in values), "NaN/Inf in output")
    if TAGS[tag] == "zero":
        require(sha(data) == ZERO_HASH, "Zero output differs from first board baseline; stop for review")
    return {"input_case": TAGS[tag], "sha256": sha(data), "raw_crc32": "%08x" % crc(data),
            "bytes": len(data), "finite_floats": len(values), "min": min(values), "max": max(values),
            "argmax": max(range(len(values)), key=values.__getitem__), "run_wall_us": elapsed}


def compare(root):
    records = {tag: output_check(root, tag) for tag in TAGS}
    for first, second in [("zero-a", "zero-b"), ("encoder-a", "encoder-b")]:
        require(records[first]["sha256"] == records[second]["sha256"], "Repeat mismatch: " + first)
    # Each input must independently influence the output, including when the
    # other input is nonzero. Unchanged results are INCONCLUSIVE/fail, not pass.
    for first, second in [("zero-a", "encoder-a"), ("zero-a", "decoder-a"),
                          ("encoder-a", "both-a"), ("decoder-a", "both-a")]:
        require(records[first]["sha256"] != records[second]["sha256"],
                "No observable input sensitivity: %s vs %s; stop for diagnosis" % (first, second))
    return {"result": "PASS_SYNTHETIC_JOINER_ONLY", "runs": records,
            "limitations": ["No real speech/accuracy oracle", "No encoder state feedback",
                            "No TTS or openvela execution", "Fresh process per run, not warm benchmark"]}


def progress_check(root, tag):
    record = output_check(root, tag)
    checks = {"encoder-a": [("zero-a", False)], "decoder-a": [("zero-a", False)],
              "both-a": [("encoder-a", False), ("decoder-a", False)],
              "encoder-b": [("encoder-a", True)], "zero-b": [("zero-a", True)]}
    for previous, same in checks.get(tag, []):
        prior = output_check(root, previous)
        require((record["sha256"] == prior["sha256"]) == same,
                "Repeat/sensitivity mismatch: %s vs %s; stop for diagnosis" % (tag, previous))
    return record


def main():
    try:
        command = sys.argv[1]
        root = Path(sys.argv[2])
        if command == "prepare" and len(sys.argv) == 3:
            prepare(root)
        elif command == "fixtures" and len(sys.argv) == 3:
            fixtures_check(root)
        elif command == "contract" and len(sys.argv) == 3:
            contract(read_file(root).decode("utf-8"))
        elif command == "output" and len(sys.argv) == 4:
            print(json.dumps(progress_check(root, sys.argv[3]), sort_keys=True))
        elif command == "compare" and len(sys.argv) == 3:
            print(json.dumps(compare(root), indent=2, sort_keys=True))
        else:
            raise ValueError("Usage: joiner-fixtures.py prepare|fixtures|contract|output|compare PATH [TAG]")
    except (ValueError, OSError, IndexError, UnicodeError, struct.error) as error:
        print("STOP: " + str(error), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
