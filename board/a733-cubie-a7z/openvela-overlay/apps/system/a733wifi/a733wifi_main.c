/****************************************************************************
 * apps/system/a733wifi/a733wifi_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <syslog.h>
#ifdef CONFIG_SYSTEM_A733SERVICES
#include <system/a733_services.h>
#endif

#include <netutils/netlib.h>
#include <wireless/wapi.h>
#ifdef CONFIG_NETUTILS_NTPCLIENT
#  include <netutils/ntpclient.h>
#endif

#define WIFI_DEVICE       "/dev/a733-wifi"
#define WIFI_IFNAME       "wlan0"
#define WIFI_REPORT_SIZE  12288
#define WIFI_PASSWORD_MAX 66

static pthread_mutex_t g_operation_lock = PTHREAD_MUTEX_INITIALIZER;
static pid_t g_operation_pid;
static volatile sig_atomic_t g_cancel;

static void wifi_cancel_handler(int signo)
{
  (void)signo;
  g_cancel = 1;
}

#ifdef CONFIG_SYSTEM_A733SERVICES
struct wifi_profile
{
  uint32_t version;
  uint32_t band;
  char ssid[33];
  char password[64];
};

static int wifi_profile_load(struct wifi_profile *profile)
{
  int ret = a733_config_read("wifi.profile", profile, sizeof(*profile));
  if (ret == 0 && (profile->version != 1 ||
      (profile->band != 0 && profile->band != 2 && profile->band != 5) ||
      memchr(profile->ssid, 0, sizeof(profile->ssid)) == NULL ||
      profile->ssid[0] == 0 ||
      memchr(profile->password, 0, sizeof(profile->password)) == NULL ||
      strlen(profile->password) < 8)) ret = -EINVAL;
  if (ret < 0) explicit_bzero(profile, sizeof(*profile));
  return ret;
}
#endif

static void wifi_sync_time(void)
{
#ifdef CONFIG_NETUTILS_NTPCLIENT
  time_t now;
  int ret;

  now = time(NULL);
  if (now >= 1704067200) /* 2024-01-01 UTC */
    {
      return;
    }

  /* NTP is independent of connectivity.  Do not keep the console blocked
   * for 30 seconds, or call a still-running DNS lookup an NTP failure. */
  ntpc_dualstack_family(AF_INET);
  ret = ntpc_start();
  if (ret < 0)
    {
      printf("Warning: NTP start failed (%d); Wi-Fi remains connected.\n",
             ret);
      return;
    }

  printf("NTP background task pid=%d; SSH can be used now.\n", ret);
  puts("Use 'wifi time' / 'ntpcstatus' to check time before HTTPS.");
#endif
}

static int wifi_time(void)
{
  struct tm utc;
  char stamp[32];
  time_t now = time(NULL);

  if (now < 1704067200)
    {
      puts("Clock not set; starting/reusing background NTP. "
           "HTTPS certificate time checks remain enabled.");
      wifi_sync_time();
      return -EAGAIN;
    }

  gmtime_r(&now, &utc);
  strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
  printf("System UTC: %s (use ntpcstatus for NTP sample evidence).\n", stamp);
  return 0;
}

static void wifi_usage(const char *progname)
{
  fprintf(stderr,
          "Usage:\n"
          "  %s scan [all|2|5|dfs]\n"
          "  %s list\n"
          "  %s connect <ssid> [2|5]\n"
          "  %s disconnect\n"
          "  %s dns <ipv4-address>\n"
          "  %s status\n"
          "  %s time\n"
          "\n"
          "connect reads the WPA2 passphrase interactively; it is never "
          "accepted on the command line.\n",
          progname, progname, progname, progname, progname, progname, progname);
  fputs("  wifi autoconnect on|off|status\n"
        "  wifi reconnect | forget | cancel\n"
        "Autoconnect defaults on: successful connect saves credentials only on this board.\n"
        "FAT storage is not encrypted; use forget to remove the saved profile.\n", stderr);
}

