#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import hashlib
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("fetch", Path(__file__).parent / "npu-voice/fetch-asr-source.py")
fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch)


class Response(io.BytesIO):
    url = "https://example.test/model"


class SourceFetchTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name) / "new"
        self.data = b"source-only-fixture"
        self.lock = patch.object(fetch, "FILES", {"a.onnx": (len(self.data), hashlib.sha256(self.data).hexdigest())})
        self.lock.start()
        self.addCleanup(self.lock.stop)

    def test_download_verify_resume_no_overwrite(self):
        with patch.object(fetch.urllib.request, "urlopen", return_value=Response(self.data)) as request:
            fetch.fetch(self.directory)
            self.assertEqual(request.call_count, 1)
        fetch.verify(self.directory)
        with patch.object(fetch.urllib.request, "urlopen") as request:
            fetch.fetch(self.directory, True)
            request.assert_not_called()
        with self.assertRaises(FileExistsError):
            fetch.fetch(self.directory)
        self.assertEqual((self.directory / "a.onnx").read_bytes(), self.data)

    def test_corrupt_retained_not_accepted(self):
        with patch.object(fetch.urllib.request, "urlopen", return_value=Response(b"X" * len(self.data))):
            with self.assertRaises(ValueError):
                fetch.fetch(self.directory)
        self.assertFalse((self.directory / "a.onnx").exists())
        partial = self.directory / "a.onnx.part"
        self.assertTrue(partial.exists())
        with patch.object(fetch.urllib.request, "urlopen", return_value=Response(self.data)):
            with self.assertRaises(FileExistsError):
                fetch.fetch(self.directory, True)
        self.assertEqual(partial.read_bytes(), b"X" * len(self.data))

    def test_bad_complete_not_replaced(self):
        self.directory.mkdir()
        path = self.directory / "a.onnx"
        path.write_bytes(b"bad")
        with patch.object(fetch.urllib.request, "urlopen") as request:
            with self.assertRaises(ValueError):
                fetch.fetch(self.directory, True)
            request.assert_not_called()
        self.assertEqual(path.read_bytes(), b"bad")

    def test_oversize_and_insecure_response(self):
        with patch.object(fetch.urllib.request, "urlopen", return_value=Response(self.data + b"extra")):
            with self.assertRaises(ValueError):
                fetch.fetch(self.directory)
        self.assertFalse((self.directory / "a.onnx").exists())
        self.directory = Path(self.tmp.name) / "another"
        response = Response(self.data)
        response.url = "http://example.test/model"
        with patch.object(fetch.urllib.request, "urlopen", return_value=response):
            with self.assertRaises(ValueError):
                fetch.fetch(self.directory)
        self.assertFalse((self.directory / "a.onnx").exists())


if __name__ == "__main__":
    unittest.main()
