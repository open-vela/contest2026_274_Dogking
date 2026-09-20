/* SPDX-License-Identifier: Apache-2.0
 * Board-local records only. CRC detects truncation/corruption, not tampering.
 * FAT does not provide reliable Unix access control or encryption.
 */
#include <nuttx/config.h>
#include <system/a733_services.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

#ifndef A733_CONFIG_DIR
#define A733_CONFIG_DIR "/data/system"
#endif
#ifndef A733_CONFIG_MOUNT
#define A733_CONFIG_MOUNT "/data"
#endif
static pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
struct record_header { uint32_t magic; uint32_t size; uint32_t crc; };

static uint32_t checksum(const void *data, size_t size)
{
  const unsigned char *p = data;
  uint32_t crc = ~0u;
  while (size--)
    {
      crc ^= *p++;
      for (int bit = 0; bit < 8; bit++)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
  return ~crc;
}

static int path_for(const char *name, char path[96])
{
  if (strcmp(name, "wifi.profile") && strcmp(name, "ftp.auth") &&
      strcmp(name, "ssh.auto") && strcmp(name, "ftp.auto") &&
      strcmp(name, "wifi.auto") && strcmp(name, "agent.auto")) return -EINVAL;
  return snprintf(path, 96, "%s/%s", A733_CONFIG_DIR, name) < 96 ? 0 : -ENAMETOOLONG;
}

static int exact_io(int fd, void *data, size_t size, bool writing)
{
  unsigned char *p = data;
  while (size)
    {
      ssize_t n = writing ? write(fd, p, size) : read(fd, p, size);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return n < 0 ? -errno : -EIO;
      p += n;
      size -= n;
    }
  return 0;
}

int a733_config_read(const char *name, void *data, size_t size)
{
  char path[96];
  struct record_header header;
  struct stat st;
  sigset_t mask, saved;
  int fd;
  int ret = path_for(name, path);
  if (ret < 0 || size > 256) return -EINVAL;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, &saved);
  pthread_mutex_lock(&g_config_lock);
  fd = open(path, O_RDONLY | O_NOFOLLOW);
  ret = fd < 0 ? -errno : 0;
  if (ret == 0 && (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) ||
      st.st_size != sizeof(header) + size)) ret = -EINVAL;
  if (ret == 0) ret = exact_io(fd, &header, sizeof(header), false);
  if (ret == 0 && (header.magic != 0x41374346 || header.size != size)) ret = -EINVAL;
  if (ret == 0) ret = exact_io(fd, data, size, false);
  if (ret == 0 && header.crc != checksum(data, size)) ret = -EINVAL;
  if (fd >= 0) close(fd);
  pthread_mutex_unlock(&g_config_lock);
  if (ret < 0) explicit_bzero(data, size);
  sigprocmask(SIG_SETMASK, &saved, NULL);
  return ret;
}

int a733_config_write(const char *name, const void *data, size_t size)
{
  char path[96];
  char temp[104];
  struct stat st;
  struct statfs fs;
  sigset_t mask, saved;
  struct record_header header = {0x41374346, 0, 0};
  int fd = -1;
  int ret = path_for(name, path);
  if (ret < 0 || size > 256) return -EINVAL;
  header.size = size;
  header.crc = checksum(data, size);
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, &saved);
  pthread_mutex_lock(&g_config_lock);
  if (statfs(A733_CONFIG_MOUNT, &fs) < 0 || fs.f_blocks == 0)
    { ret = -ENODEV; goto out; }
  if ((mkdir(A733_CONFIG_DIR, 0700) < 0 && errno != EEXIST) ||
      lstat(A733_CONFIG_DIR, &st) < 0 || !S_ISDIR(st.st_mode))
    { ret = -EIO; goto out; }
  fd = open(temp, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW, 0600);
  if (fd < 0) { ret = -errno; goto out; }
  ret = exact_io(fd, &header, sizeof(header), true);
  if (ret == 0) ret = exact_io(fd, (void *)data, size, true);
  if (ret == 0 && fsync(fd) < 0) ret = -errno;
  if (close(fd) < 0 && ret == 0) ret = -errno;
  fd = -1;
  if (ret == 0 && rename(temp, path) < 0) ret = -errno;
  if (ret < 0) unlink(temp);
