#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Four fixed native goldens + bounded PREPARED memory/command layout capture.

No command synthesis, NBG conversion, ASR claim, live capture, reset or retry.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import platform
import re
import shutil
import signal
import stat
import subprocess
import sys
import time
from pathlib import Path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec); spec.loader.exec_module(value)
    return value


SK = module("layout_stream", "stream-kws.py")
VB = module("layout_batch", "run-voice-batch.py")
R, read, sha = SK.require, SK.read_file, SK.sha


def check(base):
    contract, _, _ = SK.load_assets(base)
    contract["vocoder"] = json.loads(read(base / "vocoder-contract.json"))
    locked = json.loads(read(base / "stream-assets.json"))["files"]
    R({"voice-layout-linux-aarch64", "libvip-tensor-trace.so", "layout-fixtures/index.json",
       "capture-voice-layout.py", "vocoder-contract.json"} <= set(locked), "Unlocked capture components")
    fixture = json.loads(read(base / "layout-fixtures/index.json"))
    R(set(fixture["models"]) == set(contract), "Need exactly four captured model contracts")
    for name, model in fixture["models"].items():
        R(len(model["inputs"]) == len(contract[name]["input"]) and
          len(model["outputs"]) == len(contract[name]["output"]), "Wrong fixture counts")
        for i, entry in enumerate(model["inputs"]):
            data = read(base / entry["path"], contract[name]["input"][i]["bytes"])
            R(sha(data) == entry["sha256"], "Changed fixed native input")
            SK.floats(data)
    return contract, fixture


def trace_manifest(folder, max_buffer=32*1024**2):
    entries = {}
    for path in folder.iterdir():
        R(path.is_file() and not path.is_symlink() and
          re.fullmatch(r"[a-zA-Z0-9_.-]+", path.name), "Unexpected trace file")
        R(0 < path.stat().st_size <= max_buffer,
          "Invalid trace file %s: %d bytes (bound=%d)" % (path.name,path.stat().st_size,max_buffer))
        h = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024*1024), b""):
                h.update(block)
        entries[path.name] = dict(bytes=path.stat().st_size, sha256=h.hexdigest())
    R(sum(e["bytes"] for e in entries.values()) <= 513*1024**2, "Trace exceeds total bound")
    return entries


def bindings(log, folder, records, inputs, outputs, max_buffer=32*1024**2):
    result = {}
    for kind, buffers, phase in (("input", inputs, "pre-submit"), ("output", outputs, "post-wait")):
        addresses = re.findall(r"^native-buffer %s index=([0-9]+) logical=([0-9a-f]{16}) bytes=([0-9]+)$" % kind,
                               log, re.MULTILINE)
        R(len(addresses) == len(buffers) and [int(row[0]) for row in addresses] == list(range(len(buffers))),
          "Missing/duplicate native logical bindings")
        groups = {}
        for record in records:
            if record.get("kind") == "snapshot" and record.get("phase") == phase:
                groups.setdefault(record["snapshot"], []).append(record)
        candidates = []
        for number, group in sorted(groups.items()):
            rows, file_cache = [], {}
            for (index, logical, size), expected in zip(addresses, buffers):
                logical, size = int(logical, 16), int(size)
                if size != len(expected):
                    break
                matches = [e for e in group if int(e["logical"],16) <= logical and
                           logical+size <= int(e["logical"],16)+e["bytes"]]
                if len(matches) != 1:
                    break
                entry = matches[0]; name = entry["file"]
                R(re.fullmatch(r"[a-zA-Z0-9_.-]+", name), "Unsafe snapshot path")
                if name not in file_cache:
                    path = folder / name
                    R(path.is_file() and not path.is_symlink() and path.stat().st_size == entry["bytes"] and
                      entry["bytes"] <= max_buffer, "Truncated/unsafe native snapshot")
                    file_cache[name] = path.read_bytes()
                offset = logical - int(entry["logical"],16)
                if file_cache[name][offset:offset+size] != expected:
                    break
                rows.append(dict(index=int(index), logical="%016x" % logical,
                                 vip="%016x" % (int(entry["vip"],16)+offset), bytes=size,
                                 snapshot=number, file=name, allocation_offset=offset,
                                 tensor_sha256=sha(expected)))
            if len(rows) == len(buffers):
                candidates.append(rows)
        R(candidates, "No complete native %s snapshot binding; preserve trace, do NOT invent addresses" % kind)
        result[kind] = candidates[-1]
    return result


