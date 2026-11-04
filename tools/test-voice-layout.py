#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host mocks: complete I/O bindings, malformed traces and no automatic retry."""
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec=importlib.util.spec_from_file_location("voice_layout",Path(__file__).parent/"npu-voice/capture-voice-layout.py")
VL=importlib.util.module_from_spec(spec); spec.loader.exec_module(VL)


class LayoutTests(unittest.TestCase):
    def fixture(self,root):
        source=struct.pack("<2f",.25,.5); output=struct.pack("<f",.75)
        (root/"input.bin").write_bytes(source)
        records=[dict(kind="snapshot",phase="pre-submit",snapshot=1,logical="0000000000001000",vip="0000000003000000",
                      bytes=len(source)+16,file="before.bin"),
                 dict(kind="snapshot",phase="post-wait",snapshot=2,logical="0000000000002000",vip="0000000004000000",
                      bytes=len(output)+16,file="after.bin")]
        (root/"before.bin").write_bytes(bytes(8)+source+bytes(8))
        (root/"after.bin").write_bytes(bytes(8)+output+bytes(8))
        log="native-buffer input index=0 logical=0000000000001008 bytes=8\n" \
            "native-buffer output index=0 logical=0000000000002008 bytes=4\n"
        return source,output,records,log

    def test_exact_suballocation_binding_and_not_an_address_guess(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); source,output,records,log=self.fixture(root)
            result=VL.bindings(log,root,records,[source],[output])
            self.assertEqual(result["input"][0]["vip"],"0000000003000008")
            self.assertEqual(result["output"][0]["allocation_offset"],8)

    def test_missing_duplicate_ambiguous_and_wrong_snapshot_rejected(self):
        for kind in ("missing","duplicate","ambiguous","corrupt","truncated"):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as folder:
                root=Path(folder); source,output,records,log=self.fixture(root)
                if kind=="missing": log=""
                if kind=="duplicate": log+=log
                if kind=="ambiguous": records.append(records[0].copy())
                if kind=="corrupt": (root/"after.bin").write_bytes(bytes(len(output)+16))
                if kind=="truncated": (root/"before.bin").write_bytes(bytes(1))
                with self.assertRaises(ValueError): VL.bindings(log,root,records,[source],[output])

    def test_trace_symlinks_and_empty_files_are_not_proof(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); (root/"empty.bin").touch()
            with self.assertRaisesRegex(ValueError,"empty.bin: 0 bytes"): VL.trace_manifest(root)

    def test_four_runs_and_bad_golden_or_trace_stop_without_retry(self):
        for failure in (None,"native-exit","golden","trace","read-error","empty"):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as folder:
                root=Path(folder); base=root/"kit";work=root/"report";base.mkdir();work.mkdir()
                source=struct.pack("<2f",.25,.5); output=struct.pack("<f",.75)
                (base/"input.bin").write_bytes(source)
                names=("encoder","decoder","joiner","vocoder")
                desc=lambda n: dict(bytes=n*4,format=0,quant=0,dims=[n])
                contracts={name:dict(input=[desc(2)],output=[desc(1)]) for name in names}
                fixture=dict(provenance={"mock":True},models={name:dict(model=name+".nb",
                            inputs=[dict(path="input.bin",sha256=VL.sha(source))],
                            outputs=[dict(sha256=VL.sha(output))]) for name in names})
                seen=[]
                class Process:
                    def __init__(self,argv,**kwargs):
                        name=Path(argv[-3]).stem; seen.append(name)
                        target=Path(argv[-1]);target.mkdir()
                        actual=bytes(4) if failure=="golden" else output
                        (target/"output0.bin").write_bytes(actual)
                        trace=Path(kwargs["env"]["VIP_TENSOR_TRACE_DIR"])
                        (trace/"before.bin").write_bytes(bytes(8)+source+bytes(8))
                        (trace/"after.bin").write_bytes(bytes(8)+actual+bytes(8))
                        records=[dict(kind="snapshot",phase="pre-submit",snapshot=1,logical="0000000000001000",vip="0000000003000000",
                                      bytes=24,file="before.bin"),
                                 dict(kind="snapshot",phase="post-wait",snapshot=2,logical="0000000000002000",vip="0000000004000000",
                                      bytes=20,file="after.bin"),
                                 dict(kind="final",failed=1 if failure=="trace" else 0,snapshots=2,dumped_bytes=44)]
                        (trace/"trace.jsonl").write_text("".join(json.dumps(r)+"\n" for r in records))
                        if failure=="read-error":
                            with (trace/"trace.jsonl").open("a") as stream:
                                stream.write(json.dumps(dict(kind="read-error",file="before.bin",errno=14))+"\n")
                        if failure=="empty": (trace/"before.bin").write_bytes(b"")
                        log="inputs=1 outputs=1\ninput[0] name=x padded-bytes=8 format=0 quant=0 dims=2\n" \
                            "output[0] name=y padded-bytes=4 format=0 quant=0 dims=1\n"
                        log+="input[0] bytes=8 raw-crc32=%08x\n"%VL.SK.crc(source)
                        log+="output[0] bytes=4 raw-crc32=%08x changed=4 status=0\n"%VL.SK.crc(actual)
                        log+="native-buffer input index=0 logical=0000000000001008 bytes=8\n" \
                             "native-buffer output index=0 logical=0000000000002008 bytes=4\nreference-status=0; mocked\n"
                        kwargs["stdout"].write(log.encode())
                    def wait(self,timeout): return 124 if failure=="native-exit" else 0
                with patch.object(VL,"check",return_value=(contracts,fixture)), \
                     patch.object(VL.VB,"environment"),patch.object(VL.subprocess,"Popen",Process):
                    if failure:
                        with self.assertRaises(ValueError): VL.run(base,work)
                        self.assertEqual(len(seen),1)
                        self.assertFalse((work/"layout-summary.json").exists())
                    else:
                        VL.run(base,work)
                        self.assertEqual(seen,list(names))
                        summary=json.loads((work/"layout-summary.json").read_text())
                        self.assertFalse(summary["openvela_hardware_executed"])
                        self.assertFalse(summary["prepared_package_exported"])


if __name__=="__main__": unittest.main()