static void wifi_clear(void *buffer, size_t length)
{
  volatile unsigned char *p = buffer;

  while (length-- > 0)
    {
      *p++ = 0;
    }
}

#ifdef CONFIG_A733_WIFI_PRIVATE_CONTROL_COMPAT
static int wifi_control(const char *command)
{
  int fd;
  ssize_t written;
  int error;

  fd = open(WIFI_DEVICE, O_WRONLY);
  if (fd < 0)
    {
      return -errno;
    }

  written = write(fd, command, strlen(command));
  error = written < 0 ? -errno : (size_t)written == strlen(command) ? 0 : -EIO;
  close(fd);
  return error;
}
#endif

static int wifi_report(char *report, size_t report_size)
{
  size_t total = 0;
  int fd;

  fd = open(WIFI_DEVICE, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  while (total + 1 < report_size)
    {
      ssize_t count = read(fd, report + total, report_size - total - 1);

      if (count < 0)
        {
          int error = -errno;
          close(fd);
          return error;
        }

      if (count == 0)
        {
          break;
        }

      total += count;
    }

  close(fd);
  report[total] = '\0';
  return 0;
}

static bool wifi_band_matches(double frequency, unsigned int band)
{
  double mhz = frequency > 1000000.0 ? frequency / 1000000.0 : frequency;

  return band == 0 || (band == 2 && mhz < 5000.0) ||
         (band == 5 && mhz >= 5000.0);
}

static int wifi_wapi_collect(unsigned int band, const char *wanted,
                             double *selected_frequency, bool print)
{
  struct wapi_list_s list;
  struct wapi_scan_info_s *info;
  double best_frequency = 0.0;
  int best_rssi = -256;
  int found = 0;
  int sock;
  int ret;

  memset(&list, 0, sizeof(list));
  sock = wapi_make_socket();
  if (sock < 0)
    {
      return sock;
    }

  ret = wapi_scan_coll(sock, WIFI_IFNAME, &list);
  close(sock);
  if (ret < 0)
    {
      return ret;
    }

  for (info = list.head.scan; info != NULL; info = info->next)
    {
      double mhz = info->freq > 1000000.0 ?
                   info->freq / 1000000.0 : info->freq;

      if (!info->has_essid || !info->has_freq ||
          !wifi_band_matches(info->freq, band))
        {
          continue;
        }

      if (print)
        {
          printf("[%d] %-4s %4.0f MHz %4d dBm  %s\n", found,
                 mhz >= 5000.0 ? "5G" : "2.4G", mhz,
                 info->has_rssi ? info->rssi : -128, info->essid);
        }

      found++;
      if (wanted != NULL && strcmp(info->essid, wanted) == 0 &&
          (!info->has_rssi || info->rssi > best_rssi))
        {
          best_rssi = info->has_rssi ? info->rssi : -128;
          best_frequency = info->freq;
        }
    }

  wapi_scan_coll_free(&list);
  if (selected_frequency != NULL)
    {
      *selected_frequency = best_frequency;
    }

  if (wanted != NULL && best_frequency == 0.0)
    {
      return -ENOENT;
    }

  if (found == 0 && print)
    {
      puts("No networks found.");
    }

  return found > 0 ? found : -ENODATA;
}

static int wifi_wapi_scan(unsigned int band, const char *wanted,
                          double *selected_frequency, bool print)
{
  int sock;
  int ret;
  int attempt;

  sock = wapi_make_socket();
  if (sock < 0)
    {
      return sock;
    }

  ret = wapi_scan_init(sock, WIFI_IFNAME, NULL);
  if (ret < 0)
    {
      close(sock);
      return ret;
    }

  for (attempt = 0; attempt < 25 && !g_cancel; attempt++)
    {
      ret = wapi_scan_stat(sock, WIFI_IFNAME);
      if (ret <= 0)
        {
          break;
        }

      usleep(200000);
    }

  close(sock);
  if (g_cancel)
    {
      return -EINTR;
    }

  if (ret < 0)
    {
      return ret;
    }

  return wifi_wapi_collect(band, wanted, selected_frequency, print);
}

static int wifi_wapi_disconnect(void)
{
  int sock = wapi_make_socket();

  if (sock < 0)
    {
      return sock;
    }

  wpa_driver_wext_disconnect(sock, WIFI_IFNAME);
  close(sock);
  return 0;
}

static int wifi_wapi_associate(const char *ssid, const char *password,
                               double frequency)
{
  int sock;
  int ret;

  sock = wapi_make_socket();
  if (sock < 0)
    {
      return sock;
    }

  ret = wapi_set_mode(sock, WIFI_IFNAME, WAPI_MODE_MANAGED);
  if (ret == 0)
    {
      ret = wpa_driver_wext_set_auth_param(sock, WIFI_IFNAME,
                                           IW_AUTH_WPA_VERSION,
                                           IW_AUTH_WPA_VERSION_WPA2);
    }

  if (ret == 0)
    {
      ret = wpa_driver_wext_set_auth_param(sock, WIFI_IFNAME,
                                           IW_AUTH_CIPHER_PAIRWISE,
                                           IW_AUTH_CIPHER_CCMP);
    }

  if (ret == 0 && frequency > 0.0)
    {
      ret = wapi_set_freq(sock, WIFI_IFNAME, frequency, WAPI_FREQ_FIXED);
    }

  if (ret == 0)
    {
      ret = wpa_driver_wext_set_key_ext(sock, WIFI_IFNAME, WPA_ALG_CCMP,
                                        password, strlen(password));
    }

  if (ret == 0)
    {
      ret = wapi_set_essid(sock, WIFI_IFNAME, ssid, WAPI_ESSID_ON);
    }

  close(sock);
  return ret;
}

#ifdef CONFIG_A733_WIFI_PRIVATE_CONTROL_COMPAT
static int wifi_print_list(const char *report)
{
  /* Compatibility fallback for old images.  New images obtain scan results
   * from openvela's WAPI/WEXT interface in wifi_wapi_collect().
   */
  const char *line = report;
  int found = 0;

  while (line != NULL && *line != '\0')
    {
      const char *next = strchr(line, '\n');
      size_t length = next == NULL ? strlen(line) : (size_t)(next - line);

      if (length >= 4 && strncmp(line, "bss[", 4) == 0)
        {
          const char *frequency = strstr(line, " freq=");
          const char *ssid = strstr(line, " ssid='");
          unsigned int mhz = 0;
          unsigned int index = 0;
          int rssi = 0;

          sscanf(line, "bss[%u]:", &index);
          if (frequency != NULL)
            {
              sscanf(frequency, " freq=%u rssi=%d", &mhz, &rssi);
            }

          printf("[%u] %-4s %4u MHz %4d dBm  ", index,
                 mhz >= 5000 ? "5G" : "2.4G", mhz, rssi);
          if (ssid != NULL && ssid + 7 < line + length)
            {
              const char *name = ssid + 7;
              const char *end = line + length;

              if (end > name && end[-1] == '\'')
                {
                  end--;
                }

              printf("%.*s", (int)(end - name), name);
            }

          putchar('\n');
          found++;
        }

      line = next == NULL ? NULL : next + 1;
    }

  if (found == 0)
    {
      puts("No networks found.");
    }

  return found;
}

static int wifi_find_ssid(const char *report, const char *wanted,
                          unsigned int wanted_band)
{
  const char *line = report;

  while (line != NULL && *line != '\0')
    {
      const char *next = strchr(line, '\n');
      size_t length = next == NULL ? strlen(line) : (size_t)(next - line);

      if (length >= 4 && strncmp(line, "bss[", 4) == 0)
        {
          const char *ssid = strstr(line, " ssid='");
          const char *frequency = strstr(line, " freq=");

          if (ssid != NULL)
            {
              const char *name = ssid + 7;
              const char *end = line + length;
              unsigned int index;
              unsigned int mhz = 0;

              if (end > name && end[-1] == '\'')
                {
                  end--;
                }

              if (frequency != NULL)
                {
                  sscanf(frequency, " freq=%u", &mhz);
                }

              if ((wanted_band == 0 ||
                   (wanted_band == 2 && mhz < 5000) ||
                   (wanted_band == 5 && mhz >= 5000)) &&
                  (size_t)(end - name) == strlen(wanted) &&
                  memcmp(name, wanted, end - name) == 0 &&
                  sscanf(line, "bss[%u]:", &index) == 1)
                {
                  return (int)index;
                }
            }
        }

      line = next == NULL ? NULL : next + 1;
    }

  return -ENOENT;
}
#endif

static int wifi_password(char password[WIFI_PASSWORD_MAX])
{
  struct termios original;
  struct termios hidden;
  bool changed = false;
  size_t length;

  fputs("WPA2 passphrase: ", stdout);
  fflush(stdout);
  if (tcgetattr(STDIN_FILENO, &original) == 0)
    {
      hidden = original;
      hidden.c_lflag &= ~ECHO;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) == 0)
        {
          changed = true;
        }
    }

  if (!changed) return -ENOTTY;
  if (fgets(password, WIFI_PASSWORD_MAX, stdin) == NULL)
    {
      if (changed)
        {
          tcsetattr(STDIN_FILENO, TCSANOW, &original);
          putchar('\n');
        }

      return -EIO;
    }

  if (changed)
    {
      tcsetattr(STDIN_FILENO, TCSANOW, &original);
      putchar('\n');
    }

  length = strcspn(password, "\r\n");
  if (password[length] == 0)
    {
      int ch;
      while ((ch = getchar()) != '\n' && ch != EOF && !g_cancel) {}
      wifi_clear(password, WIFI_PASSWORD_MAX);
      return -EINVAL;
    }
  password[length] = '\0';
  return length >= 8 && length <= 63 ? 0 : -EINVAL;
}

