#include "agent_secret.h"
#include <assert.h>
#include <string.h>
void aipet_agent_status(int *phase,int *pid,int *err)
{ *phase=0; *pid=-1; *err=0; }
int a733_auto_option(const char *s,const char *o)
{ assert(!strcmp(s,"agent") && !strcmp(o,"on")); return 0; }
int aipet_startup(int background)
{ assert(!background); return 0; }
int main(void) { return aipet_setup(); }
