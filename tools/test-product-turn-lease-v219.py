#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual RuntimePorts/dispatcher + prepare + provider lease, mocked engines.

Unlike the original service harness, this does NOT replace RuntimePorts.speak
or RuntimePorts.chat with a success-only TestPorts implementation.
"""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1];o=r/'board/a733-cubie-a7z/openvela-overlay';app=o/'apps/system/aipet'
source=(r/'tools/test-voice-session-v190.cxx').read_text().split('\nstatic int sink(')[0]
prepare=(app/'audio_product.inc').read_text();prepare=prepare[prepare.index('int audio_product_prepare_local()'):prepare.index('int audio_i2s_cloud_speak(')]
source+='''
#include "voice_model_storage.hxx"
#include "voice_load_log.hxx"
#include "product_runtime.cxx"
static unsigned tts_calls=0,chat_calls=0,pcm_bytes=0;
static bool cloud_test=false,cloud_cancel=false;
static unsigned model_sends=0,cloud_calls=0;static int cloud_tts_error=0;
static std::string cloud_reply,submitted;
extern "C" int a733_display_pet_start(void){return 0;}
extern "C" int a733_display_pet_pause(void){return 0;}
extern "C" int a733_camera_identify_start(const char *,uint32_t *){return -ENOTSUP;}
extern "C" int a733_camera_identify_poll(uint32_t,a733_live_result *){return -ENOTSUP;}
extern "C" int a733_camera_identify_stop(uint32_t){return 0;}
extern "C" int aipet_agent_submit(const char *s,uint32_t){submitted=s;return cloud_test?0:-ENODEV;}
extern "C" int aipet_agent_poll(char *p,size_t cap,uint64_t *ms){if(!cloud_test)return -ENODEV;assert(cloud_reply.size()<cap);std::strcpy(p,cloud_reply.c_str());*ms=1;return 0;}
extern "C" void aipet_agent_cancel(void){}
namespace aipet {
static int local_cancel(void *){return 0;}
static int voice_storage_probe(const char *){return -ENOENT;}
'''+prepare+'''
void mcu_turn_end(){}
bool audio_muted(){return false;}
bool audio_product_setting(const char *,std::string &,int &){return false;}
int routed_offline_ask(const char *){return -ENOTSUP;}
bool mcu_route(const char *,bool,std::string &,int *){return false;}
int mcu_desktop_request(const McuCommand &c,std::string &s){++model_sends;assert(c.op==1&&c.payload==std::vector<uint8_t>({0,2,40,0}));s="模拟下位机没有确认，动作结果未知。";return -ETIMEDOUT;}
Rules default_routes(){Rules r;r.direct.push_back({{"你好"},{"你好呀！今天想聊什么？"}});return r;}
bool load_routes(const char *,Rules &){return false;}
std::string expand_clock_template(const std::string &s){return s;}
int audio_i2s_local_stream_speak(const char *text){
  ++tts_calls;
  auto output=[](void *,const unsigned char *,std::size_t n)->int {pcm_bytes+=n;return 0;};
  return installed.synthesize(installed.cookie,text,output,nullptr,0,nullptr,nullptr);
}
int audio_i2s_cloud_speak(const char *s){if(!cloud_test)return -ENODEV;++cloud_calls;assert(!std::strstr(s,"[ACTION:"));return cloud_tts_error;}
int routed_offline_stream_ask(const char *){++chat_calls;return audio_i2s_local_stream_speak("这是模拟引擎的回复。");}
}
int main(){
  using namespace aipet;
  product_cloud_succeeded();assert(!product_cloud_cooling());
  product_cloud_failed();assert(product_cloud_cooling());
  auto remaining=cloud_bad_until.load()-milliseconds();
  assert(remaining>=119000&&remaining<=120000);
  product_cloud_succeeded();assert(!product_cloud_cooling());
  assert(!voice_general_configure_full("/data/npu/asr","/data/npu/tts"));
  for(unsigned restart=0;restart<2;restart++) {
    VoiceGeneralSession *session=nullptr;assert(!voice_general_session_open(&session,nullptr,nullptr));
    for(unsigned round=0;round<3;round++) {
      char text[64]{};assert(!voice_general_session_listen(session,100,5,0,text,sizeof(text),nullptr,nullptr));
      assert(!product_request(ProductRoute::local,round==1?"请聊聊今天的生活":"你好",nullptr,nullptr));
    }
    pthread_t foreign;assert(!pthread_create(&foreign,nullptr,[](void *)->void * {
      assert(product_request(ProductRoute::local,"你好",nullptr,nullptr)==-EBUSY);return nullptr;
    },nullptr));assert(!pthread_join(foreign,nullptr));
    auto cancel=[](void *)->int{return -ECANCELED;};
    assert(product_request(ProductRoute::local,"你好",cancel,nullptr)==-ECANCELED);
    assert(!voice_general_session_close(session));
  }
  assert(tts_calls==6&&chat_calls==2&&pcm_bytes==12&&opens==closes&&tts_opens==tts_closes);
  assert(!voice_general_configure("off"));
  cloud_test=true;
  auto setting=[](const char *s){FILE *f=fopen(AIPET_PRODUCT_MODE_DIR "/model-actions","wb");assert(f);fputs(s,f);fputc(10,f);assert(!fclose(f));};
  setting("on");cloud_reply="我来陪你玩。[ACTION:motor.forward(2)][开心]";
  assert(!product_request(ProductRoute::cloud,"陪我玩，动一动",nullptr,nullptr));
  assert(model_sends==1&&cloud_calls==1&&submitted.find("本轮允许一个有界动作")!=std::string::npos);
  cloud_tts_error=-EIO;
  assert(product_request(ProductRoute::cloud,"陪我玩，动一动",nullptr,nullptr)==-EIO);
  assert(model_sends==2&&cloud_calls==2); // TTS failure cannot replay the UART send.
  cloud_tts_error=0;setting("off");
  assert(!product_request(ProductRoute::cloud,"陪我玩，动一动",nullptr,nullptr)&&model_sends==2);
  assert(submitted.find("本轮未开启模型动作")!=std::string::npos);
  setting("on");
  assert(!product_request(ProductRoute::cloud,"不要动一动",nullptr,nullptr)&&model_sends==2);
  cloud_reply="你好。[ACTION:motor.forward(2)][ACTION:motor.forward(2)][开心]";
  assert(!product_request(ProductRoute::cloud,"陪我玩，动一动",nullptr,nullptr)&&model_sends==2);
  cloud_cancel=true;auto stopped=[](void *)->int{return cloud_cancel?-ECANCELED:0;};
  assert(product_request(ProductRoute::cloud,"陪我玩，动一动",stopped,nullptr)==-ECANCELED&&model_sends==2);
  puts("PASS v227 actual RuntimePorts cloud chat: persona, persistent opt-in/off, negative invite, one proposal, unknown ACK once, failed TTS cannot replay action, cancellation; mocked Agent/UART/PCM, NOT cloud/hardware acceptance.");
  puts("PASS actual RuntimePorts: rule reply + local chat -> production prepare -> leased provider -> mocked PCM, 6 turns, 2 sessions; foreign/cancel cannot speak. MOCK inference/PCM, NOT board.");
}
'''
with tempfile.TemporaryDirectory(prefix='turn-lease-v219-') as d:
    root=Path(d);c=root/'turn.cxx';c.write_text(source);exe=root/'turn'
    sdk=(r/'../../quickly-openvela').resolve()
    subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-variable',
      '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-fsanitize=address,undefined','-pthread',
      '-DAIPET_TEST_RESIDENT','-DCONFIG_A733_CPU_ACOUSTIC=1',
      '-DAIPET_PRODUCT_MODE_DIR="'+str(root)+'"',
      '-I'+str(app),'-I'+str(app.parent/'a733display'),'-I'+str(o/'apps/include'),
      '-I'+str(sdk/'packages/ai_agent/src'),'-I'+str(sdk/'packages/ai_agent/include'),
      str(c),str(app/'voice_general_provider.cxx'),str(app/'product_core.cxx'),str(app/'pet_core.cxx'),str(app/'mcu_protocol.cxx'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
