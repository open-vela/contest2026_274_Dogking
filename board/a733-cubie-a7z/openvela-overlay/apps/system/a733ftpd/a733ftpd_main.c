/* SPDX-License-Identifier: Apache-2.0
 * Optional board-local password verifier and explicit boot policy.
 * FTP is unencrypted: do not expose it outside a trusted local network.
 */
#include <nuttx/config.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <netutils/ftpd.h>
#include <syslog.h>
#ifdef CONFIG_SYSTEM_A733SERVICES
#include <system/a733_services.h>
#endif

#define FTP_ROOT "/data/models"
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static FTPD_SESSION g_server;
static bool g_stop;
static int g_pid = -1;
static int g_port;
#ifdef CONFIG_SYSTEM_A733SERVICES
static struct a733_ftp_auth g_auth;

static bool verify_password(const char *user, const char *pass, void *arg)
{
  struct a733_ftp_auth *auth = arg;
  unsigned char derived[32] = {0};
  unsigned char difference = 0;
  if (strcmp(user, "openvela") || strlen(pass) < 8 || strlen(pass) > 63) return false;
  int ret = a733_password_derive(pass, auth->salt, derived);
  for (size_t i = 0; i < sizeof(derived); i++) difference |= derived[i] ^ auth->hash[i];
  explicit_bzero(derived, sizeof(derived));
  return ret == 0 && difference == 0;
}
#endif

static void erase(void *p, size_t n)
{
  volatile unsigned char *q = p;
  while (n-- != 0) *q++ = 0;
}

static int password(char *out, size_t n, const char *prompt)
{
  struct termios saved;
  struct termios hidden;
  int ret = -EINVAL;
  /* Refuse to accept passwords if echo cannot be disabled. */
  if (tcgetattr(STDIN_FILENO, &saved) < 0) return -errno;
  hidden = saved;
  hidden.c_lflag &= ~ECHO;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) < 0) return -errno;
  fputs(prompt, stdout);
  fflush(stdout);
  if (fgets(out, n, stdin) != NULL)
    {
      size_t len = strcspn(out, "\r\n");
      if (out[len] != 0 && len >= 8 && len <= 63) ret = 0;
      else if (out[len] == 0)
        {
          int ch;
          while ((ch = getchar()) != '\n' && ch != EOF) {}
        }
      out[len] = 0;
    }
  tcsetattr(STDIN_FILENO, TCSANOW, &saved);
  putchar('\n');
  return ret;
}

