#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compiler-only fp16 feasibility probe. NO fake calibration or acceptance.

Imported ASR input is /import (read-only), output /work must be NEW. Uses
Allwinner's documented A733 CID 0x1000003b, not the README's T527 target.
Resulting NBG still needs vendor-Linux dynamic tensor + transcript goldens.
"""
import hashlib,json,os,shutil,subprocess,sys,time
from pathlib import Path
import yaml
peg=[sys.executable,'/root/acuity-toolkit-whl-6.30.22/bin/pegasus.py']
report={'schema':1,'target_cid':'0x1000003b','quantization':'fp16',
        'compiler_only':True,'npu_executed':False,'asr_accepted':False,'models':[]}
def run(args,folder,log):
    return subprocess.run(peg+args,cwd=str(folder),stdout=log,stderr=subprocess.STDOUT,
                          timeout=900,env=dict(os.environ,
                          VIV_VX_ENABLE_GRAPH_TRANSFORM='-Dump[transform.rewrite.gather_to_stride_slice=0]')).returncode
for role in ('decoder','joiner','encoder'):
    folder=Path('/work')/role;folder.mkdir()
    entry={'role':role,'input':{}}
    for suffix in ('.json','.data'):
        src=Path('/import')/role/(role+suffix)
        entry['input'][suffix]=hashlib.sha256(src.read_bytes()).hexdigest()
        shutil.copyfile(src,folder/src.name)
    with (folder/'export.log').open('x') as log:
        meta=role+'_inputmeta.yml';post=role+'_postprocess_file.yml'
        rc=run(['generate','inputmeta','--model',role+'.json','--separated-database',
                '--input-meta-output',meta],folder,log)
        if not rc:rc=run(['generate','postprocess-file','--model',role+'.json',
                        '--postprocess-file-output',post],folder,log)
        if not rc:
            from acuitylib.vsi_nn import VSInn
            nn=VSInn();net=nn.create_net();nn.load_model(net,str(folder/(role+'.json')))
            nn.load_model_inputmeta(net,str(folder/meta));data=net.get_input_meta()
            for db in data.databases:
                for port in db.ports:
                    port.preprocess['preproc_node_params']['add_preproc_node']=True
                    port.preprocess['preproc_node_params']['preproc_type']='TENSOR'
            net.update_input_meta(data);nn.save_model_inputmeta(net,str(folder/meta))
            p=folder/post
            text=p.read_text();p.write_text(text.replace('add_postproc_node: false','add_postproc_node: true'))
            begin=time.monotonic()
            rc=run(['export','ovxlib','--pack-nbg-unify',
              '--optimize','VIP9000NANODI_PLUS_PID0X1000003B',
              '--viv-sdk','/root/Vivante_IDE/VivanteIDE5.11.0/cmdtools',
              '--model',role+'.json','--model-data',role+'.data','--dtype','float',
              '--target-ide-project','linux64','--with-input-meta',meta,
              '--postprocess-file',post,'--output-path',str(folder/(role+'_fp16'))],folder,log)
            entry['export_elapsed_s']=time.monotonic()-begin
        entry['returncode']=rc
        entry['nbg']=[{'path':str(p.relative_to('/work')),'bytes':p.stat().st_size,
                      'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
                      for p in (Path('/work')/(role+'_nbg_unify')).rglob('*.nb')]
        if not rc and not entry['nbg']:
            entry['returncode']=rc=2
    report['models'].append(entry)
    Path('/work/export-report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(entry),flush=True)
    if rc:sys.exit(rc)
