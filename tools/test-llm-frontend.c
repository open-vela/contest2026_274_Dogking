/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define LLM_FRONTEND_HOST_TEST
#define CONFIG_AIPETLLM_DEFAULT_MODEL "/default.gguf"
#define main llm_frontend_main
#include "../board/a733-cubie-a7z/openvela-overlay/apps/system/aipetllm/llm_main.c"
#undef main

static int calls;
static int last_argc;
static char last[6][128];
int aipetllm_dispatch(int argc, char **argv)
{
  int i;
  calls++;
  last_argc = argc;
  for (i = 0; i < argc; i++)
    snprintf(last[i], sizeof(last[i]), "%s", argv[i]);
  return 0;
}
int main(void)
{
  char *run[] = {"llm", "run", "-p", "你好", "-n", "8", "--cores", "6,7"};
  char *chat[] = {"llm", "chat", "--model", "/model.gguf", "--prompt", "Hello"};
  char *bad[] = {"llm", "run", "-p", "Hello", "-n", "65"};
  char *duplicate[] = {"llm", "run", "-p", "A", "--prompt", "B"};
  char *missing[] = {"llm", "run", "-p"};
  char *check[] = {"llm", "check"};
  char *status[] = {"llm", "status"};
  char *reset[] = {"llm", "reset"};
  int before;
  assert(llm_frontend_main(8, run) == 0);
  assert(last_argc == 6 && !strcmp(last[1], "chat") &&
         !strcmp(last[2], "/default.gguf") && !strcmp(last[3], "你好") &&
         !strcmp(last[4], "8") && !strcmp(last[5], "6,7"));
  assert(llm_frontend_main(6, chat) == 0);
  assert(!strcmp(last[1], "ask") && !strcmp(last[2], "/model.gguf"));
  before = calls;
  assert(llm_frontend_main(6, bad) == 2);
  assert(llm_frontend_main(6, duplicate) == 2);
  assert(llm_frontend_main(3, missing) == 2);
  assert(calls == before);
  assert(llm_frontend_main(2, check) == 0);
  assert(last_argc == 3 && !strcmp(last[1], "modelcheck"));
  before = calls;
  assert(llm_frontend_main(2, status) == 0 && calls == before + 2);
  assert(!strcmp(last[1], "cacheinfo"));
  assert(llm_frontend_main(2, reset) == 0 && !strcmp(last[1], "newchat"));
  puts("llm frontend argument mapping tests passed");
  return 0;
}