static int wifi_scan(const char *band, char *report)
{
  unsigned int wanted_band = 0;
  int ret;

  if (band == NULL || strcmp(band, "all") == 0)
    {
      wanted_band = 0;
    }
  else if (strcmp(band, "2") == 0 || strcmp(band, "2.4") == 0)
    {
      wanted_band = 2;
    }
  else if (strcmp(band, "5") == 0)
    {
      wanted_band = 5;
    }
  else if (strcmp(band, "dfs") == 0)
    {
      wanted_band = 5;
    }
  else
    {
      return -EINVAL;
    }

  printf("Scanning %s...\n", band == NULL ? "2.4/5 GHz" : band);
  (void)report;
  ret = wifi_wapi_scan(wanted_band, NULL, NULL, true);
  return ret > 0 ? 0 : ret;
}

static int wifi_connect(const char *ssid, const char *band, char *report,
                         const char *saved_password)
{
  char password[WIFI_PASSWORD_MAX];
  double frequency = 0.0;
  int ret;
  unsigned int waited;
  unsigned int attempt;
  unsigned int wanted_band = 0;

  if (strlen(ssid) == 0 || strlen(ssid) > 32) return -EINVAL;

  if (band != NULL)
    {
      if (strcmp(band, "2") == 0 || strcmp(band, "2.4") == 0)
        {
          wanted_band = 2;
        }
      else if (strcmp(band, "5") == 0)
        {
          wanted_band = 5;
        }
      else
        {
          return -EINVAL;
        }
    }

  /* Always perform a fresh scan before selecting a BSS.  Apart from avoiding
   * stale indices, this makes a requested band deterministic when the same
   * SSID is advertised on both 2.4 and 5 GHz.
   */

  ret = -ENOENT;
  for (attempt = 0; attempt < 3 && ret < 0; attempt++)
    {
      if (g_cancel) return -EINTR;
      printf("Scanning %s...\n", band == NULL ? "2.4/5 GHz" : band);
      ret = wifi_wapi_scan(wanted_band, ssid, &frequency, true);
      if (ret < 0 && ret != -ENOENT && ret != -ENODATA)
        {
          return ret;
        }

      if (ret < 0 && attempt + 1 < 3)
        {
          puts("Requested SSID not seen; retrying scan...");
          usleep(300000);
        }
    }

  if (ret < 0)
    {
      return ret;
    }

  memset(password, 0, sizeof(password));
  if (saved_password != NULL)
    {
      snprintf(password, sizeof(password), "%s", saved_password);
      ret = 0;
    }
  else ret = wifi_password(password);
  if (ret < 0)
    {
      wifi_clear(password, sizeof(password));
      return ret;
    }

  if (strlen(password) < 8 || strlen(password) > 63)
    { ret = -EINVAL; goto done; }
  if (g_cancel) { ret = -EINTR; goto done; }
  ret = wifi_wapi_associate(ssid, password, frequency);
  if (ret < 0)
    {
      goto done;
    }

  puts("Associating, completing WPA2 and waiting for DHCP...");
  for (waited = 0; waited < 90; waited++)
    {
      usleep(500000);
      if (g_cancel)
        {
          wifi_wapi_disconnect();
          ret = -EINTR;
          goto done;
        }
      ret = wifi_report(report, WIFI_REPORT_SIZE);
      if (ret < 0)
        {
          goto done;
        }

      if (strstr(report, "wpa2: checkpoint=0") != NULL &&
          strstr(report, " port=1") != NULL &&
          strstr(report, "dhcp: checkpoint=0") != NULL &&
          strstr(report, "address=0.0.0.0") == NULL)
        {
          puts("Connected; WPA2 and DHCP passed.");
#ifdef CONFIG_SYSTEM_A733SERVICES
          if (saved_password == NULL && a733_auto_enabled("wifi"))
            {
              struct wifi_profile profile;
              memset(&profile, 0, sizeof(profile));
              profile.version = 1;
              profile.band = wanted_band;
              snprintf(profile.ssid, sizeof(profile.ssid), "%s", ssid);
              memcpy(profile.password, password, strlen(password) + 1);
              int stored = a733_config_write("wifi.profile", &profile, sizeof(profile));
              wifi_clear(&profile, sizeof(profile));
              if (stored < 0) printf("Connected, but saving autoconnect profile failed (%d).\n", stored);
              else puts("Last successful Wi-Fi saved on this board for next boot.");
            }
#endif
          wifi_sync_time();
          ret = 0;
          goto done;
        }
    }

  ret = -ETIMEDOUT;
done:
  wifi_clear(password, sizeof(password));
  return ret;
}

