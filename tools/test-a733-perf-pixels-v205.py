#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual C fast plan vs retained scalar C; CRC raw-seed equivalence. HOST only."""
from pathlib import Path
import ctypes as c,random,struct,subprocess,tempfile,time,zlib
repo=Path(__file__).resolve().parents[1];apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
class Image(c.Structure):
    _fields_=[('data',c.c_void_p),('bytes',c.c_size_t),('width',c.c_uint32),('height',c.c_uint32),
      ('stride',c.c_uint32),('pixel',c.c_int),('bayer',c.c_int),('yuv',c.c_int),('sequence',c.c_uint32),('timestamp',c.c_uint64)]
class Tone(c.Structure):_fields_=[(x,c.c_uint32) for x in ('black','gain','red','green','blue','rotation')]
class Transform(c.Structure):
    _fields_=[(x,c.c_uint32) for x in ('sw','sh','ow','oh','iw','ih','px','py','seq')]+[('timestamp',c.c_uint64)]
with tempfile.TemporaryDirectory(prefix='pixels205-') as tmp:
    tmp=Path(tmp);shim=tmp/'shim.c'
    shim.write_text('#include <system/a733_vision.h>\nuint32_t test_crc(const uint8_t*p,size_t n,uint32_t seed){return a733_vision_crc32(p,n,seed);}\n')
    so=tmp/'test.so';subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
      '-I'+str(apps/'include'),str(apps/'system/a733camera/a733_camera_image.c'),str(shim),'-o',str(so)],check=True,timeout=40)
    lib=c.CDLL(str(so));ref=lib.a733_camera_letterbox_tone
    ref.argtypes=[c.POINTER(Image),c.c_void_p,c.c_size_t,c.c_uint32,c.c_uint32,c.c_uint8,c.POINTER(Tone),c.POINTER(Transform)]
    create=lib.a733_camera_tone_plan_create
    create.argtypes=ref.argtypes[:-1]+[c.POINTER(c.c_void_p)]
    render=lib.a733_camera_tone_plan_render;render.argtypes=[c.c_void_p,c.POINTER(Image),c.POINTER(Transform)]
    destroy=lib.a733_camera_tone_plan_destroy;destroy.argtypes=[c.c_void_p]
    crc=lib.test_crc;crc.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32];crc.restype=c.c_uint32
    rng=random.Random(205)
    for i in range(1024):
        data=rng.randbytes(rng.randrange(8193));buf=c.create_string_buffer(data);seed=rng.getrandbits(32)
        expected=(zlib.crc32(data,seed^0xffffffff)^0xffffffff)&0xffffffff
        assert crc(buf,len(data),seed)==expected
        cut=rng.randrange(len(data)+1);part=crc(buf,cut,seed)
        assert crc(c.addressof(buf)+cut,len(data)-cut,part)==expected
    zeros=c.create_string_buffer(1228800);assert crc(zeros,1228800,0)==0
    for i in range(800):
        w,h=rng.choice([4,8,12,20,28]),rng.choice([2,6,8,16]);packed=i%2;rows=[]
        for y in range(h):
            v=[rng.randrange(1024) for _ in range(w)]
            row=(b''.join(bytes([*(n>>2 for n in v[x:x+4]),sum((v[x+j]&3)<<(2*j) for j in range(4))]) for x in range(0,w,4))
                 if packed else struct.pack('<'+str(w)+'H',*v))
            rows.append(row+b'PAD!')
        raw=b''.join(rows);data=c.create_string_buffer(raw)
        s=Image(c.addressof(data),len(raw),w,h,len(rows[0]),3 if packed else 4,1+i%4,0,i+1,2**40+i)
        ow,oh=rng.randrange(1,38),rng.randrange(1,38);n=ow*oh*3
        tone=Tone(rng.randrange(256),*[rng.randrange(64,2049) for _ in range(4)],180 if i%3 else 0)
        a=c.create_string_buffer(n);b=c.create_string_buffer(n);ta=Transform();tb=Transform();p=c.c_void_p()
        assert create(c.byref(s),b,n,ow,oh,114,c.byref(tone),c.byref(p))==0
        for iteration in range(2):
            s.sequence+=1;s.timestamp+=77
            assert ref(c.byref(s),a,n,ow,oh,114,c.byref(tone),c.byref(ta))==0
            assert render(p,c.byref(s),c.byref(tb))==0
            assert a.raw==b.raw and bytes(ta)==bytes(tb),(i,iteration)
        before=b.raw;s.stride+=1
        assert render(p,c.byref(s),c.byref(tb))<0 and b.raw==before
        destroy(p)
    raw=(b''.join(struct.pack('<H',x) for x in range(1024)))*2025
    data=c.create_string_buffer(raw);s=Image(c.addressof(data),len(raw),1920,1080,3840,4,4,0,1,123)
    a=c.create_string_buffer(1228800);b=c.create_string_buffer(1228800);t=Tone(64,2048,256,256,256,180)
    ta=Transform();tb=Transform();p=c.c_void_p();assert create(c.byref(s),b,len(b.raw),640,640,114,c.byref(t),c.byref(p))==0
    times=[]
    for fn in (lambda:ref(c.byref(s),a,len(a.raw),640,640,114,c.byref(t),c.byref(ta)),lambda:render(p,c.byref(s),c.byref(tb))):
        start=time.perf_counter()
        for _ in range(3):assert fn()==0
        times.append((time.perf_counter()-start)/3)
    assert a.raw==b.raw and bytes(ta)==bytes(tb);destroy(p)
    print('HOST benchmark only scalar/plan seconds:',times)
print('PASS1024 seeded/chunk CRC cases and800x2 fast-plan/scalar pixel+metadata cases; NOT board performance')
