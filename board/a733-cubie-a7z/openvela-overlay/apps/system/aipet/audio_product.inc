/* SPDX-License-Identifier: Apache-2.0 */
/* Included INSIDE namespace aipet, after the original audited audio helpers.
 * No second device lock, kill/reset, or lifetime borrowed from an SSH shell. */
static std::atomic<unsigned> product_current_route{0};
const char *audio_product_backend(){unsigned r=product_current_route.load();return r==1?"local":r==2?"cloud":"none";}
static int product_recovery_pause(unsigned ms) {
  for(unsigned elapsed=0;elapsed<ms;elapsed+=20) {
    int rc=local_cancel(nullptr);if(rc)return rc;usleep(20000);
  }
  return local_cancel(nullptr);
}
int audio_product_prepare_local() {
  int rc=local_cancel(nullptr);if(rc)return rc;
#if defined(__NuttX__) && defined(CONFIG_SYSTEM_A733STORAGE)
  // Existing qualified one-attempt handoff/mount only. No format, migration,
  // benchmark, reset or retry. Never block inside the storage worker's I/O.
  if(!a733_storage_ready()) {
    VoiceLoadLog load("product-UFS-existing-mount");int requested=a733_storage_start(true);
    const long long deadline=voice_load_now()+15000000;
    while((!requested||requested==-EBUSY)&&!a733_storage_ready()&&voice_load_now()<deadline) {
      rc=local_cancel(nullptr);if(rc)return rc;usleep(20000);
      requested=a733_storage_start(true); // idempotent status, never repeats failed boot I/O
    }
    std::printf("product-load-v1: UFS request=%d mounted=%u; incomplete/unavailable model sets remain explicit errors, no storage repair\n",requested,a733_storage_ready());
    load.finish(a733_storage_ready()?0:requested?requested:-ETIMEDOUT);
  }
#endif
  int selected=voice_general_tts_selected();if(selected<0)return selected;
  if(!selected) {
    bool mounted=false;
#if defined(__NuttX__) && defined(CONFIG_SYSTEM_A733STORAGE)
    mounted=a733_storage_ready();
#endif
    VoiceModelRoots roots;rc=voice_model_roots(mounted,voice_storage_probe,roots);
    std::printf("product-load-v1: ASR=%s TTS=%s source=%s status=%d; microphone OFF\n",roots.asr,roots.tts,roots.reason,rc);std::fflush(stdout);
    if(!rc)rc=voice_general_configure_full(roots.asr,roots.tts);
    if(rc)return rc;
  }
  return local_cancel(nullptr);
}
int audio_i2s_cloud_speak(const char *text) {
  if(service_active.load()&&service_pid.load()!=getpid())return -EBUSY;
  if(audio_fault.load())return -EOWNERDEAD;
  Reply checked;if(!text||!parse_reply(text,checked))return -EILSEQ;
  AudioInterruptGuard signals;if(signals.error)return -signals.error;
  if(pthread_mutex_trylock(&operation_lock))return -EBUSY;
  struct Unlock {~Unlock(){pthread_mutex_unlock(&operation_lock);}} unlock;
  AudioConfig c;int rc=config(c);if(rc)return rc;
  if(c.output!=1||!c.volume)return 0;
  // Complete clauses only. No character cut or discarded tail. A too-long
  // indivisible clause is a reported error, NEVER a claimed full playback.
  SentenceStream split;std::vector<Reply> clauses;
  if(!split.feed(checked.text,clauses)||!split.finish(clauses))return -EILSEQ;
  struct CloudContext { AudioConfig config;std::atomic<bool> stop{false};bool service; } context{c,{},service_active.load()&&service_pid.load()==getpid()};
  auto synthesis=[](void *p,const char *s,unsigned char *pcm,std::size_t cap,std::size_t *n) {
    auto &ctx=*static_cast<CloudContext *>(p);
    auto stopped=[](void *v)->int {auto &c=*static_cast<CloudContext *>(v);return c.stop.load()||(c.service&&service_stop.load())||interrupted()?-ECANCELED:0;};
    int r=stopped(&ctx);if(r)return r;
    if(!engine_ready())return -ENODEV;
    for(unsigned attempt=1;attempt<=3;attempt++) {
      *n=0;r=aipet_cloud_tts_cancel(s,pcm,cap,n,stopped,&ctx);
      if(!r){product_cloud_succeeded();break;}
      if(!product_cloud_transient(r)||attempt==3)break;
      std::printf("cloud-recovery-v226: TTS attempt=%u/3 status=%d; retry UNPLAYED clause only, no semantic/action replay\n",attempt,r);
      r=0;
      for(unsigned ms=0;ms<attempt*1000&&!r;ms+=20){r=stopped(&ctx);if(!r)usleep(20000);}
      if(r)break;
      r=product_cloud_recover(stopped,&ctx);if(r==-ECANCELED)break;
      r=0;
    }
    int cancel=stopped(&ctx);return cancel?cancel:r;
  };
  auto playback=[](void *p,const unsigned char *pcm,std::size_t n,unsigned rate,unsigned channels,unsigned bits) {
    int r=local_cancel(nullptr);if(r)return r;
    return bits==16?a733_pcm_play(pcm,n,rate,channels,static_cast<CloudContext *>(p)->config.volume):-ENOTSUP;
  };
  unsigned done=0;auto started=voice_load_now();
  rc=cloud_clause_pipeline(clauses,{&context,synthesis,playback,nullptr},local_cancel,nullptr,context.stop,done);
  std::printf("cloud-prefetch-v227: clauses=%u/%u elapsed-ms=%lld slots=2; joined owned PCM, no played-prefix replay\n",done,unsigned(clauses.size()),(voice_load_now()-started)/1000);
  std::printf("product-tts-v1: backend=selected-cloud clauses=%u/%u status=%d; no local substitution\n",done,unsigned(clauses.size()),rc);
  return rc;
}
bool audio_product_setting(const char *text,std::string &out,int &rc) {
  SettingIntent intent;if(!text||!product_setting_intent(text,intent))return false;
  rc=0;
  if(service_active.load()&&service_pid.load()!=getpid())rc=-EBUSY;
  AudioConfig c;
  if(!rc) {
    pthread_mutex_lock(&config_lock);rc=load_config(c);
    if(!rc) {
      switch(intent.kind) {
      case SettingKind::volume:c.volume=unsigned(intent.value);c.output=1;break;
      case SettingKind::volume_delta: {
        int v=int(c.volume)+intent.value;c.volume=unsigned(v<0?0:v>100?100:v);c.output=1;break;}
      case SettingKind::mute:c.output=2;break;
      case SettingKind::unmute:c.output=1;if(!c.volume)c.volume=50;break;
      case SettingKind::gain:c.gain=unsigned(intent.value);break;
      default:break;
      }
      if(intent.kind!=SettingKind::volume_status)rc=save_config(c);
    }
    pthread_mutex_unlock(&config_lock);
  }
  char reply[160];
  if(rc)out="设置没有保存成功，请查看日志。";
  else if(intent.kind==SettingKind::gain) {std::snprintf(reply,sizeof(reply),"麦克风增益已设置为%u倍，下一轮生效。",c.gain);out=reply;}
  else {std::snprintf(reply,sizeof(reply),"音量%u%%，%s。",c.volume,c.output==2||!c.volume?"已静音":"声音已开启");out=reply;}
  std::printf("product-setting-v225: kind=%u value=%d status=%d; persisted, capture never started by setting\n",unsigned(intent.kind),intent.value,rc);
  return true;
}
struct ProductCloudPcm {
  std::unique_ptr<unsigned char[]> pcm;
  std::size_t used=0;
  ProductCloudPcm():pcm(new(std::nothrow) unsigned char[256000]){}
  ~ProductCloudPcm(){if(pcm)aipet_wipe(pcm.get(),256000);}
};
static int product_cloud_collect(void *p,const unsigned char *pcm,std::size_t n) {
  auto &out=*static_cast<ProductCloudPcm *>(p);
  if(!pcm||n>256000-out.used)return -EMSGSIZE;
  std::memcpy(out.pcm.get()+out.used,pcm,n);out.used+=n;return 0;
}
static void product_mic_state(void *,unsigned event) {
  pthread_mutex_lock(&service_lock);
  int previous=service_stage.load();
  if(event==1&&previous==VS_CALIBRATING){service_stage=VS_IDLE;std::puts("product-v1: READY MIC ON; waiting with opening pre-roll, half-duplex");}
  else if(event==2&&(previous==VS_CALIBRATING||previous==VS_IDLE)){service_stage=VS_SPEECH;std::puts("product-v1: speech detected; opening audio retained");}
  else if(event==3){service_stage=VS_FINALIZING;std::puts("product-v1: MIC OFF; RX stopped, ASR finalizing");}
  pthread_mutex_unlock(&service_lock);
}
static int product_voice_main() {
  pthread_mutex_lock(&service_lock);bool owner=service_active.load()&&service_pid.load()==0;
  if(owner)service_pid=getpid();
  pthread_mutex_unlock(&service_lock);if(!owner)return 1;
  AudioInterruptGuard signals;AudioConfig c;int rc=signals.error?-signals.error:config(c);
  if(!rc&&(c.output!=1&&c.output!=2))rc=-EACCES;
  VoiceGeneralSession *session=nullptr;bool local_warm=false;uint64_t cloud_noise=0;
  auto disarm=[](){mcu_desktop_session(false);char op[]="disarm";char *args[]={op};(void)mcu_control(1,args);};
  auto quarantine=[&]() {
    disarm();
    audio_fault=-EOWNERDEAD;voice_general_session_retain(session);
    service_error=-EOWNERDEAD;service_stage=VS_QUARANTINE;
    std::puts("product-v1: retained device/worker owner; REBOOT REQUIRED; no reset/kill/retry");
    for(;;)usleep(100000);
  };
  auto close_session=[&]() {
    if(!session)return 0;
    // close() can delete its wrapper even on an engine close error. NEVER
    // leave a dangling pointer and then retain/close it a second time.
    auto *closing=session;session=nullptr;int r=voice_general_session_close(closing);
    return r?-EOWNERDEAD:0;
  };
  auto open_local=[&]() {
    int r=audio_product_prepare_local();if(r)return r;
    if(!local_warm) {
      voice_service_load(4);VoiceLoadLog warm("product-LLM-preload");
      r=local_llm_preflight();if(!r&&local_llm_control("preload"))r=-EIO;
      warm.finish(r);if(r)return r;local_warm=true;
    }
    if(!session){voice_service_load(5);VoiceLoadLog load("product-ASR-vocoder-prepare");r=voice_general_session_open(&session,local_cancel,nullptr);load.finish(r);}
    return r;
  };
  // v225 desktop consent is scoped to this explicitly started service. Only
  // an exact human command can authorize motion, never an LLM response.
  if(!rc){char start[]="start";char *args[]={start};int motor=mcu_control(1,args);disarm();mcu_desktop_session(true);std::printf("product-v225: MCU link-start=%d; desktop command session, no repeated manual arm\n",motor);}
  if(!rc)product_screen_open();
  bool greeting=product_boot_greeting.exchange(false);
  if(greeting&&product_mode()!=ProductMode::local) {
    VoiceLoadLog ready("boot-network-cloud-wait");const auto end=voice_load_now()+60000000;
    while(product_network()&&!(product_mode()==ProductMode::cloud?product_cloud_configured():product_cloud_ready())&&voice_load_now()<end&&!service_stop.load())usleep(100000);
    rc=local_cancel(nullptr);
    ready.finish(rc);
    std::printf("product-boot-v228: network=%u cloud-ready=%u; cloud mode NEVER falls back; auto alone permits local fallback\n",product_network(),product_cloud_configured());
  }
  while(!rc&&!service_stop.load()) {
    service_stage=VS_PREPARING;voice_service_load(2);
    const ProductMode mode=product_mode();
    const bool cloud_only=mode==ProductMode::cloud;
    auto cloud_ready=[&](){return cloud_only?product_cloud_configured():product_cloud_ready();};
    if(cloud_only)product_current_route=2; // Waiting is cloud, never an implied local route.
    if(mode!=ProductMode::local&&(cloud_only||!product_cloud_cooling())&&(!product_network()||!cloud_ready())) {
      bool restored=false;
      for(unsigned attempt=1;attempt<=3&&!restored;attempt++) {
        rc=local_cancel(nullptr);if(rc)break;
        std::printf("cloud-recovery-v226: link attempt=%u/3 network=%u; MIC OFF\n",attempt,product_network());
        rc=product_cloud_recover(local_cancel,nullptr);if(rc==-ECANCELED)break;
        restored=!rc&&product_network()&&cloud_ready();
        if(!restored&&attempt<3){rc=product_recovery_pause(attempt*1000);if(rc)break;}
      }
      if(rc==-ECANCELED)break;
      if(!restored)product_cloud_failed();
      rc=0;
    }
    // Preserve explicit CLOUD. AUTO is the ONLY policy authorizing fallback.
    ProductChoice choice=product_choose(mode,{product_network(),cloud_ready(),true});
    rc=choice.error;
    if(rc&&cloud_only) {
      service_error=rc;service_stage=VS_WAIT_CLOUD;
      std::printf("cloud-only-v228: waiting network=%u configured=%u error=%d; MIC OFF, local-model-loads=0; retry in 5s, stop remains available\n",product_network(),cloud_ready(),rc);std::fflush(stdout);
      rc=product_recovery_pause(5000);if(rc)break;continue;
    }
    if(rc)break;
    service_error=0;
    product_current_route=choice.route==ProductRoute::cloud?2:1;
    std::printf("product-route-v1: turn=%u frozen=%s reason=%s; each turn re-evaluated, never switch after action/audio\n",service_turns.load()+1,choice.route==ProductRoute::cloud?"cloud":"local",choice.reason);std::fflush(stdout);
    if(choice.route==ProductRoute::local)rc=open_local();else rc=close_session();
    if(rc) {
      if(rc==-EOWNERDEAD)quarantine();
      if(mode==ProductMode::local||rc==-ECANCELED)break;
      service_error=rc;std::printf("cloud-recovery-v226: local fallback unavailable=%d; MIC OFF, waiting without exiting\n",rc);
      rc=product_recovery_pause(5000);if(rc)break;product_cloud_succeeded();continue;
    }
    if(greeting) {
      std::puts("product-boot-v225: models READY; greeting before MIC ON: 我是桌宠vela，有什么可以帮助您的吗");std::fflush(stdout);
      if(audio_muted())std::puts("product-boot-v225: saved mute respected; greeting NOT played");
      else rc=choice.route==ProductRoute::local?audio_i2s_local_stream_speak("我是桌宠vela，有什么可以帮助您的吗"):audio_i2s_cloud_speak("我是桌宠vela，有什么可以帮助您的吗");
      if(rc) {
        if(mode==ProductMode::local||rc==-ECANCELED||rc==-EOWNERDEAD)break;
        service_error=rc;product_cloud_failed();greeting=false;
        std::puts("cloud-recovery-v228: greeting failed; not replayed, selected mode retained; cloud NEVER selects local");
        rc=0;continue;
      }
      greeting=false;
    }
    rc=config(c);if(rc)break; // Gain changes apply on the very next capture.
    voice_service_load(6);service_stage=VS_CALIBRATING;char text[2048]{};
    std::unique_ptr<ProductCloudPcm> cloud;
    if(choice.route==ProductRoute::cloud) {cloud.reset(new(std::nothrow) ProductCloudPcm);if(!cloud||!cloud->pcm){rc=-ENOMEM;break;}}
    if(pthread_mutex_trylock(&operation_lock)){rc=-EBUSY;break;}
    if(choice.route==ProductRoute::local)rc=voice_general_session_wait(session,c.gain,service_floor.load(),text,sizeof(text),local_cancel,nullptr,product_mic_state,nullptr);
    else {
      LiveCaptureMetrics metrics{};
      rc=audio_capture_dialogue(c.gain,service_floor.load(),product_cloud_collect,cloud.get(),local_cancel,nullptr,product_mic_state,nullptr,&metrics,&cloud_noise);
      if(metrics.consumer_retained){cloud.release();rc=-EOWNERDEAD;}
    }
    pthread_mutex_unlock(&operation_lock);
    if(rc==-EOWNERDEAD)quarantine();
    if(rc==-EMSGSIZE){++service_long;rc=0;std::puts("product-v1: overlong utterance discarded, no partial LLM/action");continue;}
    if(rc)break;
    if(choice.route==ProductRoute::cloud) {
      rc=local_cancel(nullptr);
      if(!rc&&cloud->used) {
        for(unsigned attempt=1;attempt<=3;attempt++) {
          rc=aipet_cloud_asr_cancel(cloud->pcm.get(),cloud->used,text,sizeof(text),local_cancel,nullptr);
          if(!rc){product_cloud_succeeded();break;}
          if(!product_cloud_transient(rc)||attempt==3)break;
          std::printf("cloud-recovery-v226: ASR attempt=%u/3 status=%d; same retained PCM, BEFORE dispatch\n",attempt,rc);
          rc=product_recovery_pause(attempt*1000);if(rc)break;
          rc=product_cloud_recover(local_cancel,nullptr);if(rc==-ECANCELED)break;
        }
      }
      if(!cloud->used)rc=-ENODATA;
      // Re-recognizing PCM locally requires explicit AUTO, never CLOUD.
      if(rc&&rc!=-ECANCELED&&rc!=-EOWNERDEAD&&rc!=-ENODATA&&mode==ProductMode::automatic&&!service_stop.load()) {
        product_cloud_failed();std::printf("product-route-v1: cloud-ASR=%d; retained PCM re-recognized locally BEFORE any dispatch\n",rc);
        rc=audio_product_prepare_local();if(!rc)rc=voice_local_recognize(cloud->pcm.get(),cloud->used,text,sizeof(text),local_cancel,nullptr);
        choice.route=ProductRoute::local;product_current_route=1;
      }
      if(rc&&rc!=-ECANCELED&&rc!=-ENODATA)product_cloud_failed();
    }
    if(rc==-EOWNERDEAD){cloud.release();quarantine();}
    if(rc==-ENODATA){rc=0;++service_empty;continue;}
    if(rc) {
      if(rc!=-ECANCELED&&rc!=-EOWNERDEAD&&mode!=ProductMode::local){service_error=rc;std::printf("cloud-recovery-v226: turn failed=%d; discarded, no replay, listener retained\n",rc);rc=product_recovery_pause(1000);if(!rc)continue;}
      break;
    }
    if(!*text){++service_empty;continue;}
    if(service_stop.load()){rc=-ECANCELED;break;}
    service_stage=VS_REPLY;std::printf("product-v1: [asr backend=%s] %s\n",choice.route==ProductRoute::cloud?"cloud":"local",text);
    bool vision=product_pending_vision(text);
    if(vision)rc=close_session(); // No ASR/vocoder lease during camera NPU work.
    if(!rc)rc=product_request(choice.route,text,local_cancel,nullptr);
    aipet_wipe(text,sizeof(text));
    if(rc==-EOWNERDEAD)quarantine();
    if(!rc){++service_turns;service_error=0;}
    // Recoverable turn errors are visible, never claimed spoken/completed;
    // no semantic retry and no incomplete LLM context commit.
    if(rc&&rc!=-ECANCELED&&rc!=-EOWNERDEAD) {
      service_error=rc;std::printf("product-v1: turn failed=%d; not retried; listener resumes\n",rc);rc=0;
      product_screen_resume();
    }
  }
  int closed=close_session();if(closed==-EOWNERDEAD)quarantine();if(!rc)rc=closed;
  if(rc==-EOWNERDEAD)quarantine();
  disarm();
  int screen=product_screen_close();if(screen)quarantine();
  product_current_route=0;service_error=rc==-ECANCELED?0:rc;service_stage=VS_OFF;service_active=false;
  std::printf("product-v1: stopped=%d; microphone OFF; engines released, weights may remain resident\n",rc);
  return rc&&rc!=-ECANCELED?1:0;
}