out:
  pthread_mutex_unlock(&g_config_lock);
  sigprocmask(SIG_SETMASK, &saved, NULL);
  return ret;
}

int a733_config_remove(const char *name)
{
  char path[96];
  sigset_t mask, saved;
  if (path_for(name, path) < 0) return -EINVAL;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigprocmask(SIG_BLOCK, &mask, &saved);
  pthread_mutex_lock(&g_config_lock);
  int ret = unlink(path) < 0 && errno != ENOENT ? -errno : 0;
  pthread_mutex_unlock(&g_config_lock);
  sigprocmask(SIG_SETMASK, &saved, NULL);
  return ret;
}

bool a733_auto_enabled(const char *service)
{
  char name[24];
  uint8_t enabled = 0;
  snprintf(name, sizeof(name), "%s.auto", service);
  int ret = a733_config_read(name, &enabled, sizeof(enabled));
  /* SSH/FTP are opt-in. Wi-Fi remembers the next successful connection. */
  if (ret == -ENOENT) return !strcmp(service, "wifi") || !strcmp(service, "agent");
  return ret == 0 && enabled == 1;
}

int a733_ftp_auth_load(struct a733_ftp_auth *auth)
{
  int ret = a733_config_read("ftp.auth", auth, sizeof(*auth));
  if (ret == 0 && (auth->version != 1 || auth->port < 1 ||
      auth->port > 65535 || auth->iterations != A733_AUTH_ITERATIONS)) ret = -EINVAL;
  if (ret < 0) explicit_bzero(auth, sizeof(*auth));
  return ret;
}

int a733_auto_option(const char *service, const char *option)
{
  char name[24];
  uint8_t enabled;
  int ret;
  if (strcmp(service, "ssh") && strcmp(service, "ftp") && strcmp(service, "wifi") &&
      strcmp(service, "agent"))
    return -EINVAL;
  if (strcmp(option, "status") == 0)
    {
      printf("%s autostart: %s\n", service, a733_auto_enabled(service) ? "on" : "off");
      return 0;
    }
  if (!strcmp(option, "on") || !strcmp(option, "enable")) enabled = 1;
  else if (!strcmp(option, "off") || !strcmp(option, "disable")) enabled = 0;
  else return -EINVAL;
  if (enabled && !strcmp(service, "ftp"))
    {
      struct a733_ftp_auth auth;
      ret = a733_ftp_auth_load(&auth);
      explicit_bzero(&auth, sizeof(auth));
      if (ret < 0) { puts("Run 'ftpd setup' first."); return ret; }
      puts("Plain FTP is unencrypted. Enable only on a trusted LAN.");
    }
  if (enabled && !strcmp(service, "ssh") &&
      access("/data/ssh/sshd.auth", R_OK) < 0 &&
      access("/data/ssh/authorized_keys", R_OK) < 0)
    { puts("Run 'sshd --setup openvela' first."); return -ENOENT; }
  snprintf(name, sizeof(name), "%s.auto", service);
  ret = a733_config_write(name, &enabled, sizeof(enabled));
  if (ret == 0) printf("%s autostart %s; applies next boot (running tasks unchanged).\n",
                       service, enabled ? "on" : "off");
  return ret;
}

int a733_password_derive(const char *password, const unsigned char salt[16],
                         unsigned char hash[32])
{
  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == NULL) return -ENOSYS;
  mbedtls_md_init(&ctx);
  int ret = mbedtls_md_setup(&ctx, info, 1);
  if (ret == 0) ret = mbedtls_pkcs5_pbkdf2_hmac(&ctx,
      (const unsigned char *)password, strlen(password), salt, 16,
      A733_AUTH_ITERATIONS, 32, hash);
  mbedtls_md_free(&ctx);
  return ret;
}
