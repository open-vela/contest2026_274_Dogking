#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Close the old DNS UDP descriptor before TCP fallback; SDK staging only."""
from pathlib import Path
import re,sys
def transform(source):
    assert 'dns-close-v229' not in source
    pattern=r'(if \(!stream && should_try_stream\)\n\s*\{\n)(\s*)(stream = true;)'
    assert len(re.findall(pattern,source))==2
    return re.sub(pattern,r'\1\2/* dns-close-v229: release UDP before opening TCP. */\n\2close(sd);\n\2\3',source)
if __name__=='__main__':
    p=Path(sys.argv[1]).resolve(strict=True)
    assert p.name=='lib_dnsquery.c' and p.parent.name=='netdb'
    p.write_text(transform(p.read_text())) # staged mechanical edit, trap restores exact original bytes
