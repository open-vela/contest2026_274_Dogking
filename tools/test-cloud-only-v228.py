#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Execute actual resident worker under ASan/UBSan. Network/engines/UART MOCK."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
app=r/'board/a733-cubie-a7z/openvela-overlay/apps/system/aipet'
source=(r/'tools/test-aipet-audio.cxx').read_text().split('\nint main()\n')[0]
source=source.replace('cap==4097','(cap==4097||cap==2048)')
source=source.replace('return r?r:aipet_cloud_asr(p,n,text,cap);','asr_error=asr_fail?-EIO:0;return r?r:aipet_cloud_asr(p,n,text,cap);')
source=source.replace('return r?r:aipet_cloud_tts(text,p,cap,n);','tts_error=tts_fail?-EIO:0;return r?r:aipet_cloud_tts(text,p,cap,n);')
source=source.replace('extern "C" int aipet_cloud_asr_cancel','static bool asr_fail=false,tts_fail=false;\nextern "C" int aipet_cloud_asr_cancel',1)
stub=(r/'tools/test-product-service-v1.py').read_text().split("source+='''",1)[1].split('int main()',1)[0]
stub=stub.replace('bool product_network(){return false;}','static bool network=true,configured=true,bad=false,recover_link=false;static unsigned recoveries=0;\nbool product_network(){return network;}')
stub=stub.replace('bool product_cloud_ready(){return false;}','bool product_cloud_ready(){return configured&&!bad;}')
stub=stub.replace('bool product_cloud_configured(){return false;}','bool product_cloud_configured(){return configured;}')
stub=stub.replace('void product_cloud_failed(){}','void product_cloud_failed(){bad=true;}')
stub=stub.replace('void product_cloud_succeeded(){}','void product_cloud_succeeded(){bad=false;}')
stub=stub.replace('bool product_cloud_cooling(){return false;}','bool product_cloud_cooling(){return bad;}')
stub=stub.replace('return cancel?cancel(cookie):-ENETDOWN;','++recoveries;if(recover_link)network=true;return cancel?cancel(cookie):0;')
stub=stub.replace('static unsigned vision_calls=', 'static bool llm_fail=false;\nstatic unsigned vision_calls=')
stub=stub.replace('int chat(ProductRoute,const char *,std::string &,bool &done)override{++chat_calls;done=true;return 0;}', 'int chat(ProductRoute route,const char *,std::string &,bool &done)override{assert(route==ProductRoute::cloud);++chat_calls;done=true;return llm_fail?-EIO:0;}')
stub=stub.replace('assert(route==ProductRoute::local);++spoken_calls;return 0;', 'assert(route==ProductRoute::cloud);++spoken_calls;return audio_i2s_cloud_speak("你好。");')
source+=stub+r'''
namespace aipet {
static std::atomic<unsigned> listening{0};
static unsigned local_recognizes=0;
int audio_capture_dialogue(unsigned,unsigned,LiveSink sink,void *cookie,VoiceLocalCancel cancel,void *cc,
  LiveVoiceState state,void *sc,LiveCaptureMetrics *metrics,uint64_t *) {
  *metrics={};state(sc,1);
  if(++listening==1) {
    state(sc,2);unsigned char pcm[3200]{};int rc=sink(cookie,pcm,sizeof(pcm));state(sc,3);return rc;
  }
  for(;;){int rc=cancel(cc);if(rc)return rc;usleep(1000);}
}
}
int main() {
  using namespace aipet;
  auto now=[](){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000ll+t.tv_nsec/1000;};
  assert(!command({"output","i2s"}));assert(!command({"volume","50"}));
  general_selected=true;warmup_result=0;test_mode=ProductMode::cloud;expected_asr_bytes=3200;
  VoiceLocalProvider forbidden{"FORBIDDEN-LOCAL-MOCK",1,0,0,nullptr,
    [](void *,const unsigned char *,size_t,char *,size_t,long long,VoiceLocalCancel,void *){++local_recognizes;return -EACCES;},nullptr};
  assert(!voice_local_install(&forbidden));
  asr_text="前进一百二十三步";
  auto no_local=[](){assert(general_opens==0&&general_closes==0&&warmups==0&&resident_waits==0&&local_recognizes==0);};
  auto start=[](){listening=0;assert(!command({"service","start"}));};
  auto stop=[&](){auto at=now();assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));assert(now()-at<1000000);assert(!resident_result&&!audio_voice_service_active());no_local();};
  auto listen_again=[](){for(unsigned i=0;i<18000&&listening<2;i++)usleep(1000);assert(listening==2&&audio_voice_service_active());assert(std::string(audio_product_backend())=="cloud");};
  auto waiting=[](){for(unsigned i=0;i<15000&&audio_voice_service_phase_for_test()!=8;i++)usleep(1000);assert(audio_voice_service_phase_for_test()==8&&audio_voice_service_active());assert(std::string(audio_product_backend())=="cloud");};
  // Explicit CLOUD ignores AUTO's stale 120s cooldown. Exactly one dispatch.
  bad=true;start();listen_again();assert(uploads==1&&motor_calls==1&&plays==1);stop();
  // No carrier: three bounded recoveries, remain active, NO local preload/capture.
  network=false;bad=false;unsigned calls=recoveries,u=uploads,m=motor_calls;
  start();waiting();assert(recoveries==calls+3&&uploads==u&&listening==0);
  // The SAME worker notices restored carrier despite AUTO cooldown, no run.
  network=true;bad=true;listen_again();assert(uploads==u+1&&motor_calls==m+1);stop();u=uploads;m=motor_calls;
  // Missing Agent/keys/time: same visible waiting policy, not silent local.
  network=true;configured=false;calls=recoveries;start();waiting();assert(recoveries==calls+3&&uploads==u);stop();configured=true;
  // Three ASR failures discard this utterance. No local recognition/action.
  asr_fail=true;start();listen_again();assert(uploads==u+3&&motor_calls==m);stop();asr_fail=false;
  // A failed LLM turn must resume cloud, never local chat or recognition.
  asr_text="聊天测试";llm_fail=true;u=chat_calls;start();listen_again();assert(chat_calls==u+1);stop();llm_fail=false;
  // Exhausted TTS retries: next utterance still cloud, no first-action replay.
  asr_text="前进一百二十三步";tts_fail=true;u=syntheses;m=motor_calls;start();listen_again();assert(syntheses==u+3&&motor_calls==m+1);stop();tts_fail=false;
  // A later start may recover the link and select cloud without local warmup.
  network=false;recover_link=true;calls=recoveries;start();listen_again();assert(network&&recoveries==calls+1);stop();
  assert(!voice_local_install(nullptr));
  puts("PASS v228 ACTUAL resident CLOUD: network/config failure waits, old cooldown ignored, ASR/LLM/TTS exhausted retries never call local, next turn stays cloud, stop/restart, no action replay. ASan/UBSan; network/MIC/engines/UART MOCK, NOT board.");
}
'''
with tempfile.TemporaryDirectory(prefix='cloud-only-v228-') as d:
    t=Path(d);fixture=t/'strict.cxx';fixture.write_text(source)
    capture=t/'capture.cxx'
    capture.write_text((app/'audio_capture_live.cxx').read_text().replace('int audio_capture_dialogue(', 'int unused_audio_capture_dialogue(',1))
    script=(r/'tools/test-aipet-audio.sh').read_text()
    script=script.replace('repo=$(cd "$(dirname "$0")/.." && pwd)',"repo='"+str(r)+"'")
    script=script[:script.index('\ng++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \\\n')]
    script=script.replace('"$repo/tools/test-aipet-audio.cxx"','"'+str(fixture)+'" "$pet/product_core.cxx"')
    script=script.replace('"$pet/audio_capture_live.cxx"','"'+str(capture)+'" -I"$pet"')
    script=script.replace('-DAIPET_AUDIO_DIR=', '-DAIPET_TEST_RESIDENT -DCONFIG_A733_PRODUCT_V1=1 -Wno-unused-function -Wl,--wrap=posix_spawn -Wl,--wrap=posix_spawn_file_actions_addopen -Wl,--wrap=getpid -DAIPET_AUDIO_DIR=',1)
    subprocess.run(['bash','-c',script],check=True,timeout=180)
