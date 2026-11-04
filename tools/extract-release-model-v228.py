#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Recover the SAME verified private LLM from the preserved v227 image, read-only."""
from pathlib import Path
import hashlib,os,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
    return h.hexdigest()
source=r.parent/'openvela-a733-cubie-a7z-sd-v3.0pre-v227-candidate.img'
assert source.is_file() and source.stat().st_size==2147483648
assert sha(source)=='e4348914e72bcf282f42755ee3ca5e0d48985cfb62eceeab165d98827673ce64'
directory=r/'out/private-model-v228';directory.mkdir(exist_ok=True)
model=directory/'MiniCPM5-1B-Q4_K_M.gguf';assert not model.exists()
with tempfile.TemporaryDirectory(prefix='v228-model-read-') as d:
    mcopy=Path(d)/'mcopy';mcopy.symlink_to((r/'../../quickly-openvela/prebuilts/build-tools/linux-x86_64/bin/mtools').resolve(strict=True))
    subprocess.run([str(mcopy),'-i',str(source)+'@@469762048','::/models/MiniCPM5-1B-Q4_K_M.gguf',str(model)],env=dict(os.environ),check=True)
assert model.stat().st_size==688065920
assert sha(model)=='81b64d05a23b17b34c475f42b3e72fbde62d4b92cc34541f7a8031d0752deafa'
print('PASS read-only v227 model recovery; old image unchanged; private cache NOT for Git:',model)
