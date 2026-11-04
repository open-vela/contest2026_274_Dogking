#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual resident worker, saved settings and boot greeting ordering; MOCK engines."""
from pathlib import Path
import subprocess
p=Path(__file__).with_name('test-product-service-v1.py');s=p.read_text()
s=s.replace('-DCONFIG_A733_PRODUCT_V1=1','-DCONFIG_A733_PRODUCT_V1=1 -DCONFIG_A733_PRODUCT_V12=1')
needle='  test_mode=ProductMode::cloud;'
assert s.count(needle)==1
insert=r'''
  std::string setting_reply;int setting_rc=0;
  assert(audio_product_setting("音量调到九十",setting_reply,setting_rc)&&!setting_rc);
  assert(audio_product_setting("大声一点",setting_reply,setting_rc)&&!setting_rc);
  assert(audio_product_setting("大声一点",setting_reply,setting_rc)&&!setting_rc);
  assert(audio_product_setting("当前音量",setting_reply,setting_rc)&&!setting_rc&&setting_reply.find("100%")!=std::string::npos);
  assert(audio_product_setting("静音",setting_reply,setting_rc)&&!setting_rc&&audio_muted());
  unsigned waits=resident_waits,opens=general_opens,warms=warmups;
  assert(!command({"service","start-boot"}));
  for(unsigned i=0;i<10000&&resident_waits==waits;i++)usleep(1000);
  assert(resident_waits==waits+1&&general_opens==opens+1&&warmups==warms+1);
  resident_foreign=true;
  assert(audio_product_setting("音量五十",setting_reply,setting_rc)&&setting_rc==-EBUSY);
  resident_foreign=false;
  assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&!audio_voice_service_active());
  assert(audio_product_setting("取消静音",setting_reply,setting_rc)&&!setting_rc&&!audio_muted());
  assert(audio_product_setting("音量五十",setting_reply,setting_rc)&&!setting_rc);
  expected_volume=50;stream_supported=true;
  static std::atomic<unsigned> greetings{0};static bool greet_fail=false;
  VoiceLocalProvider local{"boot-test-MOCK",0,1,2,nullptr,nullptr,
    [](void *,const char *text,VoiceLocalPcmSink sink,void *cookie,long long,VoiceLocalCancel,void *){
      assert(std::string(text)=="我是桌宠vela，有什么可以帮助您的吗");
      assert(general_opens>general_closes&&warmups>=3);++greetings;
      if(greet_fail)return -EIO;
      unsigned char pcm[8820]{};return sink(cookie,pcm,sizeof(pcm));
    }};
  assert(!voice_local_install(&local));waits=resident_waits;unsigned played=stream_plays;
  assert(!command({"service","start-boot"}));
  for(unsigned i=0;i<10000&&resident_waits==waits;i++)usleep(1000);
  assert(resident_waits==waits+1&&greetings==1&&stream_plays>=played+1);
  assert(!command({"service","stop"}));assert(!pthread_join(resident_thread,nullptr));
  assert(!resident_result&&!audio_voice_service_active());
  greet_fail=true;waits=resident_waits;
  assert(!command({"service","start-boot"}));assert(!pthread_join(resident_thread,nullptr));
  assert(resident_result==1&&resident_waits==waits&&greetings==2&&!audio_voice_service_active());
  assert(!voice_local_install(nullptr));stream_supported=false;
  std::puts("PASS v225 BOOT: actual saved/clamped volume, mute/unmute, foreign owner rejection; greeting after models/session prepare and before first MIC; failed greeting never enables MIC. Engines/PCM mocked, NOT acoustic acceptance.");
'''
s=s.replace(needle,insert+needle)
# Extra boot cycles alter only the final mock wait counter, not old assertions.
s=s.replace('resident_waits==6&&captures==0&&uploads==0','resident_waits==waits&&captures==0&&uploads==0')
exec(compile(s,str(p),'exec'),dict(__file__=str(p),__name__='__main__'))
