#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual HTTPS gateway/auth on localhost, MOCK Edge backend; no cloud calls."""
from pathlib import Path
import argparse,base64,http.client,importlib.util,json,socket,ssl,subprocess,tempfile,threading
from http.server import HTTPServer
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(r/'out') and not out.exists();out.mkdir()
spec=importlib.util.spec_from_file_location('edge_gateway',r/'tools/cloud-voice/edge_gateway.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
calls=[]
async def fake(text,voice):
    calls.append((text,voice))
    if text=='simulate failure':raise RuntimeError('must not expose this upstream message')
    return dict(sample_rate=16000,channels=1,bits=16,pcm=base64.b64encode(b'\x01\x00\x02\x00').decode())
m.synthesize=fake
with tempfile.TemporaryDirectory(prefix='voice-edge-https-v218-') as d:
    t=Path(d)
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-sha256','-nodes','-days','1',
      '-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
      '-keyout',str(t/'server.key'),'-out',str(t/'server.pem')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    server=HTTPServer(('127.0.0.1',0),m.Handler);server.token='SYNTHETIC-not-a-real-service-token-123456789'
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(t/'server.pem',t/'server.key');server.socket=context.wrap_socket(server.socket,server_side=True,do_handshake_on_connect=False)
    worker=threading.Thread(target=server.serve_forever);worker.start()
    client_context=ssl.create_default_context(cafile=str(t/'server.pem'))
    def request(path='/v1/tts',payload=None,token=None,headers=None):
        body=json.dumps(payload or dict(text='你好 English',voice='zh-CN-XiaoxiaoNeural')).encode()
        h={'Content-Type':'application/json','Authorization':token or 'Bearer '+server.token}
        if headers:h.update(headers)
        client=http.client.HTTPSConnection('localhost',server.server_port,timeout=5,context=client_context)
        try:
            client.request('POST',path,body=body,headers=h);response=client.getresponse()
            result=response.status,json.loads(response.read());assert response.getheader('Cache-Control')=='no-store'
            return result
        finally:client.close()
    try:
        raw=socket.create_connection(('127.0.0.1',server.server_port),timeout=2)
        raw.settimeout(7)
        try:assert raw.recv(1)==b''  # missing TLS handshake expires; server resumes
        finally:raw.close()
        assert request(token='Bearer WRONG')[0]==401 and not calls
        assert request(path='/elsewhere')[0]==404 and not calls
        assert request(headers={'Content-Length':'99999999999999999999'})[0]==400 and not calls
        assert request(headers={'Content-Length':'20000'})[0]==413 and not calls
        assert request(payload=dict(text='你好',voice='../bad'))[0]==422 and not calls
        status,result=request();assert status==200 and base64.b64decode(result['pcm'])==b'\x01\x00\x02\x00' and len(calls)==1
        status,result=request(payload=dict(text='simulate failure',voice='zh-CN-XiaoxiaoNeural'))
        assert status==502 and 'must not expose' not in json.dumps(result) and len(calls)==2
    finally:
        server.shutdown();worker.join(timeout=5);server.server_close();assert not worker.is_alive()
(out/'summary.json').write_text(json.dumps(dict(result='V218_ACTUAL_LOCALHOST_HTTPS_GATEWAY_PASSED_MOCK_EDGE',
  checks=['TLS','unauthenticated-TLS-handshake-timeout','bearer-auth','path','length-cap','invalid-payload','PCM-contract','safe-upstream-error','shutdown'],
  real_edge_synthesis=False,real_mp3_decode=False,cloud_api_executed=False,hardware_executed=False),indent=2)+'\n')
print('PASS actual HTTPS Edge companion handlers/auth/limits/errors; MOCK upstream, no Edge/FFmpeg/cloud/board acceptance.')
