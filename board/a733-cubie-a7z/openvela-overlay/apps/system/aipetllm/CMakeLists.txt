# ##############################################################################
# apps/system/aipetllm/CMakeLists.txt
# ##############################################################################

if(CONFIG_SYSTEM_AIPETLLM)
  set(AIPETLLM_GGML_SOURCES
      third_party/ggml/src/ggml-quants.c
      third_party/ggml/src/ggml-cpu/quants.c
      third_party/ggml/src/ggml-cpu/arch/arm/quants.c)

  set_source_files_properties(
    ${AIPETLLM_GGML_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-unused-function")

  nuttx_add_application(
    NAME
    ${CONFIG_AIPETLLM_PROGNAME}
    SRCS
    aipetllm_main.c
    aipetllm_cpucheck.c
    aipetllm_cxx.cxx
    aipetllm_bpe.cxx
    aipetllm_api.cxx
    aipetllm_ggml_quant.c
    aipetllm_modelcheck.c
    aipetllm_tokencheck.c
    ${AIPETLLM_GGML_SOURCES}
    STACKSIZE
    ${CONFIG_AIPETLLM_STACKSIZE}
    PRIORITY
    ${CONFIG_AIPETLLM_PRIORITY})

  target_include_directories(
    apps_aipetllm
    PRIVATE third_party/ggml/include third_party/ggml/src
            third_party/ggml/src/ggml-cpu)

  if(CONFIG_AIPETLLM_LLM_FRONTEND)
    nuttx_add_application(
      NAME llm
      SRCS llm_main.c
      STACKSIZE ${CONFIG_AIPETLLM_STACKSIZE}
      PRIORITY ${CONFIG_AIPETLLM_PRIORITY})
  endif()
endif()
