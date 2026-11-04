/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define CMD_OK 0
#define CMD_INVALID_OPT -4
#define PRINT(...) do { printf(__VA_ARGS__); puts(""); } while (0)
typedef struct { void *data; } uv_async_queue_t;
typedef struct { bool async_api; } bttool_t;
static void *g_bttool_ins;
static int calls, last_argc, forced_rc, closed;
static char first_arg[20];
static int execute_command(void *handle, int argc, char **argv)
{
    (void)handle;
    assert(argc > 0 && argc < 32);
    assert(argv[argc] == NULL);
    calls++;
    last_argc = argc;
    snprintf(first_arg, sizeof(first_arg), "%s", argv[0]);
    return forced_rc;
}
static const char *cmd_err_str(int rc) { (void)rc; return "mock error"; }
static void bttool_ins_uninit(void *data) { (void)data; }
static void handle_close_cb(void *data) { (void)data; }
static void uv_async_queue_close(uv_async_queue_t *handle, void (*cb)(void *))
{ closed++; cb(handle); }
#include "bttool-parser-extracted.h"
static void run(uv_async_queue_t *queue, const char *text)
{
    char *owned = strdup(text);
    assert(owned);
    bttool_execute_command_cb(queue, owned);
}
int main(void)
{
    bttool_t tool = {false};
    uv_async_queue_t queue = {&tool};
    run(&queue, "pair\tauto 0\r\n");
    assert(calls == 1 && last_argc == 3 && !strcmp(first_arg, "pair"));
    run(&queue, "   \t\r\n");
    assert(calls == 1);
    char many[100];
    for (int i = 0; i < 31; i++) { many[2*i] = 'x'; many[2*i + 1] = ' '; }
    many[62] = 0;
    run(&queue, many);
    assert(calls == 2 && last_argc == 31);
    strcat(many, "x");
    run(&queue, many);
    assert(calls == 2); /* 32 tokens rejected before dispatch */
    tool.async_api = true;
    run(&queue, "state");
    assert(calls == 2);
    tool.async_api = false;
    forced_rc = -2;
    run(&queue, "quit");
    assert(calls == 3 && closed == 1);
    puts("bttool parser whitespace/argv sentinel/limit/quit tests: PASS");
    return 0;
}
