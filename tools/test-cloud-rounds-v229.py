#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Same-process real TLS/WS lifecycle; synthetic service, no paid/cloud call."""
from pathlib import Path
import argparse,base64,hashlib,json,socket,ssl,struct,subprocess,tempfile,threading
r=Path(__file__).resolve().parents[1];sdk=(r/'../../quickly-openvela').resolve()
app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet';mb=sdk/'apps/crypto/mbedtls/mbedtls'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--mbedtls-build',type=Path,default=r/'out/cloud-voice-v218-mbedtls-host-r1');a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(r/'out') and not out.exists();out.mkdir();libs=a.mbedtls_build.resolve()/'library'
with tempfile.TemporaryDirectory(prefix='wss-rounds-v229-') as d:
    t=Path(d);subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-keyout',str(t/'key'),'-out',str(t/'ca')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    flags=['-O1','-g','-fsanitize=address,undefined','-fno-pie','-Wall','-Wextra','-Werror']
    subprocess.run(['gcc',*flags,'-c',str(sdk/'apps/netutils/cjson/cJSON/cJSON.c'),'-o',str(t/'json.o')],check=True)
    subprocess.run(['g++','-std=c++17',*flags,'-no-pie','-pthread','-Wl,--wrap=getaddrinfo','-Wl,--wrap=freeaddrinfo','-Wl,--wrap=socket','-DAIPET_CLOUD_CA_FILE="'+str(t/'ca')+'"','-I'+str(app),'-I'+str(mb/'include'),'-I'+str(sdk/'apps/netutils/cjson/cJSON'),str(r/'tools/test-cloud-rounds-v229.cxx'),str(app/'cloud_voice_socket.cxx'),str(app/'cloud_voice_protocol.cxx'),str(t/'json.o'),*[str(libs/n) for n in ('libmbedtls.a','libmbedx509.a','libmbedcrypto.a')],'-o',str(t/'client')],check=True)
    ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.minimum_version=ssl.TLSVersion.TLSv1_2;ctx.load_cert_chain(t/'ca',t/'key')
    for case in ('socket-failure','refused','cancel','rounds','silent-close','peer-close','tls-failover'):
        listener=socket.socket();listener.bind(('127.0.0.1',0));port=listener.getsockname()[1]
        count=50 if case=='rounds' else 1;expected=count*3 if case in ('rounds','silent-close','peer-close','tls-failover') else 0
        errors=[];closures=[];bad=None
        if expected:listener.listen(8);listener.settimeout(15)
        else:listener.close()
        def serve():
            try:
                for _ in range(expected):
                    raw,_=listener.accept();raw.settimeout(5)
                    with ctx.wrap_socket(raw,server_side=True) as s:
                        def exact(n):
                            b=b''
                            while len(b)<n:
                                more=s.recv(n-len(b));assert more;b+=more
                            return b
                        def frame():
                            h=exact(2);assert h[1]&128;n=h[1]&127
                            if n==126:n=struct.unpack('>H',exact(2))[0]
                            mask=exact(4);b=exact(n);return h[0]&15,bytes(c^mask[i%4] for i,c in enumerate(b))
                        req=b''
                        while not req.endswith(b'\r\n\r\n'):req+=exact(1)
                        key=next(v.split(b':',1)[1].strip() for v in req.split(b'\r\n') if v.lower().startswith(b'sec-websocket-key:'))
                        accept=base64.b64encode(hashlib.sha1(key+b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
                        s.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n')
                        op,b=frame();assert op==1 and b.startswith(b'round-');s.sendall(b'\x81\x0d{"mock":true}')
                        if case=='peer-close':s.sendall(b'\x88\x02\x03\xe8')
                        op,b=frame();assert op==8 and b==b'\x03\xe8'
                        if case=='silent-close':
                            assert not s.recv(1);closures.append('bounded-no-peer-close')
                        else:
                            if case!='peer-close':s.sendall(b'\x88\x02\x03\xe8')
                            plain=s.unwrap();plain.close();closures.append('WS+TLS-close')
            except Exception as e:errors.append(repr(e))
            finally:
                if expected:listener.close()
        # First address accepts TCP but resets during TLS; second succeeds.
        def reject():
            try:
                for _ in range(expected):
                    c,_=bad.accept();c.recv(4096);c.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0));c.close()
            except Exception as e:errors.append(repr(e))
            finally:bad.close()
        if case=='tls-failover':
            bad=socket.socket();bad.bind(('127.0.0.2',port));bad.listen(8);bad.settimeout(15);rejecter=threading.Thread(target=reject);rejecter.start()
        worker=threading.Thread(target=serve);worker.start()
        with (out/(case+'.log')).open('x') as f:subprocess.run([str(t/'client'),str(port),str(count),case],check=True,timeout=120,stdout=f,stderr=subprocess.STDOUT)
        worker.join(15)
        if bad:rejecter.join(15);assert not rejecter.is_alive()
        assert not worker.is_alive() and not errors,(case,errors)
        assert len(closures)==expected
        if expected:
            logs=(out/(case+'.log')).read_text();assert logs.count('owned-WSS=0; completed audio')==expected
            if case=='tls-failover':assert logs.count('phase=TLS-handshake')>=expected
    report=dict(result='V229_REAL_TLS_SAME_PROCESS_ROUNDS_PASSED',rounds=50,connections_in_single_process=150,FD_growth=0,ASan_UBSan=True,
                tests=['socket-EMFILE-no-invalid-handle','SO_ERROR-ECONNREFUSED-preserved','cancel-before-DNS','50-ASR-like-plus-2-TTS-like-rounds','WS-TLS-close','peer-initiated-close-response','500ms-silent-peer-close-bound','TLS-failure-next-DNS-address'],
                synthetic_service=True,cloud_api_executed=False,hardware_executed=False)
    (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS actual TLS: 50 same-process mock dialogue rounds /150 connections, FD growth=0, TLS failover, precise errors, bounded shutdown; NOT board acceptance.')
