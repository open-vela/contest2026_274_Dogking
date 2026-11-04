#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""NEW regular image only; all older images and SDK restoration verified."""
from pathlib import Path
p=Path(__file__).with_name('seal-product-v20pre-v225.py');s=p.read_text()
s=s.replace('v225','v227').replace('V225','V227').replace('v20pre','v30pre').replace('v2.0pre','v3.0pre')
s=s.replace("len(voice['tests'])==36","len(voice['tests'])==41").replace('software_gates=36','software_gates=41')
s=s.replace('paths=316','paths=319').replace('SDK_restored_paths=316','SDK_restored_paths=319')
needle="assert read(a.build_report/'sdk-restored.json')==dict(result='SDK_RESTORED_BYTE_EXACT',paths=319)"
assert needle in s
s=s.replace(needle,needle+"\nsubprocess.run([sys.executable,str(r/'tools/check-a733-mcu-staging.py'),'restored',str((r/'../../quickly-openvela').resolve()),str(a.build_report/'sdk-before.json')],cwd=r,check=True)",1)
s=s.replace('spoken_numeric_steps_scale=10','spoken_numeric_steps_scale=20')
for label in ('mcu-desktop','product-setting','product-boot'):
    s=s.replace("b'"+label+"-v227:'","b'"+label+"-v225:'")
for name in ('command-route','boot-policy','boot-worker'):
    s=s.replace("'v227-"+name+"'","'v225-"+name+"'")
needle='for name,digest in old_images.items():';at=s.index(needle)
s=s[:at]+"""old_images.update({
 'openvela-a733-cubie-a7z-sd-v2.0pre-v225-candidate.img':'cd4cd64097720b7da15abef1ee7f1e8291c258888a8683590a8c537998ddc51c',
 'openvela-a733-cubie-a7z-sd-v2.1pre-v226-candidate.img':'63aa91f38a6223e0b3ae0d549f3be17020916824aa5273c15906697188799683'})
"""+s[at:]
needle="for name in ('summary.json','run.log'):";at=s.index(needle)
s=s[:at]+"""for name in ('cloud-recovery-v226','wifi-recovery-v226','actual-wss-v226','cloud-providers-v226','cloud-policy-prefetch-v227'):
    shutil.copyfile(a.voice_report/(name+'.log'),evidence/(name+'.log'))
shutil.copyfile(a.voice_report/'actual-wss/summary.json',evidence/'actual-wss-summary.json')
"""+s[at:]
exec(compile(s,str(p),'exec'))
sdk_llm=(r/'../../quickly-openvela/packages/ai_agent/src/llm/llm_proxy.c').resolve()
sdk_before=read(a.build_report/'sdk-before.json')['packages/ai_agent/src/llm/llm_proxy.c']
assert sdk_llm.stat().st_size==sdk_before['bytes'] and sha(sdk_llm)==sdk_before['sha256']
(evidence/'sdk-llm-restore.json').write_text(json.dumps(dict(path='packages/ai_agent/src/llm/llm_proxy.c',
 before=sdk_before,after_sha256=sha(sdk_llm),result='EXACT_RESTORED_AND_RECHECKED_AT_SEAL'),indent=2)+'\n')
result.update(tts_prefetch_slots=2,model_actions_default=False,max_model_actions_per_turn=1,
 desktop_exact_human_command_consent=False,desktop_rule_commands_exact=True,
 model_motion_requires_opt_in=True,model_motion_requires_current_invitation=True,
 max_user_steps=512,max_wire_pulses=10240,deepseek_flash_thinking='disabled',
 recovery_attempts=3,played_audio_replay=False,action_replay=False,
 Xfyun_real_account_TTS_validated=False,latency_accepted=False)
(evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print('V227 seal complete; board/cloud/acoustic tests still required',flush=True)
