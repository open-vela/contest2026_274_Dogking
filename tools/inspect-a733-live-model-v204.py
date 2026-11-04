#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read only the default YOLO package from a regular SD image; no model archive."""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,tempfile,zlib
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('image',type=Path);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();assert a.image.is_file() and a.image.stat().st_size==2147483648 and not a.output.exists()
host=(repo/'../../quickly-openvela/prebuilts/build-tools/linux-x86_64/bin').resolve()
result=dict(image=a.image.name,model_path='/data/npu/yolov8n-pcq-v3.a7pm',hardware_verified=False,model_archived=False)
with tempfile.TemporaryDirectory(prefix='model204-') as tmp:
    tmp=Path(tmp);alias=tmp/'mcopy';alias.symlink_to(host/'mtools')
    output=tmp/'model';env={**os.environ,'PATH':str(host)+os.pathsep+os.environ.get('PATH','')}
    r=subprocess.run([str(alias),'-i',str(a.image.resolve())+'@@469762048','::/npu/yolov8n-pcq-v3.a7pm',str(output)],env=env,capture_output=True,text=True,timeout=60)
    result['present']=r.returncode==0
    if not r.returncode:
        data=output.read_bytes();header=struct.unpack('<12I',data[:48]);name=data[48:80].split(b'\0')[0].decode('ascii')
        assert header[0]==0x4d503741 and header[1]==3 and header[2]==128 and header[3]==0x1000003b
        assert name.startswith('yolov8n_6_pcq_a') and header[10]==len(data)
        result.update(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),crc32=f'{(zlib.crc32(data,0xffffffff)^0xffffffff):08x}',name=name,cid=f'{header[3]:08x}',version=header[1])
    else:result['tool_error']=r.stderr
with a.output.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
print(json.dumps(result,indent=2))
