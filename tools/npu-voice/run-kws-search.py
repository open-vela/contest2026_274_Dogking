#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""C search core + frozen real encoder outputs + bounded native oracle.

replay never invokes an accelerator and NEVER invents missing logits.
run fills missing decoder/joiner branches on Linux A733; no encoder/live/ASR
or openvela-hardware acceptance is implied by a keyword hit.
"""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
import os
import platform
import shutil
import signal
import stat
import struct
import sys
from pathlib import Path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


SK = module("search_stream", "stream-kws.py")
VB = module("search_batch", "run-voice-batch.py")
R, read, sha = SK.require, SK.read_file, SK.sha
POLICY = dict(max_span=75, max_blank=15, prefix_wait=12, cooldown=12,
              start_floor=.0001, strong_probability=.12, min_strong=2)


class Word(C.Structure):
    _fields_ = [("count", C.c_uint32), ("tokens", C.c_int32 * 16),
                ("boost", C.c_float), ("threshold", C.c_float)]


class Config(C.Structure):
    _fields_ = [("max_span", C.c_uint32), ("max_blank", C.c_uint32),
                ("prefix_wait", C.c_uint32), ("cooldown", C.c_uint32),
                ("start_floor", C.c_float), ("strong_probability", C.c_float),
                ("min_strong", C.c_uint32)]


class Hit(C.Structure):
    _fields_ = [("word", C.c_int32), ("frame", C.c_uint32),
                ("mean_probability", C.c_float), ("relative_score", C.c_float)]


def keywords(text, vocabulary):
    result = []
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        phones, phrase = line.split("@", 1)
        phones, threshold = phones.split("#", 1)
        phones, boost = phones.split(":", 1)
        ids = [vocabulary[token] for token in phones.split()]
        R(1 <= len(ids) <= 16 and phrase.strip(), "Bad keyword")
        result.append(dict(phrase=phrase.strip(), ids=ids, boost=float(boost), threshold=float(threshold)))
    R(1 <= len(result) <= 8, "Bad keyword count")
    return result


class Search:
    def __init__(self, library, words):
        self.lib = C.CDLL(str(library.resolve(strict=True)))
        self.lib.aipet_kws_bytes.restype = C.c_size_t
        self.lib.aipet_kws_init.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Word), C.c_size_t, C.POINTER(Config)]
        self.lib.aipet_kws_contexts.argtypes = [C.c_void_p, C.POINTER(C.c_int32), C.c_size_t]
        self.lib.aipet_kws_advance.argtypes = [C.c_void_p, C.POINTER(C.c_float), C.c_size_t, C.POINTER(Hit)]
        self.lib.aipet_kws_reset.argtypes = [C.c_void_p]
        size = self.lib.aipet_kws_bytes()
        R(0 < size <= 16384, "Unexpected search ABI size")
        self.memory = C.create_string_buffer(size)
        native = (Word * len(words))()
        for dst, src in zip(native, words):
            dst.count = len(src["ids"])
            dst.tokens[:] = src["ids"] + [0] * (16 - len(src["ids"]))
            dst.boost, dst.threshold = src["boost"], src["threshold"]
        self.words = words
        R(self.lib.aipet_kws_init(self.memory, size, native, len(words), C.byref(Config(**POLICY))) == 0,
          "Search init rejected graph/policy")

    def contexts(self):
        pairs = (C.c_int32 * 34)()
        rows = self.lib.aipet_kws_contexts(self.memory, pairs, 34)
        R(1 <= rows <= 17, "Search context ABI error")
        return [tuple(pairs[i*2:i*2+2]) for i in range(rows)]

    def advance(self, logits):
        R(len(logits) == len(self.contexts()), "Logit row order/count mismatch")
        values = b"".join(logits)
        R(all(len(row) == 1052 for row in logits), "Bad row bytes")
        SK.floats(values)
        array = (C.c_float * (len(values)//4)).from_buffer_copy(values)
        hit = Hit()
        R(self.lib.aipet_kws_advance(self.memory, array, len(logits), C.byref(hit)) == 0,
          "Search advance rejected inputs")
        if hit.word < 0:
            return None
        R(hit.word < len(self.words), "Bad hit word index")
        return dict(phrase=self.words[hit.word]["phrase"], frame=hit.frame,
                    mean_probability=hit.mean_probability, relative_score=hit.relative_score)


def check(base):
    contract, _, tokens = SK.load_assets(base)
    locked = json.loads(read(base / "stream-assets.json"))["files"]
    R({"search-fixtures/index.json", "run-kws-search.py", "libaipet-kws-aarch64.so", "keywords.txt"}
      <= set(locked), "Unlocked search components")
    index = json.loads(read(base / "search-fixtures/index.json"))
    R(index["schema"] == 1 and len(index["cases"]) == 6, "Bad frozen case index")
    for case in index["cases"]:
        R(1 <= len(case["encoder_frames"]) <= 200, "Unbounded frozen case")
        for record in case["encoder_frames"]:
            R(sha(read(base / record["path"], 1280)) == record["sha256"], "Frozen encoder changed")
    words = keywords(read(base / "keywords.txt").decode("utf-8"), {v: k for k, v in tokens.items()})
    return contract, words, index


class MissingLogits(Exception):
    pass


class Oracle:
    def __init__(self, base, work, index, runner=None):
        self.base, self.work, self.runner = base, work, runner
        self.cached = {}
        self.decoders = {}
        self.reused = self.fresh = 0
        for entry in index["logits"]:
            data = read(base / entry["path"], 1052)
            R(sha(data) == entry["sha256"], "Changed frozen logits")
            key = (entry["encoder_sha256"], tuple(entry["context"]))
            R(key not in self.cached or self.cached[key][0] == data, "Conflicting frozen outputs")
            self.cached[key] = (data, "frozen:" + entry["origin"])

    def query(self, encoder, context, tag):
        key = (sha(encoder), tuple(context))
        if key in self.cached:
            self.reused += 1
            return self.cached[key]
        if self.runner is None:
            raise MissingLogits(str(dict(encoder_sha256=key[0], context=context)))
        if context not in self.decoders:
            R(len(self.decoders) < 4096, "Decoder cache budget")
            data = self.runner.invoke(tag + "-decoder", "decoder", [struct.pack("<2f", *context)])[0]
            self.decoders[context] = data
        data = self.runner.invoke(tag + "-joiner", "joiner", [encoder, self.decoders[context]])[0]
        self.cached[key] = (data, "native:" + tag + "-joiner")
        self.fresh += 1
        return self.cached[key]


def run(base, work, library, native):
    contract, words, index = check(base)
    runner = SK.Runner(base, work, contract, budget_seconds=3600, max_calls=12000) if native else None
    oracle = Oracle(base, work, index, runner)
    results = []
    try:
        for case in index["cases"]:
            search = Search(library, words)
            hits, frames, blocked = [], [], None
            for frame, record in enumerate(case["encoder_frames"]):
                if native:
                    VB.environment(work, case["tag"] + "-f%d" % frame)
                encoder = read(base / record["path"], 1280)
                contexts = search.contexts()
                try:
                    rows = [oracle.query(encoder, ctx, "%s-f%d-r%d" % (case["tag"], frame, row))
                            for row, ctx in enumerate(contexts)]
                except MissingLogits as error:
                    blocked = str(error)
                    break
                hit = search.advance([data for data, origin in rows])
                frames.append(dict(frame=frame, contexts=contexts, origins=[origin for data, origin in rows], hit=hit))
                if hit:
                    hits.append(hit)
                print("SEARCH %s frame=%d/%d contexts=%d hits=%d fresh=%d" %
                      (case["tag"], frame+1, len(case["encoder_frames"]), len(contexts), len(hits), oracle.fresh), flush=True)
            names = [hit["phrase"] for hit in hits]
            result = dict(scenario=case["tag"], expected=case["expected"], hits=hits, frames=frames,
                          status="NEEDS_NATIVE_BRANCH" if blocked else "COLLECTED",
                          missing=blocked, classification_ok=None if blocked else
                          (bool(names) if case["expected"] else not names),
                          exact_phrase_ok=None if blocked else
                          (case["expected"] in names if case["expected"] else not names))
            VB.write_json(work / (case["tag"] + ".json"), result)
            results.append({k: v for k, v in result.items() if k != "frames"})
        summary = dict(result="SEARCH_REGRESSION_COLLECTED", policy=POLICY, words=words, cases=results,
                       frozen_encoder_provenance=index["provenance"], fresh_joiner_branches=oracle.fresh,
                       cached_queries=oracle.reused, native_process_calls=runner.calls if runner else 0,
                       openvela_hardware_executed=False, end_to_end_accepted=False,
                       note="Frozen Linux NPU encoder outputs. No live mic/encoder/ASR/TTS/Agent run; policy uncalibrated.")
        VB.write_json(work / "search-summary.json", summary)
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        return summary
    finally:
        if runner:
            runner.close()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=["check", "replay", "run"])
    p.add_argument("base", type=Path)
    p.add_argument("work", nargs="?", type=Path)
    p.add_argument("--library", type=Path)
    p.add_argument("--npu-idle-confirmed", action="store_true")
    args = p.parse_args()
    try:
        base = args.base.resolve(strict=True)
        if args.mode == "check":
            check(base); print("Search kit verified; no shared library/NPU executed")
        else:
            R(args.work and args.work.is_dir(), "Use a NEW existing report directory")
            if args.mode == "run":
                R(args.npu_idle_confirmed and platform.system() == "Linux" and platform.machine() == "aarch64" and
                  stat.S_ISCHR(os.stat("/dev/vipcore").st_mode), "Use confirmed physical A733 Linux wrapper")
                R(shutil.disk_usage(args.work).free >= 1024**3, "Need 1 GiB report space")
                R(args.library is None, "Native mode only executes manifest-locked packaged library")
            def interrupted(signum, frame):
                raise KeyboardInterrupt("Signal %d" % signum)
            signal.signal(signal.SIGINT, interrupted); signal.signal(signal.SIGTERM, interrupted)
            library = args.library or base / "libaipet-kws-aarch64.so"
            run(base, args.work, library, args.mode == "run")
    except KeyboardInterrupt as error:
        print("STOP:", error, file=sys.stderr); sys.exit(130)
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        print("STOP:", error, file=sys.stderr); sys.exit(2)
