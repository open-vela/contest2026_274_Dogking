#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile changed C with a real saved NuttX compile database, no SDK writes.

This is a target-object check, NOT a linked/booted image or hardware test.
Uses the project's existing compiler/config, keeps legacy vs extension distinct.
"""
import argparse
import hashlib
import json
import shlex
import subprocess
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(build, output):
    repo = Path(__file__).resolve().parent.parent
    overlay = repo / "board/a733-cubie-a7z/openvela-overlay"
    chip = overlay / "vendor/allwinnertech/chips/a733"
    db = json.loads((build / "compile_commands.json").read_text())
    kernel = next(e for e in db if e["file"].endswith("/a733_npu_runtime.c"))
    client = next(e for e in db if e["file"].endswith("/a733_audio_client.c"))
    output.mkdir()
    records = []
    for label, source, original, enabled in [
        ("runtime-baseline-disabled", chip / "a733_npu_runtime.c", kernel, False),
        ("runtime-tensors-enabled", chip / "a733_npu_runtime.c", kernel, True),
        ("tensor-plan", chip / "a733_tensor_plan.c", kernel, True),
        ("tensor-client", chip / "a733_tensor_client.c", client, False),
        ("kws-search", overlay / "apps/system/aipet/voice_kws.c", client, False)]:
        argv = shlex.split(original["command"])
        filtered = [argv[0], "-I" + str(overlay / "nuttx/include"),
                    "-I" + str(overlay / "apps/include"), "-I" + str(chip / "include")]
        index = 1
        while index < len(argv):
            if argv[index] in ("-o", "-c", "-MF", "-MT", "-MQ"):
                index += 2
            elif argv[index] in ("-MD", "-MMD"):
                index += 1
            else:
                filtered.append(argv[index]); index += 1
        if enabled:
            filtered.append("-DCONFIG_A733_NPU_TENSOR_IO=1")
        target = output / (label + ".o")
        filtered += ["-c", str(source), "-o", str(target)]
        with (output / (label + ".log")).open("xb") as log:
            status = subprocess.run(filtered, cwd=original["directory"], stdout=log, stderr=subprocess.STDOUT).returncode
        record = dict(label=label, source_sha256=sha(source), command=filtered, status=status,
                      output_sha256=sha(target) if target.exists() else None)
        records.append(record)
        print(label, "PASS" if status == 0 else "FAILED", flush=True)
        if status:
            break
    with (output / "compile-summary.json").open("x") as stream:
        sources = [chip / "a733_npu_runtime.c", chip / "a733_tensor_runtime.inc", chip / "a733_tensor_crc.inc", chip / "a733_tensor_plan.c",
                   chip / "include/a733_tensor_plan.h", chip / "a733_tensor_client.c",
                   overlay / "nuttx/include/nuttx/npu/a733_vip2.h", overlay / "nuttx/include/nuttx/npu/a733_tensor.h",
                   overlay / "apps/include/system/a733_tensor_client.h"]
        json.dump(dict(records=records, sources={str(p.relative_to(repo)): sha(p) for p in sources},
                       base_config_sha256=sha(build / ".config"), sdk_modified=False,
                       linked_image=False, hardware_executed=False), stream, indent=2)
    if len(records) != 5 or any(r["status"] for r in records):
        raise SystemExit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("existing_build", type=Path)
    parser.add_argument("new_output", type=Path)
    args = parser.parse_args()
    run(args.existing_build.resolve(strict=True), args.new_output.resolve())
