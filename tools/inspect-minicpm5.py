#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read a bounded GGUF directory; no inference, dependencies or model writes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('model',type=Path)
a=p.parse_args()
fields={};tensors=[];special={}
with a.model.open('rb') as f:
    def read(fmt):
        size=struct.calcsize('<'+fmt);raw=f.read(size)
        if len(raw)!=size:raise ValueError('Truncated GGUF')
        return struct.unpack('<'+fmt,raw)[0]
    def string():
        size=read('Q')
        if size>1024*1024:raise ValueError('Oversized metadata string')
        raw=f.read(size)
        if len(raw)!=size:raise ValueError('Truncated string')
        return raw.decode('utf-8')
    scalar={0:'B',1:'b',2:'H',3:'h',4:'I',5:'i',6:'f',7:'?',10:'Q',11:'q',12:'d'}
    def value(kind,key='',depth=0):
        if depth>3:raise ValueError('Nested array bound')
        if kind in scalar:return read(scalar[kind])
        if kind==8:return string()
        if kind!=9:raise ValueError('Unknown type')
        element=read('I');count=read('Q')
        if count>1024*1024:raise ValueError('Metadata array bound')
        for i in range(count):
            v=value(element,depth=depth+1)
            if key=='tokenizer.ggml.tokens' and v in ('<|im_start|>','<|im_end|>','<think>','</think>'):
                special[v]=i
        return {'entries':count,'type':element}
    if f.read(4)!=b'GGUF' or read('I') not in (2,3):raise ValueError('GGUF header')
    nt,nm=read('Q'),read('Q')
    if nt>4096 or nm>4096:raise ValueError('Directory bound')
    for i in range(nm):
        key=string();fields[key]=value(read('I'),key)
    for i in range(nt):
        name=string();rank=read('I')
        if rank<1 or rank>4:raise ValueError('Tensor rank')
        shape=[read('Q') for _ in range(rank)];kind=read('I');offset=read('Q')
        tensors.append(dict(name=name,type=kind,shape=shape,offset=offset))
h=hashlib.sha256()
with a.model.open('rb') as f:
    while chunk:=f.read(1024*1024):h.update(chunk)
print(json.dumps(dict(bytes=a.model.stat().st_size,sha256=h.hexdigest(),metadata=fields,
    special_tokens=special,tensors=tensors),ensure_ascii=False,indent=2))
