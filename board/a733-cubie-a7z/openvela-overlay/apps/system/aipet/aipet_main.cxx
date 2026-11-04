#include "agent_bridge.h"
#include "agent_secret.h"
#include "pet_core.hxx"
#include "pet_routes.hxx"
#include "local_llm.hxx"
#include "audio_app.hxx"
#include "mcu_link.hxx"
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#ifdef CONFIG_A733_PRODUCT_V1
#include "product_runtime.hxx"
#endif
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

extern "C" int aipet_main(int argc, char **argv)
{
  if (argc==2 && !std::strcmp(argv[1],"--mcu-worker"))
    return aipet::mcu_worker_main();
  aipet::install_local_llm_backend();
  if(argc==2 && !std::strcmp(argv[1],"--voice-worker"))
    return aipet::audio_voice_service_main();
  if(argc==2 && !std::strcmp(argv[1],"--boot"))return aipet_startup(1);
#ifdef CONFIG_A733_PRODUCT_V1
  if(argc==2&&!std::strcmp(argv[1],"--pet-boot")) {
    char op[]="boot";char *args[]={op};return aipet::product_control(1,args);
  }
#endif
  if (argc>=2 && !std::strcmp(argv[1],"mcu"))
    return aipet::mcu_control(argc-2,argv+2);
  if (argc>=2 && !std::strcmp(argv[1],"audio"))
    return aipet::audio_control(argc-2,argv+2);
#ifdef CONFIG_A733_PRODUCT_V1
  if(argc>=2 && (!std::strcmp(argv[1],"mode") || !std::strcmp(argv[1],"run") ||
      !std::strcmp(argv[1],"stop") || !std::strcmp(argv[1],"help") || !std::strcmp(argv[1],"autostart") || !std::strcmp(argv[1],"model-actions")))
    return aipet::product_control(argc-1,argv+1);
  if(argc>=2 && !std::strcmp(argv[1],"vision")) {
    if(aipet::audio_voice_service_active() && !(argc==3&&(!std::strcmp(argv[2],"status")||!std::strcmp(argv[2],"stop"))))return 1;
    return aipet::product_control(argc-1,argv+1);
  }
  if(argc>=2&&!std::strcmp(argv[1],"memory"))return aipet::product_control(argc-1,argv+1);
  if(aipet::product_vision_busy()&&argc>=2&&(!std::strcmp(argv[1],"ask")||!std::strcmp(argv[1],"local"))) {
    std::puts("product-v1: camera owns NPU/display; wait for camera live-status cleanup=0");return 1;
  }
#endif
  if(aipet::audio_voice_service_active() && argc>=2 &&
     std::strcmp(argv[1],"status") && std::strcmp(argv[1],"route")) {
    std::puts("aipet: resident voice owns LLM/audio; use aipet audio service stop/status first");return 1;
  }
  if (argc == 2 && (!std::strcmp(argv[1], "init") || !std::strcmp(argv[1], "setup")))
    return aipet_setup();
  if (argc == 2 && !std::strcmp(argv[1], "status"))
    { aipet_startup_status(); return 0; }
  if (argc == 2 && !std::strcmp(argv[1], "start"))
    return aipet_startup(0);
  if (argc == 2 && !std::strcmp(argv[1], "--boot"))
    return aipet_startup(1);
  if (argc == 2 && !std::strcmp(argv[1], "cancel"))
    { aipet_agent_cancel(); return 0; }
  if (argc == 3 && !std::strcmp(argv[1], "route"))
    return aipet::routed_ask(argv[2], true, false);
  if (argc == 3 && !std::strcmp(argv[1], "cloud"))
    return aipet::routed_ask(argv[2], false, true);
  if (argc == 3 && !std::strcmp(argv[1], "local"))
    return aipet::local_llm_control(argv[2]);
  if (argc == 4 && !std::strcmp(argv[1], "local") &&
      !std::strcmp(argv[2], "ask"))
    return aipet::routed_local_ask(argv[3]);
  if (argc == 4 && !std::strcmp(argv[1], "local"))
    return aipet::local_llm_control(argv[2], argv[3]);
  if (argc == 3 && !std::strcmp(argv[1], "tts"))
    return aipet::uart_tts_control(argv[2]);
  if (argc == 4 && !std::strcmp(argv[1], "tts") &&
      !std::strcmp(argv[2], "test"))
    return aipet::uart_tts_control(argv[2], argv[3]);
  if (argc != 3 || std::strcmp(argv[1], "ask"))
    {
      std::printf("aipet init (one-time online setup via serial/SSH)\n"
                  "aipet ask \"text\" | aipet cancel\n"
                  "aipet route \"text\" (inspect only) | cloud \"text\" (bypass rules)\n"
                  "aipet start | status; service agent on|off|status\n"
                  "aipet mcu start|status|ping|arm|disarm|stop|servo|move\n"
                  "aipet tts status|off (legacy UART TTS retired)\n"
                  "aipet audio status|setup|output|volume|gain|record|features|play|play22k|asr|listen|speak\n"
                  "aipet local status|on|off|preload|unload|stop|clear\n"
                  "aipet local ask \"text\" | model PATH | tokens N | cores MASK | context 0..4 | batch 1|4\n"
                  "Boot initialization waits for network/time/config.\n"
                  "Official Agent + I2S speech + dedicated UART4 STM32 control at 115200.\n");
      return 1;
    }
#ifdef CONFIG_A733_PRODUCT_V1
  return aipet::product_text_ask(argv[2]);
#else
  return aipet::routed_ask(argv[2], false, false);
#endif
}
