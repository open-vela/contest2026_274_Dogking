/* SPDX-License-Identifier: Apache-2.0 */
#include "agent_secret.h"
#include <mbedtls/gcm.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef AIPET_KEY_FILE
#define AIPET_KEY_FILE "/data/ai_agent/device.key"
#endif
static pthread_mutex_t key_lock = PTHREAD_MUTEX_INITIALIZER;
void aipet_wipe(void *p, size_t n)
{ volatile unsigned char *b = p; while (n--) *b++ = 0; }
static int exact(int fd, unsigned char *p, size_t n, int writing)
{
  while (n) {
    ssize_t r = writing ? write(fd, p, n) : read(fd, p, n);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) return -EIO;
    p += r; n -= r;
  }
  return 0;
}
static int random_bytes(unsigned char *p, size_t n)
{
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) return -errno;
  int r = exact(fd, p, n, 0); close(fd); return r;
}
static int device_key(unsigned char key[32], int create)
{
  struct stat st;
  int fd, r;
  pthread_mutex_lock(&key_lock);
  fd = open(AIPET_KEY_FILE, O_RDONLY | O_NOFOLLOW);
  if (fd < 0 && errno == ENOENT && create) {
    r = random_bytes(key, 32);
    if (r) goto done;
    fd = open(AIPET_KEY_FILE, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) { r = -errno; goto done; }
    r = exact(fd, key, 32, 1);
    if (!r && fsync(fd)) r = -errno;
    if (close(fd) && !r) r = -errno;
    if (r) unlink(AIPET_KEY_FILE);
    goto done;
  }
  if (fd < 0) { r = -errno; goto done; }
  r = fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size != 32 ? -EIO :
      exact(fd, key, 32, 0);
  close(fd);
done:
  pthread_mutex_unlock(&key_lock);
  return r;
}
static const char prefix[] = "a7g1:";
static const unsigned char aad[] = "aipet/api_key/v1";
int aipet_secret_seal(const char *plain, char *out, size_t cap)
{
  unsigned char key[32], blob[12 + 127 + 16];
  size_t n = strnlen(plain, 128);
  int r = -EINVAL;
  mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx);
  if (!n || n > 127 || cap < 5 + 2 * (n + 28) + 1) goto done;
  r = device_key(key, 1);
  if (r) goto done;
  r = random_bytes(blob, 12);
  if (r) goto done;
  r = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (!r) r = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, n,
    blob, 12, aad, sizeof(aad)-1, (const unsigned char *)plain, blob+12,
    16, blob+12+n);
  if (!r) {
    static const char hex[] = "0123456789abcdef";
    memcpy(out, prefix, 5);
    for (size_t i=0; i<n+28; ++i) {
      out[5+2*i]=hex[blob[i]>>4]; out[6+2*i]=hex[blob[i]&15];
    }
    out[5+2*(n+28)]=0;
  }
done:
  mbedtls_gcm_free(&ctx); aipet_wipe(key,sizeof(key)); aipet_wipe(blob,sizeof(blob));
  return r;
}
static int nibble(char c)
{ return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : -1; }
int aipet_secret_open(const char *sealed, char *out, size_t cap)
{
  unsigned char key[32], blob[155];
  mbedtls_gcm_context ctx; mbedtls_gcm_init(&ctx);
  int r = -EINVAL;
  if (!cap) goto done;
  out[0]=0;
  size_t len = strnlen(sealed, 316);
  /* Legacy JSON accepted for compatibility; setup always writes encryption. */
  if (strncmp(sealed, prefix, 5)) {
    if (len && len < 128 && len < cap && strncmp(sealed,"REPLACE",7)) {
      memcpy(out,sealed,len+1); r=0;
    }
    goto done;
  }
  if (len < 63 || len > 315 || (len-5)%2) goto done;
  size_t bytes=(len-5)/2, n=bytes-28;
  if (n+1>cap) goto done;
  for (size_t i=0;i<bytes;i++) {
    int a=nibble(sealed[5+2*i]), b=nibble(sealed[6+2*i]);
    if (a<0 || b<0) goto done;
    blob[i]=(unsigned char)((a<<4)|b);
  }
  r=device_key(key,0);
  if (r) goto done;
  r=mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key,256);
  if (!r) r=mbedtls_gcm_auth_decrypt(&ctx,n,blob,12,aad,sizeof(aad)-1,
    blob+12+n,16,blob+12,(unsigned char *)out);
  if (!r) out[n]=0;
done:
  if (r && cap) aipet_wipe(out,cap);
  mbedtls_gcm_free(&ctx); aipet_wipe(key,sizeof(key)); aipet_wipe(blob,sizeof(blob));
  return r;
}