static int wifi_dns(const char *address)
{
  struct in_addr dns;

  if (inet_pton(AF_INET, address, &dns) != 1 || dns.s_addr == 0)
    {
      return -EINVAL;
    }

  if (netlib_set_ipv4dnsaddr(&dns) < 0)
    {
      return -errno;
    }

  printf("DNS server set to %s.\n", address);
  return 0;
}

int main(int argc, char *argv[])
{
  char *report;
  int ret = -EINVAL;
  bool owns_operation = false;
  struct sigaction previous, action;
  bool control = argc >= 2 && (!strcmp(argv[1], "scan") ||
      !strcmp(argv[1], "connect") || !strcmp(argv[1], "disconnect") ||
      !strcmp(argv[1], "reconnect") || !strcmp(argv[1], "--autoconnect"));

  if (argc == 2 && !strcmp(argv[1], "cancel"))
    {
      g_cancel = 1;
      puts("Wi-Fi cancellation requested; active operation will release its state.");
      return 0;
    }

  report = malloc(WIFI_REPORT_SIZE);
  if (report == NULL)
    {
      fprintf(stderr, "wifi: out of memory\n");
      return EXIT_FAILURE;
    }

  if (control)
    {
      pthread_mutex_lock(&g_operation_lock);
      if (g_operation_pid > 0 && kill(g_operation_pid, 0) == 0)
        {
          pthread_mutex_unlock(&g_operation_lock);
          free(report);
          puts("Wi-Fi operation busy; use 'wifi cancel' before retrying.");
          return 1;
        }
      g_operation_pid = getpid();
      g_cancel = 0;
      owns_operation = true;
      memset(&action, 0, sizeof(action));
      action.sa_handler = wifi_cancel_handler;
      sigemptyset(&action.sa_mask);
      if (sigaction(SIGINT, &action, &previous) < 0)
        {
          g_operation_pid = 0;
          pthread_mutex_unlock(&g_operation_lock);
          free(report);
          return 1;
        }
      pthread_mutex_unlock(&g_operation_lock);
    }

  if (argc >= 2 && strcmp(argv[1], "scan") == 0 && argc <= 3)
    {
      ret = wifi_scan(argc == 3 ? argv[2] : NULL, report);
    }
  else if (argc == 2 && strcmp(argv[1], "list") == 0)
    {
      ret = wifi_wapi_collect(0, NULL, NULL, true);
      if (ret > 0) ret = 0;
    }
  else if ((argc == 3 || argc == 4) &&
           strcmp(argv[1], "connect") == 0)
    {
      ret = wifi_connect(argv[2], argc == 4 ? argv[3] : NULL, report, NULL);
    }
#ifdef CONFIG_SYSTEM_A733SERVICES
  else if (argc == 3 && !strcmp(argv[1], "autoconnect"))
    {
      ret = a733_auto_option("wifi", argv[2]);
      if (!strcmp(argv[2], "status"))
        {
          struct wifi_profile profile;
          if (wifi_profile_load(&profile) == 0)
            printf("Saved SSID: %s, band=%u (0=automatic)\n", profile.ssid, profile.band);
          else puts("No valid saved Wi-Fi profile; connect once to save it.");
          wifi_clear(&profile, sizeof(profile));
        }
    }
  else if (argc == 2 && !strcmp(argv[1], "forget"))
    {
      ret = a733_auto_option("wifi", "off");
      if (ret == 0) ret = a733_config_remove("wifi.profile");
      if (ret == 0) puts("Saved Wi-Fi profile removed; current connection unchanged.");
    }
  else if (argc == 2 && (!strcmp(argv[1], "reconnect") ||
                         !strcmp(argv[1], "--autoconnect")))
    {
      struct wifi_profile profile;
      bool boot = !strcmp(argv[1], "--autoconnect");
      ret = wifi_profile_load(&profile);
      if (boot && !a733_auto_enabled("wifi")) ret = 0;
      else if (ret == 0)
        ret = wifi_connect(profile.ssid, profile.band == 5 ? "5" :
                           profile.band == 2 ? "2" : NULL, report, profile.password);
      wifi_clear(&profile, sizeof(profile));
      syslog(ret == 0 ? LOG_INFO : LOG_WARNING,
             "A733 Wi-Fi saved connection finished (%d); no password prompt\n", ret);
    }
#endif
  else if (argc == 2 && strcmp(argv[1], "disconnect") == 0)
    {
      ret = wifi_wapi_disconnect();
    }
  else if (argc == 3 && strcmp(argv[1], "dns") == 0)
    {
      ret = wifi_dns(argv[2]);
    }
  else if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      ret = wifi_report(report, WIFI_REPORT_SIZE);
      if (ret == 0)
        {
          fputs(report, stdout);
        }
    }
  else if (argc == 2 && strcmp(argv[1], "time") == 0)
    {
      ret = wifi_time();
    }
  else
    {
      wifi_usage(argv[0]);
    }

  wifi_clear(report, WIFI_REPORT_SIZE);
  free(report);
  if (owns_operation)
    {
      pthread_mutex_lock(&g_operation_lock);
      g_operation_pid = 0;
      sigaction(SIGINT, &previous, NULL);
      pthread_mutex_unlock(&g_operation_lock);
    }
  if (ret < 0)
    {
      fprintf(stderr, "wifi: %s (%d)\n", strerror(-ret), ret);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
