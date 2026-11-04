"""PTY provisioning test; synthetic credential only, never a live API call."""
import json, os, pty, select, signal, subprocess, sys, termios, time
binary, directory = sys.argv[1:]
def run(provider, model, key):
    master, slave = pty.openpty()
    proc = subprocess.Popen([binary], stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    transcript = bytearray()
    for prompt, value in [(b'Provider [',provider), (b'Model name [',model),
                           (b'API key (hidden):',key)]:
        limit=time.monotonic()+10
        while prompt not in transcript:
            assert time.monotonic()<limit, bytes(transcript)
            if select.select([master],[],[],0.1)[0]:
                transcript.extend(os.read(master,4096))
        os.write(master,value+b'\n')
    assert proc.wait(timeout=10)==0
    try:
        while select.select([master],[],[],0.1)[0]:
            transcript.extend(os.read(master,4096))
    except OSError:
        pass
    os.close(master)
    assert key not in transcript, 'Secret echoed!'
    with open(directory+'/config/config.json') as f:
        cfg=json.load(f)
    assert cfg['api_key'].startswith('a7g1:')
    assert key.decode() not in json.dumps(cfg)
    return cfg
cfg=run(b'1',b'',b'host-test-secret-123')
assert cfg['model']=='deepseek-flash' and cfg['llm_host']=='api.deepseek.com'
cfg=run(b'2',b'custom-mimo-next',b'host-test-secret-456')
assert cfg['model']=='custom-mimo-next' and cfg['llm_host']=='api.xiaomimimo.com'
print('PTY setup: both providers, default/custom model, hidden key and encrypted JSON passed')
master,slave=pty.openpty()
proc=subprocess.Popen([binary,'voice'],stdin=slave,stdout=slave,stderr=slave)
os.close(slave)
transcript=bytearray()
token=b'voice-test-token-'+b'x'*220
api=b'voice-test-api-key'
for prompt,value in [(b'ASR app id [',b'12345'),(b'ASR access token (hidden',token),
                     (b'TTS V3 API key (hidden',api),(b'TTS speaker id [',b'test-speaker'),
                     (b'ASR cluster [',b'test-cluster')]:
    limit=time.monotonic()+10
    while prompt not in transcript:
        assert time.monotonic()<limit,bytes(transcript)
        if select.select([master],[],[],0.1)[0]: transcript.extend(os.read(master,4096))
    os.write(master,value+b'\n')
assert proc.wait(timeout=10)==0
try:
    while select.select([master],[],[],0.1)[0]: transcript.extend(os.read(master,4096))
except OSError: pass
os.close(master)
assert token not in transcript and api not in transcript
with open(directory+'/config/config.json') as f: voice=json.load(f)
assert voice['api_key']==cfg['api_key'] and voice['model']==cfg['model']
assert voice['volc_token'].startswith('a7g1:') and voice['volc_api_key'].startswith('a7g1:')
assert token.decode() not in json.dumps(voice) and api.decode() not in json.dumps(voice)
assert voice['volc_appkey']=='12345' and voice['volc_speaker']=='test-speaker'
print('PTY voice setup: hidden long ASR token, encrypted token/TTS key, atomic save preserves LLM config')

# New multi-provider setup uses the same actual terminal reader, AES-GCM and
# transaction writer. Synthetic keys only; no network calls.
def provider_setup(name, entries):
    master, slave = pty.openpty()
    proc = subprocess.Popen([binary, name], stdin=slave, stdout=slave, stderr=slave)
    os.close(slave); transcript = bytearray()
    for prompt, value, secret in entries:
        end = time.monotonic() + 10
        while prompt not in transcript:
            assert time.monotonic() < end, bytes(transcript)
            if select.select([master], [], [], .1)[0]: transcript.extend(os.read(master, 4096))
        os.write(master, value + b'\n')
    assert proc.wait(timeout=10) == 0
    try:
        while select.select([master], [], [], .1)[0]: transcript.extend(os.read(master, 4096))
    except OSError: pass
    os.close(master)
    data = json.load(open(directory+'/config/config.json'))
    assert data['api_key'] == voice['api_key'] and data['volc_token'] == voice['volc_token']
    for _, value, secret in entries:
        if value and secret: assert value not in transcript and value.decode() not in json.dumps(data)
    return data

cloud=provider_setup('xfyun',[(b'Xfyun APPID [',b'SYNTHETIC-APP',False),
    (b'Xfyun APIKey (',b'fake-xfyun-key',True),(b'Xfyun APISecret (',b'fake-xfyun-secret',True),
    (b'Xfyun voice [',b'xiaoyan',False)])
assert cloud['xfyun_api_key'].startswith('a7g1:') and cloud['xfyun_api_secret'].startswith('a7g1:')
cloud=provider_setup('tencent',[(b'Tencent SecretId (',b'fake-tencent-id',True),
    (b'Tencent SecretKey (',b'fake-tencent-key',True),(b'Tencent optional STS token (',b'fake-tencent-token',True),
    (b'Tencent ASR engine [',b'16k_zh',False),(b'Tencent voice type [',b'1001',False)])
assert all(cloud[k].startswith('a7g1:') for k in ('tencent_secret_id','tencent_secret_key','tencent_session_token'))
cloud=provider_setup('edge',[(b'Edge HTTPS companion hostname (',b'voice.example.test',False),
    (b'Edge HTTPS companion port [',b'8443',False),(b'Edge companion bearer token (',b'fake-edge-token-12345678901234567890',True),
    (b'Edge voice [',b'zh-CN-XiaoxiaoNeural',False)])
assert cloud['edge_gateway_token'].startswith('a7g1:')
for kind, name in [('asr','xfyun'),('tts','edge')]:
    subprocess.run([binary,'select',kind,name],check=True)
    assert json.load(open(directory+'/config/config.json'))['cloud_'+kind+'_provider']==name
before=open(directory+'/config/config.json','rb').read()
assert subprocess.run([binary,'select','asr','edge']).returncode == 1
assert open(directory+'/config/config.json','rb').read()==before
print('PTY cloud providers: Xfyun/Tencent/Edge hidden AES-GCM credentials, independent persistent selection; Edge ASR rejected without modifying config')
for interrupt in ('byte','signal','overlong'):
    master,slave=pty.openpty()
    original=termios.tcgetattr(slave)
    proc=subprocess.Popen([binary],stdin=slave,stdout=slave,stderr=slave)
    transcript=bytearray()
    for prompt in (b'Provider [',b'Model name [',b'API key (hidden):'):
        limit=time.monotonic()+10
        while prompt not in transcript:
            assert time.monotonic()<limit
            if select.select([master],[],[],0.1)[0]:
                transcript.extend(os.read(master,4096))
        if prompt!=b'API key (hidden):': os.write(master,b'\n')
    os.write(master,b'partial-secret' if interrupt!='overlong' else b'x'*160+b'\n')
    time.sleep(0.05)
    if interrupt=='byte': os.write(master,b'\x03')
    elif interrupt=='signal': os.kill(proc.pid,signal.SIGINT)
    assert proc.wait(timeout=10)==1
    assert termios.tcgetattr(slave)==original
    os.close(slave); os.close(master)
print('Ctrl+C byte/signal and overlong-key rejection restore terminal state')
