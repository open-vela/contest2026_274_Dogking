#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Static, read-only audit of locked upstream graphs and optional Zoo NPYs.
Requires onnx==1.17.0, protobuf==5.29.5, numpy in the host interpreter.
Never loads external tensor data, runs an ONNX engine or calls the NPU.
"""
import argparse
import collections
import importlib.util
import json
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--calibration", type=Path, help="Zoo examples/dataset/zipformer")
    args = parser.parse_args()
    import onnx
    import numpy as np
    spec = importlib.util.spec_from_file_location("source_lock", Path(__file__).with_name("fetch-asr-source.py"))
    lock = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lock)
    report = {"schema": 1, "revision": lock.REVISION, "static_only": True,
              "nbg_converted": False, "npu_executed": False,
              "onnx_version": onnx.__version__, "numpy_version": np.__version__, "models": []}

    def interface(value):
        tensor = value.type.tensor_type
        return {"name": value.name, "dtype": onnx.TensorProto.DataType.Name(tensor.elem_type),
                "shape": [d.dim_value if d.HasField("dim_value") else d.dim_param for d in tensor.shape.dim]}

    for name, (size, digest) in lock.FILES.items():
        path = args.directory / name
        lock.verify_file(path, size, digest)
        model = onnx.load(str(path), load_external_data=False)
        role = name.split("-", 1)[0]
        entry = {"role": role, "file": name, "bytes": size, "sha256": digest,
                 "inputs": [interface(x) for x in model.graph.input],
                 "outputs": [interface(x) for x in model.graph.output],
                 "metadata": {x.key: x.value for x in model.metadata_props},
                 "opsets": {x.domain: x.version for x in model.opset_import},
                 "node_domains": dict(collections.Counter(x.domain for x in model.graph.node)),
                 "external_initializers": sum(x.data_location == onnx.TensorProto.EXTERNAL for x in model.graph.initializer)}
        if args.calibration:
            calibration = []
            for value in model.graph.input:
                if not re.fullmatch(r"[A-Za-z_0-9]+", value.name):
                    raise ValueError("unsafe tensor name")
                sample = args.calibration / role / (value.name + ".npy")
                if role == "decoder" and value.name == "y":
                    # Vendor decoder uses TEXT, not a y.npy named dataset.
                    listing = args.calibration / role / "dataset.txt"
                    if listing.is_symlink() or listing.stat().st_size > 4096:
                        raise ValueError("invalid decoder dataset list")
                    entry["calibration_mode"] = "TEXT(dataset.txt)"
                    for line in listing.read_text().splitlines():
                        if not re.fullmatch(r"\./[A-Za-z_0-9]+\.npy", line):
                            raise ValueError("unsafe decoder dataset item")
                        fixture = args.calibration / role / line[2:]
                        if fixture.is_symlink() or fixture.stat().st_size > 64 * 1024 * 1024:
                            raise ValueError("invalid decoder fixture")
                        array = np.load(fixture, mmap_mode="r", allow_pickle=False, max_header_size=4096)
                        calibration.append({"input": value.name, "file": fixture.name, "present": True,
                                            "shape": list(array.shape), "dtype": str(array.dtype)})
                        del array
                    continue
                if not sample.is_file():
                    calibration.append({"input": value.name, "present": False})
                    continue
                if sample.is_symlink() or sample.stat().st_size > 64 * 1024 * 1024:
                    raise ValueError("invalid calibration file")
                array = np.load(sample, mmap_mode="r", allow_pickle=False, max_header_size=4096)
                calibration.append({"input": value.name, "present": True,
                                    "shape": list(array.shape), "dtype": str(array.dtype)})
                del array
            entry["calibration"] = calibration
        report["models"].append(entry)
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
