#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fetch immutable upstream ONNX references; never execute/convert a model.

Use a NEW directory. Files are NOT Allwinner NBGs or openvela A7PMs.
Incomplete .part files are preserved on failure for inspection, never reused.
"""
import argparse
import hashlib
import time
import urllib.request
from pathlib import Path

REVISION = "e2382758de9a0219b4efe682b95af30b399db3b8"
BASE = "https://huggingface.co/csukuangfj/k2fsa-zipformer-bilingual-zh-en-t/resolve/" + REVISION + "/exp/96/"
FILES = {
    "decoder-epoch-99-avg-1.onnx": (13877276, "89be509a83175261695bdef5fd1c7b9ab1129a663d1284e7ba9f8507b21e0906"),
    "encoder-epoch-99-avg-1.onnx": (88476276, "d53e858189c32678fd97dc40bf3a2dcf4acf462fd6530c265afecb6496156aef"),
    "joiner-epoch-99-avg-1.onnx": (12833675, "bcbcf0cc1b449bc32b7777084ab5a65d1199b822e2aae149d3a43adc9f4b5775"),
}


def fetch(directory, resume=False):
    if resume:
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError("resume requires an existing nonsymlink directory")
    else:
        directory.mkdir(parents=False, exist_ok=False)
    for name, (size, expected) in FILES.items():
        path = directory / name
        if resume and path.exists():
            verify_file(path, size, expected)
            print(name, "already verified; skip", flush=True)
            continue
        partial = path.with_suffix(path.suffix + ".part")
        digest, count = hashlib.sha256(), 0
        deadline = time.monotonic() + 300
        request = urllib.request.Request(BASE + name, headers={"User-Agent": "Dogking-ASR-source-audit/153"})
        with urllib.request.urlopen(request, timeout=60) as response, partial.open("xb") as dest:
            if not response.url.startswith("https://"):
                raise ValueError("refuse non-TLS model response")
            while True:
                if time.monotonic() > deadline:
                    raise TimeoutError("model download deadline exceeded")
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                count += len(chunk)
                if count > size:
                    raise ValueError("unexpected oversized model")
                digest.update(chunk)
                dest.write(chunk)
        if count != size or digest.hexdigest() != expected:
            raise ValueError("model size/SHA-256 mismatch: " + name)
        # New output directory and exclusive .part ensure no older artifact is replaced.
        if path.exists():
            raise FileExistsError(path)
        partial.rename(path)
        print(name, count, digest.hexdigest(), flush=True)


def verify_file(path, size, expected):
    if path.is_symlink() or path.stat().st_size != size:
        raise ValueError("invalid reference: " + path.name)
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != expected:
        raise ValueError("SHA-256 mismatch: " + path.name)


def verify(directory):
    for name, (size, expected) in FILES.items():
        verify_file(directory / name, size, expected)
        print(name, "verified", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--download", action="store_true")
    mode.add_argument("--verify", action="store_true")
    parser.add_argument("--resume", action="store_true", help="skip only hash-verified complete files; do not reuse .part")
    args = parser.parse_args()
    try:
        if args.resume and not args.download:
            parser.error("--resume requires --download")
        if args.download:
            fetch(args.directory, args.resume)
        else:
            verify(args.directory)
        print("ONNX source only. No NBG conversion, NPU execution or ASR acceptance.")
    except (OSError, ValueError) as error:
        parser.exit(1, "source preparation failed (partial output retained): " + str(error) + "\n")
