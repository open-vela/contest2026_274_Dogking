if(CONFIG_EXAMPLES_AI_AGENT_VELA)
  nuttx_add_application(
    NAME aipet
    STACKSIZE 16384
    PRIORITY 100
    SRCS aipet_main.cxx pet_core.cxx pet_routes.cxx local_llm.cxx agent_startup.c agent_setup.c
         uart_tts.cxx speech_output.cxx
    INCLUDE_DIRECTORIES
      ${NUTTX_APPS_DIR}/netutils/cjson/cJSON
      ${CMAKE_SOURCE_DIR}/../packages/ai_agent/src
      ${CMAKE_SOURCE_DIR}/../packages/ai_agent/include
      ${NUTTX_APPS_DIR}/system/aipetllm)
endif()
