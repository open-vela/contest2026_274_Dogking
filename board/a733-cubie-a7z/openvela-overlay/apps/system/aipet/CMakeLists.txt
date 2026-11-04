if(CONFIG_EXAMPLES_AI_AGENT_VELA)
  set(GENERAL_TTS_SRCS)
  set(AIPET_STACK_SIZE 16384)
  if(CONFIG_A733_CPU_ACOUSTIC)
    if(NOT "${CONFIG_LIBC_MAX_EXITFUNS}" STREQUAL "0")
      message(FATAL_ERROR "Resident ORT requires the audited reboot-lifetime static environment (LIBC_MAX_EXITFUNS=0)")
    endif()
    # ORT/MLAS executes synchronously on this task, not a Linux worker pool.
    # Reserve an explicit bounded stack for the full CPU runtime call chain.
    set(AIPET_STACK_SIZE 262144)
    set(ORT_PREFIX "$ENV{A733_ORT_STATIC_PREFIX}")
    if(NOT IS_ABSOLUTE "${ORT_PREFIX}" OR NOT EXISTS "${ORT_PREFIX}/manifest.json")
      message(FATAL_ERROR "CPU acoustic requires a real NuttX/AArch64 static ORT prefix")
    endif()
    file(READ "${ORT_PREFIX}/manifest.json" ORT_MANIFEST)
    foreach(pair "libc;NuttX" "architecture;ELF64-little-AArch64" "ort_version;1.23.2")
      list(GET pair 0 key)
      list(GET pair 1 expected)
      string(JSON actual GET "${ORT_MANIFEST}" ${key})
      if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "ORT manifest incompatible ${key}: ${actual}")
      endif()
    endforeach()
    string(JSON ABI_COUNT LENGTH "${ORT_MANIFEST}" required_abi)
    math(EXPR ABI_LAST "${ABI_COUNT}-1")
    foreach(index RANGE ${ABI_LAST})
      string(JSON key MEMBER "${ORT_MANIFEST}" required_abi ${index})
      string(JSON expected GET "${ORT_MANIFEST}" required_abi ${key})
      set(actual "${${key}}")
      if("${expected}" STREQUAL "n" AND NOT actual)
        set(actual n)
      endif()
      if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "ORT ABI mismatch ${key}: firmware='${actual}' library='${expected}'")
      endif()
    endforeach()
    string(JSON ORT_COUNT LENGTH "${ORT_MANIFEST}" libraries)
    if(ORT_COUNT LESS 3 OR ORT_COUNT GREATER 128)
      message(FATAL_ERROR "Invalid ORT archive count")
    endif()
    math(EXPR ORT_LAST "${ORT_COUNT}-1")
    foreach(index RANGE ${ORT_LAST})
      string(JSON relative GET "${ORT_MANIFEST}" libraries ${index} path)
      string(JSON expected GET "${ORT_MANIFEST}" libraries ${index} sha256)
      if(NOT relative MATCHES "^lib/[A-Za-z0-9_.-]+[.]a$")
        message(FATAL_ERROR "Invalid ORT archive path")
      endif()
      file(SHA256 "${ORT_PREFIX}/${relative}" actual)
      if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "ORT archive checksum mismatch: ${relative}")
      endif()
      nuttx_add_extra_library("${ORT_PREFIX}/${relative}")
    endforeach()
    set(GENERAL_TTS_SRCS voice_acoustic.cxx voice_tts_engine.cxx)
  endif()
  set(KNF_ROOT ${CMAKE_CURRENT_LIST_DIR}/third_party/kaldi-native-fbank/csrc)
  set(KNF_SRCS)
  foreach(source feature-fbank feature-functions feature-window kaldi-math log
                 mel-computations online-feature rfft fftsg)
    list(APPEND KNF_SRCS ${KNF_ROOT}/src/${source}.cc)
  endforeach()
  set(PRODUCT_SRCS)
  if(CONFIG_A733_PRODUCT_V1)
    set(PRODUCT_SRCS product_core.cxx product_runtime.cxx)
  endif()
  nuttx_add_application(
    NAME aipet
    STACKSIZE ${AIPET_STACK_SIZE}
    PRIORITY 100
    SRCS aipet_main.cxx pet_core.cxx pet_routes.cxx local_llm.cxx agent_startup.c agent_setup.c
         product_cloud_config.c cloud_voice.cxx cloud_voice_protocol.cxx cloud_voice_socket.cxx
         uart_tts.cxx speech_output.cxx audio_app.cxx audio_resample.cxx voice_fbank.cxx voice_kws.c voice_npu_check.cxx
         voice_asr_decode.c voice_asr_stream.cxx voice_tts_frontend.cxx voice_local.cxx audio_resample_stream.cxx voice_play_stream.cxx voice_phrase_pipe.cxx audio_capture_live.cxx
         voice_general_asr.cxx voice_general_provider.cxx
         ${GENERAL_TTS_SRCS} ${PRODUCT_SRCS}
         mcu_protocol.cxx mcu_link.cxx mcu_agent_tool.cxx mcu_tool_bridge.c
         ${KNF_SRCS}
         ${CMAKE_SOURCE_DIR}/../vendor/allwinnertech/chips/a733/a733_audio_client.c
         ${CMAKE_SOURCE_DIR}/../vendor/allwinnertech/chips/a733/a733_tensor_client.c
    INCLUDE_DIRECTORIES
      ${KNF_ROOT}/include
      ${NUTTX_APPS_DIR}/netutils/cjson/cJSON
      ${CMAKE_SOURCE_DIR}/../packages/ai_agent/src
      ${CMAKE_SOURCE_DIR}/../packages/ai_agent/include
      ${NUTTX_APPS_DIR}/system/aipetllm
      ${NUTTX_APPS_DIR}/system/a733display)
endif()
