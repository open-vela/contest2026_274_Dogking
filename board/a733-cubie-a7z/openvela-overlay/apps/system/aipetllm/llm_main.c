/* SPDX-License-Identifier: Apache-2.0 */
/* Built-in openvela frontend; one shared runtime, not a second model cache. */
#ifndef LLM_FRONTEND_HOST_TEST
#include <nuttx/config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int aipetllm_dispatch(int argc, char **argv);

static void help(void)
{
  puts("openvela llm 0.1 (custom Qwen2 CPU backend; not llama.cpp)");
  puts("Usage: llm run|chat|generate -p TEXT [-m MODEL] [-n 1..64] [--cores LIST]");
  puts("  run: single-turn ChatML; chat: shared bounded multi-turn history");
  puts("  generate: raw text continuation without ChatML");
  puts("  llm info|status|stop|unload|history|reset|check [-m MODEL]");
  puts("  -m/--model, -p/--prompt, -n/--n-predict, --cores (default 2,3,4,5,6,7)");
  puts("History is board-global and RAM-only, 64 tokens; KV rebuilt each turn.");
  puts("Use stop from another session; Ctrl+C/forced kill is not yet safe.");
}

int main(int argc, char **argv)
{
  char *model = CONFIG_AIPETLLM_DEFAULT_MODEL;
  char *prompt = NULL;
  char *count = "24";
  char *cores = NULL;
  char *mapped[7] = {"aipetllm", NULL, NULL, NULL, NULL, NULL, NULL};
  const char *command;
  unsigned int seen = 0;
  int index;
  int mapped_count;

  if (argc == 1 || (argc == 2 &&
      (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))))
    {
      help();
      return 0;
    }
  command = argv[1];
  if (argc == 2 && !strcmp(command, "--version"))
    {
      puts("openvela llm 0.1 / custom Qwen2 CPU GGUF / greedy decoding");
      return 0;
    }
  if (argc == 2 && !strcmp(command, "status"))
    {
      mapped[1] = "runstatus";
      aipetllm_dispatch(2, mapped);
      mapped[1] = "cacheinfo";
      return aipetllm_dispatch(2, mapped);
    }
  if (!strcmp(command, "info") || !strcmp(command, "stop") ||
      !strcmp(command, "unload") || !strcmp(command, "history") ||
      !strcmp(command, "reset"))
    {
      if (argc != 2)
        {
          fputs("llm: this command takes no options\n", stderr);
          return 2;
        }
      mapped[1] = !strcmp(command, "reset") ? "newchat" : argv[1];
      return aipetllm_dispatch(2, mapped);
    }
  if (strcmp(command, "run") && strcmp(command, "chat") &&
      strcmp(command, "generate") && strcmp(command, "check"))
    {
      fputs("llm: unknown command; use --help\n", stderr);
      return 2;
    }
  for (index = 2; index < argc; index++)
    {
      unsigned int bit;
      char **value;
      if (!strcmp(argv[index], "-m") || !strcmp(argv[index], "--model"))
        { bit = 1; value = &model; }
      else if (!strcmp(argv[index], "-p") || !strcmp(argv[index], "--prompt"))
        { bit = 2; value = &prompt; }
      else if (!strcmp(argv[index], "-n") || !strcmp(argv[index], "--n-predict"))
        { bit = 4; value = &count; }
      else if (!strcmp(argv[index], "--cores"))
        { bit = 8; value = &cores; }
      else
        {
          fputs("llm: unknown option\n", stderr);
          return 2;
        }
      if ((seen & bit) || index + 1 >= argc || !argv[index + 1][0])
        {
          fputs("llm: duplicate option or missing/empty value\n", stderr);
          return 2;
        }
      seen |= bit;
      *value = argv[++index];
    }
  if (!strcmp(command, "check"))
    {
      if (seen & ~1u)
        {
          fputs("llm: check accepts only --model\n", stderr);
          return 2;
        }
      mapped[1] = "modelcheck";
      mapped[2] = model;
      return aipetllm_dispatch(3, mapped);
    }
  if (!prompt || !count[0] || strspn(count, "0123456789") != strlen(count) ||
      strlen(count) > 2 || strtoul(count, NULL, 10) < 1 ||
      strtoul(count, NULL, 10) > 64)
    {
      fputs("llm: --prompt required and --n-predict must be 1..64\n", stderr);
      return 2;
    }
  mapped[1] = !strcmp(command, "run") ? "chat" :
              !strcmp(command, "chat") ? "ask" : "generate";
  mapped[2] = model;
  mapped[3] = prompt;
  mapped[4] = count;
  mapped_count = 5;
  if (cores)
    {
      mapped[5] = cores;
      mapped_count++;
    }
  return aipetllm_dispatch(mapped_count, mapped);
}
