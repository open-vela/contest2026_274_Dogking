/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <system/a733_services.h>
#include <spawn.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

int a733_spawn_service(const char *program, const char *option)
{
  posix_spawn_file_actions_t actions;
  char *argv[] = {(char *)program, (char *)option, NULL};
  pid_t pid;
  int ret = posix_spawn_file_actions_init(&actions);
  if (ret != 0) return -ret;
  /* A boot service must never read passwords or steal console input. */
  ret = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  const char *output = !strcmp(program,"aipet") && option && !strcmp(option,"--pet-boot") ? "/dev/a7zconsole" : "/dev/null";
  if (ret == 0) ret = posix_spawn_file_actions_addopen(&actions, 1, output, O_WRONLY, 0);
  if (ret == 0) ret = posix_spawn_file_actions_addopen(&actions, 2, output, O_WRONLY, 0);
  if (ret == 0) ret = posix_spawn(&pid, program, &actions, NULL, argv, NULL);
  posix_spawn_file_actions_destroy(&actions);
  syslog(ret ? LOG_ERR : LOG_INFO, "A733 service: %s spawn=%d pid=%d\n",
         program, ret, ret ? -1 : (int)pid);
  return ret ? -ret : 0;
}

int main(int argc, char **argv)
{
  int ret = 0;
  if (argc == 2 && !strcmp(argv[1], "--boot"))
    {
      /* Bind listeners independently of Wi-Fi association/DHCP delays. */
#ifdef CONFIG_SYSTEM_A733STORAGE
      if (a733_auto_enabled("ufs")) a733_spawn_service("ufs", "--boot");
#endif
      if (a733_auto_enabled("ssh")) a733_spawn_service("sshd", "--service");
      if (a733_auto_enabled("ftp")) a733_spawn_service("ftpd", "--service");
      if (a733_auto_enabled("wifi")) a733_spawn_service("wifi", "--autoconnect");
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
      if (a733_auto_enabled("agent")) a733_spawn_service("aipet", "--boot");
#ifdef CONFIG_A733_PRODUCT_V1
      if (a733_auto_enabled("pet")) a733_spawn_service("aipet", "--pet-boot");
#endif
#endif
      return 0;
    }
  if (argc == 2 && !strcmp(argv[1], "status"))
    {
      a733_auto_option("ssh", "status");
      a733_auto_option("ftp", "status");
      a733_auto_option("wifi", "status");
      a733_auto_option("agent", "status");
      a733_auto_option("pet", "status");
#ifdef CONFIG_SYSTEM_A733STORAGE
      a733_auto_option("ufs", "status");
#endif
    }
  else if (argc == 3) ret = a733_auto_option(argv[1], argv[2]);
  else
    {
      puts("service status | service <ssh|ftp|wifi|agent|ufs|pet> <on|off|status>\n"
           "Only changes boot policy; use sshd/ftpd/wifi for current operations.");
      return 1;
    }
  if (ret < 0) fprintf(stderr, "service: %s (%d)\n", strerror(-ret), ret);
  return ret < 0 ? 1 : 0;
}
