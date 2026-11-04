#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent original-ONNX oracle vs actual C++ C-API CPU acoustic outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
REPO=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(REPO.parent.parent/'.hostdeps-voice-batch'))
import numpy as np
import onnxruntime as ort


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('host_report',type=Path);p.add_argument('new_output',type=Path)
    a=p.parse_args();report=a.host_report.resolve();out=a.new_output.resolve();out.mkdir()
    summary=json.loads((report/'summary.json').read_text())
    assert summary['result']=='HOST_CPP_CPU_ACOUSTIC_EXECUTED' and not summary['target_runtime']
    pet=REPO/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
    for name,sha in summary['sources'].items():
        assert hashlib.sha256((pet/name).read_bytes()).hexdigest()==sha
    model=REPO.parent/'references/matcha-icefall-zh-baker/model-steps-3.onnx'
    assert hashlib.sha256(model.read_bytes()).hexdigest()=='ff1cc7f7abdedc4eeec668a4a0dc2e72a0c314c5637d5fce885345b490bd08de'
    options=ort.SessionOptions();options.intra_op_num_threads=1;options.inter_op_num_threads=1
    options.graph_optimization_level=ort.GraphOptimizationLevel.ORT_ENABLE_BASIC
    session=ort.InferenceSession(str(model),sess_options=options,providers=['CPUExecutionProvider'])
    results=[]
    for i in range(5):
        ids=np.fromfile(report/('case%d.ids'%i),dtype='<i8')
        expected=session.run(None,dict(x=ids.reshape(1,-1),x_length=np.asarray([len(ids)],dtype=np.int64),
            noise_scale=np.asarray([0.0],dtype=np.float32),length_scale=np.asarray([1.0],dtype=np.float32)))[0]
        actual=np.fromfile(report/('case%d.mel'%i),dtype='<f4').reshape(expected.shape)
        assert np.isfinite(actual).all() and np.isfinite(expected).all()
        error=np.abs(expected-actual).max().item()
        rel=np.linalg.norm((expected-actual).astype(np.float64))/max(np.linalg.norm(expected.astype(np.float64)),1e-30)
        assert error<=1e-4 and rel<=1e-5,(i,error,rel)
        expected.astype('<f4').tofile(out/('case%d.expected.f32'%i))
        results.append(dict(case=i,shape=list(expected.shape),ids=len(ids),max_abs=error,relative_l2=rel.item(),
            cpp_mel_sha256=hashlib.sha256((report/('case%d.mel'%i)).read_bytes()).hexdigest()))
    result=dict(result='ORIGINAL_ONNX_VS_CPP_C_API_CPU_MATCH',ort_version=ort.__version__,cases=results,
        noise_scale=0.0,noise_zero_purpose='equivalence only, not production voice noise choice',
        acoustic_backend='CPU',vocoder_executed=False,npu_executed=False,target_runtime=False,full_voice_chain=False)
    (out/'summary.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