static int server_task(int argc, char **argv)
{
  FTPD_SESSION server;
  signal(SIGPIPE, SIG_IGN);
  pthread_mutex_lock(&g_lock);
  server = g_server;
  pthread_mutex_unlock(&g_lock);
  for (;;)
    {
      bool stop;
      pthread_mutex_lock(&g_lock);
      stop = g_stop;
      pthread_mutex_unlock(&g_lock);
      if (stop) break;
      int ret = ftpd_session(server, 1000);
      if (ret < 0 && ret != -ETIMEDOUT && ret != -EINTR && ret != -EBUSY)
        {
          fprintf(stderr, "ftpd: accept failed: %d\n", ret);
          break;
        }
    }
  /* Close the listener and drain workers, never free their account early. */
  ftpd_close(server);
  pthread_mutex_lock(&g_lock);
  g_server = NULL;
  g_pid = -1;
  g_stop = false;
#ifdef CONFIG_SYSTEM_A733SERVICES
  erase(&g_auth, sizeof(g_auth));
#endif
  syslog(LOG_INFO, "FTPD: stopped and sessions drained\n");
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int main(int argc, char **argv)
{
  struct statfs fs;
  struct stat st;
  char first[66] = {0};
  char second[66] = {0};
  sigset_t blocked;
  sigset_t original;
  int ret = 0;
  int port = 21;
  bool setup = false;
  bool service = false;
  bool saved_auth = false;
#ifdef CONFIG_SYSTEM_A733SERVICES
  if (argc == 3 && !strcmp(argv[1], "autostart"))
    {
      ret = a733_auto_option("ftp", argv[2]);
      if (ret < 0) fprintf(stderr, "ftpd autostart: %s (%d)\n", strerror(-ret), ret);
      return ret < 0 ? 1 : 0;
    }
  setup = argc >= 2 && !strcmp(argv[1], "setup");
  service = argc == 2 && !strcmp(argv[1], "--service");
#endif
  if (argc < 2 || (strcmp(argv[1], "start") != 0 &&
      strcmp(argv[1], "stop") != 0 && strcmp(argv[1], "status") != 0 &&
      !setup && !service))
    {
      puts("ftpd start [port] | stop | status\n"
           "Login: openvela; root: /data/models; passive mode; max 4 clients.\n"
           "ftpd setup [port] | autostart on|off|status\n"
           "setup saves a password verifier on this board; start reuses it.\n"
           "Without setup, start uses a temporary password. Plain FTP: trusted LAN only.");
      return argc < 2 ? 0 : 1;
    }
  if (argc > 3 || (argc == 3 && strcmp(argv[1], "start") != 0 && !setup)) return 1;
  if (argc == 3)
    {
      char *end;
      long value = strtol(argv[2], &end, 10);
      if (*end != 0 || value < 1 || value > 65535) return 1;
      port = value;
    }
  /* A pending Ctrl+C must not leave the shared start state/terminal dirty. */
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGINT);
  sigprocmask(SIG_BLOCK, &blocked, &original);
  pthread_mutex_lock(&g_lock);
  if (strcmp(argv[1], "status") == 0)
    {
      printf("ftpd: %s pid=%d port=%d root=%s\n", g_server == NULL ? "stopped" :
             g_stop ? "draining" : "listening", g_pid, g_port, FTP_ROOT);
#ifdef CONFIG_SYSTEM_A733SERVICES
      a733_auto_option("ftp", "status");
#endif
      if (statfs(FTP_ROOT, &fs) == 0)
        printf("Free bytes: %llu\n", (unsigned long long)fs.f_bavail * fs.f_bsize);
      goto out;
    }
  if (strcmp(argv[1], "stop") == 0)
    {
      g_stop = g_server != NULL;
      puts("Stop requested; existing transfers drain, idle clients expire in 60s.");
      goto out;
    }
  if (g_server != NULL)
    {
      puts("ftpd already running/draining; use ftpd status.");
      ret = 1;
      goto out;
    }
  if (statfs("/data", &fs) < 0 || fs.f_blocks == 0 ||
      (mkdir(FTP_ROOT, 0755) < 0 && errno != EEXIST) ||
      lstat(FTP_ROOT, &st) < 0 || !S_ISDIR(st.st_mode))
    {
      puts("ftpd: mount writable /data first; /data/models must be a directory.");
      ret = 1;
      goto out;
    }
  /* Service mode is non-interactive and refuses missing/corrupt credentials. */
#ifdef CONFIG_SYSTEM_A733SERVICES
  if (!setup)
    {
      int loaded = a733_ftp_auth_load(&g_auth);
      if (loaded == 0)
        {
          saved_auth = true;
          if (argc == 2) port = g_auth.port;
        }
      else if (service || loaded != -ENOENT)
        {
          syslog(LOG_ERR, "FTPD: saved authentication unavailable (%d); run ftpd setup\n", loaded);
          ret = 1;
          goto out;
        }
    }
#endif
  if (!saved_auth)
    {
      puts(setup ? "Set board-local FTP password (8-63 characters)." :
                   "Unencrypted FTP; use a temporary password (8-63 characters).");
      if (password(first, sizeof(first), "FTP password: ") < 0 ||
      password(second, sizeof(second), "Confirm FTP password: ") < 0 ||
      strcmp(first, second) != 0)
        {
          puts("Invalid or mismatched password.");
          ret = 1;
          goto out;
        }
    }
#ifdef CONFIG_SYSTEM_A733SERVICES
  if (setup)
    {
      FILE *random = fopen("/dev/urandom", "rb");
      struct a733_ftp_auth auth = { .version = 1, .port = port,
                                   .iterations = A733_AUTH_ITERATIONS };
      if (random == NULL || fread(auth.salt, 1, sizeof(auth.salt), random) != sizeof(auth.salt))
        ret = -EIO;
      if (random != NULL) fclose(random);
      if (ret == 0) ret = a733_password_derive(first, auth.salt, auth.hash);
      if (ret == 0) ret = a733_config_write("ftp.auth", &auth, sizeof(auth));
      erase(&auth, sizeof(auth));
      if (ret == 0) puts("FTP password verifier saved. Use ftpd start / ftpd autostart on.");
      else printf("FTP setup failed (%d)\n", ret);
      ret = ret == 0 ? 0 : 1;
      goto out;
    }
#endif
  g_server = ftpd_open(port, AF_INET);
  if (g_server == NULL || ftpd_adduser(g_server, FTPD_ACCOUNTFLAG_NONE,
                                      "openvela", saved_auth ? NULL : first, FTP_ROOT) < 0)
    {
      if (g_server != NULL) ftpd_close(g_server);
      g_server = NULL;
      printf("ftpd: listen/account initialization failed (errno=%d)\n", errno);
      ret = 1;
      goto out;
    }
#ifdef CONFIG_SYSTEM_A733SERVICES
  if (saved_auth && ftpd_set_auth_callback(g_server, verify_password, &g_auth) < 0)
    {
      ftpd_close(g_server);
      g_server = NULL;
      ret = 1;
      goto out;
    }
#endif
  g_stop = false;
  g_port = port;
  g_pid = task_create("ftp-listener", 100, 8192, server_task, NULL);
  if (g_pid < 0)
    {
      ftpd_close(g_server);
      g_server = NULL;
      ret = 1;
      goto out;
    }
  printf("FTP listening on 0.0.0.0:%d, user=openvela, root=%s\n", port, FTP_ROOT);
  syslog(LOG_INFO, "FTPD: listening pid=%d port=%d max-clients=4\n", g_pid, port);
out:
  erase(first, sizeof(first));
  erase(second, sizeof(second));
  pthread_mutex_unlock(&g_lock);
  sigprocmask(SIG_SETMASK, &original, NULL);
  return ret;
}
