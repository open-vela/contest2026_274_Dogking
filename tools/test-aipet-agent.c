#include "agent_bridge.h"
#include "core/message_bus.h"
#include "core/message_bus_tap.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

static int claims;
static void *claim_worker(void *unused)
{
  (void)unused;
  if (aipet_agent_claim() == 0) __sync_fetch_and_add(&claims, 1);
  return NULL;
}

static agent_msg_t captured;
static int reject;
int message_bus_push_inbound(const agent_msg_t *msg)
{
  if (reject) return -1;
  captured = *msg;
  free(captured.content);
  captured.content = NULL;
  return 0;
}
int main(void)
{
  char text[4097];
  agent_msg_t old;
  uint64_t elapsed;
  pthread_t contenders[8];
  int phase, pid, error;
  aipet_agent_status(&phase, &pid, &error);
  assert(phase == 0 && pid == -1 && error == 0);
  for (int i = 0; i < 8; ++i)
    assert(pthread_create(&contenders[i], NULL, claim_worker, NULL) == 0);
  for (int i = 0; i < 8; ++i) assert(pthread_join(contenders[i], NULL) == 0);
  assert(claims == 1);
  aipet_agent_status(&phase, &pid, &error);
  assert(phase == 1 && pid == getpid());
  assert(aipet_agent_submit("hello", 100) == -ENODEV);
  assert(aipet_agent_attach() == 0);
  aipet_agent_status(&phase, &pid, &error);
  assert(phase == 2);
  assert(aipet_agent_submit("\xe4\xb8", 1000) == -EILSEQ);
  assert(aipet_agent_submit("\xc0\xaf", 1000) == -EILSEQ);
  assert(aipet_agent_submit("\xed\xa0\x80", 1000) == -EILSEQ);
  assert(aipet_agent_submit("\xf4\x90\x80\x80", 1000) == -EILSEQ);
  assert(aipet_agent_submit("给我讲 个故事", 1000) == 0);
  aipet_agent_cancel();
  assert(aipet_agent_poll(text, sizeof(text), NULL) == -ECANCELED);
  assert(aipet_agent_submit("hello", 1000) == 0);
  assert(aipet_agent_submit("second", 1000) == -EBUSY);
  assert(aipet_agent_poll(text, sizeof(text), NULL) == -EAGAIN);
  captured.content = "[开心]Hello";
  assert(mbus_tap_try_deliver(&captured));
  assert(aipet_agent_poll(text, 2, NULL) == -ENOSPC);
  assert(aipet_agent_poll(text, sizeof(text), &elapsed) == 0);
  assert(!strcmp(text, "[开心]Hello"));
  assert(aipet_agent_submit("backend failure", 1000) == 0);
  captured.content = "[AIPET_ERROR]backend";
  assert(mbus_tap_try_deliver(&captured));
  assert(aipet_agent_poll(text, sizeof(text), NULL) == -EIO);
  old = captured;
  assert(aipet_agent_submit("next", 1000) == 0);
  assert(mbus_tap_try_deliver(&old));
  assert(aipet_agent_poll(text, sizeof(text), NULL) == -EAGAIN);
  aipet_agent_cancel();
  assert(aipet_agent_poll(text, sizeof(text), NULL) == -ECANCELED);
  reject = 1;
  assert(aipet_agent_submit("rejected", 1000) == -EAGAIN);
  reject = 0;
  assert(aipet_agent_submit("timeout", 100) == 0);
  usleep(120000);
  assert(aipet_agent_poll(text, sizeof(text), NULL) == -ETIMEDOUT);
  aipet_agent_detach();
  assert(!mbus_tap_try_deliver(&captured));
  aipet_agent_finish(-1);
  aipet_agent_status(&phase, &pid, &error);
  assert(phase == 3 && pid == -1 && error == -1);
  assert(aipet_agent_claim() == -EBUSY);
  return 0;
}