def run(base, work):
    contract, fixture = check(base)
    results = []; deadline = time.monotonic()+600
    for name in ("encoder", "decoder", "joiner", "vocoder"):
        VB.environment(work, "layout-" + name)
        R(time.monotonic() < deadline, "Capture total budget reached")
        model = fixture["models"][name]
        folder = work / (name + "-trace"); folder.mkdir(mode=0o700)
        logpath = work / (name + ".log")
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = str(base / "lib")
        env["LD_PRELOAD"] = str(base / "libvip-tensor-trace.so")
        env["VIP_TENSOR_TRACE_DIR"] = str(folder)
        command = ["timeout", "--signal=TERM", "--kill-after=5s", "60s",
                   str(base / "voice-layout-linux-aarch64"), str(base / model["model"]),
                   str(base / "layout-fixtures" / name), str(work / (name + "-output"))]
        print("LAYOUT %s: one fixed native run; bounded trace + full golden comparison" % name, flush=True)
        with logpath.open("xb") as log:
            process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=min(66,max(.01,deadline-time.monotonic())))
            except BaseException:
                SK.stop_process(process); raise
            log.write(("process-exit=%d tee-exit=0\n" % code).encode())
        R(code == 0, "Native layout run failed; preserve report, no retry/reset")
        text = read(logpath).decode()
        R(SK.metadata(text) == contract[name], "Capture binary contract drift")
        inputs, outputs = [], []
        for i, entry in enumerate(model["inputs"]):
            data = read(base / entry["path"], contract[name]["input"][i]["bytes"])
            value = SK._jf.one(r"^input\[%d\] bytes=%d raw-crc32=([0-9a-f]{8})$" % (i,len(data)), text)
            R(int(value,16) == SK.crc(data), "Traced input CRC changed")
            inputs.append(data)
        for i, entry in enumerate(model["outputs"]):
            data = read(work / (name + "-output") / ("output%d.bin" % i), contract[name]["output"][i]["bytes"])
            SK.floats(data)
            R(sha(data) == entry["sha256"], "Tracing changed native golden output")
            value, changed = SK._jf.one(r"^output\[%d\] bytes=%d raw-crc32=([0-9a-f]{8}) changed=([0-9]+) status=0$" %
                                       (i,len(data)), text)
            R(int(value,16) == SK.crc(data) and int(changed) == sum(v != 0xa5 for v in data) and int(changed)>0,
              "Bad traced output CRC/prefill")
            outputs.append(data)
        records = [json.loads(line) for line in read(folder / "trace.jsonl").decode().splitlines()]
        final = [entry for entry in records if entry.get("kind") == "final"]
        errors = [entry for entry in records if entry.get("kind") == "read-error"]
        R(not errors, "Native trace read failure: " + json.dumps(errors[:2],sort_keys=True))
        R(len(final)==1 and final[0]["failed"]==0 and final[0]["snapshots"]>=2,
          "Incomplete/failed native trace; inspect %s/trace.jsonl" % folder.name)
        files = trace_manifest(folder)
        bound = bindings(text, folder, records, inputs, outputs)
        VB.write_json(work / (name + "-trace-manifest.json"), files)
        VB.write_json(work / (name + "-bindings.json"), bound)
        results.append(dict(model=name, raw_outputs_match_prior_golden=True, bindings=bound,
                            trace_bytes=final[0]["dumped_bytes"], snapshots=final[0]["snapshots"]))
        print("LAYOUT %s: all native outputs/bindings verified" % name, flush=True)
    VB.write_json(work / "layout-summary.json", dict(result="NATIVE_GOLDENS_AND_LAYOUT_CAPTURED", models=results,
                   provenance=fixture["provenance"], prepared_package_exported=False,
                   openvela_hardware_executed=False, end_to_end_accepted=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["check", "run"])
    parser.add_argument("base", type=Path)
    parser.add_argument("work", nargs="?", type=Path)
    parser.add_argument("--npu-idle-confirmed", action="store_true")
    args = parser.parse_args()
    try:
        if args.mode == "check":
            check(args.base.resolve(strict=True)); print("Layout assets/fixtures verified; no NPU calls")
        else:
            R(args.npu_idle_confirmed and platform.system()=="Linux" and platform.machine()=="aarch64" and
              stat.S_ISCHR(os.stat("/dev/vipcore").st_mode) and args.work and args.work.is_dir(), "Use physical A733 Linux wrapper")
            R(shutil.disk_usage(args.work).free >= 2*1024**3, "Need 2 GiB report space")
            def interrupted(signum, frame): raise KeyboardInterrupt("Signal %d" % signum)
            signal.signal(signal.SIGINT,interrupted); signal.signal(signal.SIGTERM,interrupted)
            run(args.base.resolve(strict=True),args.work)
    except KeyboardInterrupt as error:
        print("STOP:",error,file=sys.stderr); sys.exit(130)
    except (ValueError,OSError,KeyError,TypeError) as error:
        print("STOP:",error,file=sys.stderr); sys.exit(2)
