/****************************************************************************
 * apps/system/a733display/aipet_ui_ipc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_SYSTEM_A733DISPLAY_AIPET_UI_IPC_H
#define __APPS_SYSTEM_A733DISPLAY_AIPET_UI_IPC_H

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stddef.h>
#include <string.h>

/* CONFIG_FS_MQUEUE_NAME_INITIAL_SLASH is disabled in the A733 configuration.
 * Keep each message below CONFIG_MQ_MAXMSGSIZE (32 bytes).
 */

#define AIPET_UI_QUEUE_NAME    "aipet_ui"
#define AIPET_UI_MESSAGE_SIZE  24
#define AIPET_UI_IDLE_MS       10000

static inline int aipet_ui_notify(const char *emotion)
{
  char message[AIPET_UI_MESSAGE_SIZE] = {0};
  mqd_t queue;
  size_t length;
  int saved;

  if (emotion == NULL || emotion[0] == '\0')
    {
      return -EINVAL;
    }

  length = strnlen(emotion, sizeof(message));
  if (length == sizeof(message))
    {
      return -E2BIG;
    }

  memcpy(message, emotion, length);
  queue = mq_open(AIPET_UI_QUEUE_NAME, O_WRONLY | O_NONBLOCK);
  if (queue == (mqd_t)-1)
    {
      return -errno;
    }

  if (mq_send(queue, message, sizeof(message), 0) < 0)
    {
      saved = errno;
      mq_close(queue);
      return -saved;
    }

  mq_close(queue);
  return 0;
}

#endif
