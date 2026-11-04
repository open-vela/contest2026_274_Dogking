/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "agent_bridge.h"
#include "agent_secret.h"
#include <system/a733_services.h>
#include <cJSON.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <spawn.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>

static pthread_mutex_t startup_lock = PTHREAD_MUTEX_INITIALIZER;
static int boot_running;
static const char *waiting = "idle";

static int network_ready(void)
{
  struct ifaddrs *list = NULL;
  int ready = 0;
  if (getifaddrs(&list) != 0) return 0;
  for (struct ifaddrs *p = list; p; p = p->ifa_next)
    if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && p->ifa_name &&
        !strcmp(p->ifa_name, "wlan0"))
      ready = ((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr != 0;
  freeifaddrs(list);
  return ready;
}

/* Read only the credential-presence flag; never log the key or file contents. */
static int configured(void)
{
  char data[4097];
  struct stat st;
  int fd = open("/data/ai_agent/config/config.json", O_RDONLY);
  if (fd < 0) return 0;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 2 || st.st_size > 4096)
    { close(fd); return 0; }
  size_t used = 0;
  while (used < (size_t)st.st_size)
    {
      ssize_t n = read(fd, data + used, (size_t)st.st_size - used);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { close(fd); return 0; }
      used += n;
    }
  close(fd);
  data[used] = 0;
  cJSON *root = cJSON_Parse(data);
  cJSON *key = root ? cJSON_GetObjectItemCaseSensitive(root, "api_key") : NULL;
  char plain[128];
  int ready = cJSON_IsString(key) &&
              !aipet_secret_open(key->valuestring, plain, sizeof(plain));
  aipet_wipe(plain, sizeof(plain));
  cJSON_Delete(root);
  memset(data, 0, sizeof(data));
  return ready;
}

void aipet_startup_status(void)
{
  int phase, pid, error;
  const char *names[] = {"not-started", "starting", "ready", "failed", "stopped"};
  aipet_agent_status(&phase, &pid, &error);
  pthread_mutex_lock(&startup_lock);
  int running = boot_running;
  const char *reason = waiting;
  pthread_mutex_unlock(&startup_lock);
  printf("agent: %s pid=%d error=%d; initializer=%s waiting=%s\n",
         names[phase >= 0 && phase < 5 ? phase : 3], pid, error,
         running ? "running" : "idle", reason);
  a733_auto_option("agent", "status");
  puts("ready means local engine attached; cloud TLS connects on first request.");
}

int aipet_startup(int background_boot)
{
  if (!background_boot)
    return a733_spawn_service("aipet", "--boot") < 0 ? 1 : 0;
  pthread_mutex_lock(&startup_lock);
  if (boot_running) { pthread_mutex_unlock(&startup_lock); return 0; }
  boot_running = 1;
  pthread_mutex_unlock(&startup_lock);
  unsigned delay = 1;
  const char *previous = NULL;
  int result = 0;
  for (;;)
    {
      int phase, pid, error;
      aipet_agent_status(&phase, &pid, &error);
      if (phase) break; /* Already owned, including a failed/stopped engine. */
      const char *reason = !network_ready() ? "network" :
        !configured() ? "private-config" :
        access("/data/ai_agent/ca.pem", R_OK) ? "CA" :
        time(NULL) < 1735689600 ? "clock" : "starting";
      pthread_mutex_lock(&startup_lock);
      waiting = reason;
      pthread_mutex_unlock(&startup_lock);
      if (!previous || strcmp(previous, reason))
        syslog(LOG_INFO, "AIPET startup: waiting=%s\n", reason);
      previous = reason;
      if (!strcmp(reason, "starting"))
        {
          result = a733_spawn_service("ai_agent", NULL);
          break; /* No restart loop; manual and automatic paths share the gate. */
        }
      sleep(delay);
      if (delay < 8) delay *= 2;
    }
  pthread_mutex_lock(&startup_lock);
  boot_running = 0;
  waiting = result < 0 ? "spawn-failed" : "finished";
  pthread_mutex_unlock(&startup_lock);
  return result < 0 ? 1 : 0;
}
