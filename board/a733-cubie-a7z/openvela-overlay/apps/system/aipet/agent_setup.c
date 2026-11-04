/* SPDX-License-Identifier: Apache-2.0 */
#include "agent_secret.h"
#include "agent_bridge.h"
#include "cloud_voice.h"
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
  FILE *f=fopen(AIPET_SETUP_DIR "/config/config.json","rb");
  if (!f) return errno==ENOENT ? cJSON_CreateObject() : NULL;
  char *data=calloc(1,16385);
  if(!data){fclose(f);return NULL;}
  size_t n=fread(data,1,16384,f);
  int extra=fgetc(f), failed=ferror(f); fclose(f);
  if (failed || extra!=EOF) { aipet_wipe(data,16385);free(data); return NULL; }
  data[n]=0;
  cJSON *root=cJSON_ParseWithOpts(data,NULL,1); aipet_wipe(data,16385);free(data);
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
  if(size>16384)r=-EMSGSIZE;
  while (!r && done<size) {
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

/* Explicit voice provisioning. Never reuse the LLM key as an ASR/TTS key.
 * The official config store rereads this file; one atomic save keeps the
 * token/app id/voice choices coherent without logging any credential.
 */
int aipet_voice_setup(void)
{
  char app[128]={0}, token[256]={0}, api[128]={0};
  char speaker[64]={0}, cluster[128]={0};
  char sealed_token[572]={0}, sealed_api[316]={0};
  cJSON *root=NULL;
  int r=0;
  if (pthread_mutex_trylock(&setup_lock)) return -EBUSY;
  puts("Official Volcengine voice setup (separate service/billing from the LLM).");
  puts("Enter=keep existing; nonempty secrets replace encrypted values. No upload during setup.");
  r=line("ASR app id [Enter=keep]: ",app,sizeof(app),0);
  if (!r) r=line("ASR access token (hidden, Enter=keep): ",token,sizeof(token),1);
  if (!r) r=line("TTS V3 API key (hidden, Enter=keep): ",api,sizeof(api),1);
  if (!r) r=line("TTS speaker id [Enter=keep/default]: ",speaker,sizeof(speaker),0);
  if (!r) r=line("ASR cluster [Enter=keep/default]: ",cluster,sizeof(cluster),0);
  if (r) goto done;
  if ((app[0] && !ascii_field(app)) || (token[0] && !ascii_field(token)) ||
      (api[0] && !ascii_field(api)) || (speaker[0] && !ascii_field(speaker)) ||
      (cluster[0] && !ascii_field(cluster))) { r=-EINVAL; goto done; }
  root=existing();
  if (!root) { r=-EINVAL; goto done; }
  if (token[0]) {
    r=aipet_secret_seal(token,sealed_token,sizeof(sealed_token));
    if (!r) r=field(root,"volc_token",sealed_token);
  }
  if (!r && api[0]) {
    r=aipet_secret_seal(api,sealed_api,sizeof(sealed_api));
    if (!r) r=field(root,"volc_api_key",sealed_api);
  }
  if (!r && app[0]) r=field(root,"volc_appkey",app);
  if (!r && speaker[0]) r=field(root,"volc_speaker",speaker);
  if (!r && cluster[0]) r=field(root,"volc_asr_cluster",cluster);
  if (!r) r=save(root);
done:
  cJSON_Delete(root);
  aipet_wipe(token,sizeof(token)); aipet_wipe(api,sizeof(api));
  aipet_wipe(sealed_token,sizeof(sealed_token)); aipet_wipe(sealed_api,sizeof(sealed_api));
  pthread_mutex_unlock(&setup_lock);
  if (!r) puts("Voice config saved; key material not printed. Use audio asr/speak explicitly to test.");
  else printf("Voice setup failed: %d; previous config retained.\n",r);
  return r;
}

/* Selection is separate from provisioning: saving credentials never starts a
 * microphone, changes local/cloud product mode or spends service credits. */
int aipet_voice_provider_select(const char *kind,const char *provider)
{
  if(!aipet_cloud_provider_valid(kind,provider))return -EINVAL;
  if(pthread_mutex_trylock(&setup_lock))return -EBUSY;
  cJSON *root=existing();int rc=root?0:-EINVAL;
  if(!rc)rc=field(root,!strcmp(kind,"asr")?"cloud_asr_provider":"cloud_tts_provider",provider);
  if(!rc)rc=save(root);
  cJSON_Delete(root);pthread_mutex_unlock(&setup_lock);return rc;
}
struct voice_field {const char *name;const char *prompt;int secret;};
int aipet_voice_provider_setup(const char *provider)
{
  if(!provider)return -EINVAL;
  if(!strcmp(provider,"volc"))return aipet_voice_setup();
  const struct voice_field xfyun[]={
    {"xfyun_appid","Xfyun APPID [Enter=keep]: ",0},
    {"xfyun_api_key","Xfyun APIKey (hidden, Enter=keep): ",1},
    {"xfyun_api_secret","Xfyun APISecret (hidden, Enter=keep): ",1},
    {"xfyun_voice","Xfyun voice [Enter=keep/default xiaoyan]: ",0}};
  const struct voice_field tencent[]={
    {"tencent_secret_id","Tencent SecretId (hidden, Enter=keep): ",1},
    {"tencent_secret_key","Tencent SecretKey (hidden, Enter=keep): ",1},
    {"tencent_session_token","Tencent optional STS token (hidden, Enter=keep, -=clear): ",1},
    {"tencent_asr_engine","Tencent ASR engine [Enter=keep/default 16k_zh]: ",0},
    {"tencent_voice_type","Tencent voice type [Enter=keep/default 1001]: ",0}};
  const struct voice_field edge[]={
    {"edge_gateway_host","Edge HTTPS companion hostname (no scheme/path, Enter=keep): ",0},
    {"edge_gateway_port","Edge HTTPS companion port [Enter=keep/default 443]: ",0},
    {"edge_gateway_token","Edge companion bearer token (hidden, Enter=keep): ",1},
    {"edge_voice","Edge voice [Enter=keep/default zh-CN-XiaoxiaoNeural]: ",0}};
  const struct voice_field *fields=NULL;unsigned count=0;
  if(!strcmp(provider,"xfyun")){fields=xfyun;count=sizeof(xfyun)/sizeof(*xfyun);}
  if(!strcmp(provider,"tencent")){fields=tencent;count=sizeof(tencent)/sizeof(*tencent);}
  if(!strcmp(provider,"edge")){fields=edge;count=sizeof(edge)/sizeof(*edge);}
  if(!fields)return -EINVAL;
  if(pthread_mutex_trylock(&setup_lock))return -EBUSY;
  int rc=0;cJSON *root=existing();if(!root)rc=-EINVAL;
  puts("Voice-only credentials: hidden/encrypted on this board; Enter=keep. No service call.");
  if(!strcmp(provider,"edge"))puts("Edge is experimental TTS ONLY, requires an HTTPS companion + MP3 decode; NOT a Microsoft-supported developer API or native RTOS Edge client.");
  char value[256]={0},sealed[1024]={0};
  for(unsigned i=0;i<count&&!rc;i++) {
    rc=line(fields[i].prompt,value,sizeof(value),fields[i].secret);
    if(!rc&&value[0]&&!ascii_field(value))rc=-EINVAL;
    if(!rc&&value[0]) {
      if(!strcmp(fields[i].name,"tencent_session_token")&&!strcmp(value,"-"))cJSON_DeleteItemFromObjectCaseSensitive(root,fields[i].name);
      else {
        if(fields[i].secret)rc=aipet_secret_seal(value,sealed,sizeof(sealed));
        if(!rc)rc=field(root,fields[i].name,fields[i].secret?sealed:value);
      }
    }
    aipet_wipe(value,sizeof(value));aipet_wipe(sealed,sizeof(sealed));
  }
  if(!rc)rc=save(root);
  cJSON_Delete(root);pthread_mutex_unlock(&setup_lock);
  puts(rc?"Voice setup failed; prior config retained, no secret printed.":"Voice credentials saved. Choose ASR/TTS separately with 'aipet audio providers asr|tts <name>'. No live validation yet.");
  return rc;
}
