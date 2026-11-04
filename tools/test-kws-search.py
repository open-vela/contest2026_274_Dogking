#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Python/C binding and native-oracle safety tests. No accelerator calls."""
import importlib.util
import json
import os
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("kws_search", Path(__file__).parent / "npu-voice/run-kws-search.py")
KS = importlib.util.module_from_spec(spec)
spec.loader.exec_module(KS)
LIBRARY = Path(os.environ["AIPET_KWS_LIBRARY"]) if "AIPET_KWS_LIBRARY" in os.environ else None


def logits(token=0, value=8):
    row = [-20.] * 263
    row[0] = 5
    row[token] = value
    return struct.pack("<263f", *row)


class SearchTests(unittest.TestCase):
    def test_keywords_are_not_retuned_and_unknown_token_rejected(self):
        words = KS.keywords("a b c d :2 #.18 @short\na b c d e f :2.5 #.15 @long",
                            {ch: i+3 for i, ch in enumerate("abcdef")})
        self.assertEqual(words[0], dict(ids=[3,4,5,6], phrase="short", boost=2, threshold=.18))
        with self.assertRaises(KeyError):
            KS.keywords("z :2 #.18 @bad", {})

    def test_missing_logit_does_not_fabricate_or_call_native(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            oracle = KS.Oracle(root, root, {"logits": []})
            with self.assertRaises(KS.MissingLogits):
                oracle.query(bytes(1280), (0,0), "missing")
            self.assertEqual(oracle.fresh, 0)

    def test_native_context_cache_and_acoustic_frame_key(self):
        class FakeRunner:
            def __init__(self): self.calls = []
            def invoke(self, tag, name, inputs):
                self.calls.append((name, inputs))
                return [bytes(1280) if name == "decoder" else logits()]
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); runner = FakeRunner()
            oracle = KS.Oracle(root, root, {"logits": []}, runner)
            frame = bytes(1280)
            first = oracle.query(frame, (0,3), "first")
            self.assertEqual(first, oracle.query(frame, (0,3), "cached"))
            oracle.query(struct.pack("<320f", *[1.]*320), (0,3), "changed")
            self.assertEqual([name for name, inputs in runner.calls], ["decoder", "joiner", "joiner"])
            self.assertEqual(runner.calls[0][1], [struct.pack("<2f", 0,3)])
            self.assertEqual((oracle.fresh, oracle.reused), (2,1))

    def test_frozen_hash_corruption_rejected_before_native(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); (root / "logits.bin").write_bytes(logits())
            item = dict(path="logits.bin", sha256="wrong", encoder_sha256="enc", context=[0,0], origin="old")
            with self.assertRaisesRegex(ValueError, "Changed frozen"):
                KS.Oracle(root, root, {"logits": [item]})

    @unittest.skipUnless(LIBRARY, "AIPET_KWS_LIBRARY points to the host-built C core")
    def test_real_c_binding_requires_threshold_and_resets_context(self):
        search = KS.Search(LIBRARY, [dict(ids=[3,4,5,6], phrase="wake", boost=2, threshold=.18)])
        self.assertEqual(search.contexts(), [(0,0)])
        hit = None
        for token in [3,4,5,6]:
            hit = search.advance([logits(token) for ctx in search.contexts()])
        self.assertEqual(hit["phrase"], "wake")
        self.assertGreater(hit["mean_probability"], .18)
        self.assertEqual(search.contexts(), [(0,0)])
        with self.assertRaisesRegex(ValueError, "row order"):
            search.advance([])

    @unittest.skipUnless(LIBRARY, "needs host-built C core")
    def test_bad_graph_rejected_and_silence_long_run_bounded(self):
        with self.assertRaisesRegex(ValueError, "init rejected"):
            KS.Search(LIBRARY, [dict(ids=[3,4,5,6], phrase="bad", boost=2, threshold=1.1)])
        search = KS.Search(LIBRARY, [dict(ids=[3,4,5,6], phrase="wake", boost=2, threshold=.18)])
        for i in range(1000):
            self.assertIsNone(search.advance([logits() for ctx in search.contexts()]))
            self.assertLessEqual(len(search.contexts()), 17)

    @unittest.skipUnless(LIBRARY, "needs host-built C core")
    def test_incomplete_replay_is_not_a_negative_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); frame = bytes(1280); (root / "frame.bin").write_bytes(frame)
            case = dict(tag="negative-silence", expected="", encoder_frames=[dict(path="frame.bin", sha256=KS.sha(frame))])
            index = dict(cases=[case], logits=[], provenance={"mock": True})
            words = [dict(ids=[3,4,5,6], phrase="wake", boost=2, threshold=.18)]
            with patch.object(KS, "check", return_value=({}, words, index)):
                result = KS.run(root, root, LIBRARY, False)
            row = result["cases"][0]
            self.assertEqual(row["status"], "NEEDS_NATIVE_BRANCH")
            self.assertIsNone(row["classification_ok"])
            self.assertFalse(result["end_to_end_accepted"])
            self.assertEqual(result["native_process_calls"], 0)


if __name__ == "__main__":
    unittest.main()
