#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline compiler import probe of PINNED ASR graphs, not KWS/acceptance.

Runs inside the vendor compiler container; /source is read-only, /work new.
Only fixes dynamic batch N to 1 in a NEW derived ONNX; leaves tensor types,
states and model weights unchanged. No invented calibration or zero goldens.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import onnx

LOCK={
 'decoder':(13877276,'89be509a83175261695bdef5fd1c7b9ab1129a663d1284e7ba9f8507b21e0906'),
 'encoder':(88476276,'d53e858189c32678fd97dc40bf3a2dcf4acf462fd6530c265afecb6496156aef'),
 'joiner':(12833675,'bcbcf0cc1b449bc32b7777084ab5a65d1199b822e2aae149d3a43adc9f4b5775')}
report={'source_revision':'e2382758de9a0219b4efe682b95af30b399db3b8',
 'compiler':'acuity-6.30.22','npu_executed':False,'accepted_asr':False,'models':[]}
for role,(size,digest) in LOCK.items():
    src=Path('/source')/(role+'-epoch-99-avg-1.onnx')
    raw=src.read_bytes()
    if len(raw)!=size or hashlib.sha256(raw).hexdigest()!=digest:
        raise ValueError('Source lock mismatch: '+role)
    model=onnx.load_from_string(raw)
    fixes=[]
    for value in list(model.graph.input)+list(model.graph.output)+list(model.graph.value_info):
        for i,dim in enumerate(value.type.tensor_type.shape.dim):
            if dim.dim_param:
                if dim.dim_param!='N':
                    if value in model.graph.input:
                        raise ValueError('Unreviewed dynamic input: '+value.name+':'+dim.dim_param)
                    # Do not guess output/intermediate sequence lengths.
                    # The importer must infer them from real operators.
                    continue
                fixes.append([value.name,i,dim.dim_param]);dim.dim_value=1
    folder=Path('/work')/role;folder.mkdir()
    derived=folder/(role+'.onnx')
    onnx.checker.check_model(model)
    onnx.save_model(model,str(derived))
    entry={'role':role,'source_sha256':digest,'fixed_batch':fixes,
           'derived_sha256':hashlib.sha256(derived.read_bytes()).hexdigest()}
    with (folder/'import.log').open('x') as log:
        cmd=[sys.executable,'/root/acuity-toolkit-whl-6.30.22/bin/pegasus.py',
             'import','onnx','--model',str(derived),
             '--output-model',str(folder/(role+'.json')),
             '--output-data',str(folder/(role+'.data'))]
        begin=time.monotonic()
        try:
            rc=subprocess.run(cmd,cwd=str(folder),stdout=log,stderr=subprocess.STDOUT,
                              timeout=600).returncode
        except subprocess.TimeoutExpired:
            rc=124
        entry.update(returncode=rc,elapsed_s=time.monotonic()-begin)
    report['models'].append(entry)
    with Path('/work/import-report.json').open('w') as f:json.dump(report,f,indent=2)
    print(json.dumps(entry),flush=True)
    if rc:sys.exit(rc)
