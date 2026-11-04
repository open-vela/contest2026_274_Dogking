#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual portable CRC/fused-scan C vs independent zlib convention and counts."""
from pathlib import Path
import ctypes as c,random,re,subprocess,tempfile,time,zlib
repo=Path(__file__).resolve().parents[1]
apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps/include/system/a733_crc32_fast.h'
kernel=repo/'board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_crc32_fast.h'
assert apps.read_bytes()==kernel.read_bytes()
with tempfile.TemporaryDirectory(prefix='crc207-') as raw:
    tmp=Path(raw);src=tmp/'test.c';so=tmp/'test.so'
    prior=subprocess.check_output(['git','show','bc8fc224ef465bd296296b0926739559b44d7db6:board/a733-cubie-a7z/openvela-overlay/apps/include/system/a733_vision.h'],cwd=repo).decode()
    byte=re.search(r'static inline uint32_t a733_vision_crc32\([\s\S]*?\n\}',prior)[0]
    src.write_text('#include "'+str(apps)+'"\n'+byte.replace('static inline uint32_t a733_vision_crc32','uint32_t byte_crc')+'\nuint32_t scan(const uint8_t*p,size_t n,uint32_t s,uint32_t*c){return a733_raw_scan(p,n,s,c);}\n')
    subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',str(src),'-o',str(so)],check=True)
    fn=c.CDLL(str(so)).scan;fn.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32,c.POINTER(c.c_uint32)];fn.restype=c.c_uint32
    rng=random.Random(207);cases=0
    fixtures=[bytes([v])*n for v in range(256) for n in (0,1,7,8,9,31,64)]
    fixtures += [bytes.fromhex('a5a4a5a6a5a5a4a5')*16,bytes(1228800),bytes([0xa5])*1228800]
    fixtures += [rng.randbytes(rng.randrange(65537)) for _ in range(2048)]
    fixtures += [rng.randbytes(n) for n in (1638400,2048000,409600,512000,102400,128000)]
    for data in fixtures:
        offset=rng.randrange(16);buf=c.create_string_buffer(b'X'*offset+data);p=c.addressof(buf)+offset
        seed=rng.getrandbits(32);changed=c.c_uint32(999)
        expected=(zlib.crc32(data,seed^0xffffffff)^0xffffffff)&0xffffffff
        assert fn(p,len(data),seed,c.byref(changed))==expected
        assert changed.value==sum(v!=0xa5 for v in data)
        assert fn(p,len(data),seed,None)==expected
        cut=rng.randrange(len(data)+1);part=fn(p,cut,seed,None)
        assert fn(p+cut,len(data)-cut,part,None)==expected
        cases+=1
    print('PASS',cases,'seeded/misaligned/chunked CRC and exact fused byte counts; all 256 byte patterns, zero/poison/full RGB; HOST ONLY')
    old=c.CDLL(str(so)).byte_crc;old.argtypes=fn.argtypes[:3];old.restype=c.c_uint32
    data=c.create_string_buffer(rng.randbytes(8017024));bench=[]
    for f in (lambda:old(data,8017024,0),lambda:fn(data,8017024,0,None)):
        start=time.perf_counter();result=[f() for _ in range(3)];bench.append((time.perf_counter()-start)/3)
        assert len(set(result))==1
    assert old(data,8017024,0)==fn(data,8017024,0,None)
    print('HOST ONLY 8MB CRC byte/slicing8 seconds:',bench,'NOT A733 timing or FPS')
