#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build an offline private-kit integrity manifest; not an authenticity signature."""
import hashlib
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve(strict=True)
records = {}
for path in sorted(root.rglob("*")):
    if path.is_symlink():
        raise ValueError("Symlink in kit")
    if not path.is_file():
        continue
    if path.name == "stream-assets.json":
        raise ValueError("Refuse existing manifest")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    records[path.relative_to(root).as_posix()] = {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}
with (root / "stream-assets.json").open("x") as stream:
    json.dump({"schema": 1, "files": records}, stream, indent=2, sort_keys=True)
