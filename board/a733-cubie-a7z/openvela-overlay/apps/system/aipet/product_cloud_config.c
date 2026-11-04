/* SPDX-License-Identifier: Apache-2.0 */
/* Keep the official C-only compatibility header OUT of C++ translation units. */
#include "infra/config_store.h"
#include "agent_secret.h"
#include "cloud_voice.h"
#include <stdbool.h>
#include <string.h>
#include <errno.h>
int aipet_cloud_get(const char *key,char *out,size_t cap,int secret)
{
  char stored[1024]={0};
  if(!key||!out||cap<2)return -EINVAL;
  out[0]=0;
  int rc=claw_config_get(key,stored,sizeof(stored));
  if(!rc) {
    if(secret)rc=aipet_secret_open(stored,out,cap);
    else if(strlen(stored)>=cap)rc=-EMSGSIZE;
    else memcpy(out,stored,strlen(stored)+1);
  }
  aipet_wipe(stored,sizeof(stored));
  if(rc){aipet_wipe(out,cap);return -ENOENT;}
  return 0;
}
int aipet_product_cloud_keys_ready(void)
{
  return aipet_cloud_ready();
}
