#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual product service, ASan/UBSan, external ASR/LLM/PCM are explicit mocks."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='product-service-v1-') as d:
    root=Path(d)
    source=(r/'tools/test-aipet-audio.cxx').read_text().split('\nint main()\n')[0]
    source=source.replace('if(turn<=4)', 'if(turn<=5)')
    source=source.replace('std::strcpy(out,"你好");return 0;}','std::strcpy(out,turn==3?"识别物体":turn==4?"前进一百二十三步":"你好");return 0;}')
    source+='''
#include "product_runtime.hxx"
#include <system/a733_camera_live.h>
namespace aipet {
static ProductMode test_mode=ProductMode::local;
static unsigned vision_calls=0,motor_calls=0,chat_calls=0,spoken_calls=0;
ProductMode product_mode(){return test_mode;}
bool product_network(){return false;}
bool product_cloud_ready(){return false;}
bool product_cloud_configured(){return false;}
void product_cloud_failed(){}
void product_cloud_succeeded(){}
bool product_cloud_cooling(){return false;}
int product_cloud_recover(VoiceLocalCancel cancel,void *cookie){return cancel?cancel(cookie):-ENETDOWN;}
bool product_vision_busy(){return false;}
int product_screen_close(){return 0;}
void product_screen_open(){}
void product_screen_resume(){}
int product_screen_pause(){return 0;}
int product_pending_vision(const char *s){return product_vision_intent(s);}
int mcu_control(int,char **){return 0;} // No UART, no motion in this harness.
void mcu_desktop_session(bool){} // Explicit mock: no actual UART permission here.
struct TestPorts:ProductPorts {
  int vision(std::string &out)override{
    assert(general_opens==general_closes);++vision_calls;out="检测到狗。";return 0;
  }
  bool motor(const char *s,std::string &out,int &status)override {
    if(std::string(s)!="前进一百二十三步")return false;
    ++motor_calls;out="未使能，没有发送动作。";status=-EACCES;return true;
  }
  int chat(ProductRoute,const char *,std::string &,bool &done)override{++chat_calls;done=true;return 0;}
  int speak(ProductRoute route,const Reply &)override{assert(route==ProductRoute::local);++spoken_calls;return 0;}
};
int product_request(ProductRoute route,const char *s,VoiceLocalCancel,void *){TestPorts p;return product_turn(route,s,p);}
}
int main() {
  using namespace aipet;
  assert(audio_voice_service_main()==1);
  assert(!command({"output","i2s"}));assert(!command({"volume","50"}));assert(!command({"gain","5"}));
  general_selected=true;warmup_result=0;
  assert(!command({"service","start"}));
  for(unsigned i=0;i<10000&&resident_waits.load()<6;i++)usleep(1000);
  assert(resident_waits==6&&audio_voice_service_active());
  assert(vision_calls==1&&motor_calls==1&&chat_calls==1&&spoken_calls==2);
  assert(general_opens==2&&general_closes==1&&warmups==1);
  resident_foreign=true;assert(a733_product_camera_begin()==-EBUSY);resident_foreign=false;
  assert(command({"service","start"})==1&&resident_spawns==1);
  assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&!audio_voice_service_active()&&general_opens==general_closes);
  assert(!audio_product_manual_begin());
  assert(command({"service","start"})==1&&resident_spawns==1);
  resident_foreign=true;assert(a733_product_camera_begin()==-EBUSY);resident_foreign=false;
  assert(!a733_product_camera_begin());a733_product_start_unlock();audio_product_manual_end();
  unsigned before=syntheses;
  assert(audio_i2s_cloud_speak("Hello！语音测试成功。") == 0);
  assert(general_selected&&syntheses==before+2); // Explicit cloud bypasses local selection.
  test_mode=ProductMode::cloud;
  unsigned fallback_waits=resident_waits;
  assert(!command({"service","start"}));
  for(unsigned i=0;i<15000&&audio_voice_service_phase_for_test()!=8;i++)usleep(1000);
  assert(audio_voice_service_active()&&audio_voice_service_phase_for_test()==8&&resident_waits==fallback_waits&&captures==0&&uploads==0);
  assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&!audio_voice_service_active());
  std::puts("PASS PRODUCT SERVICE: actual service, local/vision leases; unconfigured CLOUD waits with MIC OFF and NEVER opens local; cooperative stop. MOCK engines/PCM, NOT board.");
}
'''
    fixture=root/'service.cxx';fixture.write_text(source)
    script=(r/'tools/test-aipet-audio.sh').read_text()
    script=script.replace('repo=$(cd "$(dirname "$0")/.." && pwd)',"repo='"+str(r)+"'")
    script=script[:script.index('\ng++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \\\n')]
    script=script.replace('"$repo/tools/test-aipet-audio.cxx"','"'+str(fixture)+'" "$pet/product_core.cxx"')
    script=script.replace('-DAIPET_AUDIO_DIR=', '-DAIPET_TEST_RESIDENT -DCONFIG_A733_PRODUCT_V1=1 -Wno-unused-function -Wl,--wrap=posix_spawn -Wl,--wrap=posix_spawn_file_actions_addopen -Wl,--wrap=getpid -DAIPET_AUDIO_DIR=',1)
    subprocess.run(['bash','-c',script],check=True,timeout=360)
