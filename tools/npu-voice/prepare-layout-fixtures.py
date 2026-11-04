#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Copy four already verified native I/O sets into a new private trace kit."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(report, output):
    selected = {"encoder": "encoder-reload-a", "decoder": "decoder-reload-a",
                "joiner": "joiner-reload-a", "vocoder": "vocoder-tts-0-0-0"}
    records = {}
    with (report / "tensor-trace.jsonl").open(encoding="utf-8") as stream:
        for line in stream:
            row = json.loads(line)
            if row["tag"] in selected.values():
                if row["tag"] in records:
                    raise ValueError("Duplicate native reference tag")
                records[row["tag"]] = row
    output.mkdir()
    models = {}
    for name, tag in selected.items():
        record = records[tag]
        if record["model"] != name:
            raise ValueError("Wrong reference model")
        folder = output / name; folder.mkdir()
        inputs, outputs = [], []
        for index, expected in enumerate(record["inputs"]):
            data = (report / (tag + "-input") / ("input%d.bin" % index)).read_bytes()
            if digest(data) != expected:
                raise ValueError("Changed original input")
            path = folder / ("input%d.bin" % index)
            with path.open("xb") as stream:
                stream.write(data)
            inputs.append(dict(path="layout-fixtures/%s/%s" % (name,path.name), sha256=expected))
        for index, expected in enumerate(record["outputs"]):
            data = (report / tag / ("output%d.bin" % index)).read_bytes()
            if digest(data) != expected:
                raise ValueError("Changed original output")
            outputs.append(dict(sha256=expected, bytes=len(data)))
        models[name] = dict(model="models/vocoder/vocoder_int16_a733.nb" if name == "vocoder" else
                            "models/kws/%s_float_a733.nb" % name, inputs=inputs, outputs=outputs, origin=tag)
    result = dict(models=models, provenance=dict(report=report.name,
                  summary_sha256=digest((report / "batch-summary.json").read_bytes()),
                  source="encoder first normalized chunk / decoder root / joiner zero / real speech vocoder",
                  source_graph_or_commands_synthesized=False))
    with (output / "index.json").open("x",encoding="utf-8") as stream:
        json.dump(result,stream,ensure_ascii=False,indent=2)
    print("Prepared exactly four fixed native reference I/O sets; original report unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report",type=Path)
    parser.add_argument("new_output",type=Path)
    args = parser.parse_args()
    prepare(args.report.resolve(strict=True),args.new_output)
