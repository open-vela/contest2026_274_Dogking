#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline: real mbedTLS signature, protocol/provider mocks + gateway validation."""
from pathlib import Path
import argparse
import base64
import hashlib
import hmac
import importlib.util
import json
import os
import subprocess
import tempfile
import urllib.parse

r=Path(__file__).resolve().parents[1]
app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
sdk=(r/'../../quickly-openvela').resolve()
mb=sdk/'apps/crypto/mbedtls/mbedtls'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();out=args.output.resolve()
assert out.is_relative_to(r/'out') and not out.exists();out.mkdir()
with tempfile.TemporaryDirectory(prefix='voice-cloud-v218-') as d:
    t=Path(d);objects=[]
    flags=['-g','-O1','-fsanitize=address,undefined','-fno-pie','-Wall','-Wextra','-Werror']
    for file in ['base64.c','constant_time.c','md.c','sha1.c','sha256.c','platform_util.c']:
        obj=t/(file+'.o')
        subprocess.run(['gcc',*flags,'-DMBEDTLS_CONFIG_FILE="mbedtls-host-config.h"',
          '-I'+str(r/'tools/cloud-voice'),'-I'+str(mb/'include'),'-I'+str(mb/'library'),
          '-c',str(mb/'library'/file),'-o',str(obj)],check=True)
        objects.append(str(obj))
    cjson=t/'cjson.o'
    subprocess.run(['gcc',*flags,'-c',str(sdk/'apps/netutils/cjson/cJSON/cJSON.c'),'-o',str(cjson)],check=True)
    exe=t/'cloud'
    cmd=['g++','-std=c++17',*flags,'-no-pie','-pthread',
      '-DMBEDTLS_CONFIG_FILE="mbedtls-host-config.h"',
      '-I'+str(app),'-I'+str(r/'tools/cloud-voice'),'-I'+str(mb/'include'),
      '-I'+str(sdk/'packages/ai_agent/src'),'-I'+str(sdk/'packages/ai_agent/include'),
      '-I'+str(sdk/'apps/netutils/cjson/cJSON'),str(r/'tools/test-cloud-voice-v218.cxx'),
      str(app/'cloud_voice.cxx'),str(app/'cloud_voice_protocol.cxx'),
      *objects,str(cjson),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    with (out/'protocol-provider.log').open('x') as f:
        subprocess.run([str(exe)],check=True,timeout=30,stdout=f,stderr=subprocess.STDOUT)
    # Independent Python hashlib/hmac oracle for the exact TC3 canonical request
    # and RFC1123 Xfyun string. No mock crypto or self-comparison as sole proof.
    lines=subprocess.check_output([str(exe),'signatures'],text=True).splitlines()
    H=lambda k,s:hmac.new(k,s.encode(),hashlib.sha256).digest()
    S=lambda s:hashlib.sha256(s.encode()).hexdigest()
    canonical='POST\n/\n\ncontent-type:application/json; charset=utf-8\nhost:asr.tencentcloudapi.com\n\ncontent-type;host\n'+S('{"DataLen":4}')
    scope='2019-02-25/asr/tc3_request'
    key=H(H(H(b'TC3SECRET','2019-02-25'),'asr'),'tc3_request')
    sig=H(key,'TC3-HMAC-SHA256\n1551113065\n'+scope+'\n'+S(canonical)).hex()
    assert lines[0]=='TC3-HMAC-SHA256 Credential=ID/'+scope+', SignedHeaders=content-type;host, Signature='+sig
    query=urllib.parse.parse_qs(urllib.parse.urlsplit(lines[1]).query,strict_parsing=True)
    date='Wed, 10 Jul 2019 07:35:43 GMT'
    assert query['date']==[date] and query['host']==['iat-api.xfyun.cn']
    signature=base64.b64encode(H(b'SECRET','host: iat-api.xfyun.cn\ndate: '+date+'\nGET /v2/iat HTTP/1.1')).decode()
    expected='api_key="KEY", algorithm="hmac-sha256", headers="host date request-line", signature="'+signature+'"'
    assert base64.b64decode(query['authorization'][0],validate=True).decode()==expected
    query=urllib.parse.parse_qs(urllib.parse.urlsplit(lines[2]).query,strict_parsing=True)
    assert query['date']==[date] and query['host']==['tts-api.xfyun.cn']
    signature=base64.b64encode(H(b'SECRET','host: tts-api.xfyun.cn\ndate: '+date+'\nGET /v2/tts HTTP/1.1')).decode()
    expected='api_key="KEY", algorithm="hmac-sha256", headers="host date request-line", signature="'+signature+'"'
    assert base64.b64decode(query['authorization'][0],validate=True).decode()==expected
    (out/'signature-oracles.json').write_text(json.dumps(dict(result='PASS_INDEPENDENT_HASHLIB_HMAC',tc3_signature=sig,xfyun_date=date,xfyun_services=['ASR','TTS']),indent=2)+'\n')
spec=importlib.util.spec_from_file_location('edge_gateway',r/'tools/cloud-voice/edge_gateway.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
assert module.validate(dict(text='Hello，中文',voice='zh-CN-XiaoxiaoNeural'))
for payload in [None,{},dict(text='\0',voice='en-US-AriaNeural'),dict(text='x',voice='../file'),dict(text='a'*4097,voice='en-US-AriaNeural'),dict(text='x',voice='en-US-AriaNeural',url='http://elsewhere')]:
    try:module.validate(payload)
    except (ValueError,UnicodeError):pass
    else:raise AssertionError(payload)
(out/'summary.json').write_text(json.dumps(dict(result='V218_OFFLINE_PROTOCOL_PROVIDER_PASSED_NOT_CLOUD_OR_BOARD',
  real_crypto=True,independent_signature_oracles=True,provider_transport='MOCK_HTTPS_WS',
  gateway_validation=True,cloud_api_executed=False,hardware_executed=False),indent=2)+'\n')
print('PASS offline v218: crypto oracle, actual providers/protocol, malformed/overflow/error/credential isolation, gateway validation; NO cloud or board calls.')
