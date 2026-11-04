#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Optimized one-pass tone vs actual pinned v203 two-pass C, byte-for-byte."""
from pathlib import Path
import ctypes as c,random,struct,subprocess,tempfile
repo=Path(__file__).resolve().parents[1];apps=repo/'board/a733-cubie-a7z/openvela-overlay/apps'
src=apps/'system/a733camera/a733_camera_image.c'
class Image(c.Structure):
    _fields_=[('data',c.c_void_p),('bytes',c.c_size_t),('width',c.c_uint32),('height',c.c_uint32),
      ('stride',c.c_uint32),('pixel',c.c_int),('bayer',c.c_int),('yuv',c.c_int),('sequence',c.c_uint32),('timestamp',c.c_uint64)]
class Tone(c.Structure):_fields_=[(x,c.c_uint32) for x in ('black','gain','red','green','blue','rotation')]
class Transform(c.Structure):
    _fields_=[(x,c.c_uint32) for x in ('sw','sh','ow','oh','iw','ih','px','py','seq')]+[('timestamp',c.c_uint64)]
with tempfile.TemporaryDirectory(prefix='tone-compat204-') as tmp:
    tmp=Path(tmp);old=tmp/'old.c'
    old.write_bytes(subprocess.check_output(['git','show','5235276368811ae694c5119491d113067de154d4:'+str(src.relative_to(repo))],cwd=repo))
    libs=[]
    for name,file in [('old',old),('new',src)]:
        so=tmp/(name+'.so');subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
          '-I'+str(apps/'include'),str(file),'-o',str(so)],check=True,timeout=30)
        lib=c.CDLL(str(so));f=lib.a733_camera_letterbox_tone
        f.argtypes=[c.POINTER(Image),c.c_void_p,c.c_size_t,c.c_uint32,c.c_uint32,c.c_uint8,c.POINTER(Tone),c.POINTER(Transform)]
        f.restype=c.c_int;libs.append(f)
    rng=random.Random(204)
    for index in range(400):
        w,h=rng.choice([4,8,12,20,28]),rng.choice([2,6,8,16]);packed=index%2
        rows=[]
        for y in range(h):
            values=[rng.randrange(1024) for _ in range(w)]
            if packed:
                row=b''.join(bytes([*(n>>2 for n in values[x:x+4]),sum((values[x+i]&3)<<(2*i) for i in range(4))]) for x in range(0,w,4))
            else:row=struct.pack('<'+str(w)+'H',*values)
            rows.append(row+b'PAD!')
        raw=b''.join(rows);data=c.create_string_buffer(raw);s=Image(c.addressof(data),len(raw),w,h,len(rows[0]),3 if packed else 4,1+index%4,0,index+1,2**40+index)
        ow,oh=rng.randrange(1,38),rng.randrange(1,38);n=ow*oh*3
        tone=Tone(rng.randrange(256),*[rng.randrange(64,2049) for _ in range(4)],180 if index%3 else 0)
        outputs=[]
        for f in libs:
            out=c.create_string_buffer(n);t=Transform()
            assert f(c.byref(s),out,n,ow,oh,114,c.byref(tone),c.byref(t))==0
            outputs.append((out.raw,bytes(t)))
        assert outputs[0]==outputs[1],index
print('PASS400 one-pass/v203 actual C comparisons:LE16/packed,4Bayer,random tone,rotation,odd padding; HOST ONLY')
