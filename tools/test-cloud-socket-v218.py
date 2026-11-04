#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Localhost TLS + real WebSocket frames, no external network/API calls."""
from pathlib import Path
import argparse,base64,hashlib,json,socket,ssl,struct,subprocess,tempfile,threading,time
r=Path(__file__).resolve().parents[1];sdk=(r/'../../quickly-openvela').resolve()
app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
mb=sdk/'apps/crypto/mbedtls/mbedtls'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--mbedtls-build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(r/'out') and not out.exists();out.mkdir()
libs=a.mbedtls_build.resolve()/'library'
with tempfile.TemporaryDirectory(prefix='voice-wss-v218-') as d:
    t=Path(d)
    for name in ['ca','untrusted']:
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-sha256','-nodes','-days','1',
          '-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
          '-keyout',str(t/(name+'.key')),'-out',str(t/(name+'.pem'))],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    exe=t/'socket'
    objects=[]
    flags=['-g','-O1','-fsanitize=address,undefined','-fno-pie','-Wall','-Wextra','-Werror']
    obj=t/'json.o';subprocess.run(['gcc',*flags,'-c',str(sdk/'apps/netutils/cjson/cJSON/cJSON.c'),'-o',str(obj)],check=True)
    subprocess.run(['g++','-std=c++17',*flags,'-no-pie','-pthread',
      '-DAIPET_CLOUD_CA_FILE="'+str(t/'ca.pem')+'"','-I'+str(app),'-I'+str(mb/'include'),
      '-I'+str(sdk/'apps/netutils/cjson/cJSON'),str(r/'tools/test-cloud-socket-v218.cxx'),
      str(app/'cloud_voice_socket.cxx'),str(app/'cloud_voice_protocol.cxx'),str(obj),
      str(libs/'libmbedtls.a'),str(libs/'libmbedx509.a'),str(libs/'libmbedcrypto.a'),'-o',str(exe)],check=True)
    for case in ['fragment-ping','bad-accept','masked','oversize','bad-cert','cancel','eof','idle','hard-budget','slow-first','large']:
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(10)
        port=listener.getsockname()[1];errors=[]
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.minimum_version=ssl.TLSVersion.TLSv1_2
        name='untrusted' if case=='bad-cert' else 'ca'
        context.load_cert_chain(t/(name+'.pem'),t/(name+'.key'))
        def serve():
            try:
                raw,_=listener.accept();raw.settimeout(10)
                with context.wrap_socket(raw,server_side=True) as client:
                    def exact(n):
                        result=b''
                        while len(result)<n:
                            chunk=client.recv(n-len(result));assert chunk;result+=chunk
                        return result
                    request=b''
                    while not request.endswith(b'\r\n\r\n'):request+=exact(1)
                    key=next(line.split(b':',1)[1].strip() for line in request.split(b'\r\n') if line.lower().startswith(b'sec-websocket-key:'))
                    accept=base64.b64encode(hashlib.sha1(key+b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
                    if case=='bad-accept':accept=b'incorrect'
                    client.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n')
                    if case=='bad-accept':return
                    # One short complete client frame must share its first TLS
                    # record; catches the old separate-header record path.
                    first=client.recv(4096);assert len(first)==15, (case,len(first))
                    pending=first
                    old_exact=exact
                    def exact(n):
                        nonlocal pending
                        got,pending=pending[:n],pending[n:]
                        return got+old_exact(n-len(got))
                    h=exact(2);assert h[0]==0x81 and h[1]&128
                    n=h[1]&127;assert n<126;mask=exact(4);payload=exact(n)
                    assert bytes(c^mask[i%4] for i,c in enumerate(payload)).decode()=='客户端'
                    if case=='masked':client.sendall(b'\x81\x80')
                    elif case=='eof':return
                    elif case=='cancel':
                        client.settimeout(3);assert not client.recv(1);return
                    elif case=='oversize':client.sendall(b'\x81\x7f'+struct.pack('>Q',131073))
                    elif case=='idle':time.sleep(.4)
                    elif case=='hard-budget':
                        for _ in range(12):
                            try:client.sendall(b'\x89\x00')
                            except (BrokenPipeError,ConnectionResetError,ssl.SSLError):break
                            time.sleep(.04)
                    elif case=='large':
                        client.sendall(b'\x81\x7f'+struct.pack('>Q',100000))
                        for at in range(0,100000,7000):client.sendall(b'a'*min(7000,100000-at))
                    elif case=='slow-first':
                        time.sleep(.25);client.sendall(b'\x81\x0b{\"ok\":true}')
                    else:
                        client.sendall(b'\x01\x02{\"'+b'\x89\x02hi'+b'\x80\x09ok\":true}')
                        h=exact(2);assert h[0]==0x8a and h[1]==0x82
                        mask=exact(4);payload=exact(2)
                        assert bytes(c^mask[i%4] for i,c in enumerate(payload))==b'hi'
            except ssl.SSLError:
                if case!='bad-cert':errors.append('unexpected TLS error')
            except Exception as e:errors.append(repr(e))
            finally:listener.close()
        worker=threading.Thread(target=serve);worker.start()
        with (out/(case+'.log')).open('x') as log:
            subprocess.run([str(exe),str(port),case],check=True,timeout=15,stdout=log,stderr=subprocess.STDOUT)
        worker.join(timeout=12);assert not worker.is_alive() and not errors,(case,errors)
    (out/'summary.json').write_text(json.dumps(dict(result='V218_ACTUAL_LOCALHOST_TLS_WEBSOCKET_PASSED',
      tests=['valid-certified-TLS','coalesced-masked-client-frame','fragmented-JSON','ping-pong','bad-upgrade-accept','masked-server-rejected','oversize-rejected','untrusted-cert-rejected','cancel-within-1s','EOF-not-generic-EIO','idle-budget','hard-budget-survives-ping-progress','delayed-first-packet','100k-response'],
      cloud_api_executed=False,hardware_executed=False),indent=2)+'\n')
print('PASS actual loopback mbedTLS/WebSocket: certificate verification, masked frames, fragmented JSON, ping and invalid/oversize rejection; no external call.')
