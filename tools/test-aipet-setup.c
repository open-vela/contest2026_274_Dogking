#include "agent_secret.h"
#include "cloud_voice.h"
#include <assert.h>
#include <string.h>
void aipet_agent_status(int *phase,int *pid,int *err)
{ *phase=0; *pid=-1; *err=0; }
int a733_auto_option(const char *s,const char *o)
{ assert(!strcmp(s,"agent") && !strcmp(o,"on")); return 0; }
int aipet_startup(int background)
{ assert(!background); return 0; }
/* Actual setup/secret implementation; provider validation is pure and also
 * exercised against the real dispatcher in test-cloud-voice-v218.py. */
int aipet_cloud_provider_valid(const char *kind,const char *name) {
  if(strcmp(kind,"asr")&&strcmp(kind,"tts"))return 0;
  return !strcmp(name,"volc")||!strcmp(name,"xfyun")||!strcmp(name,"tencent")||(!strcmp(kind,"tts")&&!strcmp(name,"edge"));
}
int main(int argc,char **argv)
{
  if(argc==2&&!strcmp(argv[1],"voice"))return aipet_voice_setup()!=0;
  if(argc==2&&(!strcmp(argv[1],"xfyun")||!strcmp(argv[1],"tencent")||!strcmp(argv[1],"edge")))return aipet_voice_provider_setup(argv[1])!=0;
  if(argc==4&&!strcmp(argv[1],"select"))return aipet_voice_provider_select(argv[2],argv[3])!=0;
  return aipet_setup();
}
