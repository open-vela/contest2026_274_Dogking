#include "agent_bridge.h"
#include "core/message_bus.h"
#include "core/message_bus_tap.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PET_TEXT_MAX 4096
/* Process lifetime storage: official unregister does not join in-flight taps. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int attached, pending, ready, result;
static int cancelled;
/* No automatic restart after teardown: official thread joins are not audited. */
static int lifecycle, owner = -1, lifecycle_error;

int aipet_agent_claim(void)
{
  pthread_mutex_lock(&lock);
  int status = lifecycle ? -EBUSY : 0;
  if (!status) { lifecycle = 1; owner = getpid(); lifecycle_error = 0; }
  pthread_mutex_unlock(&lock);
  return status;
}

void aipet_agent_finish(int status)
{
  pthread_mutex_lock(&lock);
  lifecycle = status ? 3 : 4;
  lifecycle_error = status;
  owner = -1;
  pthread_mutex_unlock(&lock);
}

void aipet_agent_status(int *phase, int *pid, int *error)
{
  pthread_mutex_lock(&lock);
  *phase = attached ? 2 : lifecycle;
  *pid = owner;
  *error = lifecycle_error;
  pthread_mutex_unlock(&lock);
}
static uint64_t sequence, started, deadline, finished;
static char chat_id[64], response[PET_TEXT_MAX + 1];

static uint64_t monotonic_ms(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void receive(const agent_msg_t *msg, void *cookie)
{
  size_t length;
  (void)cookie;
  pthread_mutex_lock(&lock);
  if (!attached || !pending || strcmp(msg->chat_id, chat_id))
    { pthread_mutex_unlock(&lock); return; }
  finished = monotonic_ms();
  length = msg->content ? strnlen(msg->content, PET_TEXT_MAX + 1) : 0;
  result = finished >= deadline ? -ETIMEDOUT :
           !length ? -ENODATA : length > PET_TEXT_MAX ? -EMSGSIZE : 0;
  if (!result && !strncmp(msg->content, "[AIPET_ERROR]", 13)) result = -EIO;
  if (!result) memcpy(response, msg->content, length + 1);
  pending = 0;
  ready = 1;
  pthread_mutex_unlock(&lock);
}

int aipet_agent_attach(void)
{
  int status;
  pthread_mutex_lock(&lock);
  if (attached) { pthread_mutex_unlock(&lock); return 0; }
  status = mbus_tap_register("pet", receive, NULL);
  if (!status) attached = 1;
  pthread_mutex_unlock(&lock);
  return status ? -EBUSY : 0;
}

/* Strict RFC 3629: reject truncated, overlong, surrogate and out-of-range
 * input before cJSON (which deliberately preserves unchecked bytes). */
static int valid_utf8(const unsigned char *s, size_t n)
{
  size_t i=0;
  while (i<n) {
    unsigned c=s[i++], cp; size_t rest; unsigned min;
    if (c<128) continue;
    if (c>=0xc2 && c<=0xdf) { rest=1; cp=c&31; min=0x80; }
    else if (c>=0xe0 && c<=0xef) { rest=2; cp=c&15; min=0x800; }
    else if (c>=0xf0 && c<=0xf4) { rest=3; cp=c&7; min=0x10000; }
    else return 0;
    if (rest>n-i) return 0;
    while (rest--) {
      c=s[i++]; if ((c&0xc0)!=0x80) return 0;
      cp=(cp<<6)|(c&63);
    }
    if (cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return 0;
  }
  return 1;
}
int aipet_agent_submit(const char *text, uint32_t timeout_ms)
{
  agent_msg_t msg = {0};
  size_t length;
  int status;
  if (!text || timeout_ms < 100 || timeout_ms > 120000) return -EINVAL;
  length = strnlen(text, PET_TEXT_MAX + 1);
    if (!length || length > PET_TEXT_MAX) return -EMSGSIZE;
    if (!valid_utf8((const unsigned char *)text, length)) return -EILSEQ;
  msg.content = malloc(length + 1);
  if (!msg.content) return -ENOMEM;
  memcpy(msg.content, text, length + 1);
  pthread_mutex_lock(&lock);
  if (!attached || pending || ready)
    {
      status = attached ? -EBUSY : -ENODEV;
      pthread_mutex_unlock(&lock);
      free(msg.content);
      return status;
    }
  snprintf(chat_id, sizeof(chat_id), "pet-%llu", (unsigned long long)++sequence);
  strcpy(msg.channel, "pet");
  strcpy(msg.chat_id, chat_id);
  started = monotonic_ms();
  cancelled = 0;
  deadline = started + timeout_ms;
  pending = 1;
  status = message_bus_push_inbound(&msg);
  if (status) { pending = 0; free(msg.content); }
  pthread_mutex_unlock(&lock);
  return status ? -EAGAIN : 0;
}

int aipet_agent_poll(char *reply, size_t capacity, uint64_t *elapsed_ms)
{
  int status;
  if (!reply || !capacity) return -EINVAL;
  pthread_mutex_lock(&lock);
  if (pending && monotonic_ms() >= deadline)
    { pending = 0; ready = 1; finished = monotonic_ms(); result = -ETIMEDOUT; }
  if (!ready) { status = cancelled ? -ECANCELED : -EAGAIN;
    pthread_mutex_unlock(&lock); return status; }
  status = result;
  if (!status && strlen(response) + 1 > capacity)
    { pthread_mutex_unlock(&lock); return -ENOSPC; }
  if (!status) strcpy(reply, response);
  else reply[0] = '\0';
  if (elapsed_ms) *elapsed_ms = finished - started;
  ready = 0;
  response[0] = '\0';
  pthread_mutex_unlock(&lock);
  return status;
}

void aipet_agent_cancel(void)
{
  pthread_mutex_lock(&lock);
  pending = ready = 0;
  cancelled = 1;
  chat_id[0] = response[0] = '\0';
  pthread_mutex_unlock(&lock);
  /* Discards presentation only; does not pretend to cancel the cloud request. */
}

void aipet_agent_detach(void)
{
  pthread_mutex_lock(&lock);
  if (attached) mbus_tap_unregister("pet");
  attached = pending = ready = 0;
  cancelled = 1;
  chat_id[0] = response[0] = '\0';
  pthread_mutex_unlock(&lock);
}
