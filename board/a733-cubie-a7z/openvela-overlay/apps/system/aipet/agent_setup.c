/* SPDX-License-Identifier: Apache-2.0 */
#include "agent_secret.h"
#include "agent_bridge.h"
#include <cJSON.h>
#include <system/a733_services.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#ifndef AIPET_SETUP_DIR
#define AIPET_SETUP_DIR "/data/ai_agent"
#endif
static pthread_mutex_t setup_lock = PTHREAD_MUTEX_INITIALIZER;
/* Raw-byte terminal reader: Ctrl+C cancels, UTF-8 bytes are not stripped.
 * Signal mask and terminal state are restored before returning. */
static int line(const char *prompt, char *out, size_t cap, int secret)
{
  struct termios old, changed;
  sigset_t set, saved;
  size_t used=0;
  int result=0;
  int flags=fcntl(0,F_GETFL);
  if (flags<0) return -errno;
  if (tcgetattr(0,&old)) return -ENOTTY;
  sigemptyset(&set); sigaddset(&set,SIGINT);
  int sr=pthread_sigmask(SIG_BLOCK,&set,&saved);
  if (sr) return -sr;
  changed=old; changed.c_lflag &= ~(ICANON|ISIG|ECHO);
  changed.c_cc[VMIN]=1; changed.c_cc[VTIME]=0;
  if (tcsetattr(0,TCSANOW,&changed)) { result=-errno; goto done; }
  if (fcntl(0,F_SETFL,flags|O_NONBLOCK)) {
    result=-errno; tcsetattr(0,TCSANOW,&old); goto done;
  }
  printf("%s",prompt); fflush(stdout);
  for (;;) {
    sigset_t pending;
    if (!sigpending(&pending) && sigismember(&pending,SIGINT)) {
      struct timespec zero={0,0};
      sigtimedwait(&set,NULL,&zero);
      result=-ECANCELED; break;
    }
    unsigned char ch;
    ssize_t r=read(0,&ch,1);
    if (r<0 && errno==EINTR) continue;
    if (r<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) { usleep(10000); continue; }
    if (r!=1) { result=-EIO; break; }
    if (ch==3) { result=-ECANCELED; break; }
    if (ch=='\r' || ch=='\n') break;
    if (ch==8 || ch==127) {
      if (used) { --used; if (!secret) { fputs("\b \b",stdout); fflush(stdout); } }
      continue;
    }
    /* Drain rejected input through newline even on the polling UART, whose
     * tcflush support is limited. Never leave a key suffix as an NSH command. */
    if (result) continue;
    if (ch<32 || ch==127) { result=-EINVAL; continue; }
    if (used+1>=cap) { result=-EMSGSIZE; continue; }
    out[used++]=(char)ch;
    if (!secret) { putchar(ch); fflush(stdout); }
  }
  out[used]=0;
  /* Flush leftover CRLF or overlong input, never leave a key as an NSH command. */
  tcflush(0,TCIFLUSH);
  if (fcntl(0,F_SETFL,flags) && !result) result=-errno;
  if (tcsetattr(0,TCSANOW,&old) && !result) result=-errno;
  putchar('\n'); fflush(stdout);
done:
  pthread_sigmask(SIG_SETMASK,&saved,NULL);
  if (result) aipet_wipe(out,cap);
  return result;
}
static int ascii_field(const char *s)
{
  if (!s[0]) return 0;
  for (;*s;s++) if ((unsigned char)*s<33 || (unsigned char)*s>126) return 0;
  return 1;
}
static cJSON *existing(void)
{
  char data[4097];
  FILE *f=fopen(AIPET_SETUP_DIR "/config/config.json","rb");
  if (!f) return errno==ENOENT ? cJSON_CreateObject() : NULL;
  size_t n=fread(data,1,sizeof(data)-1,f);
  int extra=fgetc(f), failed=ferror(f); fclose(f);
  if (failed || extra!=EOF) { aipet_wipe(data,sizeof(data)); return NULL; }
  data[n]=0;
  cJSON *root=cJSON_Parse(data); aipet_wipe(data,sizeof(data));
  if (!cJSON_IsObject(root)) { cJSON_Delete(root); return NULL; }
  return root;
}
static int field(cJSON *root,const char *name,const char *value)
{
  cJSON_DeleteItemFromObjectCaseSensitive(root,name);
  return cJSON_AddStringToObject(root,name,value) ? 0 : -ENOMEM;
}
static int save(cJSON *root)
{
  const char *tmp=AIPET_SETUP_DIR "/config/config.json.new";
  char *json=cJSON_PrintUnformatted(root);
  if (!json) return -ENOMEM;
  int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600), r=0;
  if (fd<0) { free(json); return -errno; }
  size_t size=strlen(json), done=0;
  while (done<size) {
    ssize_t n=write(fd,json+done,size-done);
    if (n<0 && errno==EINTR) continue;
    if (n<=0) { r=-EIO; break; }
    done+=n;
  }
  if (!r && fsync(fd)) r=-errno;
  if (close(fd) && !r) r=-errno;
  if (!r && rename(tmp,AIPET_SETUP_DIR "/config/config.json")) r=-errno;
  if (r) unlink(tmp);
  aipet_wipe(json,size); free(json); return r;
}
int aipet_setup(void)
{
  char provider[16]={0}, model[64]={0}, key[128]={0}, sealed[316]={0};
  int phase,pid,error,r;
  cJSON *root=NULL;
  if (pthread_mutex_trylock(&setup_lock)) { puts("Setup already running."); return 1; }
  aipet_agent_status(&phase,&pid,&error);
  if (phase) { puts("Agent already started. Disable agent autostart and reboot before reconfiguration."); r=-EBUSY; goto done; }
  puts("Online setup: DeepSeek or MiMo. API key is hidden and encrypted on this board.");
  r=line("Provider [1=deepseek (default), 2=mimo]: ",provider,sizeof(provider),0);
  if (r) goto done;
  int mimo=!strcmp(provider,"2") || !strcmp(provider,"mimo");
  if (provider[0] && strcmp(provider,"1") && strcmp(provider,"deepseek") && !mimo)
    { r=-EINVAL; goto done; }
  const char *def=mimo ? "MiMo-v2-Flash" : "deepseek-flash";
  printf("Default model: %s\n",def);
  r=line("Model name [Enter=default, or custom name]: ",model,sizeof(model),0);
  if (r) goto done;
  if (!model[0]) strcpy(model,def);
  if (!ascii_field(model)) { r=-EINVAL; goto done; }
  r=line("API key (hidden): ",key,sizeof(key),1);
  if (r) goto done;
  if (!ascii_field(key)) { r=-EINVAL; goto done; }
  if (mkdir(AIPET_SETUP_DIR,0700) && errno!=EEXIST) { r=-errno; goto done; }
  if (mkdir(AIPET_SETUP_DIR "/config",0700) && errno!=EEXIST) { r=-errno; goto done; }
  root=existing();
  if (!root) { r=-EINVAL; goto done; }
  r=aipet_secret_seal(key,sealed,sizeof(sealed));
  aipet_wipe(key,sizeof(key));
  if (r) goto done;
  if (field(root,"api_key",sealed) || field(root,"model",model) ||
      field(root,"llm_host",mimo?"api.xiaomimimo.com":"api.deepseek.com") ||
      field(root,"llm_path","/v1/chat/completions") || field(root,"llm_port","443"))
    { r=-ENOMEM; goto done; }
  r=save(root);
  if (r) goto done;
  r=a733_auto_option("agent","on");
  if (!r) {
    puts("Configuration saved. Waiting for network/clock/CA; future boots start automatically.");
    r=aipet_startup(0);
  }
done:
  cJSON_Delete(root); aipet_wipe(key,sizeof(key)); aipet_wipe(sealed,sizeof(sealed));
  if (r) printf("aipet setup failed: %d (no key printed)\n",r);
  pthread_mutex_unlock(&setup_lock);
  return r ? 1 : 0;
}
