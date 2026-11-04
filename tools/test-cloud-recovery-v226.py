#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual resident/dispatch/TTS retry paths. Network, MIC and UART are MOCK."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
source=(r/'tools/test-aipet-audio.cxx').read_text().split('\nint main()\n')[0]
source=source.replace('cap==4097','(cap==4097||cap==2048)')
source=source.replace('return r?r:aipet_cloud_asr(p,n,text,cap);','if(!r&&asr_error_count){--asr_error_count;asr_error=-EIO;}else asr_error=0;return r?r:aipet_cloud_asr(p,n,text,cap);')
source=source.replace('return r?r:aipet_cloud_tts(text,p,cap,n);','tts_error=(tts_error_count||std::strstr(text,"后半句"))?-EIO:0;if(tts_error_count)--tts_error_count;return r?r:aipet_cloud_tts(text,p,cap,n);')
source=source.replace('extern "C" int aipet_cloud_asr_cancel','static unsigned asr_error_count=0,tts_error_count=0;\nextern "C" int aipet_cloud_asr_cancel',1)
stub=(r/'tools/test-product-service-v1.py').read_text().split("source+='''",1)[1].split('int main()',1)[0]
stub=stub.replace('bool product_network(){return false;}','static bool network=true,link_will_recover=false;static unsigned link_calls=0;\nbool product_network(){return network;}')
stub=stub.replace('bool product_cloud_ready(){return false;}','static bool bad=false;\nbool product_cloud_ready(){return !bad;}')
stub=stub.replace('void product_cloud_failed(){}','void product_cloud_failed(){bad=true;}')
stub=stub.replace('void product_cloud_succeeded(){}','void product_cloud_succeeded(){bad=false;}')
stub=stub.replace('bool product_cloud_cooling(){return false;}','bool product_cloud_cooling(){return bad;}')
stub=stub.replace('return cancel?cancel(cookie):-ENETDOWN;','++link_calls;if(link_will_recover)network=true;return cancel?cancel(cookie):0;')
stub=stub.replace('assert(route==ProductRoute::local);++spoken_calls;return 0;','++spoken_calls;return route==ProductRoute::cloud?audio_i2s_cloud_speak("你好。"):0;')
source+=stub+r'''
namespace aipet {
static std::atomic<unsigned> test_captures{0};
int audio_capture_dialogue(unsigned,unsigned,LiveSink sink,void *cookie,VoiceLocalCancel cancel,void *cc,
  LiveVoiceState state,void *sc,LiveCaptureMetrics *metrics,uint64_t *) {
  *metrics={};state(sc,1);
  if(++test_captures==1) {
    state(sc,2);unsigned char pcm[3200]{};int rc=sink(cookie,pcm,sizeof(pcm));state(sc,3);return rc;
  }
  for(;;){int rc=cancel(cc);if(rc)return rc;usleep(1000);}
}
}
int main() {
  using namespace aipet;
  assert(!command({"output","i2s"}));assert(!command({"volume","50"}));
  general_selected=true;warmup_result=0;test_mode=ProductMode::automatic; // Only explicit AUTO authorizes these fallback cases.
  asr_text="前进一百二十三步";asr_error_count=2;tts_error_count=2;
  expected_asr_bytes=3200;
  assert(!command({"service","start"}));
  for(unsigned i=0;i<20000&&test_captures<2;i++)usleep(1000);
  assert(test_captures==2&&audio_voice_service_active());
  assert(uploads==3&&syntheses==3&&plays==1&&motor_calls==1&&spoken_calls==1);
  assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&!audio_voice_service_active());
  // Three failed cloud ASR attempts, then local recognition BEFORE dispatch.
  VoiceLocalProvider local{"MOCK-CPU-ASR",1,0,0,nullptr,
    [](void *,const unsigned char *,size_t,char *out,size_t,long long,VoiceLocalCancel,void *){
      std::strcpy(out,"前进一百二十三步");return 0;},nullptr};
  assert(!voice_local_install(&local));test_captures=0;asr_error_count=10;
  unsigned u=uploads,m=motor_calls;resident_waits=99;
  assert(!command({"service","start"}));
  for(unsigned i=0;i<20000&&resident_waits<100;i++)usleep(1000);
  assert(audio_voice_service_active()&&resident_waits==100&&bad);
  assert(uploads==u+3&&motor_calls==m+1); // not 3 semantic sends
  assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&general_opens==general_closes);
  // Credentials/Agent still READY with carrier down: MUST recover three times
  // before local fallback. A stale cloud-ready flag must not skip recovery.
  bad=false;network=false;link_will_recover=false;asr_error_count=0;
  u=uploads;m=link_calls;resident_waits=99;
  assert(!command({"service","start"}));
  for(unsigned i=0;i<20000&&resident_waits<100;i++)usleep(1000);
  assert(audio_voice_service_active()&&resident_waits==100&&bad);
  assert(link_calls==m+3&&uploads==u);
  assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&general_opens==general_closes);
  // One successful link recovery selects cloud, without loading local again.
  bad=false;network=false;link_will_recover=true;test_captures=0;tts_error_count=0;
  u=uploads;m=link_calls;
  assert(!command({"service","start"}));
  for(unsigned i=0;i<20000&&test_captures<2;i++)usleep(1000);
  assert(test_captures==2&&audio_voice_service_active()&&network);
  assert(link_calls==m+1&&uploads==u+1);
  assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&general_opens==general_closes);
  assert(!voice_local_install(nullptr));asr_error_count=0;tts_error_count=0;
  // First clause played once. Failed second clause NEVER replays first/action.
  u=syntheses;m=plays;unsigned actions=motor_calls;
  assert(audio_i2s_cloud_speak("你好。后半句。")==-EIO);
  assert(plays==m+1&&syntheses==u+4&&motor_calls==actions);
  assert(!product_cloud_transient(-EACCES)&&!product_cloud_transient(-EPROTO));
  assert(product_cloud_transient(-EIO)&&product_cloud_transient(-EHOSTUNREACH));
  puts("PASS v226 ACTUAL service/TTS recovery: ASR third attempt success, exactly one dispatch; exhausted ASR -> local before dispatch; no service exit; TTS unplayed-clause-only retries, no played-prefix/action replay; stop/restart/lease cleanup. MOCK network/MIC/engines/UART, NOT board.");
}
'''
with tempfile.TemporaryDirectory(prefix='cloud-recovery-v226-') as d:
    t=Path(d);fixture=t/'recovery.cxx';fixture.write_text(source)
    capture=t/'capture.cxx'
    capture.write_text((app/'audio_capture_live.cxx').read_text().replace('int audio_capture_dialogue(', 'int unused_audio_capture_dialogue(',1))
    script=(r/'tools/test-aipet-audio.sh').read_text()
    script=script.replace('repo=$(cd "$(dirname "$0")/.." && pwd)',"repo='"+str(r)+"'")
    script=script[:script.index('\ng++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \\\n')]
    script=script.replace('"$repo/tools/test-aipet-audio.cxx"','"'+str(fixture)+'" "$pet/product_core.cxx"')
    script=script.replace('"$pet/audio_capture_live.cxx"','"'+str(capture)+'" -I"$pet"')
    script=script.replace('-DAIPET_AUDIO_DIR=', '-DAIPET_TEST_RESIDENT -DCONFIG_A733_PRODUCT_V1=1 -Wno-unused-function -Wl,--wrap=posix_spawn -Wl,--wrap=posix_spawn_file_actions_addopen -Wl,--wrap=getpid -DAIPET_AUDIO_DIR=',1)
    subprocess.run(['bash','-c',script],check=True,timeout=360)
