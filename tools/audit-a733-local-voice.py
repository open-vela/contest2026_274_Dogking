#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only dependency audit. NBG header validity is NOT execution evidence."""
import argparse
import hashlib
import json
import re
import struct
import wave
from pathlib import Path

REFERENCE_ARCHIVE_SHA256 = "af54166cc85522d7585c51cb44fe2fe57b894be929d21d94c5f08fb29f839f08"
RADXA_ZOO_ARCHIVE_SHA256 = "f209bbcbc1222c52035ff0ea9796b9de6cdb96598a1c5cf7aaec2a20ab2673a5"
USER_ZOO_V110_SHA256 = "7023c82789956db0ee7cab31c6c0a2c4bcdcc274a7f6c5a7d3fd4260186aa0be"
# Content lock of the downloaded snapshot, not a Git commit or license grant.
LOCK = {
    "prebuilt/kws/encoder_float_a733.nb": "c89747b37baca00ad8bf70126b3bd6b7b36def0875a6bdb33d95bcb8f65707c0",
    "prebuilt/kws/decoder_float_a733.nb": "034f5266ccf5b62cc507593c0c4fec708073365282fc76b15521ee424f3035cc",
    "prebuilt/kws/joiner_float_a733.nb": "f51cb0ceca61a44ee6bc8618d3ebbac788e7c3744a210c5c24cac9c6ffa4bf5e",
    "prebuilt/kws/tokens.txt": "2d3f32311f9b692b964da3c90e830258d3e78e013cb0c992dbfb15cd5a1a71b0",
    "prebuilt/vocoder/vocoder_int16_a733.nb": "00121f4ccb6d50833ccddc92d14da717e66e2f044c05316924cbf215ecf3403a",
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def nbg(path):
    with path.open("rb") as source:
        header = source.read(64)
    if len(header) != 64:
        raise ValueError("truncated NBG header")
    magic, version, cid = struct.unpack_from("<III", header)
    if magic != 0x4e4d5056 or version not in (0x10020, 0x20000) or cid != 0x1000003b:
        raise ValueError(f"unsupported magic/version/CID {magic:08x}/{version:08x}/{cid:08x}")
    return {"format": "NBG (not A7PM)", "version": f"{version:08x}",
            "cid": f"{cid:08x}",
            "existing_metadata_probe_accepts_version": version == 0x10020,
            "execution_proven": False}


def wav_info(path):
    with wave.open(str(path), "rb") as source:
        if source.getcomptype() != "NONE" or source.getsampwidth() != 2:
            raise ValueError("expected PCM16 fixture")
        frames = source.getnframes()
        channels = source.getnchannels()
        rate = source.getframerate()
        if channels != 1 or rate != 16000 or not 0 < frames <= 16000 * 15:
            raise ValueError("expected bounded mono/16k fixture")
        if len(source.readframes(frames)) != frames * 2:
            raise ValueError("truncated WAV data")
    return {"rate": rate, "channels": channels, "frames": frames,
            "seconds": frames / rate, "sha256": sha256(path)}


def audit(reference, zoo, repo):
    report = {"schema": 1, "openvela_voice_ready": False,
              "audit_only": True, "artifacts": [], "fixtures": [], "blockers": []}
    for name, expected in LOCK.items():
        path = reference / name
        entry = {"path": name, "expected_sha256": expected}
        try:
            if not path.is_file() or path.is_symlink():
                raise ValueError("missing or symlink artifact")
            entry.update(bytes=path.stat().st_size, sha256=sha256(path))
            if entry["sha256"] != expected:
                raise ValueError("content does not match locked reference")
            if path.suffix == ".nb":
                entry.update(nbg(path))
                if not entry["existing_metadata_probe_accepts_version"]:
                    report["blockers"].append(f"{name}: NBG v2.0 header; old metadata probe accepts 0x10020 only")
            entry["valid"] = True
        except (OSError, ValueError) as error:
            entry.update(valid=False, error=str(error))
            report["blockers"].append(f"artifact: {name}: {error}")
        report["artifacts"].append(entry)
    for path in sorted((reference / "samples").glob("*.wav")):
        try:
            report["fixtures"].append(dict(path=path.name, **wav_info(path)))
        except (OSError, ValueError, wave.Error, EOFError) as error:
            report["blockers"].append(f"fixture: {path.name}: {error}")
    if not report["fixtures"]:
        report["blockers"].append("no WAV fixtures")
    report["zoo"] = {"path": str(zoo), "version": None}
    version = zoo / "version"
    if version.is_file():
        report["zoo"]["version"] = version.read_text(encoding="utf-8").strip()
    for name in ("zipformer", "tts"):
        directory = zoo / "examples" / name
        report["zoo"][name] = directory.is_dir()
        if not directory.is_dir():
            report["blockers"].append(f"Model Zoo lacks examples/{name}")
    zipformer = zoo / "examples/zipformer"
    report["asr_assets"] = {"compiled_models": {}, "expected_encoder_inputs": 36,
                            "segment_frames": 103, "mel_bins": 80,
                            "kws_is_not_asr": True}
    for role in ("encoder", "decoder", "joiner"):
        assets = sorted((zipformer / "model").glob(role + "*.nb"))
        report["asr_assets"]["compiled_models"][role] = [
            {"path": str(p), "bytes": p.stat().st_size, "sha256": sha256(p)}
            for p in assets if p.is_file() and not p.is_symlink()]
        if not report["asr_assets"]["compiled_models"][role]:
            report["blockers"].append(f"ASR compiled {role} NBG absent (source/calibration is not a model)")
    report["asr_assets"]["calibration_present"] = (zoo / "examples/dataset/zipformer").is_dir()
    frontend = repo / "board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/voice_fbank.cxx"
    report["frontend"] = {"source_present": frontend.is_file(), "cpu_only": True,
                           "target_acoustic_acceptance": False, "npu_inference": False}
    acoustic = reference / "models/tts/matcha-icefall-zh-baker/model-steps-3.onnx"
    if not acoustic.is_file():
        acoustic = reference.parent / "matcha-icefall-zh-baker/model-steps-3.onnx"
    report["acoustic"] = {"cpu_only_in_reference": True, "present": acoustic.is_file(), "path": str(acoustic),
                          "license": "Baker non-commercial dataset; verify deployment rights"}
    if acoustic.is_file():
        report["acoustic"].update(bytes=acoustic.stat().st_size, sha256=sha256(acoustic))
    else:
        report["blockers"].append("Matcha acoustic ONNX missing")
    report["blockers"].append("Matcha target CPU runtime not ported; downloaded ONNX is not executable firmware")
    runtime = repo / "board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_npu_runtime.c"
    text = runtime.read_text(encoding="utf-8")
    limits = {}
    for key in ("INPUTS", "OUTPUTS", "REGIONS"):
        match = re.search(r"#define A733_RT_A7PM_MAX_" + key + r"\s+(\d+)u", text)
        limits[key.lower()] = int(match[1]) if match else None
    report["existing_runtime"] = dict(limits, dynamic_input="one file", output="CRC + 32-byte preview",
                                      source_sha256=sha256(runtime))
    report["blockers"].extend([
        "KWS reference encoder 39-in/39-out needs full recurrent-state I/O, not current A7PM limits",
        "authorized voice prepared packages, Linux golden tensors and target dynamic-input proofs missing",
        "CPU fbank frontend source is not KWS/ASR inference; endpointing, recurrent executor and offline product routing pending",
        "native 22050 Hz playback unsupported; play22k CPU adapter is host-tested only, target pending",
    ])
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--zoo", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--report", type=Path, help="create a new report, never replace an old one")
    args = parser.parse_args()
    try:
        report = audit(args.reference, args.zoo, args.repo)
        content = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.report:
            with args.report.open("x", encoding="utf-8", newline="\n") as dest:
                dest.write(content)
        print(content, end="")
        # An audit always has an incomplete inference gate in this revision.
        return 2 if report["blockers"] else 0
    except (OSError, ValueError) as error:
        parser.exit(1, f"audit failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
